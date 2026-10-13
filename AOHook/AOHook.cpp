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
#include <float.h>
#include <string.h>
#include <Detours/detours.h>
#include "ai_mission_protocol.h"

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
#define AO_SIMPLE_CHAR_IDENTITY_TYPE 0xC350
#define AO_N3_KNUBOT_ANSWER 0x2103247D
#define AO_N3_KNUBOT_CLOSE_CHAT 0x270A4C62
#define AO_N3_KNUBOT_OPEN_CHAT 0x3B132D64
#define AO_N3_KNUBOT_ANSWER_LIST 0x55704D31
#define AO_N3_QUEST 0x212C487A
#define AO_PACKET_HEADER_SIZE 16
#define AO_AI_MAX_DIALOG_OPTIONS 64
#define AO_AI_DIALOG_OPTION_SIZE 256

#define AI_STATE_STOPPED CS_AI_STATE_STOPPED
#define AI_STATE_WAITING_FOR_CONFIRMATION CS_AI_STATE_WAITING_FOR_CONFIRMATION
#define AI_STATE_WAITING_FOR_DIFFICULTY CS_AI_STATE_WAITING_FOR_DIFFICULTY
#define AI_STATE_WAITING_FOR_MISSION CS_AI_STATE_WAITING_FOR_MISSION
#define AI_STATE_WAITING_FOR_DECISION CS_AI_STATE_WAITING_FOR_DECISION
#define AI_STATE_WAITING_FOR_DELETION CS_AI_STATE_WAITING_FOR_DELETION
#define AI_STATE_WAITING_FOR_REOPEN CS_AI_STATE_WAITING_FOR_REOPEN
#define AI_STATE_WAITING_FOR_CLOSE CS_AI_STATE_WAITING_FOR_CLOSE

typedef struct AOIdentity
{
    DWORD type;
    DWORD instance;
} AOIdentity;

typedef struct AOVector3
{
    float x;
    float y;
    float z;
} AOVector3;

typedef struct AIAnswerList
{
    AOIdentity target;
    DWORD count;
    char options[AO_AI_MAX_DIALOG_OPTIONS][AO_AI_DIALOG_OPTION_SIZE];
} AIAnswerList;

static AIAnswerList g_AILastAnswerList = { 0 };

typedef struct
{
    DWORD version;
    DWORD count;
    DWORD missionIds[CS_ACCEPTED_MISSION_SYNC_MAX];
} AcceptedMissionSnapshot;

typedef void* (__cdecl *GetN3EngineInstanceFn)(void);
typedef void* (__thiscall *GetClientControlDynelFn)(void* pEngine);
typedef int (__thiscall *GetClientInstFn)(void* pEngine);
typedef void* (__cdecl *GetClientInstanceFn)(void);
typedef void* (__cdecl *GetInputConfigInstanceFn)(void);
typedef AOIdentity* (__thiscall *GetCurrentTargetFn)(void* inputConfig,
    AOIdentity* target);
typedef const char* (__thiscall *GetDynelNameFn)(void* pEngine,
    AOIdentity* identity, AOIdentity* unknownIdentity);
typedef BOOL (__thiscall *GetQuestWorldPosFn)(void* pEngine,
    AOIdentity* mission, AOIdentity* playfield, AOVector3* universePosition,
    AOVector3* zonePosition);
typedef int (__thiscall *ConnectionSendFn)(void* connection, unsigned int unknown,
    int length, const void* buffer);

static volatile LONG g_bStopMissionScanner = 0;
static HANDLE g_hMissionScannerThread = NULL;
static HANDLE g_hAICommandThread = NULL;
static HWND g_hAICommandWindow = NULL;
static HWND g_ClickSaverWindow = NULL;
static volatile LONG g_AIMissionState = AI_STATE_STOPPED;
static DWORD g_AIMissionDifficulty = CS_AI_DIFFICULTY_EASY;
static volatile LONG g_AIPendingMissionId = 0;
static volatile LONG g_bAIGoodbyeSelected = 0;
static volatile LONG g_AILastAnswerListGeneration = 0;
static volatile LONG g_AIGoodbyeAnswerGeneration = 0;
static AOIdentity g_AINpcIdentity = { 0, 0 };
static AcceptedMissionSnapshot g_AIMissionBaseline = { 0 };
static CRITICAL_SECTION g_AIMissionBaselineLock;
static volatile LONG g_bBaselineLockInitialized = 0;
static volatile LONG g_bDataBlockHookInstalled = 0;

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

        if (!mission || !IsReadableRange(mission, 0x40))
            return FALSE;

        identityType = *(DWORD*)mission;
        missionId = *(DWORD*)(mission + sizeof(DWORD));
        if (identityType != AO_MISSION_IDENTITY_TYPE)
            return FALSE;

        for (i = 0; i < snapshot->count; i++)
            if (snapshot->missionIds[i] == missionId)
                break;

        if (i == snapshot->count)
        {
            snapshot->missionIds[snapshot->count] = missionId;
            snapshot->count++;
        }
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

static DWORD ReadBE32(const BYTE* value)
{
    return ((DWORD)value[0] << 24) | ((DWORD)value[1] << 16) |
        ((DWORD)value[2] << 8) | value[3];
}

static void WriteBE16(BYTE* destination, WORD value)
{
    destination[0] = (BYTE)(value >> 8);
    destination[1] = (BYTE)value;
}

static void WriteBE32(BYTE* destination, DWORD value)
{
    destination[0] = (BYTE)(value >> 24);
    destination[1] = (BYTE)(value >> 16);
    destination[2] = (BYTE)(value >> 8);
    destination[3] = (BYTE)value;
}

static void SendAIEvent(HWND clickSaverWindow, CSAIMissionEvent* event)
{
    COPYDATASTRUCT data = { 0 };
    DWORD_PTR result = 0;
    if (!clickSaverWindow || !event)
        return;
    data.dwData = CS_AI_EVENT_COPYDATA;
    data.cbData = sizeof(*event);
    data.lpData = event;
    SendMessageTimeoutA(clickSaverWindow, WM_COPYDATA,
        (WPARAM)GetAOClientWindow(), (LPARAM)&data,
        SMTO_ABORTIFHUNG, 1000, &result);
}

static void SendAIStatus(const char* status)
{
    CSAIMissionEvent event = { 0 };
    event.type = CS_AI_EVENT_STATUS;
    event.state = (DWORD)InterlockedCompareExchange(&g_AIMissionState,
        AI_STATE_STOPPED, AI_STATE_STOPPED);
    snprintf(event.status, sizeof(event.status), "%s", status ? status : "");
    SendAIEvent(g_ClickSaverWindow, &event);
}

static BOOL SendAON3Message(DWORD messageType, const BYTE* specificData,
    DWORD specificDataSize)
{
    HMODULE n3Module = GetModuleHandleA("N3.dll");
    HMODULE interfaceModule = GetModuleHandleA("Interfaces.dll");
    HMODULE connectionModule = GetModuleHandleA("Connection.dll");
    GetN3EngineInstanceFn getEngine;
    GetClientInstFn getClientInst;
    GetClientInstanceFn getClientInstance;
    ConnectionSendFn send;
    BYTE packet[128] = { 0 };
    BYTE* body = packet + AO_PACKET_HEADER_SIZE;
    void* engine;
    void* client;
    void* connection;
    DWORD localInstance;
    DWORD packetSize = AO_PACKET_HEADER_SIZE + 13 + specificDataSize;

    if (!n3Module || !interfaceModule || !connectionModule ||
        packetSize > sizeof(packet))
        return FALSE;

    getEngine = (GetN3EngineInstanceFn)GetProcAddress(
        n3Module, "?GetInstance@n3Engine_t@@SAPAV1@XZ");
    getClientInst = (GetClientInstFn)GetProcAddress(
        n3Module, "?GetClientInst@n3EngineClient_t@@QBEIXZ");
    getClientInstance = (GetClientInstanceFn)GetProcAddress(
        interfaceModule, "?GetInstanceIfAny@Client_t@@SAPAV1@XZ");
    send = (ConnectionSendFn)GetProcAddress(
        connectionModule, "?Send@Connection_t@@QAEHIIPBX@Z");
    if (!getEngine || !getClientInst || !getClientInstance || !send)
        return FALSE;

    engine = getEngine();
    client = getClientInstance();
    if (!engine || !client || !IsReadableRange(client, 0x88))
        return FALSE;

    __try
    {
        localInstance = (DWORD)getClientInst(engine);
        connection = *(void**)((BYTE*)client + 0x84);
        if (!localInstance || !connection ||
            !IsReadableRange(connection, sizeof(void*)))
            return FALSE;

        WriteBE16(packet, 0xFFFF);
        WriteBE16(packet + 2, 0x000A);
        WriteBE16(packet + 4, 0x0001);
        WriteBE16(packet + 6, (WORD)packetSize);
        WriteBE32(packet + 8, localInstance);
        WriteBE32(packet + 12, 0x00000002);

        WriteBE32(body, messageType);
        WriteBE32(body + 4, AO_SIMPLE_CHAR_IDENTITY_TYPE);
        WriteBE32(body + 8, localInstance);
        body[12] = 0x01;
        if (specificDataSize)
            memcpy(body + 13, specificData, specificDataSize);

        return send(connection, 0, (int)packetSize, packet) >= 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return FALSE;
    }
}

static BOOL SendOpenChatWindow(AOIdentity target)
{
    BYTE specific[18] = { 0 };
    WriteBE16(specific, 2);
    WriteBE32(specific + 2, target.type);
    WriteBE32(specific + 6, target.instance);
    WriteBE32(specific + 10, 0);
    WriteBE32(specific + 14, 0);
    return SendAON3Message(AO_N3_KNUBOT_OPEN_CHAT, specific,
        sizeof(specific));
}

static BOOL SendCloseChatWindow(AOIdentity target)
{
    BYTE specific[18] = { 0 };
    WriteBE16(specific, 2);
    WriteBE32(specific + 2, target.type);
    WriteBE32(specific + 6, target.instance);
    return SendAON3Message(AO_N3_KNUBOT_CLOSE_CHAT, specific,
        sizeof(specific));
}

static BOOL SendNpcAnswer(AOIdentity target, DWORD answer)
{
    BYTE specific[14] = { 0 };
    WriteBE16(specific, 2);
    WriteBE32(specific + 2, target.type);
    WriteBE32(specific + 6, target.instance);
    WriteBE32(specific + 10, answer);
    return SendAON3Message(AO_N3_KNUBOT_ANSWER, specific,
        sizeof(specific));
}

static BOOL SendMissionDelete(DWORD missionId)
{
    BYTE specific[24] = { 0 };
    WriteBE32(specific, 1);
    WriteBE32(specific + 4, 0);
    WriteBE32(specific + 8, AO_MISSION_IDENTITY_TYPE);
    WriteBE32(specific + 12, missionId);
    WriteBE32(specific + 16, 0);
    WriteBE32(specific + 20, 0);
    return SendAON3Message(AO_N3_QUEST, specific, sizeof(specific));
}

static BOOL ParseAIAnswerList(const BYTE* data, SIZE_T size,
    AIAnswerList* answerList)
{
    if (!data || !answerList || size < 32)
        return FALSE;

    for (SIZE_T offset = 0; offset + 32 < size; offset++)
    {
        const BYTE* message = data + offset;
        SIZE_T remaining = size - offset;
        DWORD count;
        SIZE_T cursor;
        BOOL complete = TRUE;

        if (ReadBE32(message) != AO_N3_KNUBOT_ANSWER_LIST)
            continue;

        answerList->target.type = ReadBE32(message + 15);
        answerList->target.instance = ReadBE32(message + 19);
        if (answerList->target.type != g_AINpcIdentity.type ||
            answerList->target.instance != g_AINpcIdentity.instance)
            continue;

        count = ReadBE32(message + 23);
        if (count == 0 || count > AO_AI_MAX_DIALOG_OPTIONS)
            continue;

        cursor = 27;
        for (DWORD i = 0; i < count; i++)
        {
            DWORD length;
            if (cursor + 4 > remaining)
            {
                complete = FALSE;
                break;
            }
            length = ReadBE32(message + cursor);
            cursor += 4;
            if (length >= AO_AI_DIALOG_OPTION_SIZE ||
                cursor + length > remaining)
            {
                complete = FALSE;
                break;
            }
            memcpy(answerList->options[i], message + cursor, length);
            answerList->options[i][length] = '\0';
            cursor += length;
        }
        if (complete && answerList->options[0][0])
        {
            answerList->count = count;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL GetCurrentOfficerTarget(AOIdentity* target)
{
    HMODULE guiModule = GetModuleHandleA("GUI.dll");
    HMODULE n3Module = GetModuleHandleA("N3.dll");
    HMODULE gamecodeModule = GetModuleHandleA("Gamecode.dll");
    GetInputConfigInstanceFn getInputConfig;
    GetCurrentTargetFn getCurrentTarget;
    GetN3EngineInstanceFn getEngine;
    GetDynelNameFn getName;
    void* inputConfig;
    void* engine;
    AOIdentity unknownIdentity = { 0, 0 };
    const char* name;

    if (!guiModule || !n3Module || !gamecodeModule || !target)
        return FALSE;
    getInputConfig = (GetInputConfigInstanceFn)GetProcAddress(
        guiModule, "?GetInstance@InputConfig_t@@SAPAV1@XZ");
    getCurrentTarget = (GetCurrentTargetFn)GetProcAddress(
        guiModule, "?GetCurrentTarget@InputConfig_t@@QBE?AVIdentity_t@@XZ");
    getEngine = (GetN3EngineInstanceFn)GetProcAddress(
        n3Module, "?GetInstance@n3Engine_t@@SAPAV1@XZ");
    getName = (GetDynelNameFn)GetProcAddress(gamecodeModule,
        "?N3Msg_GetName@n3EngineClientAnarchy_t@@QBEPBDABVIdentity_t@@0@Z");
    if (!getInputConfig || !getCurrentTarget || !getEngine || !getName)
        return FALSE;

    inputConfig = getInputConfig();
    engine = getEngine();
    if (!inputConfig || !engine || !IsReadableRange(inputConfig, sizeof(void*)))
        return FALSE;

    __try
    {
        memset(target, 0, sizeof(*target));
        if (!getCurrentTarget(inputConfig, target) || !target->instance)
            return FALSE;

        name = getName(engine, target, &unknownIdentity);
        return name && IsReadableRange(name,
            sizeof("Unicorn Intelligence Officer")) &&
            _stricmp(name, "Unicorn Intelligence Officer") == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return FALSE;
    }
}

static BOOL TryGetMissionLocation(DWORD missionId, DWORD* zoneId,
    float* x, float* y)
{
    HMODULE gamecodeModule = GetModuleHandleA("Gamecode.dll");
    HMODULE n3Module = GetModuleHandleA("N3.dll");
    GetN3EngineInstanceFn getEngine;
    GetQuestWorldPosFn getQuestWorldPos;
    void* engine;
    AOIdentity mission = { AO_MISSION_IDENTITY_TYPE, missionId };
    AOIdentity playfield = { 0, 0 };
    AOVector3 universePosition = { 0, 0, 0 };
    AOVector3 zonePosition = { 0, 0, 0 };

    if (!gamecodeModule || !n3Module || !zoneId || !x || !y)
        return FALSE;
    getEngine = (GetN3EngineInstanceFn)GetProcAddress(
        n3Module, "?GetInstance@n3Engine_t@@SAPAV1@XZ");
    getQuestWorldPos = (GetQuestWorldPosFn)GetProcAddress(
        gamecodeModule,
        "?N3Msg_GetQuestWorldPos@n3EngineClientAnarchy_t@@QBE_NABVIdentity_t@@AAV2@AAVVector3_t@@2@Z");
    if (!getEngine || !getQuestWorldPos)
        return FALSE;
    __try
    {
        engine = getEngine();
        if (!engine || !getQuestWorldPos(engine, &mission, &playfield,
            &universePosition, &zonePosition))
            return FALSE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return FALSE;
    }

    if (!playfield.instance || !_finite(zonePosition.x) ||
        !_finite(zonePosition.z))
        return FALSE;
    *zoneId = playfield.instance;
    *x = zonePosition.x;
    *y = zonePosition.z;
    return TRUE;
}

#define WM_CS_AI_ANSWER_LIST (WM_APP + 41)
#define WM_CS_AI_REOPEN_CHAT (WM_APP + 42)
#define TIMER_AI_REOPEN_CHAT 1
#define AI_REOPEN_CHAT_POLL_MS 1000

static const char* GetDifficultyAnswer(DWORD difficulty)
{
    switch (difficulty)
    {
    case CS_AI_DIFFICULTY_EASY:
        return "Go easy on me for now...";
    case CS_AI_DIFFICULTY_NORMAL:
        return "I'm pretty sure of myself.";
    case CS_AI_DIFFICULTY_HARD:
        return "Give me the most dangerous assignment you have!";
    default:
        return NULL;
    }
}

static int FindDialogOption(const AIAnswerList* answerList,
    const char* expectedText)
{
    if (!answerList || !expectedText)
        return -1;
    size_t expectedLength = strlen(expectedText);
    for (DWORD i = 0; i < answerList->count; i++)
    {
        const char* option = answerList->options[i];
        while (*option == ' ' || *option == '\t')
            option++;
        size_t length = strlen(option);
        while (length && (option[length - 1] == ' ' ||
            option[length - 1] == '\t' || option[length - 1] == '\r' ||
            option[length - 1] == '\n'))
            length--;
        for (size_t start = 0; start + expectedLength <= length; start++)
            if (_strnicmp(option + start, expectedText, expectedLength) == 0)
                return (int)i;
    }
    return -1;
}

static LRESULT HandleAIAnswerList(const AIAnswerList* answerList)
{
    LONG state = InterlockedCompareExchange(&g_AIMissionState,
        AI_STATE_STOPPED, AI_STATE_STOPPED);
    const char* expectedText;
    int answer;
    AcceptedMissionSnapshot missionBaseline = { 0 };

    if (!answerList || answerList->target.type != g_AINpcIdentity.type ||
        answerList->target.instance != g_AINpcIdentity.instance)
        return 0;

    if (state == AI_STATE_WAITING_FOR_DELETION ||
        state == AI_STATE_WAITING_FOR_REOPEN ||
        state == AI_STATE_WAITING_FOR_CLOSE)
    {
        answer = FindDialogOption(answerList, "Goodbye");
        if (answer >= 0 &&
            !InterlockedCompareExchange(&g_bAIGoodbyeSelected, 0, 0) &&
            SendNpcAnswer(g_AINpcIdentity, (DWORD)answer))
        {
            InterlockedExchange(&g_bAIGoodbyeSelected, 1);
            InterlockedExchange(&g_AIGoodbyeAnswerGeneration,
                InterlockedCompareExchange(
                    &g_AILastAnswerListGeneration, 0, 0));
            if (state == AI_STATE_WAITING_FOR_REOPEN &&
                !SetTimer(g_hAICommandWindow, TIMER_AI_REOPEN_CHAT,
                    AI_REOPEN_CHAT_POLL_MS, NULL))
            {
                InterlockedExchange(&g_AIPendingMissionId, 0);
                InterlockedExchange(&g_AIMissionState, AI_STATE_STOPPED);
                SendAIStatus("sent Goodbye, but could not schedule the dialog-close check");
                return 1;
            }
            if (state == AI_STATE_WAITING_FOR_CLOSE &&
                !SetTimer(g_hAICommandWindow, TIMER_AI_REOPEN_CHAT,
                    AI_REOPEN_CHAT_POLL_MS, NULL))
            {
                InterlockedExchange(&g_AIMissionState, AI_STATE_STOPPED);
                SendAIStatus("sent Goodbye, but could not schedule the dialog-close check");
                return 1;
            }
            if (state != AI_STATE_WAITING_FOR_CLOSE)
                SendAIStatus(state == AI_STATE_WAITING_FOR_REOPEN
                    ? "sent Goodbye; checking for the dialog to close"
                    : "closing the completed officer dialogue");
            return 1;
        }
        return 0;
    }

    if (state == AI_STATE_WAITING_FOR_CONFIRMATION)
        expectedText = "Yes, I am";
    else if (state == AI_STATE_WAITING_FOR_DIFFICULTY)
        expectedText = GetDifficultyAnswer(g_AIMissionDifficulty);
    else
        return 0;

    answer = FindDialogOption(answerList, expectedText);
    if (answer < 0)
    {
        SendAIStatus("waiting for the expected officer reply");
        return 0;
    }

    if (state == AI_STATE_WAITING_FOR_DIFFICULTY &&
        !TryReadAcceptedMissionSnapshot(&missionBaseline))
    {
        InterlockedExchange(&g_AIMissionState, AI_STATE_STOPPED);
        SendAIStatus("could not read current missions; stopped");
        return 0;
    }

    if (!SendNpcAnswer(g_AINpcIdentity, (DWORD)answer))
    {
        InterlockedExchange(&g_AIMissionState, AI_STATE_STOPPED);
        SendAIStatus("could not send the officer reply; stopped");
        return 0;
    }

    if (state == AI_STATE_WAITING_FOR_CONFIRMATION)
    {
        InterlockedExchange(&g_AIMissionState, AI_STATE_WAITING_FOR_DIFFICULTY);
        SendAIStatus("confirming eligibility");
    }
    else
    {
        EnterCriticalSection(&g_AIMissionBaselineLock);
        g_AIMissionBaseline = missionBaseline;
        LeaveCriticalSection(&g_AIMissionBaselineLock);
        InterlockedExchange(&g_AIMissionState, AI_STATE_WAITING_FOR_MISSION);
        SendAIStatus("waiting for the AI mission");
    }
    return 1;
}

static BOOL HandleAICommand(const CSAIMissionCommand* command)
{
    if (!command)
        return FALSE;

    switch (command->type)
    {
    case CS_AI_COMMAND_START:
        {
            AOIdentity target = { 0, 0 };
            AcceptedMissionSnapshot snapshot = { 0 };
            if (InterlockedCompareExchange(&g_AIMissionState,
                AI_STATE_STOPPED, AI_STATE_STOPPED) != AI_STATE_STOPPED)
            {
                SendAIStatus("AI missions are already running");
                return FALSE;
            }
            if (command->difficulty > CS_AI_DIFFICULTY_HARD)
            {
                SendAIStatus("invalid difficulty selection");
                return FALSE;
            }
            if (!GetCurrentOfficerTarget(&target))
            {
                SendAIStatus("target the Unicorn Intelligence Officer first");
                return FALSE;
            }
            if (!TryReadAcceptedMissionSnapshot(&snapshot))
            {
                SendAIStatus("could not read current missions");
                return FALSE;
            }

            g_AINpcIdentity = target;
            g_AIMissionDifficulty = command->difficulty;
            InterlockedExchange(&g_bAIGoodbyeSelected, 0);
            InterlockedExchange(&g_AILastAnswerListGeneration, 0);
            InterlockedExchange(&g_AIGoodbyeAnswerGeneration, 0);
            memset(&g_AILastAnswerList, 0, sizeof(g_AILastAnswerList));
            EnterCriticalSection(&g_AIMissionBaselineLock);
            g_AIMissionBaseline = snapshot;
            LeaveCriticalSection(&g_AIMissionBaselineLock);
            InterlockedExchange(&g_AIMissionState,
                AI_STATE_WAITING_FOR_CONFIRMATION);
            if (!SendOpenChatWindow(target))
            {
                InterlockedExchange(&g_AIMissionState, AI_STATE_STOPPED);
                SendAIStatus("could not open officer dialogue");
                return FALSE;
            }
            SendAIStatus("officer dialogue opened");
            return TRUE;
        }
    case CS_AI_COMMAND_STOP:
        InterlockedExchange(&g_AIMissionState, AI_STATE_STOPPED);
        InterlockedExchange(&g_AIPendingMissionId, 0);
        return TRUE;
    case CS_AI_COMMAND_ACCEPT_MISSION:
        if (InterlockedCompareExchange(&g_AIMissionState,
            AI_STATE_STOPPED, AI_STATE_STOPPED) != AI_STATE_WAITING_FOR_DECISION ||
            command->missionId != g_AIPendingMissionId)
            return FALSE;
        InterlockedExchange(&g_AIPendingMissionId, 0);
        InterlockedExchange(&g_bAIGoodbyeSelected, 0);
        InterlockedExchange(&g_AIMissionState, AI_STATE_WAITING_FOR_CLOSE);
        SendAIStatus("destination accepted; mission kept");
        HandleAIAnswerList(&g_AILastAnswerList);
        if (!SetTimer(g_hAICommandWindow, TIMER_AI_REOPEN_CHAT,
            AI_REOPEN_CHAT_POLL_MS, NULL))
        {
            InterlockedExchange(&g_AIMissionState, AI_STATE_STOPPED);
            if (SendCloseChatWindow(g_AINpcIdentity))
                SendAIStatus("mission kept; could not schedule Goodbye retries; close request sent");
            else
                SendAIStatus("mission kept; could not schedule Goodbye retries or close the officer dialogue");
        }
        return TRUE;
    case CS_AI_COMMAND_REJECT_MISSION:
        if (InterlockedCompareExchange(&g_AIMissionState,
            AI_STATE_STOPPED, AI_STATE_STOPPED) != AI_STATE_WAITING_FOR_DECISION ||
            command->missionId != g_AIPendingMissionId)
            return FALSE;
        InterlockedExchange(&g_bAIGoodbyeSelected, 0);
        if (!SendMissionDelete(command->missionId))
        {
            InterlockedExchange(&g_AIPendingMissionId, 0);
            InterlockedExchange(&g_AIMissionState, AI_STATE_STOPPED);
            SendAIStatus("could not delete the wrong-location mission");
            return FALSE;
        }
        InterlockedExchange(&g_AIPendingMissionId,
            (LONG)command->missionId);
        InterlockedExchange(&g_AIMissionState, AI_STATE_WAITING_FOR_DELETION);
        SendAIStatus("wrong-location mission deleted; waiting to restart dialogue");
        HandleAIAnswerList(&g_AILastAnswerList);
        return TRUE;
    default:
        return FALSE;
    }
}

static LRESULT CALLBACK AICommandWndProc(HWND window, UINT message,
    WPARAM wParam, LPARAM lParam)
{
    if (message == WM_COPYDATA)
    {
        COPYDATASTRUCT* data = (COPYDATASTRUCT*)lParam;
        char senderClass[64] = "";
        if (!data || data->dwData != CS_AI_COMMAND_COPYDATA ||
            data->cbData != sizeof(CSAIMissionCommand) || !data->lpData)
            return FALSE;
        if (!IsWindow((HWND)wParam) ||
            !GetClassNameA((HWND)wParam, senderClass, sizeof(senderClass)) ||
            strcmp(senderClass, "ClickSaverHookWindowClass") != 0)
            return FALSE;
        g_ClickSaverWindow = (HWND)wParam;
        return HandleAICommand((const CSAIMissionCommand*)data->lpData);
    }
    if (message == WM_CS_AI_ANSWER_LIST)
    {
        const AIAnswerList* answerList = (const AIAnswerList*)lParam;
        if (!answerList)
            return 0;
        g_AILastAnswerList = *answerList;
        InterlockedIncrement(&g_AILastAnswerListGeneration);
        return HandleAIAnswerList(answerList);
    }
    if (message == WM_CS_AI_REOPEN_CHAT)
    {
        if (InterlockedCompareExchange(&g_AIMissionState,
            AI_STATE_STOPPED, AI_STATE_STOPPED) == AI_STATE_WAITING_FOR_REOPEN &&
            (DWORD)InterlockedCompareExchange(&g_AIPendingMissionId, 0, 0) ==
                (DWORD)wParam)
        {
            if (!SetTimer(window, TIMER_AI_REOPEN_CHAT,
                AI_REOPEN_CHAT_POLL_MS, NULL))
            {
                InterlockedExchange(&g_AIPendingMissionId, 0);
                InterlockedExchange(&g_AIMissionState, AI_STATE_STOPPED);
                SendAIStatus("mission deleted, but could not schedule the Goodbye check");
                return 0;
            }
            SendAIStatus("mission deleted");
            return 1;
        }
        return 0;
    }
    if (message == WM_TIMER && wParam == TIMER_AI_REOPEN_CHAT)
    {
        KillTimer(window, TIMER_AI_REOPEN_CHAT);
        LONG state = InterlockedCompareExchange(&g_AIMissionState,
            AI_STATE_STOPPED, AI_STATE_STOPPED);
        if (state == AI_STATE_WAITING_FOR_CLOSE)
        {
            int goodbyeAnswer = FindDialogOption(
                &g_AILastAnswerList, "Goodbye");
            if (goodbyeAnswer >= 0)
            {
                if (SendNpcAnswer(g_AINpcIdentity, (DWORD)goodbyeAnswer))
                {
                    InterlockedExchange(&g_bAIGoodbyeSelected, 1);
                    InterlockedExchange(&g_AIGoodbyeAnswerGeneration,
                        InterlockedCompareExchange(
                            &g_AILastAnswerListGeneration, 0, 0));
                }
                else
                    SendAIStatus("could not resend Goodbye; retrying");
                if (!SetTimer(window, TIMER_AI_REOPEN_CHAT,
                    AI_REOPEN_CHAT_POLL_MS, NULL))
                {
                    InterlockedExchange(&g_AIMissionState, AI_STATE_STOPPED);
                    SendAIStatus("could not schedule the next Goodbye retry");
                }
                return 0;
            }

            InterlockedExchange(&g_AIMissionState, AI_STATE_STOPPED);
            if (InterlockedCompareExchange(&g_bAIGoodbyeSelected, 0, 0))
            {
                if (SendCloseChatWindow(g_AINpcIdentity))
                    SendAIStatus("mission kept; Goodbye sent; officer-dialog close request sent");
                else
                    SendAIStatus("mission kept; Goodbye sent; could not send officer-dialog close request");
            }
            else if (SendCloseChatWindow(g_AINpcIdentity))
                SendAIStatus("mission kept; Goodbye was unavailable; officer-dialog close request sent");
            else
                SendAIStatus("mission kept; Goodbye unavailable and could not close officer dialogue");
            return 0;
        }
        if (state == AI_STATE_WAITING_FOR_REOPEN)
        {
            LONG answerGeneration = InterlockedCompareExchange(
                &g_AILastAnswerListGeneration, 0, 0);
            LONG goodbyeGeneration = InterlockedCompareExchange(
                &g_AIGoodbyeAnswerGeneration, 0, 0);
            int goodbyeAnswer = FindDialogOption(
                &g_AILastAnswerList, "Goodbye");
            if (!InterlockedCompareExchange(&g_bAIGoodbyeSelected, 0, 0))
            {
                if (!SetTimer(window, TIMER_AI_REOPEN_CHAT,
                    AI_REOPEN_CHAT_POLL_MS, NULL))
                {
                    InterlockedExchange(&g_AIPendingMissionId, 0);
                    InterlockedExchange(&g_AIMissionState, AI_STATE_STOPPED);
                    SendAIStatus("could not schedule the next Goodbye check");
                }
                return 0;
            }

            if (answerGeneration > goodbyeGeneration && goodbyeAnswer >= 0)
            {
                if (!SendNpcAnswer(g_AINpcIdentity, (DWORD)goodbyeAnswer))
                    SendAIStatus("could not resend Goodbye; will retry");
                else
                {
                    InterlockedExchange(&g_AIGoodbyeAnswerGeneration,
                        answerGeneration);
                }
                if (!SetTimer(window, TIMER_AI_REOPEN_CHAT,
                    AI_REOPEN_CHAT_POLL_MS, NULL))
                {
                    InterlockedExchange(&g_AIPendingMissionId, 0);
                    InterlockedExchange(&g_AIMissionState, AI_STATE_STOPPED);
                    SendAIStatus("could not schedule the next dialog-close check");
                }
                return 0;
            }

            InterlockedExchange(&g_AIPendingMissionId, 0);
            InterlockedExchange(&g_AIMissionState,
                AI_STATE_WAITING_FOR_CONFIRMATION);
            InterlockedExchange(&g_bAIGoodbyeSelected, 0);
            if (!SendOpenChatWindow(g_AINpcIdentity))
            {
                InterlockedExchange(&g_AIMissionState, AI_STATE_STOPPED);
                SendAIStatus("could not restart officer dialogue");
                return 0;
            }
            SendAIStatus("Goodbye dialog closed; reopening officer dialogue");
            return 1;
        }
        return 0;
    }
    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, wParam, lParam);
}

static DWORD WINAPI AICommandWindowThread(void*)
{
    WNDCLASSEXA windowClass = { 0 };
    MSG message;
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = AICommandWndProc;
    windowClass.hInstance = GetModuleHandleA(NULL);
    windowClass.lpszClassName = "ClickSaverAICommandWindowClass";
    if (!RegisterClassExA(&windowClass) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 1;

    g_hAICommandWindow = CreateWindowExA(0, windowClass.lpszClassName,
        "ClickSaverAICommandWindow", WS_POPUP, 0, 0, 0, 0,
        NULL, NULL, windowClass.hInstance, NULL);
    if (!g_hAICommandWindow)
    {
        UnregisterClassA(windowClass.lpszClassName, windowClass.hInstance);
        return 1;
    }

    while (GetMessageA(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    g_hAICommandWindow = NULL;
    UnregisterClassA(windowClass.lpszClassName, windowClass.hInstance);
    return 0;
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
            LONG aiState = InterlockedCompareExchange(&g_AIMissionState,
                AI_STATE_STOPPED, AI_STATE_STOPPED);
            if (aiState == AI_STATE_WAITING_FOR_MISSION)
            {
                AcceptedMissionSnapshot baseline = { 0 };
                EnterCriticalSection(&g_AIMissionBaselineLock);
                baseline = g_AIMissionBaseline;
                LeaveCriticalSection(&g_AIMissionBaselineLock);
                for (DWORD i = 0; i < snapshot.count; i++)
                {
                    DWORD j;
                    for (j = 0; j < baseline.count; j++)
                        if (snapshot.missionIds[i] == baseline.missionIds[j])
                            break;
                    if (j == baseline.count &&
                        InterlockedCompareExchange(&g_AIMissionState,
                            AI_STATE_WAITING_FOR_DECISION,
                            AI_STATE_WAITING_FOR_MISSION) ==
                                AI_STATE_WAITING_FOR_MISSION)
                    {
                        CSAIMissionEvent event = { 0 };
                        event.type = CS_AI_EVENT_MISSION;
                        event.missionId = snapshot.missionIds[i];
                        InterlockedExchange(&g_AIPendingMissionId,
                            (LONG)event.missionId);
                        if (TryGetMissionLocation(event.missionId,
                            &event.zoneId, &event.x, &event.y))
                        {
                            SendAIEvent(FindWindowA(
                                "ClickSaverHookWindowClass",
                                "ClickSaverHookWindow"), &event);
                        }
                        else
                        {
                            InterlockedExchange(&g_AIPendingMissionId, 0);
                            InterlockedExchange(&g_AIMissionState,
                                AI_STATE_WAITING_FOR_MISSION);
                            SendAIStatus("could not read the new mission destination");
                        }
                        break;
                    }
                }
            }
            else if (aiState == AI_STATE_WAITING_FOR_DELETION)
            {
                BOOL pendingMissionStillPresent = FALSE;
                for (DWORD i = 0; i < snapshot.count; i++)
                    if (snapshot.missionIds[i] ==
                        (DWORD)InterlockedCompareExchange(
                            &g_AIPendingMissionId, 0, 0))
                        pendingMissionStillPresent = TRUE;
                if (!pendingMissionStillPresent && g_hAICommandWindow &&
                    InterlockedCompareExchange(&g_AIMissionState,
                        AI_STATE_WAITING_FOR_REOPEN,
                        AI_STATE_WAITING_FOR_DELETION) ==
                            AI_STATE_WAITING_FOR_DELETION)
                    PostMessageA(g_hAICommandWindow, WM_CS_AI_REOPEN_CHAT,
                        (WPARAM)InterlockedCompareExchange(
                            &g_AIPendingMissionId, 0, 0), 0);
            }

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
    HWND hWnd;
    LONG aiState = InterlockedCompareExchange(&g_AIMissionState,
        AI_STATE_STOPPED, AI_STATE_STOPPED);

    if (_Size > 0 && _pDataBlock && g_hAICommandWindow &&
        (aiState == AI_STATE_WAITING_FOR_CONFIRMATION ||
            aiState == AI_STATE_WAITING_FOR_DIFFICULTY ||
            aiState == AI_STATE_WAITING_FOR_MISSION ||
            aiState == AI_STATE_WAITING_FOR_DECISION ||
        aiState == AI_STATE_WAITING_FOR_DELETION ||
            aiState == AI_STATE_WAITING_FOR_REOPEN ||
            aiState == AI_STATE_WAITING_FOR_CLOSE))
    {
        AIAnswerList answerList = { 0 };
        if (ParseAIAnswerList((const BYTE*)_pDataBlock, (SIZE_T)_Size,
            &answerList))
        {
            SendMessageA(g_hAICommandWindow, WM_CS_AI_ANSWER_LIST,
                0, (LPARAM)&answerList);
        }
    }

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
    HMODULE messageProtocol = GetModuleHandleA("MessageProtocol.dll");
    if (!messageProtocol)
        return FALSE;

    pOriginalDataBlockToMessage = (Message_t* (__cdecl*)(int, void*))
        GetProcAddress(messageProtocol,
            "?DataBlockToMessage@@YAPAVMessage_t@@IPAX@Z");
    if (!pOriginalDataBlockToMessage)
        return FALSE;

    LONG detourResult = DetourTransactionBegin();
    if (detourResult != NO_ERROR)
        return FALSE;
    detourResult = DetourUpdateThread(GetCurrentThread());
    if (detourResult == NO_ERROR)
        detourResult = DetourAttach((PVOID*)&pOriginalDataBlockToMessage,
            DataBlockToMessageHook);
    if (detourResult != NO_ERROR)
    {
        DetourTransactionAbort();
        pOriginalDataBlockToMessage = NULL;
        return FALSE;
    }
    detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        pOriginalDataBlockToMessage = NULL;
        return FALSE;
    }
    InterlockedExchange(&g_bDataBlockHookInstalled, 1);

    InitializeCriticalSection(&g_AIMissionBaselineLock);
    InterlockedExchange(&g_bBaselineLockInitialized, 1);
    InterlockedExchange(&g_bStopMissionScanner, 0);
    g_hAICommandThread = CreateThread(NULL, 0,
        AICommandWindowThread, NULL, 0, NULL);
    g_hMissionScannerThread = CreateThread(NULL, 0,
        AcceptedMissionScannerThread, NULL, 0, NULL);
    return TRUE;
}


int ProcessDetach( HINSTANCE _hModule )
{
    InterlockedExchange(&g_AIMissionState, AI_STATE_STOPPED);
    InterlockedExchange(&g_bStopMissionScanner, 1);
    if (g_hMissionScannerThread)
    {
        WaitForSingleObject(g_hMissionScannerThread, INFINITE);
        CloseHandle(g_hMissionScannerThread);
        g_hMissionScannerThread = NULL;
    }
    if (g_hAICommandWindow)
        PostMessageA(g_hAICommandWindow, WM_CLOSE, 0, 0);
    if (g_hAICommandThread)
    {
        DWORD waitResult = WaitForSingleObject(g_hAICommandThread, 5000);
        CloseHandle(g_hAICommandThread);
        g_hAICommandThread = NULL;
        if (waitResult == WAIT_OBJECT_0 &&
            InterlockedExchange(&g_bBaselineLockInitialized, 0))
            DeleteCriticalSection(&g_AIMissionBaselineLock);
    }
    else if (InterlockedExchange(&g_bBaselineLockInitialized, 0))
    {
        DeleteCriticalSection(&g_AIMissionBaselineLock);
    }

    if (InterlockedExchange(&g_bDataBlockHookInstalled, 0))
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach((PVOID*)&pOriginalDataBlockToMessage,
            DataBlockToMessageHook);
        DetourTransactionCommit();
    }
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
