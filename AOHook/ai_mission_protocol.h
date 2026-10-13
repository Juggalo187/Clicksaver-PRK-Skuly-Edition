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
    CS_AI_COMMAND_ACCEPT_MISSION
} CSAIMissionCommandType;

typedef enum CSAIMissionEventType
{
    CS_AI_EVENT_STATUS = 1,
    CS_AI_EVENT_MISSION
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
    CS_AI_STATE_WAITING_FOR_CLOSE
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
} CSAIMissionCommand;

typedef struct CSAIMissionEvent
{
    DWORD type;
    DWORD state;
    DWORD missionId;
    DWORD zoneId;
    float x;
    float y;
    char status[160];
} CSAIMissionEvent;

#endif
