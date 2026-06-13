#ifdef _WIN32
#include <windows.h>
#include "list_entry.h"

typedef struct _ShimWaitAddress
{
    PVOID Address;
    LONG ReferenceCount;
    SRWLOCK ReferenceCountLock;

    LONG Value;
    SRWLOCK SrwLock;
    CONDITION_VARIABLE CondVar;

    LIST_ENTRY ListEntry;
} ShimWaitAddress;

BOOL ShimWaitAddressListInit = FALSE;
LIST_ENTRY ShimWaitAddressList;
SRWLOCK ShimWaitAddressListLock;

void ShimInitWaitAddressList()
{
    if (ShimWaitAddressListInit)
        return;

    InitializeListHead(&ShimWaitAddressList);
    InitializeSRWLock(&ShimWaitAddressListLock);
    ShimWaitAddressListInit = TRUE;
}

VOID ShimAcquireWaitAddress(ShimWaitAddress *WaitAddress)
{
    AcquireSRWLockExclusive(&WaitAddress->ReferenceCountLock);
    WaitAddress->ReferenceCount++;
    ReleaseSRWLockExclusive(&WaitAddress->ReferenceCountLock);
}

VOID ShimReleaseWaitAddress(ShimWaitAddress *WaitAddress)
{
    AcquireSRWLockExclusive(&ShimWaitAddressListLock);

    AcquireSRWLockExclusive(&WaitAddress->ReferenceCountLock);
    WaitAddress->ReferenceCount--;

    if (WaitAddress->ReferenceCount < 1)
    {
        RemoveEntryList(&WaitAddress->ListEntry);
        ReleaseSRWLockExclusive(&WaitAddress->ReferenceCountLock);

        free(WaitAddress);
    }
    else
    {
        ReleaseSRWLockExclusive(&WaitAddress->ReferenceCountLock);
    }

    ReleaseSRWLockExclusive(&ShimWaitAddressListLock);
}

ShimWaitAddress *ShimFindWaitAddress(volatile VOID *Address, BOOL ReleaseLock)
{
    ShimWaitAddress *WaitAddress = NULL;

    ShimInitWaitAddressList();

    AcquireSRWLockExclusive(&ShimWaitAddressListLock);

    PLIST_ENTRY EndWaitAddress = &ShimWaitAddressList;
    PLIST_ENTRY CurrentWaitAddress = EndWaitAddress->Flink;

    while (CurrentWaitAddress != EndWaitAddress)
    {
        WaitAddress = CONTAINING_RECORD(CurrentWaitAddress, ShimWaitAddress, ListEntry);
        if (WaitAddress->Address == Address)
            goto result;

        CurrentWaitAddress = CurrentWaitAddress->Flink;
    }

result:
    if (ReleaseLock)
        ReleaseSRWLockExclusive(&ShimWaitAddressListLock);

    return WaitAddress;
}

ShimWaitAddress *ShimCreateOrUseWaitAddress(volatile VOID *Address)
{
    ShimWaitAddress *WaitAddress = ShimFindWaitAddress(Address, FALSE);
    if (WaitAddress)
        goto result;

    WaitAddress = malloc(sizeof(*WaitAddress));
    if (!WaitAddress)
    {
        goto result;
    }
    RtlZeroMemory(WaitAddress, sizeof(*WaitAddress));

    WaitAddress->Address = (PVOID)Address;

    InitializeSRWLock(&WaitAddress->ReferenceCountLock);

    InitializeSRWLock(&WaitAddress->SrwLock);
    InitializeConditionVariable(&WaitAddress->CondVar);
    InitializeListHead(&WaitAddress->ListEntry);

    InsertTailList(&ShimWaitAddressList, &WaitAddress->ListEntry);

result:
    if (WaitAddress)
        ShimAcquireWaitAddress(WaitAddress);

    ReleaseSRWLockExclusive(&ShimWaitAddressListLock);

    return WaitAddress;
}

WINBOOL WINAPI WaitOnAddress(volatile VOID *Address, PVOID CompareAddress, SIZE_T AddressSize, DWORD dwMilliseconds)
{
    BOOL Result = TRUE;
    ShimWaitAddress *WaitAddress = ShimCreateOrUseWaitAddress(Address);
    if (!WaitAddress)
    {
        Result = FALSE;
        goto result;
    }

    AcquireSRWLockExclusive(&WaitAddress->SrwLock);

    if (!memcmp(WaitAddress->Address, CompareAddress, AddressSize))
    {
        goto result;
    }

    Result = SleepConditionVariableSRW(&WaitAddress->CondVar, &WaitAddress->SrwLock, dwMilliseconds, 0);

result:
    if (WaitAddress)
    {
        ReleaseSRWLockExclusive(&WaitAddress->SrwLock);

        ShimReleaseWaitAddress(WaitAddress);
    }

    return Result;
}

VOID WINAPI WakeByAddressSingle(PVOID Address)
{
    ShimWaitAddress *WaitAddress = ShimFindWaitAddress(Address, TRUE);

    ShimAcquireWaitAddress(WaitAddress);

    if (WaitAddress)
    {
        AcquireSRWLockExclusive(&WaitAddress->SrwLock);
        WakeConditionVariable(&WaitAddress->CondVar);
        ReleaseSRWLockExclusive(&WaitAddress->SrwLock);

        ShimReleaseWaitAddress(WaitAddress);
    }
}

#endif

int ShimSynchApi()
{
    return 0;
}
