#ifndef CS_AI_MISSION_PROTOCOL_H
#define CS_AI_MISSION_PROTOCOL_H

#include <windows.h>

#define CS_AI_COMMAND_COPYDATA 0x32494353UL
#define CS_AI_EVENT_COPYDATA 0x32494553UL

typedef enum CSAIMissionCommandType
{
    CS_AI_COMMAND_START = 1,
    CS_AI_COMMAND_STOP,
    CS_AI_COMMAND_REJECT_MISSION,
    CS_AI_COMMAND_ACCEPT_MISSION,
    CS_AI_COMMAND_NEXT,
    CS_AI_COMMAND_MONITOR_START,
    CS_AI_COMMAND_MONITOR_STOP,
    CS_AI_COMMAND_DELETE_MONITORED_MISSION,
    CS_AI_COMMAND_MONITOR_PING,
    CS_AI_COMMAND_PREPARE_UNLOAD
} CSAIMissionCommandType;

typedef enum CSAIMissionEventType
{
    CS_AI_EVENT_STATUS = 1,
    CS_AI_EVENT_MISSION,
    CS_AI_EVENT_MONITORED_MISSION,
    CS_AI_EVENT_MONITOR_STATUS
} CSAIMissionEventType;

typedef enum CSAIMissionState
{
    CS_AI_STATE_STOPPED,
    CS_AI_STATE_WAITING_FOR_CONFIRMATION,
    CS_AI_STATE_WAITING_FOR_DIFFICULTY,
    CS_AI_STATE_WAITING_FOR_MISSION,
    CS_AI_STATE_WAITING_FOR_DECISION,
    CS_AI_STATE_WAITING_FOR_DELETION,
    CS_AI_STATE_WAITING_FOR_REOPEN,
    CS_AI_STATE_WAITING_FOR_CLOSE,
    CS_AI_STATE_WAITING_FOR_RESTART
} CSAIMissionState;

typedef enum CSAIMissionDifficulty
{
    CS_AI_DIFFICULTY_EASY = 0,
    CS_AI_DIFFICULTY_NORMAL,
    CS_AI_DIFFICULTY_HARD
} CSAIMissionDifficulty;

typedef struct CSAIMissionCommand
{
    DWORD type;
    DWORD difficulty;
    DWORD missionId;
    DWORD manual;
} CSAIMissionCommand;

typedef struct CSAIMissionEvent
{
    DWORD type;
    DWORD state;
    DWORD missionId;
    DWORD zoneId;
    HWND clientWindow;
    DWORD clientProcessId;
    float x;
    float y;
    char status[160];
} CSAIMissionEvent;

#endif
