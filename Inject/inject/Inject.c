#include "Inject.h"
#include "../Memory.h"
#include "MemLoadDll.h"
#include <ntimage.h>

typedef NTSTATUS(NTAPI *_ZwCreateThreadEx)(
	OUT PHANDLE ThreadHandle,
	IN ACCESS_MASK DesiredAccess,
	IN POBJECT_ATTRIBUTES ObjectAttributes OPTIONAL,
	IN HANDLE ProcessHandle,
	IN PVOID StartRoutine,
	IN PVOID StartContext,
	IN ULONG CreateThreadFlags,
	IN SIZE_T ZeroBits OPTIONAL,
	IN SIZE_T StackSize OPTIONAL,
	IN SIZE_T MaximumStackSize OPTIONAL,
	IN PVOID AttributeList
	);


_ZwCreateThreadEx GetCreateThreadExFunc()
{

	static ULONG64 findFunc = NULL;
	if (findFunc) return (_ZwCreateThreadEx)findFunc;
	UNICODE_STRING unName = { 0 };
	RtlInitUnicodeString(&unName, L"ZwCreateSymbolicLinkObject");
	PUCHAR func = (PUCHAR)MmGetSystemRoutineAddress(&unName);
	func += 5;

	for (int i = 0; i < 0x30; i++)
	{
		if (func[i] == 0x48 && func[i + 1] == 0x8b && func[i + 2] == 0xc4)
		{
			findFunc = (ULONG64)(func + i);
			break;
		}
	}


	if (!findFunc) return NULL;

	KdPrint(("GetZwCreateThreadExAddr %llx\r\n", findFunc));
	return (_ZwCreateThreadEx)findFunc;
}

BOOLEAN CreateRemoteThreadByProcess(HANDLE pid, IN PVOID Address, IN ULONG64 Arg, PETHREAD * pthread)
{
	NTSTATUS Status = STATUS_UNSUCCESSFUL;
	KAPC_STATE Kpc = { 0, };
	PEPROCESS eprocess = NULL;
	ULONG64 ReginSize = 8;
	HANDLE hThread = NULL;

	_ZwCreateThreadEx threadFunc = GetCreateThreadExFunc();

	if (threadFunc == NULL)
	{
		KdPrintEx((77, 0, "û�л�ȡ���̺߳���ZwCreateThreadEx %X\r\n", Status));
		return FALSE;
	}



	Status = PsLookupProcessByProcessId(pid, &eprocess);

	if (!NT_SUCCESS(Status))
	{
		return FALSE;
	}

	ObDereferenceObject(eprocess);


	KeStackAttachProcess(eprocess, &Kpc);

	do
	{
		Status = threadFunc(&hThread, THREAD_ALL_ACCESS, NULL, NtCurrentProcess(), Address, Arg, 0, 0, 0x100000, 0x200000, NULL);
		if (!NT_SUCCESS(Status))
		{
			KdPrintEx((77, 0, "�����߳�ʧ�� %X\r\n", Status));
			break;
		}

		if (hThread)
		{
			ObReferenceObjectByHandle(hThread, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, pthread, NULL);
			ZwClose(hThread);
		}

	} while (0);

	KeUnstackDetachProcess(&Kpc);

	return TRUE;
}



NTSTATUS InjectX64(HANDLE pid, char * shellcode, SIZE_T shellcodeSize)
{
	PEPROCESS Process = NULL;
	NTSTATUS status = PsLookupProcessByProcessId(pid, &Process);
	KAPC_STATE kApcState = {0};

	if (!NT_SUCCESS(status))
	{
		return status;
	}

	if (PsGetProcessExitStatus(Process) != STATUS_PENDING)
	{
		ObDereferenceObject(Process);
		return NULL;
	}

	PUCHAR kfileDll = ExAllocatePool(PagedPool, shellcodeSize);
	memcpy(kfileDll, shellcode, shellcodeSize);

	BOOLEAN isuFileAllocatedll = FALSE;
	BOOLEAN isuShellcode = FALSE;
	BOOLEAN isuimageDll = FALSE;

	PUCHAR ufileDll = NULL;
	PUCHAR uShellcode = NULL;
	SIZE_T uShellcodeSize = 0;
	PUCHAR uImage = NULL;
	SIZE_T uImageSize = 0;

	KeStackAttachProcess(Process, &kApcState);
	do 
	{
		ufileDll = AllocateMemoryNotExecute(pid, shellcodeSize);

		if (!ufileDll)
		{
			break;
		}
		
		memcpy(ufileDll, kfileDll, shellcodeSize);

		isuFileAllocatedll = TRUE;

		uShellcode = AllocateMemory(pid, sizeof(MemLoadShellcode_x64));

		if (!uShellcode)
		{
			break;
		}

		isuShellcode = TRUE;

		memcpy(uShellcode, MemLoadShellcode_x64, sizeof(MemLoadShellcode_x64));

		PIMAGE_DOS_HEADER pDos = (PIMAGE_DOS_HEADER)ufileDll;
		PIMAGE_NT_HEADERS pNts = (PIMAGE_NT_HEADERS)(ufileDll + pDos->e_lfanew);
		uImageSize = pNts->OptionalHeader.SizeOfImage;

		uImage = AllocateMemory(pid, uImageSize);

		if (!uImage)
		{
			break;
		}

		uShellcode[0x50f] = 0x90;
		uShellcode[0x510] = 0x48;
		uShellcode[0x511] = 0xb8;
		*(PULONG64)&uShellcode[0x512] = (ULONG64)uImage;

		

		PETHREAD thread = NULL;
		if (CreateRemoteThreadByProcess(pid, uShellcode, ufileDll, &thread))
		{
			KeWaitForSingleObject(thread, Executive, KernelMode, FALSE, NULL);
			memset(uImage, 0, PAGE_SIZE);
		}
		else 
		{
			isuimageDll = TRUE;
		}
	} while (0);


	if (isuFileAllocatedll)
	{
		FreeMemory(pid, ufileDll, shellcodeSize);
	}

	if (isuShellcode)
	{
		FreeMemory(pid, uShellcode, uShellcodeSize);
	}

	if (isuimageDll)
	{
		FreeMemory(pid, uImage, uImageSize);
	}

	KeUnstackDetachProcess(&kApcState);

	ExFreePool(kfileDll);

	return status;
}

NTSTATUS InjectX86(HANDLE pid, char *shellcode, SIZE_T shellcodeSize)
{
	return STATUS_UNSUCCESSFUL;
}