///////////////////////////////////////////////////////////////////////////////
//
// Module Name:
//
//     kxgdi.c
//
// Abstract:
//
//     VxKex extension DLL for gdi32.dll.
//
//     All gdi32 exports are forwarded to the real gdi32.dll (see kxgdi.def).
//     Functions that do not exist on Windows 7 are implemented here on top of
//     APIs that Windows 7 does have.
//
//     Currently implemented:
//
//       D3DKMTOpenAdapterFromLuid (Windows 8+)
//         Emulated by enumerating all display adapters, opening each one with
//         D3DKMTOpenAdapterFromDeviceName / D3DKMTOpenAdapterFromGdiDisplayName
//         (both available since Vista) and returning the handle whose LUID
//         matches. Non-matching handles are closed again.
//
//     Usage with VxKex 1.2.0 or later: copy kxgdi.dll into KexDir\Kex64 and
//     set the IFEO value KEX_DllRewriteEntries to "gdi32:kxgdi" for the
//     program. The replacement name must not be longer than the original
//     name ("kxgdi" and "gdi32" are both 5 characters), because VxKex rewrites
//     import names in place.
//
//     kxgdi.dll MUST live in KexDir\Kex64 (or Kex32). VxKex does not rewrite
//     imports of DLLs inside KexDir, so our own import of gdi32 reaches the
//     real gdi32.dll. Anywhere else, VxKex would rewrite kxgdi's own gdi32
//     import to kxgdi itself.
//
//     This file deliberately uses no Windows SDK headers and no C runtime, so
//     it can be built with nothing more than cl.exe, link.exe and lib.exe.
//
// Debug output:
//
//     Every call to an emulated function is reported via OutputDebugStringW
//     with the prefix "KxGdi:". Use Sysinternals DebugView to watch it.
//
///////////////////////////////////////////////////////////////////////////////

//
// Minimal Win32 type definitions (no SDK headers).
//

typedef int					BOOL;
typedef unsigned char		BYTE;
typedef unsigned short		WORD;
typedef unsigned short		WCHAR;
typedef unsigned long		DWORD;
typedef long				LONG;
typedef long				NTSTATUS;
typedef unsigned int		UINT;
typedef void				*PVOID;
typedef void				*HANDLE;
typedef void				*HMODULE;
typedef void				*HWND;
typedef const WCHAR			*PCWSTR;
typedef WCHAR				*PWSTR;

#ifdef _WIN64
typedef unsigned __int64	SIZE_T;
typedef unsigned __int64	ULONG_PTR;
#else
typedef unsigned int		SIZE_T;
typedef unsigned long		ULONG_PTR;
#endif

typedef struct _LUID {
	DWORD	LowPart;
	LONG	HighPart;
} LUID;

typedef struct _GUID {
	DWORD	Data1;
	WORD	Data2;
	WORD	Data3;
	BYTE	Data4[8];
} GUID;

#define WINAPI					__stdcall
#define TRUE					1
#define FALSE					0
#define NULL					((void *) 0)
#define INVALID_HANDLE_VALUE	((HANDLE) (LONG_PTR_MINUS_ONE))
#define LONG_PTR_MINUS_ONE		(~(ULONG_PTR) 0)
#define DLL_PROCESS_ATTACH		1
#define HEAP_ZERO_MEMORY		0x00000008
#define NT_SUCCESS(Status)		(((NTSTATUS) (Status)) >= 0)

#define STATUS_SUCCESS				((NTSTATUS) 0x00000000L)
#define STATUS_INVALID_PARAMETER	((NTSTATUS) 0xC000000DL)

//
// D3DKMT definitions (from d3dkmthk.h, WDK).
//

typedef UINT D3DKMT_HANDLE;

typedef struct _D3DKMT_OPENADAPTERFROMLUID {
	LUID			AdapterLuid;	// in
	D3DKMT_HANDLE	hAdapter;		// out
} D3DKMT_OPENADAPTERFROMLUID;

typedef struct _D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME {
	WCHAR			DeviceName[32];	// in
	D3DKMT_HANDLE	hAdapter;		// out
	LUID			AdapterLuid;	// out
	UINT			VidPnSourceId;	// out
} D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME;

typedef struct _D3DKMT_OPENADAPTERFROMDEVICENAME {
	PCWSTR			pDeviceName;	// in
	D3DKMT_HANDLE	hAdapter;		// out
	LUID			AdapterLuid;	// out
} D3DKMT_OPENADAPTERFROMDEVICENAME;

typedef struct _D3DKMT_CLOSEADAPTER {
	D3DKMT_HANDLE	hAdapter;		// in
} D3DKMT_CLOSEADAPTER;

//
// user32 / setupapi definitions (resolved dynamically).
//

typedef struct _DISPLAY_DEVICEW {
	DWORD	cb;
	WCHAR	DeviceName[32];
	WCHAR	DeviceString[128];
	DWORD	StateFlags;
	WCHAR	DeviceID[128];
	WCHAR	DeviceKey[128];
} DISPLAY_DEVICEW;

typedef struct _SP_DEVICE_INTERFACE_DATA {
	DWORD		cbSize;
	GUID		InterfaceClassGuid;
	DWORD		Flags;
	ULONG_PTR	Reserved;
} SP_DEVICE_INTERFACE_DATA;

// SP_DEVICE_INTERFACE_DETAIL_DATA_W: DWORD cbSize; WCHAR DevicePath[ANYSIZE_ARRAY];
// setupapi.h packs this structure to 1 byte on 32-bit Windows, so the expected
// cbSize is 6 on x86 and 8 on x64.
#ifdef _WIN64
#  define SP_DEVICE_INTERFACE_DETAIL_DATA_W_CBSIZE 8
#else
#  define SP_DEVICE_INTERFACE_DETAIL_DATA_W_CBSIZE 6
#endif

#define DIGCF_PRESENT			0x00000002
#define DIGCF_DEVICEINTERFACE	0x00000010

// {1CA05180-A699-450A-9A0C-DE4FBE3DDD89} (ntddvdeo.h)
static const GUID GUID_DISPLAY_DEVICE_ARRIVAL = {
	0x1CA05180, 0xA699, 0x450A, {0x9A, 0x0C, 0xDE, 0x4F, 0xBE, 0x3D, 0xDD, 0x89}
};

typedef BOOL (WINAPI *PFN_ENUMDISPLAYDEVICESW)(
	PCWSTR Device, DWORD DevNum, DISPLAY_DEVICEW *DisplayDevice, DWORD Flags);

typedef HANDLE (WINAPI *PFN_SETUPDIGETCLASSDEVSW)(
	const GUID *ClassGuid, PCWSTR Enumerator, HWND Parent, DWORD Flags);

typedef BOOL (WINAPI *PFN_SETUPDIENUMDEVICEINTERFACES)(
	HANDLE DeviceInfoSet, PVOID DeviceInfoData, const GUID *InterfaceClassGuid,
	DWORD MemberIndex, SP_DEVICE_INTERFACE_DATA *DeviceInterfaceData);

typedef BOOL (WINAPI *PFN_SETUPDIGETDEVICEINTERFACEDETAILW)(
	HANDLE DeviceInfoSet, SP_DEVICE_INTERFACE_DATA *DeviceInterfaceData,
	PVOID DeviceInterfaceDetailData, DWORD DeviceInterfaceDetailDataSize,
	DWORD *RequiredSize, PVOID DeviceInfoData);

typedef BOOL (WINAPI *PFN_SETUPDIDESTROYDEVICEINFOLIST)(HANDLE DeviceInfoSet);

//
// Static imports. kernel32 and gdi32 functions used here all exist on Windows 7.
//

__declspec(dllimport) HMODULE	WINAPI LoadLibraryW(PCWSTR LibFileName);
__declspec(dllimport) PVOID		WINAPI GetProcAddress(HMODULE Module, const char *ProcName);
__declspec(dllimport) BOOL		WINAPI DisableThreadLibraryCalls(HMODULE Module);
__declspec(dllimport) HANDLE	WINAPI GetProcessHeap(void);
__declspec(dllimport) PVOID		WINAPI HeapAlloc(HANDLE Heap, DWORD Flags, SIZE_T Bytes);
__declspec(dllimport) BOOL		WINAPI HeapFree(HANDLE Heap, DWORD Flags, PVOID Mem);
__declspec(dllimport) void		WINAPI OutputDebugStringW(PCWSTR OutputString);
__declspec(dllimport) HMODULE	WINAPI GetModuleHandleW(PCWSTR ModuleName);

typedef PCWSTR (*PFN_KXGDILDRGETLOG)(void);

__declspec(dllimport) NTSTATUS	WINAPI D3DKMTOpenAdapterFromGdiDisplayName(
	D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME *Args);

__declspec(dllimport) NTSTATUS	WINAPI D3DKMTOpenAdapterFromDeviceName(
	D3DKMT_OPENADAPTERFROMDEVICENAME *Args);

__declspec(dllimport) NTSTATUS	WINAPI D3DKMTCloseAdapter(
	const D3DKMT_CLOSEADAPTER *Args);

//
// No CRT: the compiler may still emit calls to memset/memcpy for struct
// initialization, so provide them.
//

#pragma function(memset)
void *memset(void *Destination, int Value, SIZE_T Count)
{
	volatile BYTE *Pointer = (volatile BYTE *) Destination;

	while (Count--) {
		*Pointer++ = (BYTE) Value;
	}

	return Destination;
}

#pragma function(memcpy)
void *memcpy(void *Destination, const void *Source, SIZE_T Count)
{
	volatile BYTE *Out = (volatile BYTE *) Destination;
	const BYTE *In = (const BYTE *) Source;

	while (Count--) {
		*Out++ = *In++;
	}

	return Destination;
}

//
// Tiny logging helpers (OutputDebugStringW, no wsprintf).
//

#define LOG_BUFFER_CCH 256

typedef struct _LOG_BUFFER {
	WCHAR	Text[LOG_BUFFER_CCH];
	UINT	Length;
} LOG_BUFFER;

static void LogAppend(LOG_BUFFER *Log, PCWSTR String)
{
	while (*String && Log->Length < LOG_BUFFER_CCH - 1) {
		Log->Text[Log->Length++] = *String++;
	}

	Log->Text[Log->Length] = 0;
}

static void LogAppendHex(LOG_BUFFER *Log, DWORD Value)
{
	static const WCHAR Digits[] = L"0123456789ABCDEF";
	WCHAR Hex[11];
	int Index;

	Hex[0] = '0';
	Hex[1] = 'x';

	for (Index = 0; Index < 8; ++Index) {
		Hex[2 + Index] = Digits[(Value >> (28 - 4 * Index)) & 0xF];
	}

	Hex[10] = 0;
	LogAppend(Log, Hex);
}

static void LogAppendLuid(LOG_BUFFER *Log, LUID Luid)
{
	LogAppendHex(Log, (DWORD) Luid.HighPart);
	LogAppend(Log, L":");
	LogAppendHex(Log, Luid.LowPart);
}

static void LogFlush(LOG_BUFFER *Log)
{
	LogAppend(Log, L"\r\n");
	OutputDebugStringW(Log->Text);
}

static BOOL LuidEqual(LUID A, LUID B)
{
	return A.LowPart == B.LowPart && A.HighPart == B.HighPart;
}

static void CloseKmtAdapter(D3DKMT_HANDLE Adapter)
{
	D3DKMT_CLOSEADAPTER CloseArgs;

	CloseArgs.hAdapter = Adapter;
	D3DKMTCloseAdapter(&CloseArgs);
}

//
// Strategy 1: enumerate display adapter device interfaces via SetupAPI and
// open each one with D3DKMTOpenAdapterFromDeviceName. This also finds adapters
// without an active display output (e.g. render-only GPUs in laptops).
//
// Returns TRUE and stores the handle in *Adapter if a matching adapter was found.
//

static BOOL OpenAdapterByLuidFromDeviceInterfaces(
	LUID			Luid,
	D3DKMT_HANDLE	*Adapter)
{
	HMODULE SetupApi;
	PFN_SETUPDIGETCLASSDEVSW pSetupDiGetClassDevsW;
	PFN_SETUPDIENUMDEVICEINTERFACES pSetupDiEnumDeviceInterfaces;
	PFN_SETUPDIGETDEVICEINTERFACEDETAILW pSetupDiGetDeviceInterfaceDetailW;
	PFN_SETUPDIDESTROYDEVICEINFOLIST pSetupDiDestroyDeviceInfoList;
	HANDLE DevInfo;
	HANDLE Heap;
	DWORD Index;
	BOOL Found;

	Found = FALSE;

	SetupApi = LoadLibraryW(L"setupapi.dll");
	if (!SetupApi) {
		return FALSE;
	}

	pSetupDiGetClassDevsW = (PFN_SETUPDIGETCLASSDEVSW)
		GetProcAddress(SetupApi, "SetupDiGetClassDevsW");
	pSetupDiEnumDeviceInterfaces = (PFN_SETUPDIENUMDEVICEINTERFACES)
		GetProcAddress(SetupApi, "SetupDiEnumDeviceInterfaces");
	pSetupDiGetDeviceInterfaceDetailW = (PFN_SETUPDIGETDEVICEINTERFACEDETAILW)
		GetProcAddress(SetupApi, "SetupDiGetDeviceInterfaceDetailW");
	pSetupDiDestroyDeviceInfoList = (PFN_SETUPDIDESTROYDEVICEINFOLIST)
		GetProcAddress(SetupApi, "SetupDiDestroyDeviceInfoList");

	if (!pSetupDiGetClassDevsW || !pSetupDiEnumDeviceInterfaces ||
		!pSetupDiGetDeviceInterfaceDetailW || !pSetupDiDestroyDeviceInfoList) {

		return FALSE;
	}

	DevInfo = pSetupDiGetClassDevsW(
		&GUID_DISPLAY_DEVICE_ARRIVAL,
		NULL,
		NULL,
		DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

	if (DevInfo == INVALID_HANDLE_VALUE) {
		return FALSE;
	}

	Heap = GetProcessHeap();

	for (Index = 0; !Found; ++Index) {
		SP_DEVICE_INTERFACE_DATA InterfaceData;
		DWORD RequiredSize;
		BYTE *Detail;
		D3DKMT_OPENADAPTERFROMDEVICENAME OpenArgs;
		NTSTATUS Status;

		InterfaceData.cbSize = sizeof(InterfaceData);

		if (!pSetupDiEnumDeviceInterfaces(
				DevInfo, NULL, &GUID_DISPLAY_DEVICE_ARRIVAL, Index, &InterfaceData)) {

			break; // ERROR_NO_MORE_ITEMS (or any other error)
		}

		RequiredSize = 0;
		pSetupDiGetDeviceInterfaceDetailW(DevInfo, &InterfaceData, NULL, 0, &RequiredSize, NULL);

		if (RequiredSize < SP_DEVICE_INTERFACE_DETAIL_DATA_W_CBSIZE + sizeof(WCHAR)) {
			continue;
		}

		Detail = (BYTE *) HeapAlloc(Heap, HEAP_ZERO_MEMORY, RequiredSize);
		if (!Detail) {
			continue;
		}

		*(DWORD *) Detail = SP_DEVICE_INTERFACE_DETAIL_DATA_W_CBSIZE;

		if (pSetupDiGetDeviceInterfaceDetailW(
				DevInfo, &InterfaceData, Detail, RequiredSize, NULL, NULL)) {

			// DevicePath starts directly after the DWORD cbSize member.
			OpenArgs.pDeviceName = (PCWSTR) (Detail + sizeof(DWORD));
			OpenArgs.hAdapter = 0;
			OpenArgs.AdapterLuid.LowPart = 0;
			OpenArgs.AdapterLuid.HighPart = 0;

			Status = D3DKMTOpenAdapterFromDeviceName(&OpenArgs);

			if (NT_SUCCESS(Status)) {
				if (LuidEqual(OpenArgs.AdapterLuid, Luid)) {
					*Adapter = OpenArgs.hAdapter;
					Found = TRUE;
				} else {
					CloseKmtAdapter(OpenArgs.hAdapter);
				}
			}
		}

		HeapFree(Heap, 0, Detail);
	}

	pSetupDiDestroyDeviceInfoList(DevInfo);

	// setupapi.dll stays loaded on purpose (cheap, and avoids reload costs).
	return Found;
}

//
// Strategy 2 (fallback): enumerate GDI display devices (\\.\DISPLAY1, ...) and
// open each with D3DKMTOpenAdapterFromGdiDisplayName. Only finds adapters that
// drive at least one display that is part of the desktop.
//

static BOOL OpenAdapterByLuidFromGdiDisplayNames(
	LUID			Luid,
	D3DKMT_HANDLE	*Adapter)
{
	HMODULE User32;
	PFN_ENUMDISPLAYDEVICESW pEnumDisplayDevicesW;
	DWORD Index;

	User32 = LoadLibraryW(L"user32.dll");
	if (!User32) {
		return FALSE;
	}

	pEnumDisplayDevicesW = (PFN_ENUMDISPLAYDEVICESW)
		GetProcAddress(User32, "EnumDisplayDevicesW");

	if (!pEnumDisplayDevicesW) {
		return FALSE;
	}

	for (Index = 0; ; ++Index) {
		DISPLAY_DEVICEW DisplayDevice;
		D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME OpenArgs;
		NTSTATUS Status;
		UINT CharIndex;

		memset(&DisplayDevice, 0, sizeof(DisplayDevice));
		DisplayDevice.cb = sizeof(DisplayDevice);

		if (!pEnumDisplayDevicesW(NULL, Index, &DisplayDevice, 0)) {
			break;
		}

		memset(&OpenArgs, 0, sizeof(OpenArgs));

		for (CharIndex = 0; CharIndex < 31 && DisplayDevice.DeviceName[CharIndex]; ++CharIndex) {
			OpenArgs.DeviceName[CharIndex] = DisplayDevice.DeviceName[CharIndex];
		}

		Status = D3DKMTOpenAdapterFromGdiDisplayName(&OpenArgs);

		if (!NT_SUCCESS(Status)) {
			continue; // e.g. display not attached to the desktop
		}

		if (LuidEqual(OpenArgs.AdapterLuid, Luid)) {
			*Adapter = OpenArgs.hAdapter;
			return TRUE;
		}

		CloseKmtAdapter(OpenArgs.hAdapter);
	}

	return FALSE;
}

//
// Exported: NTSTATUS APIENTRY D3DKMTOpenAdapterFromLuid(
//               const D3DKMT_OPENADAPTERFROMLUID *unnamedParam1);
//
// (The SDK declares the parameter const, but hAdapter is an output.)
//

NTSTATUS WINAPI Ext_D3DKMTOpenAdapterFromLuid(
	D3DKMT_OPENADAPTERFROMLUID *Args)
{
	LOG_BUFFER Log;
	D3DKMT_HANDLE Adapter;
	PCWSTR Method;

	Log.Length = 0;
	LogAppend(&Log, L"KxGdi: D3DKMTOpenAdapterFromLuid(");

	if (!Args) {
		LogAppend(&Log, L"NULL) -> STATUS_INVALID_PARAMETER");
		LogFlush(&Log);
		return STATUS_INVALID_PARAMETER;
	}

	LogAppendLuid(&Log, Args->AdapterLuid);
	LogAppend(&Log, L") -> ");

	Adapter = 0;
	Method = NULL;

	if (OpenAdapterByLuidFromDeviceInterfaces(Args->AdapterLuid, &Adapter)) {
		Method = L"device interface";
	} else if (OpenAdapterByLuidFromGdiDisplayNames(Args->AdapterLuid, &Adapter)) {
		Method = L"GDI display name";
	}

	if (!Method) {
		LogAppend(&Log, L"no adapter with this LUID, STATUS_INVALID_PARAMETER");
		LogFlush(&Log);
		return STATUS_INVALID_PARAMETER;
	}

	Args->hAdapter = Adapter;

	LogAppend(&Log, L"hAdapter ");
	LogAppendHex(&Log, Adapter);
	LogAppend(&Log, L" via ");
	LogAppend(&Log, Method);
	LogFlush(&Log);

	return STATUS_SUCCESS;
}

//
// DLL entry point (no CRT).
//

BOOL WINAPI DllMain(
	HMODULE	DllHandle,
	DWORD	Reason,
	PVOID	Reserved)
{
	(void) Reserved;

	if (Reason == DLL_PROCESS_ATTACH) {
		HMODULE Loader;

		DisableThreadLibraryCalls(DllHandle);

		//
		// When the standalone loader (kxgdildr.dll) brought us in, print its
		// log now. It ran before kernel32 was usable and could not print
		// anything itself.
		//

		Loader = GetModuleHandleW(L"kxgdildr.dll");
		if (Loader) {
			PFN_KXGDILDRGETLOG GetLog = (PFN_KXGDILDRGETLOG) GetProcAddress(Loader, "KxGdiLdrGetLog");
			if (GetLog) {
				PCWSTR Text = GetLog();
				if (Text && Text[0]) OutputDebugStringW(Text);
			}
		}
	}

	return TRUE;
}
