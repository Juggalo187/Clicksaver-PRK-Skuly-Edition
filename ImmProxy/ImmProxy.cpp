/*
 * IMM32.dll proxy for Anarchy Online under Wine/Proton.
 *
 * Drop IMM32.dll next to anarchy.exe together with AOHook.dll and launch AO with
 * WINEDLLOVERRIDES="imm32=n,b". AO loads this proxy at startup, which forwards every
 * Imm* call to the system imm32.dll and loads AOHook.dll inside the game process, so
 * the hook no longer depends on ClickSaver injecting it from the same Wine prefix.
 */
#include <windows.h>
#include <string.h>

#define IMM_EXPORTS(X) \
    X(ImmAssociateContext) X(ImmAssociateContextEx) X(ImmConfigureIMEA) X(ImmConfigureIMEW) \
    X(ImmCreateContext) X(ImmCreateIMCC) X(ImmCreateSoftKeyboard) X(ImmDestroyContext) \
    X(ImmDestroyIMCC) X(ImmDestroySoftKeyboard) X(ImmDisableIME) X(ImmDisableLegacyIME) \
    X(ImmDisableTextFrameService) X(ImmEnumInputContext) X(ImmEnumRegisterWordA) \
    X(ImmEnumRegisterWordW) X(ImmEscapeA) X(ImmEscapeW) X(ImmGenerateMessage) \
    X(ImmGetCandidateListA) X(ImmGetCandidateListCountA) X(ImmGetCandidateListCountW) \
    X(ImmGetCandidateListW) X(ImmGetCandidateWindow) X(ImmGetCompositionFontA) \
    X(ImmGetCompositionFontW) X(ImmGetCompositionStringA) X(ImmGetCompositionStringW) \
    X(ImmGetCompositionWindow) X(ImmGetContext) X(ImmGetConversionListA) \
    X(ImmGetConversionListW) X(ImmGetConversionStatus) X(ImmGetDefaultIMEWnd) \
    X(ImmGetDescriptionA) X(ImmGetDescriptionW) X(ImmGetGuideLineA) X(ImmGetGuideLineW) \
    X(ImmGetHotKey) X(ImmGetIMCCLockCount) X(ImmGetIMCCSize) X(ImmGetIMCLockCount) \
    X(ImmGetIMEFileNameA) X(ImmGetIMEFileNameW) X(ImmGetImeMenuItemsA) X(ImmGetImeMenuItemsW) \
    X(ImmGetOpenStatus) X(ImmGetProperty) X(ImmGetRegisterWordStyleA) \
    X(ImmGetRegisterWordStyleW) X(ImmGetStatusWindowPos) X(ImmGetVirtualKey) \
    X(ImmInstallIMEA) X(ImmInstallIMEW) X(ImmIsIME) X(ImmIsUIMessageA) X(ImmIsUIMessageW) \
    X(ImmLockIMC) X(ImmLockIMCC) X(ImmNotifyIME) X(ImmProcessKey) X(ImmReSizeIMCC) \
    X(ImmRegisterWordA) X(ImmRegisterWordW) X(ImmReleaseContext) X(ImmRequestMessageA) \
    X(ImmRequestMessageW) X(ImmSetCandidateWindow) X(ImmSetCompositionFontA) \
    X(ImmSetCompositionFontW) X(ImmSetCompositionStringA) X(ImmSetCompositionStringW) \
    X(ImmSetCompositionWindow) X(ImmSetConversionStatus) X(ImmSetHotKey) X(ImmSetOpenStatus) \
    X(ImmSetStatusWindowPos) X(ImmShowSoftKeyboard) X(ImmSimulateHotKey) X(ImmUnlockIMC) \
    X(ImmUnlockIMCC) X(ImmUnregisterWordA) X(ImmUnregisterWordW) X(ImmWINNLSEnableIME) \
    X(ImmWINNLSGetEnableStatus)

enum {
#define X(name) idx_##name,
    IMM_EXPORTS(X)
#undef X
    idx_count
};

extern "C" FARPROC g_real[idx_count];
FARPROC g_real[idx_count];

// Naked stubs jump to the real export without touching the stack, so any calling convention works.
#define X(name) \
    extern "C" __declspec(naked) void fwd_##name() { __asm { jmp dword ptr [g_real + idx_##name * 4] } } \
    __pragma(comment(linker, "/export:" #name "=_fwd_" #name))
IMM_EXPORTS(X)
#undef X

static BOOL LoadRealImm32()
{
    char path[MAX_PATH];
    UINT length = GetSystemDirectoryA(path, MAX_PATH);
    if (!length || length + 12 >= MAX_PATH)
        return FALSE;
    strcat_s(path, "\\imm32.dll");

    HMODULE real = LoadLibraryA(path);
    if (!real)
        return FALSE;
#define X(name) g_real[idx_##name] = GetProcAddress(real, #name);
    IMM_EXPORTS(X)
#undef X
    return TRUE;
}

static BOOL IsAnarchyProcess()
{
    char path[MAX_PATH];
    if (!GetModuleFileNameA(NULL, path, MAX_PATH))
        return FALSE;
    const char* name = strrchr(path, '\\');
    return _stricmp(name ? name + 1 : path, "anarchy.exe") == 0;
}

static DWORD WINAPI LoadHookThread(void* module)
{
    char path[MAX_PATH];
    DWORD length = GetModuleFileNameA((HMODULE)module, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (!length || !slash || (size_t)(slash - path) + 12 >= MAX_PATH)
        return 0;
    strcpy_s(slash + 1, MAX_PATH - (size_t)(slash + 1 - path), "AOHook.dll");
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
        return 0;

    // AOHook patches MessageProtocol.dll, which loads later than this proxy.
    for (int i = 0; i < 1200 && !GetModuleHandleA("MessageProtocol.dll"); i++)
        Sleep(100);
    if (!GetModuleHandleA("MessageProtocol.dll") || GetModuleHandleA("AOHook.dll"))
        return 0;
    LoadLibraryA(path);
    return 0;
}

BOOL APIENTRY DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        if (!LoadRealImm32())
            return FALSE;
        if (IsAnarchyProcess())
        {
            HANDLE thread = CreateThread(NULL, 0, LoadHookThread, module, 0, NULL);
            if (thread)
                CloseHandle(thread);
        }
    }
    return TRUE;
}
