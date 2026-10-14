/*
ClickSaver process hooking -  Anarchy Online mission helper
Copyright (C) 2001, 2002 Morb
Parts Copyright (C) 2003, 2004 gnarf
Parts Copyright (C) 2012 Darkbane, Adjuster

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "Platform.h"

#include <stdio.h>
#include <cmath>
#include <climits>
#include <string>
#include <TlHelp32.h>
#include "clicksaver.h"

extern "C" HANDLE g_Mutex;
extern "C" char g_CSDir[ MAX_PATH ];
extern "C" char g_CurrentPacket[ 65536 ];
extern "C" HANDLE g_hThreadExitEvent;
extern "C" volatile LONG g_bPreserveAOHookForAIMonitor;

static HWND FindAOCommandWindow(DWORD processId)
{
    struct SearchContext {
        DWORD processId;
        HWND result;
    } context = { processId, NULL };

    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        SearchContext* search = reinterpret_cast<SearchContext*>(parameter);
        char className[64] = "";
        DWORD windowProcessId = 0;
        GetClassNameA(window, className, sizeof(className));
        if (strcmp(className, "ClickSaverAICommandWindowClass") == 0) {
            GetWindowThreadProcessId(window, &windowProcessId);
            if (windowProcessId == search->processId) {
                search->result = window;
                return FALSE;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));
    return context.result;
}

static BOOL g_AIManualMode = FALSE;

extern "C" void SetAIMissionManualMode(BOOL manual)
{
    g_AIManualMode = manual;
}

extern "C" BOOL SendAIMissionCommand(DWORD command, DWORD difficulty,
    DWORD missionId, HWND clientWindow, DWORD clientProcessId)
{
    char className[64] = "";
    DWORD windowProcessId = 0;
    if (!clientWindow || !clientProcessId || !IsWindow(clientWindow) ||
        !GetClassNameA(clientWindow, className, sizeof(className)) ||
        strcmp(className, "Anarchy client") != 0)
        return FALSE;
    GetWindowThreadProcessId(clientWindow, &windowProcessId);
    if (windowProcessId != clientProcessId)
        return FALSE;

    HWND commandWindow = FindAOCommandWindow(clientProcessId);
    if (!commandWindow)
        return FALSE;

    CSAIMissionCommand payload = { command, difficulty, missionId,
        g_AIManualMode ? 1UL : 0UL };
    COPYDATASTRUCT data = { 0 };
    DWORD_PTR result = 0;
    data.dwData = CS_AI_COMMAND_COPYDATA;
    data.cbData = sizeof(payload);
    data.lpData = &payload;
    HWND clickSaverWindow = FindWindowA(
        "ClickSaverHookWindowClass", "ClickSaverHookWindow");
    if (!clickSaverWindow ||
        !SendMessageTimeoutA(commandWindow, WM_COPYDATA,
            (WPARAM)clickSaverWindow, (LPARAM)&data,
            SMTO_ABORTIFHUNG, 1000, &result))
        return FALSE;
    return result != 0;
}

static DWORD RemoteCall(HANDLE hProcess, LPVOID pFunction, LPVOID pParam, DWORD dwTimeout = 5000)
{
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pFunction, pParam, 0, NULL);
    if (!hThread)
        return 0;
    DWORD exitCode = 0;
    if (WaitForSingleObject(hThread, dwTimeout) != WAIT_OBJECT_0 ||
        !GetExitCodeThread(hThread, &exitCode))
        exitCode = 0;
    CloseHandle(hThread);
    return exitCode;
}

static HMODULE GetRemoteModuleHandle(DWORD processId, const char* moduleName)
{
    HANDLE snapshot = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 5; attempt++)
    {
        snapshot = CreateToolhelp32Snapshot(
            TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
        if (snapshot != INVALID_HANDLE_VALUE ||
            GetLastError() != ERROR_BAD_LENGTH)
            break;
        Sleep(10);
    }
    if (snapshot == INVALID_HANDLE_VALUE)
        return NULL;

    MODULEENTRY32 module = { 0 };
    module.dwSize = sizeof(module);
    HMODULE result = NULL;
    if (Module32First(snapshot, &module))
    {
        do
        {
            if (_stricmp(module.szModule, moduleName) == 0)
            {
                result = module.hModule;
                break;
            }
        } while (Module32Next(snapshot, &module));
    }
    CloseHandle(snapshot);
    return result;
}

static void UnloadAOHook(HWND clientWindow, DWORD processId)
{
    HMODULE hRemoteModule = GetRemoteModuleHandle(processId, "AOHook.dll");
    if (!hRemoteModule)
        return;

    // Let AOHook stop its own threads first; see CS_AI_COMMAND_PREPARE_UNLOAD.
    if (SendAIMissionCommand(CS_AI_COMMAND_PREPARE_UNLOAD, 0, 0,
        clientWindow, processId))
        Sleep(500);

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (!hProcess)
        return;
    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    if (!kernel32)
    {
        CloseHandle(hProcess);
        return;
    }
    FARPROC pFreeLibrary = GetProcAddress(kernel32, "FreeLibrary");
    if (pFreeLibrary)
    {
        RemoteCall(hProcess, pFreeLibrary, hRemoteModule);
    }
    CloseHandle(hProcess);
}

std::string to_ascii_copy( std::wstring const& input )
{
    if (input.length() > INT_MAX)
        return std::string();
    if (input.empty())
        return std::string();

    int inputLength = static_cast<int>(input.length());
    int outputLength = WideCharToMultiByte(
        CP_ACP, 0, input.c_str(), inputLength, NULL, 0, NULL, NULL);
    if (!outputLength)
        return std::string();

    std::string result(outputLength, '\0');
    int convertedLength = WideCharToMultiByte(
        CP_ACP, 0, input.c_str(), inputLength,
        &result[0], outputLength, NULL, NULL);
    if (convertedLength != outputLength)
        return std::string();

    result.resize(convertedLength);
    return result;
}

inline std::string to_ascii_copy( std::string const& input )
{
    return input;
}

bool InjectDLL( DWORD ProcessID, std::string const& dllName )
{
    if( !ProcessID )
    {
        //LOG("No process ID specified.");
        return false;
    }
    SetLastError( 0 );
    HANDLE Proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, ProcessID);
    if( !Proc )
    {
        return false;
    }
    std::string dllNameA = to_ascii_copy( dllName );
    if (dllNameA.empty())
    {
        CloseHandle(Proc);
        return false;
    }
    SIZE_T pathSize = dllNameA.length() + 1;
    LPVOID RemoteString = VirtualAllocEx(
        Proc, NULL, pathSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
    if( !RemoteString )
    {
        CloseHandle( Proc );
        return false;
    }
    SIZE_T bytesWritten = 0;
    if( !WriteProcessMemory( Proc, RemoteString, dllNameA.c_str(),
        pathSize, &bytesWritten ) || bytesWritten != pathSize )
    {
        VirtualFreeEx(Proc, RemoteString, 0, MEM_RELEASE);
        CloseHandle( Proc );
        return false;
    }
    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    if (!kernel32)
    {
        VirtualFreeEx(Proc, RemoteString, 0, MEM_RELEASE);
        CloseHandle(Proc);
        return false;
    }
    LPVOID LoadLibAddy = (LPVOID)GetProcAddress(kernel32, "LoadLibraryA");
    if (!LoadLibAddy)
    {
        VirtualFreeEx(Proc, RemoteString, 0, MEM_RELEASE);
        CloseHandle(Proc);
        return false;
    }
    HANDLE thread = CreateRemoteThread(Proc, NULL, 0,
        (LPTHREAD_START_ROUTINE)LoadLibAddy, RemoteString, 0, NULL);
    if (!thread)
    {
        VirtualFreeEx(Proc, RemoteString, 0, MEM_RELEASE);
        CloseHandle( Proc );
        return false;
    }
    DWORD waitResult = WaitForSingleObject(thread, 15000);
    DWORD exitCode = 0;
    if (waitResult == WAIT_OBJECT_0)
    {
        GetExitCodeThread(thread, &exitCode);
        VirtualFreeEx(Proc, RemoteString, 0, MEM_RELEASE);
    }
    CloseHandle(thread);
    CloseHandle( Proc );
    return waitResult == WAIT_OBJECT_0 && exitCode != 0;
}


void Inject()
{
    struct InjectionContext {
        char dllPath[MAX_PATH];
    } context;
    snprintf(context.dllPath, sizeof(context.dllPath), "%s\\AOHook.dll", g_CSDir);

    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        char className[64] = "";
        DWORD processId = 0;
        GetClassNameA(window, className, sizeof(className));
        if (strcmp(className, "Anarchy client") != 0)
            return TRUE;

        GetWindowThreadProcessId(window, &processId);
        if (!processId)
            return TRUE;

        HMODULE installed = GetRemoteModuleHandle(processId, "AOHook.dll");
        if (!installed)
            InjectDLL(processId,
                static_cast<InjectionContext*>(reinterpret_cast<void*>(parameter))->dllPath);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));
}


LRESULT CALLBACK HookWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
    COPYDATASTRUCT* pData;

    switch( msg )
    {
    case WM_TIMER:
        Inject();
        break;
    case WM_COPYDATA: {
        pData = (PCOPYDATASTRUCT)lParam;
        if (pData && pData->dwData == CS_AI_EVENT_COPYDATA &&
            pData->lpData && pData->cbData == sizeof(CSAIMissionEvent))
        {
            CSAIMissionEvent* event = (CSAIMissionEvent*)malloc(sizeof(*event));
            if (event) {
                memcpy(event, pData->lpData, sizeof(*event));
                event->clientWindow = (HWND)wParam;
                GetWindowThreadProcessId(event->clientWindow,
                    &event->clientProcessId);
                if (puTrySendAppMessage(CSAM_AI_EVENT, (PUU32)event))
                    return 1;
                free(event);
                WriteLog("Could not queue an AI mission event.");
                return 0;
            } else {
                WriteLog("Could not allocate an AI mission event.");
                return 0;
            }
        }
        if (pData && pData->dwData == CS_ACCEPTED_MISSION_SYNC_COPYDATA)
        {
            if (pData->lpData && pData->cbData > 0)
                ReceiveAcceptedMissionSnapshot(pData->lpData, pData->cbData,
                    (HWND)wParam);
            break;
        }
        if (!pData || !pData->lpData || pData->cbData > sizeof(g_CurrentPacket))
            break;
        char characterName[CS_ACCEPTED_MISSION_CHARACTER_SIZE] = "";
        if (wParam && IsWindow((HWND)wParam))
            GetWindowTextA((HWND)wParam, characterName, sizeof(characterName));
        if (QueueIncomingMissionPacket(pData->lpData, pData->cbData, characterName))
            puSendAppMessage(CSAM_NEWMISSIONS, 0);
        break;
    }
    default:
        return DefWindowProc( hWnd, msg, wParam, lParam );
    }
    return 0;
}

extern "C" DWORD WINAPI HookManagerThread( void* _pParam )
{
    WNDCLASSEX HookWndClass;
    HWND hWnd;
    MSG msg;
    UINT Timer;

    SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL );

    HookWndClass.cbSize = sizeof( WNDCLASSEX );
    HookWndClass.style = 0;
    HookWndClass.lpfnWndProc = HookWndProc;
    HookWndClass.cbClsExtra = 0;
    HookWndClass.cbWndExtra = 0;
    HookWndClass.hInstance = NULL;
    HookWndClass.hIcon = NULL;
    HookWndClass.hCursor = NULL;
    HookWndClass.hbrBackground = NULL;
    HookWndClass.lpszMenuName = NULL;
    HookWndClass.lpszClassName = "ClickSaverHookWindowClass";
    HookWndClass.hIconSm = NULL;

    RegisterClassEx( &HookWndClass );

    /* Create window */
    hWnd = CreateWindowEx( 0, "ClickSaverHookWindowClass", "ClickSaverHookWindow", WS_MINIMIZE, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, NULL, NULL );
    Inject();
    Timer = SetTimer( hWnd, 0, 5000, NULL );

    DWORD dwWaitResult;
    HANDLE hEvents[1] = { g_hThreadExitEvent };

    while (1)
    {
        dwWaitResult = MsgWaitForMultipleObjects(1, hEvents, FALSE, INFINITE, QS_ALLINPUT);

        if (dwWaitResult == WAIT_OBJECT_0)
        {
            // Keep AOHook alive so an active AI monitor can be reclaimed.
            if (!InterlockedCompareExchange(
                &g_bPreserveAOHookForAIMonitor, 0, 0))
            {
                HWND AOWnd = FindWindow( "Anarchy client", NULL );
                if (AOWnd)
                {
                    DWORD pid = 0;
                    GetWindowThreadProcessId(AOWnd, &pid);
                    if (pid)
                        UnloadAOHook(AOWnd, pid);
                }
            }
            break;
        }
        else if (dwWaitResult == WAIT_OBJECT_0 + 1)
        {
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                    goto cleanup;
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        else
        {
            break;
        }
    }

cleanup:
    KillTimer(hWnd, Timer);
    DestroyWindow(hWnd);
    return TRUE;
}
