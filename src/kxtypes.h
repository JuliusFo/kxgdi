///////////////////////////////////////////////////////////////////////////////
//
// kxtypes.h - minimal Windows/NT type definitions for kxgdildr and its tests.
//
// The project is built without the Windows SDK, so only what is needed is
// declared here. x64 only.
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

typedef int					BOOL;
typedef unsigned char		BYTE;
typedef char				CHAR;
typedef unsigned short		WORD;
typedef unsigned short		USHORT;
typedef unsigned short		WCHAR;
typedef unsigned long		DWORD;
typedef unsigned long		ULONG;
typedef long				LONG;
typedef long				NTSTATUS;
typedef unsigned int		UINT;
typedef unsigned __int64	ULONGLONG;
typedef unsigned __int64	SIZE_T;
typedef unsigned __int64	ULONG_PTR;
typedef void				*PVOID;
typedef void				*HANDLE;
typedef void				*HMODULE;
typedef const WCHAR			*PCWSTR;
typedef WCHAR				*PWSTR;
typedef const CHAR			*PCSTR;

#define WINAPI				__stdcall
#define NTAPI				__stdcall
#define TRUE				1
#define FALSE				0
#define NULL				((void *) 0)

#define NT_SUCCESS(Status)	(((NTSTATUS) (Status)) >= 0)

#define STATUS_SUCCESS					((NTSTATUS) 0x00000000L)
#define STATUS_UNSUCCESSFUL				((NTSTATUS) 0xC0000001L)
#define STATUS_ACCESS_VIOLATION			((NTSTATUS) 0xC0000005L)
#define STATUS_INVALID_PARAMETER		((NTSTATUS) 0xC000000DL)
#define STATUS_INVALID_IMAGE_FORMAT		((NTSTATUS) 0xC000007BL)

#define DLL_PROCESS_DETACH		0
#define DLL_PROCESS_ATTACH		1
#define DLL_PROCESS_VERIFIER	4

#define PAGE_READWRITE			0x04
#define NtCurrentProcess()		((HANDLE) (LONG_PTR) -1)
typedef __int64					LONG_PTR;

typedef struct _UNICODE_STRING {
	USHORT	Length;			// bytes, without terminator
	USHORT	MaximumLength;
	PWSTR	Buffer;
} UNICODE_STRING;

typedef struct _ANSI_STRING {
	USHORT	Length;
	USHORT	MaximumLength;
	CHAR	*Buffer;
} ANSI_STRING;

//
// PE image structures (PE32+ only).
//

#define IMAGE_DOS_SIGNATURE				0x5A4D		// MZ
#define IMAGE_NT_SIGNATURE				0x00004550	// PE\0\0
#define IMAGE_NT_OPTIONAL_HDR64_MAGIC	0x20B

#define IMAGE_DIRECTORY_ENTRY_IMPORT		1
#define IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT	11
#define IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT	13

typedef struct _IMAGE_DOS_HEADER {
	WORD	e_magic;
	WORD	e_unused[29];
	LONG	e_lfanew;
} IMAGE_DOS_HEADER;

typedef struct _IMAGE_FILE_HEADER {
	WORD	Machine;
	WORD	NumberOfSections;
	DWORD	TimeDateStamp;
	DWORD	PointerToSymbolTable;
	DWORD	NumberOfSymbols;
	WORD	SizeOfOptionalHeader;
	WORD	Characteristics;
} IMAGE_FILE_HEADER;

typedef struct _IMAGE_DATA_DIRECTORY {
	DWORD	VirtualAddress;
	DWORD	Size;
} IMAGE_DATA_DIRECTORY;

typedef struct _IMAGE_OPTIONAL_HEADER64 {
	WORD		Magic;
	BYTE		MajorLinkerVersion;
	BYTE		MinorLinkerVersion;
	DWORD		SizeOfCode;
	DWORD		SizeOfInitializedData;
	DWORD		SizeOfUninitializedData;
	DWORD		AddressOfEntryPoint;
	DWORD		BaseOfCode;
	ULONGLONG	ImageBase;
	DWORD		SectionAlignment;
	DWORD		FileAlignment;
	WORD		MajorOperatingSystemVersion;
	WORD		MinorOperatingSystemVersion;
	WORD		MajorImageVersion;
	WORD		MinorImageVersion;
	WORD		MajorSubsystemVersion;
	WORD		MinorSubsystemVersion;
	DWORD		Win32VersionValue;
	DWORD		SizeOfImage;
	DWORD		SizeOfHeaders;
	DWORD		CheckSum;
	WORD		Subsystem;
	WORD		DllCharacteristics;
	ULONGLONG	SizeOfStackReserve;
	ULONGLONG	SizeOfStackCommit;
	ULONGLONG	SizeOfHeapReserve;
	ULONGLONG	SizeOfHeapCommit;
	DWORD		LoaderFlags;
	DWORD		NumberOfRvaAndSizes;
	IMAGE_DATA_DIRECTORY DataDirectory[16];
} IMAGE_OPTIONAL_HEADER64;

typedef struct _IMAGE_NT_HEADERS64 {
	DWORD					Signature;
	IMAGE_FILE_HEADER		FileHeader;
	IMAGE_OPTIONAL_HEADER64	OptionalHeader;
} IMAGE_NT_HEADERS64;

typedef struct _IMAGE_IMPORT_DESCRIPTOR {
	DWORD	OriginalFirstThunk;
	DWORD	TimeDateStamp;		// non-zero = bound
	DWORD	ForwarderChain;
	DWORD	Name;
	DWORD	FirstThunk;
} IMAGE_IMPORT_DESCRIPTOR;

typedef struct _IMAGE_DELAYLOAD_DESCRIPTOR {
	DWORD	Attributes;			// bit 0: RVA based
	DWORD	DllNameRVA;
	DWORD	ModuleHandleRVA;
	DWORD	ImportAddressTableRVA;
	DWORD	ImportNameTableRVA;
	DWORD	BoundImportAddressTableRVA;
	DWORD	UnloadInformationTableRVA;
	DWORD	TimeDateStamp;
} IMAGE_DELAYLOAD_DESCRIPTOR;

//
// Loader notifications (Windows Vista and later).
//

#define LDR_DLL_NOTIFICATION_REASON_LOADED		1

typedef struct _LDR_DLL_LOADED_NOTIFICATION_DATA {
	ULONG					Flags;
	const UNICODE_STRING	*FullDllName;
	const UNICODE_STRING	*BaseDllName;
	PVOID					DllBase;
	ULONG					SizeOfImage;
} LDR_DLL_LOADED_NOTIFICATION_DATA;

typedef void (NTAPI *PLDR_DLL_NOTIFICATION_FUNCTION)(
	ULONG								NotificationReason,
	const LDR_DLL_LOADED_NOTIFICATION_DATA	*NotificationData,
	PVOID								Context);

//
// Application Verifier provider interface.
//

typedef struct _RTL_VERIFIER_DLL_DESCRIPTOR {
	PWSTR	DllName;
	DWORD	DllFlags;
	PVOID	DllAddress;
	PVOID	DllThunks;
} RTL_VERIFIER_DLL_DESCRIPTOR;

typedef struct _RTL_VERIFIER_PROVIDER_DESCRIPTOR {
	DWORD						Length;
	RTL_VERIFIER_DLL_DESCRIPTOR	*ProviderDlls;
	PVOID						ProviderDllLoadCallback;
	PVOID						ProviderDllUnloadCallback;
	PWSTR						VerifierImage;
	DWORD						VerifierFlags;
	DWORD						VerifierDebug;
	PVOID						RtlpGetStackTraceAddress;
	PVOID						RtlpDebugPageHeapCreate;
	PVOID						RtlpDebugPageHeapDestroy;
	PVOID						ProviderNtdllHeapFreeCallback;
} RTL_VERIFIER_PROVIDER_DESCRIPTOR;

//
// ntdll imports (all available on Windows 7).
//

__declspec(dllimport) NTSTATUS NTAPI LdrGetDllHandle(
	PWSTR DllPath, ULONG *DllCharacteristics, UNICODE_STRING *DllName, PVOID *DllHandle);
__declspec(dllimport) NTSTATUS NTAPI LdrGetProcedureAddress(
	PVOID DllHandle, ANSI_STRING *ProcedureName, ULONG ProcedureNumber, PVOID *ProcedureAddress);
__declspec(dllimport) NTSTATUS NTAPI LdrRegisterDllNotification(
	ULONG Flags, PLDR_DLL_NOTIFICATION_FUNCTION Function, PVOID Context, PVOID *Cookie);
__declspec(dllimport) NTSTATUS NTAPI NtProtectVirtualMemory(
	HANDLE Process, PVOID *BaseAddress, SIZE_T *RegionSize, ULONG NewProtect, ULONG *OldProtect);
__declspec(dllimport) NTSTATUS NTAPI NtSetInformationProcess(
	HANDLE Process, ULONG InformationClass, PVOID Information, ULONG Length);
__declspec(dllimport) void NTAPI RtlGetNtVersionNumbers(
	ULONG *Major, ULONG *Minor, ULONG *Build);

#define ProcessHandleTracing	32

//
// File I/O without kernel32 (log file).
//

typedef struct _OBJECT_ATTRIBUTES {
	ULONG			Length;
	HANDLE			RootDirectory;
	UNICODE_STRING	*ObjectName;
	ULONG			Attributes;
	PVOID			SecurityDescriptor;
	PVOID			SecurityQualityOfService;
} OBJECT_ATTRIBUTES;

typedef struct _IO_STATUS_BLOCK {
	union {
		NTSTATUS	Status;
		PVOID		Pointer;
	} u;
	ULONG_PTR		Information;
} IO_STATUS_BLOCK;

#define OBJ_CASE_INSENSITIVE			0x00000040
#define GENERIC_WRITE					0x40000000
#define SYNCHRONIZE						0x00100000
#define FILE_ATTRIBUTE_NORMAL			0x00000080
#define FILE_SHARE_READ					0x00000001
#define FILE_OVERWRITE_IF				0x00000005
#define FILE_SYNCHRONOUS_IO_NONALERT	0x00000020
#define FILE_NON_DIRECTORY_FILE			0x00000040

__declspec(dllimport) NTSTATUS NTAPI NtCreateFile(
	HANDLE *FileHandle, ULONG DesiredAccess, OBJECT_ATTRIBUTES *ObjectAttributes,
	IO_STATUS_BLOCK *IoStatusBlock, PVOID AllocationSize, ULONG FileAttributes,
	ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions,
	PVOID EaBuffer, ULONG EaLength);
__declspec(dllimport) NTSTATUS NTAPI NtWriteFile(
	HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext,
	IO_STATUS_BLOCK *IoStatusBlock, const void *Buffer, ULONG Length,
	PVOID ByteOffset, ULONG *Key);

//
// PEB access (x64).
//

unsigned __int64 __readgsqword(unsigned long Offset);
#pragma intrinsic(__readgsqword)

#define KxCurrentPeb()				((BYTE *) __readgsqword(0x60))
#define KxPebImageBaseAddress()		(*(PVOID *) (KxCurrentPeb() + 0x10))
#define KxPebNtGlobalFlag()			((ULONG *) (KxCurrentPeb() + 0xBC))
// PEB.ProcessParameters (+0x20) -> RTL_USER_PROCESS_PARAMETERS.ImagePathName (+0x60)
#define KxPebImagePathName()		((UNICODE_STRING *) (*(BYTE **) (KxCurrentPeb() + 0x20) + 0x60))

#define FLG_APPLICATION_VERIFIER	0x00000100
#define FLG_HEAP_PAGE_ALLOCS		0x02000000

// KUSER_SHARED_DATA.NtSystemRoot, e.g. L"C:\\Windows"
#define KxSharedNtSystemRoot()		((PCWSTR) (ULONG_PTR) 0x7FFE0030)
