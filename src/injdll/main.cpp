#define _ARM_WINAPI_PARTITION_DESKTOP_SDK_AVAILABLE 1

#include "wow64log.h"
#include <windows.h>

//
// Include NTDLL-related headers.
//
#define NTDLL_NO_INLINE_INIT_STRING
#include <ntdll.h>






#define __STAGE2DLLPATHPREFIX_A(x) x"C:\\SDAG\\SDAG_dll_injection\\"
#if defined(_M_IX86)
#define __STAGE2DLLNAME_A(x) x"SDAG_injected_dll_x86.dll"
#elif defined(_M_AMD64)
#define __STAGE2DLLNAME_A(x) x"SDAG_injected_dll_x64.dll"
#else
#  error Unknown architecture
#endif

#define STAGE2DLLPATHPREFIX_A __STAGE2DLLPATHPREFIX_A("")
#define STAGE2DLLNAME_A __STAGE2DLLNAME_A("")
#define STAGE2DLLPATH_A STAGE2DLLPATHPREFIX_A STAGE2DLLNAME_A

#define STAGE2DLLPATHPREFIX_W __STAGE2DLLPATHPREFIX_A(L)
#define STAGE2DLLNAME_W __STAGE2DLLNAME_A(L)
#define STAGE2DLLPATH_W STAGE2DLLPATHPREFIX_W STAGE2DLLNAME_W


#if defined(_M_IX86)
#  define ARCH_A         "x86"
#  define ARCH_W         L"x86"
#elif defined(_M_AMD64)
#  define ARCH_A          "x64"
#  define ARCH_W         L"x64"
#elif defined(_M_ARM)
#  define ARCH_A          "ARM32"
#  define ARCH_W         L"ARM32"
#elif defined(_M_ARM64)
#  define ARCH_A          "ARM64"
#  define ARCH_W         L"ARM64"
#else
#  error Unknown architecture
#endif


// size_t strlen(const char * str)
// {
//   const char *s;
//   for (s = str; *s; ++s) {}
//   return(s - str);
// }

//
// Include support for ETW logging.
// Note that following functions are mocked, because they're
// located in advapi32.dll.  Fortunatelly, advapi32.dll simply
// redirects calls to these functions to the ntdll.dll.
//

#define EventActivityIdControl  EtwEventActivityIdControl
#define EventEnabled            EtwEventEnabled
#define EventProviderEnabled    EtwEventProviderEnabled
#define EventRegister           EtwEventRegister
#define EventSetInformation     EtwEventSetInformation
#define EventUnregister         EtwEventUnregister
#define EventWrite              EtwEventWrite
#define EventWriteEndScenario   EtwEventWriteEndScenario
#define EventWriteEx            EtwEventWriteEx
#define EventWriteStartScenario EtwEventWriteStartScenario
#define EventWriteString        EtwEventWriteString
#define EventWriteTransfer      EtwEventWriteTransfer

#include <evntprov.h>

//
// Include Detours.
//

#include <detours.h>



//
// This is necessary for x86 builds because of SEH,
// which is used by Detours.  Look at loadcfg.c file
// in Visual Studio's CRT source codes for the original
// implementation.
//

#if defined(_M_IX86) || defined(_X86_)

EXTERN_C PVOID __safe_se_handler_table[]; /* base of safe handler entry table */
EXTERN_C BYTE  __safe_se_handler_count;   /* absolute symbol whose address is
                                             the count of table entries */
EXTERN_C
CONST
DECLSPEC_SELECTANY
IMAGE_LOAD_CONFIG_DIRECTORY
_load_config_used = {
    sizeof(_load_config_used),
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    (SIZE_T)__safe_se_handler_table,
    (SIZE_T)&__safe_se_handler_count,
};

#endif

//
// Unfortunatelly sprintf-like functions are not exposed
// by ntdll.lib, which we're linking against.  We have to
// load them dynamically.
//

using _snwprintf_fn_t = int (__cdecl*)(
  wchar_t *buffer,
  size_t count,
  const wchar_t *format,
  ...
  );

inline _snwprintf_fn_t _snwprintf = nullptr;

using _vsnprintf_fn_t = int(__cdecl*)(
  char* buffer,
  size_t count,
  const char* format,
  va_list argptr
  );
inline _vsnprintf_fn_t _vsnprintf = nullptr;

void mydebugprint(const char* format, ...)
{
  if (_vsnprintf != nullptr)
  {
    va_list args;
    va_start(args, format);
    char buffer[1024];
    _vsnprintf(buffer, RTL_NUMBER_OF(buffer), format, args);
    va_end(args);
    OutputDebugStringA(buffer);
  }
}

//
// ETW provider GUID and global provider handle.
//

//
// GUID:
//   {a4b4ba50-a667-43f5-919b-1e52a6d69bd5}
//

GUID ProviderGuid = {
  0xa4b4ba50, 0xa667, 0x43f5, { 0x91, 0x9b, 0x1e, 0x52, 0xa6, 0xd6, 0x9b, 0xd5 }
};

REGHANDLE ProviderHandle;

//
// Hooking functions and prototypes.
//

inline decltype(NtQuerySystemInformation)* OrigNtQuerySystemInformation = nullptr;

EXTERN_C
NTSTATUS
NTAPI
HookNtQuerySystemInformation(
  _In_ SYSTEM_INFORMATION_CLASS SystemInformationClass,
  _Out_writes_bytes_opt_(SystemInformationLength) PVOID SystemInformation,
  _In_ ULONG SystemInformationLength,
  _Out_opt_ PULONG ReturnLength
  )
{
  //
  // Log the function call.
  //

  WCHAR Buffer[128];
  _snwprintf(Buffer,
             RTL_NUMBER_OF(Buffer),
             L"NtQuerySystemInformation(%i, %p, %i)",
             SystemInformationClass,
             SystemInformation,
             SystemInformationLength);

  EtwEventWriteString(ProviderHandle, 0, 0, Buffer);

  //
  // Call original function.
  //

  return OrigNtQuerySystemInformation(SystemInformationClass,
                                      SystemInformation,
                                      SystemInformationLength,
                                      ReturnLength);
}

inline decltype(NtCreateThreadEx)* OrigNtCreateThreadEx = nullptr;

NTSTATUS
NTAPI
HookNtCreateThreadEx(
  _Out_ PHANDLE ThreadHandle,
  _In_ ACCESS_MASK DesiredAccess,
  _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
  _In_ HANDLE ProcessHandle,
  _In_ PVOID StartRoutine, // PUSER_THREAD_START_ROUTINE
  _In_opt_ PVOID Argument,
  _In_ ULONG CreateFlags, // THREAD_CREATE_FLAGS_*
  _In_ SIZE_T ZeroBits,
  _In_ SIZE_T StackSize,
  _In_ SIZE_T MaximumStackSize,
  _In_opt_ PPS_ATTRIBUTE_LIST AttributeList
  )
{
  //
  // Log the function call.
  //

  WCHAR Buffer[128];
  _snwprintf(Buffer,
             RTL_NUMBER_OF(Buffer),
             L"NtCreateThreadEx(%p, %p)",
             ProcessHandle,
             StartRoutine);

  EtwEventWriteString(ProviderHandle, 0, 0, Buffer);

  //
  // Call original function.
  //

  return OrigNtCreateThreadEx(ThreadHandle,
                              DesiredAccess,
                              ObjectAttributes,
                              ProcessHandle,
                              StartRoutine,
                              Argument,
                              CreateFlags,
                              ZeroBits,
                              StackSize,
                              MaximumStackSize,
                              AttributeList);
}

NTSTATUS
NTAPI
ThreadRoutine(
  _In_ PVOID ThreadParameter
  )
{
  LARGE_INTEGER Delay;
  Delay.QuadPart = -10 * 1000 * 100; // 100ms

  for (;;)
  {
    // EtwEventWriteString(ProviderHandle, 0, 0, L"NtDelayExecution(100ms)");

    NtDelayExecution(FALSE, &Delay);
  }
}



NTSTATUS
NTAPI
DisableDetours(
  VOID
  )
{
  //DetourTransactionBegin();
  //{
  //  DetourDetach((PVOID*)&OrigNtQuerySystemInformation, HookNtQuerySystemInformation);
  //  DetourDetach((PVOID*)&OrigNtCreateThreadEx, HookNtCreateThreadEx);
  //}
  //DetourTransactionCommit();

  return STATUS_SUCCESS;
}

// SK: 20 pointer arguments should be enoguh to capture
// wmain or WinMain __stdcall arguments.
typedef void (__stdcall *process_entry_point_signature)(
  void* arg_1, void* arg_2, void* arg_3,
  void* arg_4, void* arg_5, void* arg_6,
  void* arg_7, void* arg_8, void* arg_9,
  void* arg_10, void* arg_11, void* arg_12, void* arg_13,
  void* arg_14, void* arg_15, void* arg_16,
  void* arg_17, void* arg_18, void* arg_19,
  void* arg_20);
static process_entry_point_signature trampoline_process_entry_point;
void __stdcall hook_process_entry_point(void* arg_1, void* arg_2, void* arg_3,
  void* arg_4, void* arg_5, void* arg_6,
  void* arg_7, void* arg_8, void* arg_9,
  void* arg_10, void* arg_11, void* arg_12, void* arg_13,
  void* arg_14, void* arg_15, void* arg_16,
  void* arg_17, void* arg_18, void* arg_19,
  void* arg_20)
{
  DetourTransactionBegin();
  DetourDetach((PVOID*)&trampoline_process_entry_point, hook_process_entry_point);
  DetourTransactionCommit();

  // SK: Load stage 2 DLL

  /*PWSTR DllPath = (PWSTR)STAGE2DLLPATH_W;
  ULONG DllCharacteristics = 0;
  UNICODE_STRING DllName;
  RtlInitUnicodeString(&DllName, (PWSTR)STAGE2DLLNAME_W);
  PVOID DllHandle;

  NTSTATUS result = LdrLoadDll(
    DllPath,
    &DllCharacteristics,
    &DllName,
    &DllHandle
  );

  mydebugprint("Stage 2 DLL path: %s", STAGE2DLLPATH_A);
  OutputDebugStringW(STAGE2DLLPATH_W);
  mydebugprint("Stage 2 DLL name: %s", STAGE2DLLNAME_A);
  OutputDebugStringW(STAGE2DLLNAME_W);

  if (result != STATUS_SUCCESS) {
    mydebugprint("Failed to load DLL. Return value: %x", result);
  }
  else {
    OutputDebugStringA("Loaded DLL");
  }*/

  HMODULE h = LoadLibraryA(STAGE2DLLPATH_A);
  if (h == NULL) {
    mydebugprint("Failed to load DLL from %s. GetLastError: %x", STAGE2DLLPATH_A, GetLastError());
  }

  trampoline_process_entry_point(arg_1, arg_2, arg_3,
    arg_4, arg_5, arg_6,
    arg_7, arg_8, arg_9,
    arg_10, arg_11, arg_12, arg_13,
    arg_14, arg_15, arg_16,
    arg_17, arg_18, arg_19,
    arg_20);
}


void print_detour_error(LONG detour_result) {
  switch (detour_result) {
  case NO_ERROR:
    OutputDebugStringA("NO_ERROR");
    break;
  case ERROR_INVALID_BLOCK:
    OutputDebugStringA("ERROR_INVALID_BLOCK");
    break;
  case ERROR_INVALID_HANDLE:
    OutputDebugStringA("ERROR_INVALID_HANDLE");
    break;
  case ERROR_INVALID_OPERATION:
    OutputDebugStringA("ERROR_INVALID_OPERATION");
    break;
  case ERROR_NOT_ENOUGH_MEMORY:
    OutputDebugStringA("ERROR_NOT_ENOUGH_MEMORY");
    break;
  case ERROR_INVALID_PARAMETER:
    OutputDebugStringA("ERROR_INVALID_PARAMETER");
    break;
  case ERROR_INVALID_DATA:
    OutputDebugStringA("ERROR_INVALID_DATA");
    break;
  case ERROR_NOT_SUPPORTED:
    OutputDebugStringA("ERROR_NOT_SUPPORTED");
    break;
  default:
    OutputDebugStringA("Unknown error");
    break;
  }
}


NTSTATUS
NTAPI
OnProcessAttach(
  _In_ PVOID ModuleHandle
  )
{
  //
  // First, resolve address of the _snwprintf function.
  //

  ANSI_STRING RoutineName;
  RtlInitAnsiString(&RoutineName, (PSTR)"_snwprintf");

  UNICODE_STRING NtdllPath;
  RtlInitUnicodeString(&NtdllPath, (PWSTR)L"ntdll.dll");

  HANDLE NtdllHandle;
  LdrGetDllHandle(NULL, 0, &NtdllPath, &NtdllHandle);

  LdrGetProcedureAddress(NtdllHandle, &RoutineName, 0, (PVOID*)&_snwprintf);

  RtlInitAnsiString(&RoutineName, (PSTR)"_vsnprintf");
  LdrGetProcedureAddress(NtdllHandle, &RoutineName, 0, (PVOID*)&_vsnprintf);

  //
  // Make us unloadable (by FreeLibrary calls).
  //

  LdrAddRefDll(LDR_ADDREF_DLL_PIN, ModuleHandle);

  //
  // Hide this DLL from the PEB.
  //

  PPEB Peb = NtCurrentPeb();
  PLIST_ENTRY ListEntry;

  for (ListEntry =   Peb->Ldr->InLoadOrderModuleList.Flink;
       ListEntry != &Peb->Ldr->InLoadOrderModuleList;
       ListEntry =   ListEntry->Flink)
  {
    PLDR_DATA_TABLE_ENTRY LdrEntry = CONTAINING_RECORD(ListEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

    //
    // ModuleHandle is same as DLL base address.
    //

    if (LdrEntry->DllBase == ModuleHandle)
    {
      RemoveEntryList(&LdrEntry->InLoadOrderLinks);
      RemoveEntryList(&LdrEntry->InInitializationOrderLinks);
      RemoveEntryList(&LdrEntry->InMemoryOrderLinks);
      RemoveEntryList(&LdrEntry->HashLinks);

      break;
    }
  }

  //
  // Create exports for Wow64Log* functions in
  // the PE header of this DLL.
  //

  Wow64LogCreateExports(ModuleHandle);


  //
  // Register ETW provider.
  //

  EtwEventRegister(&ProviderGuid,
                   NULL,
                   NULL,
                   &ProviderHandle);

  //
  // Create dummy thread - used for testing.
  //

  // RtlCreateUserThread(NtCurrentProcess(),
  //                     NULL,
  //                     FALSE,
  //                     0,
  //                     0,
  //                     0,
  //                     &ThreadRoutine,
  //                     NULL,
  //                     NULL,
  //                     NULL);

  //
  // Get command line of the current process and send it.
  //

  PWSTR CommandLine = Peb->ProcessParameters->CommandLine.Buffer;

  WCHAR Buffer[1024];
  _snwprintf(Buffer,
             RTL_NUMBER_OF(Buffer),
             L"Arch: %s, CommandLine: '%s'",
             ARCH_W,
             CommandLine);

  EtwEventWriteString(ProviderHandle, 0, 0, Buffer);

  //
  // Hook all functions.
  //


  PPEB peb = NtCurrentPeb();
  PVOID pImage = peb->ImageBaseAddress;

  PVOID pEntry;
  PIMAGE_NT_HEADERS pNtHeaders;

  pNtHeaders = (PIMAGE_NT_HEADERS)((PCHAR)pImage + ((PIMAGE_DOS_HEADER)pImage)->e_lfanew);
  pEntry = (PVOID)((PCHAR)pImage + pNtHeaders->OptionalHeader.AddressOfEntryPoint);

  if (pEntry == pImage) {
    // AddressOfEntryPoint is 0; this seems to be a .NET PE
    // Try to resolve _CorExeMain and hook it instead
    RtlInitAnsiString(&RoutineName, (PSTR)"_CorExeMain");
    RtlInitUnicodeString(&NtdllPath, (PWSTR)L"mscoree.dll");

    NtdllHandle = NULL;
    LdrGetDllHandle(NULL, 0, &NtdllPath, &NtdllHandle);
    LdrGetProcedureAddress(NtdllHandle, &RoutineName, 0, (PVOID*)&pEntry);
  }

  trampoline_process_entry_point = (process_entry_point_signature)pEntry;

  unsigned int first_byte = *(unsigned char*)trampoline_process_entry_point;

  mydebugprint("trampoline_process_entry_point: %p, first byte: %x", trampoline_process_entry_point,
    first_byte);
  OutputDebugStringW(Buffer);

  LONG detour_result;

  detour_result = DetourTransactionBegin();

  if (detour_result == NO_ERROR) {

    detour_result = DetourAttach((PVOID*)&trampoline_process_entry_point, hook_process_entry_point);
    if (detour_result != NO_ERROR) {
      _snwprintf(Buffer, RTL_NUMBER_OF(Buffer), L"DetourAttach failed: %d", detour_result);
      OutputDebugStringW(Buffer);
      print_detour_error(detour_result);
    }

    detour_result = DetourTransactionCommit();
    if (detour_result != NO_ERROR) {
      _snwprintf(Buffer, RTL_NUMBER_OF(Buffer), L"DetourTransactionCommit failed: %d", detour_result);
      OutputDebugStringW(Buffer);
      print_detour_error(detour_result);
    }
  }
  else {
    OutputDebugStringA("DetourTransactionBegin failed");
    print_detour_error(detour_result);
  }

  return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
OnProcessDetach(
  _In_ HANDLE ModuleHandle
  )
{
  //
  // Unhook all functions.
  //

  return DisableDetours();
}

EXTERN_C
BOOL
NTAPI
NtDllMain(
  _In_ HANDLE ModuleHandle,
  _In_ ULONG Reason,
  _In_ LPVOID Reserved
  )
{
  switch (Reason)
  {
    case DLL_PROCESS_ATTACH:
      OnProcessAttach(ModuleHandle);
      break;

    case DLL_PROCESS_DETACH:
      OnProcessDetach(ModuleHandle);
      break;

    case DLL_THREAD_ATTACH:

      break;

    case DLL_THREAD_DETACH:

      break;
  }

  return TRUE;
}

