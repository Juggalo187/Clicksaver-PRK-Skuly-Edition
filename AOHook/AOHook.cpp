/*
 * This DLL patches the DataBlockToMessage function
 * in AO's MessageProtocol.dll. It is injected by
 * ClickSaver into AO process.
 *
 * Note that you have to compile this project separately.
 * If you set it as dependance of ClickSaver, it will
 * tell the linker to link ClickSaver with AOHook.lib,
 * which doesn't exist.
 *
 * The output path for the dll is the clicksaver sources
 * directory (and not AOHook/Release|Debug), for easier
 * debugging ;)
 */
/*
AOHook.dll - AO process hook -  Anarchy Online mission helper
Copyright (C) 2002 Morb

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

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <Detours/detours.h>

void*   g_pDataBlockToMessage = NULL;

// According to this documention of VC++ name-mangling
// http://www.kegel.com/mangle.html:
//
// ?DataBlockToMessage@@YAPAVMessage_t@@IPAX@Z
// is named DataBlockToMessage, returns a Message_t*,
// and takes an int and a void* as parameters.

// Just to be more readable.
// Message_t is actually a class of AO
// But just to define a pointer on it,
// we don't need to know what it is.
typedef void Message_t;

Message_t* ( *pOriginalDataBlockToMessage )( int _Size, void* _pDataBlock );

#define CS_ACCEPTED_MISSION_SYNC_COPYDATA 0x314D5343UL
#define CS_ACCEPTED_MISSION_SYNC_VERSION 1UL
#define CS_ACCEPTED_MISSION_SYNC_MAX 128
#define AO_LOCAL_PLAYER_MISSION_LIST_OFFSET 0x1C4
#define AO_MISSION_IDENTITY_TYPE 0xDAC3

typedef struct
{
    DWORD version;
    DWORD count;
    DWORD missionIds[CS_ACCEPTED_MISSION_SYNC_MAX];
} AcceptedMissionSnapshot;

typedef void* (__cdecl *GetN3EngineInstanceFn)(void);
typedef void* (__thiscall *GetClientControlDynelFn)(void* pEngine);

static volatile LONG g_bStopMissionScanner = 0;
static HANDLE g_hMissionScannerThread = NULL;

static HWND GetAOClientWindow()
{
    struct WindowSearch {
        DWORD processId;
        HWND window;
    } search = { GetCurrentProcessId(), NULL };

    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        WindowSearch* search = reinterpret_cast<WindowSearch*>(parameter);
        char className[64] = "";
        DWORD processId = 0;
        GetClassNameA(window, className, sizeof(className));
        if (strcmp(className, "Anarchy client") == 0) {
            GetWindowThreadProcessId(window, &processId);
            if (processId == search->processId) {
                search->window = window;
                return FALSE;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));

    return search.window;
}

static BOOL IsReadableRange(const void* address, SIZE_T size)
{
    MEMORY_BASIC_INFORMATION info;
    uintptr_t start = (uintptr_t)address;
    uintptr_t end = start + size;

    if (!address || size == 0 || end < start ||
        VirtualQuery(address, &info, sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        (info.Protect & 0xFF) == PAGE_EXECUTE)
        return FALSE;

    return end <= (uintptr_t)info.BaseAddress + info.RegionSize;
}

static BOOL ReadAcceptedMissionSnapshot(AcceptedMissionSnapshot* snapshot)
{
    HMODULE n3Module = GetModuleHandleA("N3.dll");
    GetN3EngineInstanceFn getEngine;
    GetClientControlDynelFn getClientControlDynel;
    BYTE* localPlayer;
    BYTE* missionListOwner;
    BYTE* vector;
    BYTE* first;
    BYTE* last;
    SIZE_T vectorSize;

    if (!snapshot || !n3Module)
        return FALSE;

    getEngine = (GetN3EngineInstanceFn)GetProcAddress(
        n3Module, "?GetInstance@n3Engine_t@@SAPAV1@XZ");
    getClientControlDynel = (GetClientControlDynelFn)GetProcAddress(
        n3Module, "?GetClientControlDynel@n3EngineClient_t@@QBEPAVn3VisualDynel_t@@XZ");
    if (!getEngine || !getClientControlDynel)
        return FALSE;

    void* engine = getEngine();
    if (!engine)
        return FALSE;

    localPlayer = (BYTE*)getClientControlDynel(engine);
    if (!localPlayer || !IsReadableRange(localPlayer, AO_LOCAL_PLAYER_MISSION_LIST_OFFSET + 4))
        return FALSE;

    // Match AOSharp's mission-list traversal: LocalPlayer+0x1C4 points to an
    // owner whose embedded vector at +4 contains pointers to Mission objects.
    missionListOwner = *(BYTE**)(localPlayer + AO_LOCAL_PLAYER_MISSION_LIST_OFFSET);
    if (!missionListOwner ||
        !IsReadableRange(missionListOwner, 4 + 2 * sizeof(void*)))
        return FALSE;

    vector = missionListOwner + 4;
    first = *(BYTE**)vector;
    last = *(BYTE**)(vector + sizeof(void*));

    if (!first && !last)
    {
        snapshot->version = CS_ACCEPTED_MISSION_SYNC_VERSION;
        snapshot->count = 0;
        return TRUE;
    }
    if (!first || !last || (uintptr_t)last < (uintptr_t)first)
        return FALSE;

    vectorSize = (SIZE_T)(last - first);
    if (vectorSize % sizeof(void*) != 0 ||
        vectorSize / sizeof(void*) > CS_ACCEPTED_MISSION_SYNC_MAX ||
        (vectorSize && !IsReadableRange(first, vectorSize)))
        return FALSE;

    snapshot->version = CS_ACCEPTED_MISSION_SYNC_VERSION;
    snapshot->count = 0;
    for (BYTE* slot = first; slot < last; slot += sizeof(void*))
    {
        BYTE* mission = *(BYTE**)slot;
        DWORD identityType;
        DWORD missionId;
        DWORD i;

        if (!mission || !IsReadableRange(mission, 2 * sizeof(DWORD)))
            return FALSE;

        identityType = *(DWORD*)mission;
        missionId = *(DWORD*)(mission + sizeof(DWORD));
        if (identityType != AO_MISSION_IDENTITY_TYPE)
            return FALSE;

        for (i = 0; i < snapshot->count; i++)
            if (snapshot->missionIds[i] == missionId)
                break;

        if (i == snapshot->count)
            snapshot->missionIds[snapshot->count++] = missionId;
    }

    return TRUE;
}

static BOOL TryReadAcceptedMissionSnapshot(AcceptedMissionSnapshot* snapshot)
{
    __try
    {
        return ReadAcceptedMissionSnapshot(snapshot);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return FALSE;
    }
}

static DWORD WINAPI AcceptedMissionScannerThread(void*)
{
    AcceptedMissionSnapshot previousSnapshot = { 0 };
    BOOL hasPreviousSnapshot = FALSE;
    HWND previousWindow = NULL;
    DWORD lastSentAt = 0;

    while (InterlockedCompareExchange(&g_bStopMissionScanner, 0, 0) == 0)
    {
        AcceptedMissionSnapshot snapshot = { 0 };
        HWND clickSaverWindow = NULL;
        HWND aoWindow = GetAOClientWindow();

        if (aoWindow && TryReadAcceptedMissionSnapshot(&snapshot))
        {
            BOOL changed = !hasPreviousSnapshot ||
                snapshot.count != previousSnapshot.count;
            for (DWORD i = 0; !changed && i < snapshot.count; i++)
            {
                DWORD j;
                for (j = 0; j < previousSnapshot.count; j++)
                    if (snapshot.missionIds[i] == previousSnapshot.missionIds[j])
                        break;
                if (j == previousSnapshot.count)
                    changed = TRUE;
            }

            clickSaverWindow = FindWindowA(
                "ClickSaverHookWindowClass", "ClickSaverHookWindow");
            if (clickSaverWindow &&
                (changed || clickSaverWindow != previousWindow ||
                    GetTickCount() - lastSentAt >= 5000))
            {
                COPYDATASTRUCT data = { 0 };
                DWORD_PTR result = 0;
                data.dwData = CS_ACCEPTED_MISSION_SYNC_COPYDATA;
                data.cbData = sizeof(snapshot.version) + sizeof(snapshot.count) +
                    snapshot.count * sizeof(snapshot.missionIds[0]);
                data.lpData = &snapshot;
                if (SendMessageTimeoutA(clickSaverWindow, WM_COPYDATA,
                    (WPARAM)aoWindow,
                    (LPARAM)&data, SMTO_ABORTIFHUNG, 500, &result))
                {
                    previousSnapshot = snapshot;
                    hasPreviousSnapshot = TRUE;
                    previousWindow = clickSaverWindow;
                    lastSentAt = GetTickCount();
                }
            }
        }

        if (!clickSaverWindow || !aoWindow)
            previousWindow = NULL;
        Sleep(1000);
    }

    return 0;
}

Message_t* DataBlockToMessageHook( int _Size, void* _pDataBlock )
{
    unsigned long* pData, Temp;
    //FILE* fp;
    HWND hWnd;

    if( _Size > 0x40 )
    {
        // For 14.2
        // pData = ( unsigned long* )( ( char* )_pDataBlock + 0x34 );
        // For 14.4.0.2 on test...
        pData = (unsigned long*)( (char*)_pDataBlock + 0x33 );
        Temp = *pData;
        /*
        if (fp = fopen( "g:\\AOHook_Log.bin", "ab")) {
        fwrite( _pDataBlock, _Size, 1, fp );
        fprintf( fp, "********" );
        fclose( fp );
        }
        */
        if( Temp == 0xc3da0000 )
        {
            // Find ClickSaver's hook thread window and send the datas
            // using WM_COPYDATA
            HWND aoWindow = GetAOClientWindow();
            if( (hWnd = FindWindow( "ClickSaverHookWindowClass", "ClickSaverHookWindow" )) &&
                aoWindow )
            {
                COPYDATASTRUCT Data = { 0 };
                Data.dwData = 0;
                Data.cbData = _Size;
                Data.lpData = _pDataBlock;
                SendMessage( hWnd, WM_COPYDATA, (WPARAM)aoWindow, (LPARAM)&Data );
            }
        }
    }
    return pOriginalDataBlockToMessage( _Size, _pDataBlock );
}

int ProcessAttach( HINSTANCE _hModule )
{
    // Hook ::DataBlockToMessage() (incoming)
    pOriginalDataBlockToMessage = ( Message_t *( __cdecl* )( int, void* ) )::GetProcAddress( ::GetModuleHandle( "MessageProtocol.dll" ), "?DataBlockToMessage@@YAPAVMessage_t@@IPAX@Z" );
    DetourTransactionBegin();
    DetourUpdateThread( GetCurrentThread() );
    DetourAttach( (PVOID*)&pOriginalDataBlockToMessage, DataBlockToMessageHook );
    DetourTransactionCommit();
    InterlockedExchange(&g_bStopMissionScanner, 0);
    g_hMissionScannerThread = CreateThread(NULL, 0,
        AcceptedMissionScannerThread, NULL, 0, NULL);
    return TRUE;
}


int ProcessDetach( HINSTANCE _hModule )
{
    InterlockedExchange(&g_bStopMissionScanner, 1);
    if (g_hMissionScannerThread)
    {
        WaitForSingleObject(g_hMissionScannerThread, INFINITE);
        CloseHandle(g_hMissionScannerThread);
        g_hMissionScannerThread = NULL;
    }

    LONG res1 = DetourTransactionBegin();
    LONG res2 = DetourUpdateThread( GetCurrentThread() );
    LONG res3 = DetourDetach( (PVOID*)&pOriginalDataBlockToMessage, DataBlockToMessageHook );
    LONG res4 = DetourTransactionCommit();
    return TRUE;
}

BOOL APIENTRY DllMain( HINSTANCE _hModule, DWORD _dwReason, PVOID _lpReserved )
{
    switch( _dwReason )
    {
    case DLL_PROCESS_ATTACH:
        return ProcessAttach( _hModule );
    case DLL_PROCESS_DETACH:
        return ProcessDetach( _hModule );
    }
    return TRUE;
}
