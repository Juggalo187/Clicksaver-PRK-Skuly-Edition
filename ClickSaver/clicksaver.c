	/*
 * $Log: clicksaver.c,v $
 * Revision 1.16  2004/12/27 17:28:12  gnarf37
 * Added Option for multiple missions, quick change
 *
 * Revision 1.15  2004/09/03 19:16:46  gnarf37
 * Version 2.3.1 AI Updates
 *
 * Revision 1.14  2004/08/28 18:04:08  gnarf37
 * Moved some GUI Options arounds, added Skip Rebuild option
 *
 * Revision 1.13  2004/01/25 19:35:52  gnarf37
 * 2.3.0 beta 3 - Shrunk Database a bit, added Item Value options, make options menu smaller a tad so that 800x600 might be able to use it again...
 *
 * Revision 1.12  2004/01/23 08:19:09  ibender
 * added mission slider settings
 *
 * Revision 1.11  2003/11/06 23:41:50  gnarf37
 * Version 2.3.0 beta 2 - Fixed issues with 15.2.0 and added an option for auto expand team missions
 *
 * Revision 1.10  2003/10/31 03:40:50  gnarf37
 * Saving/Loading Configurations
 *
 * Revision 1.9  2003/10/25 21:33:32  gnarf37
 * Fixed date/time checking... Should get rid of the major problem everyone is havving
 *
 * Revision 1.8  2003/05/27 00:14:42  gnarf37
 * Added Checkbox to stop mouse movement, and cleaned up mission info parsing so it doesnt match stale missions
 *
 * Revision 1.7  2003/05/08 09:11:09  gnarf37
 * Fullscreen Mode
 *
 * Revision 1.6  2003/05/08 08:40:04  gnarf37
 * Added Logging to Missions
 *
 * Revision 1.5  2003/05/08 07:36:55  gnarf37
 * Added Sounds
 *
 * Revision 1.4  2003/05/07 14:05:28  gnarf37
 * *** empty log message ***
 *
 */
/*
ClickSaver -  Anarchy Online mission helper
Copyright (C) 2001, 2002 Morb
Some parts Copyright (C) 2003, 2004 gnarf
Some parts Copyright (C) 2012 Darkbane, Adjuster

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

#include <stdarg.h>
#include "Platform.h"

#include <pul/pul.h>

#include <winuser.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <commctrl.h>
#include <windowsx.h>
#include "clicksaver.h"
#include "resource.h"

#include <ctype.h>
#include <shellapi.h>   
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")

#include "sqlite3.h"
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")

#define MATCH_AUTO_THRESHOLD 1

static FILE* g_AcceptedLogFile = NULL;

void ResetAcceptedMissionLog(void);
void LogAcceptedMission(int zoneId, float x, float y, PUU32 missionTypeId, const char* findItem, PUU32 mishId, const char* missionTitle);
void CloseAcceptedMissionLog(void);

sqlite3*      g_pSQLite = NULL;
sqlite3_stmt* g_stmtItem = NULL;
sqlite3_stmt* g_stmtIcon = NULL;
sqlite3_stmt* g_stmtPF = NULL;

void BuildItemNameCache(const char *filename);
int LoadItemNameCache(const char *cacheFilePath);
void FreeItemNameCache(void);

void CleanUp();
void ImportSettings( char* filename );
void ExportSettings( char* filename );

void DisplayErrorMessage( PUU8* _pMessage, PUU32 _bAsynchronous );

void GetFolder( HWND hWndOwner, char *strTitle, char *strPath );
BOOL GetFile( HWND hWndOwner, BOOL saving, char *buffer, int buffersize );

int BuyingAgent( int delay );
void EndBuyingAgent();
void UpdateAcceptedCountersForMission( int mishIndex );

PUU8 g_bForceUIRefresh;
PUU32 g_GUIDef[];
pusObjectCollection* g_pCol;
PULID g_ItemWatchList, g_LocWatchList, g_MainWin;
PULID g_DisabledItemWatchList;

void _setSliders( int easy_hard, int good_bad, int order_chaos, int open_hidden, int phys_myst, int headon_stealth, int money_xp );

PUU32 g_BuyingAgentCount = 0;
PUU32 g_BuyingAgentDelay = 5200;
PUU32 g_BuyingAgentMissions = 0;
PUU32 g_BuyingAgentMaxTries = 0;
PUU32 g_BuyingAgentMaxMissions = 0;
PUU32 g_TotalAttempts = 0;
PUU32 g_bFirstRound = TRUE;
PUU8 g_MishNumber = 0, g_FoundMish = -1;
PUU8 g_bFullscreen = 0;
PUU8 g_bUpdatingCounters = 0;
PUU8 g_bBuyingAgentActive = 0;
PUU8 g_bForceUIRefresh = 0;
PUU8 g_bPaused = 0;
int g_BAWindowX = 300;
int g_BAWindowY = 100;
void EditActiveItem(void);
void EditDisabledItem(void);
char g_CurrentPacket[ 65536 ];

char g_AODir[ MAX_PATH ] = { 0 };
char g_CSDir[ MAX_PATH ] = { 0 };

HANDLE g_Mutex = INVALID_HANDLE_VALUE;
HANDLE g_Thread = INVALID_HANDLE_VALUE;
HANDLE g_hThreadExitEvent = NULL;
HANDLE g_hAbortEvent = NULL;
DWORD WINAPI HookManagerThread( void *pParam );

static HBRUSH g_hDialogBgBrush = NULL;
static HBRUSH g_hButtonBgBrush = NULL;

static char** g_LoggedMissionKeys = NULL;
static int g_LoggedMissionCount = 0;
static int g_LoggedMissionCapacity = 0;
static char g_LastLoggedPlayfield[256] = "";

static const char* const PROP_BUTTON_IDS = "ClickSaver_OwnerDrawButtons";

typedef struct {
    char type[16];
    char name[128];
    int zoneId;
    float x, y;
    int group;
} ExitLocation;

static ExitLocation g_Exits[] = {
    // Whompahs (Neutral)
    {"Whompah", "Newland City", 566, 384.6f, 303.5f, 1},
    {"Whompah", "Newland Desert", 565, 2195.3f, 1565.7f, 1},
    {"Whompah", "Hope", 560, 2891.4f, 1910.2f, 1},
    {"Whompah", "Stret West Bank", 790, 1275.4f, 2883.5f, 1},
    {"Whompah", "Borealis", 800, 682.6f, 539.4f, 1},
    {"Whompah", "ICC", 655, 3238.7f, 900.0f, 1},
    // Whompahs (Omni)
    {"Whompah", "Omni Trade", 710, 341.7f, 382.3f, 2},
    {"Whompah", "Galway Castle", 685, 2528.5f, 1185.8f, 2},
    {"Whompah", "Outpost 10-3", 610, 1151.2f, 2346.4f, 2},
    {"Whompah", "2HO", 635, 791.6f, 1613.1f, 2},
    {"Whompah", "The Longest Road", 795, 2063.4f, 723.2f, 2},
    {"Whompah", "4Holes", 760, 1217.8f, 1230.9f, 2},
    {"Whompah", "20K", 630, 1245.9f, 2301.9f, 2},
    {"Whompah", "Broken Shores", 665, 2330.2f, 2259.2f, 2},
    {"Whompah", "Rome", 730, 353.8f, 323.0f, 2},
    {"Whompah", "Omni Ent", 705, 885.9f, 470.0f, 2},
    {"Whompah", "Mutant Domain", 696, 334.2f, 1335.4f, 2},
    // Whompahs (Clan)
    {"Whompah", "Tir", 640, 630.0f, 338.6f, 0},
    {"Whompah", "Varmint Woods", 600, 2488.0f, 2104.3f, 0},
    {"Whompah", "Wine", 605, 2150.8f, 2321.2f, 0},
    {"Whompah", "Wailing Wastes", 551, 1361.1f, 1738.8f, 0},
    {"Whompah", "Old Athen", 540, 462.9f, 309.2f, 0},
    {"Whompah", "Bliss", 795, 3712.1f, 1604.8f, 0},
    {"Whompah", "Broken Shores", 665, 1001.0f, 3760.6f, 0},
    {"Whompah", "Avalon", 505, 2165.7f, 3820.7f, 0},
	// Grid Exits (Neutral)
	{"Grid", "Newland City", 567, 1161.8f, 481.6f, 4},
	{"Grid", "Borealis", 800, 634.5f, 723.8f, 4},
	{"Grid", "Meetmedere", 565, 1529.5f, 2724.4f, 4},
	{"Grid", "Broken Shores", 665, 647.5f, 1315.4f, 4},
	{"Grid", "Harry's", 695, 3125.1f, 3176.1f, 4},
	{"Grid", "Sentinels", 560, 1939.9f, 1253.9f, 4},
	// Grid Exits (Omni)
	{"Grid", "Clondyke", 670, 1054.2f, 4033.5f, 4},
	{"Grid", "Galway", 685, 1419.8f, 1086.6f, 5},
	{"Grid", "Lush Hills", 695, 1453.5f, 665.7f, 5},
	{"Grid", "Omni Ent", 705, 582.0f, 330.0f, 5},
	{"Grid", "Rome", 730, 258.1f, 317.9f, 5},
	{"Grid", "2HO", 635, 668.1f, 1648.5f, 5},
	{"Grid", "4Holes", 760, 870.8f, 1606.5f, 5},
	{"Grid", "Omni HQ", 700, 603.6f, 474.0f, 5},
	// Grid Exits (Clan)
	{"Grid", "Tir", 640, 544.9f, 536.3f, 3},
	{"Grid", "Old Athen", 540, 515.4f, 565.6f, 3},
	{"Grid", "West Athen", 545, 473.3f, 408.9f, 3},
	{"Grid", "Camelot", 505, 2064.4f, 3760.4f, 3},
	
};

static int g_NumExits = sizeof(g_Exits) / sizeof(g_Exits[0]);
int g_ExitProximityRadius = 200;

static int IsMissionNearCheckedExit(int zoneId, float mx, float my, int radius)
{
    if (!PUL_GET_CB(CS_ALERTEXIT_CB)) return 0;
    for (int i = 0; i < g_NumExits; i++) {
        PULID chk = puGetObjectFromCollection(g_pCol, CS_EXIT_FIRST + i);
        if (chk && puGetAttribute(chk, PUA_CHECKBOX_CHECKED) &&
            g_Exits[i].zoneId == zoneId) {
            float dx = g_Exits[i].x - mx;
            float dy = g_Exits[i].y - my;
            if (sqrtf(dx*dx + dy*dy) <= (float)radius) return 1;
        }
    }
    return 0;
}

int CheckMissionNearExit(int zoneId, float x, float y)
{
    return IsMissionNearCheckedExit(zoneId, x, y, g_ExitProximityRadius);
}

void InitDialogColors(void) {
    if (!g_hDialogBgBrush)
        g_hDialogBgBrush = CreateSolidBrush(RGB(170, 170, 170));
    if (!g_hButtonBgBrush)
        g_hButtonBgBrush = CreateSolidBrush(RGB(112, 143, 166));
}

void FreeDialogColors(void) {
    if (g_hDialogBgBrush) DeleteObject(g_hDialogBgBrush);
    if (g_hButtonBgBrush) DeleteObject(g_hButtonBgBrush);
    g_hDialogBgBrush = NULL;
    g_hButtonBgBrush = NULL;
}


static LRESULT CALLBACK DialogColorSubclass(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (uMsg) {
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
            if (g_hDialogBgBrush) {
                SetBkMode((HDC)wParam, TRANSPARENT);
                SetTextColor((HDC)wParam, RGB(0, 0, 0));
                return (LRESULT)g_hDialogBgBrush;
            }
            break;

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT lpDIS = (LPDRAWITEMSTRUCT)lParam;
            const int* buttonIds = (const int*)GetPropA(hDlg, PROP_BUTTON_IDS);
            if (buttonIds) {
                for (int i = 0; buttonIds[i] != 0; i++) {
                    if (lpDIS->hwndItem == GetDlgItem(hDlg, buttonIds[i])) {
                        FillRect(lpDIS->hDC, &lpDIS->rcItem, g_hButtonBgBrush);
                        char text[256];
                        GetWindowTextA(lpDIS->hwndItem, text, sizeof(text));
                        SetBkMode(lpDIS->hDC, TRANSPARENT);
                        SetTextColor(lpDIS->hDC, RGB(0, 0, 0));
                        DrawTextA(lpDIS->hDC, text, -1, &lpDIS->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                        if (lpDIS->itemState & ODS_SELECTED)
                            DrawEdge(lpDIS->hDC, &lpDIS->rcItem, EDGE_SUNKEN, BF_RECT);
                        else
                            DrawEdge(lpDIS->hDC, &lpDIS->rcItem, EDGE_RAISED, BF_RECT);
                        return TRUE;
                    }
                }
            }
            break;
        }
    }
    return DefSubclassProc(hDlg, uMsg, wParam, lParam);
}

void EnableDialogColors(HWND hDlg, const int* buttonIds) {
    if (buttonIds) {
        int count = 0;
        while (buttonIds[count] != 0) count++;
        int* copy = (int*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (count + 1) * sizeof(int));
        if (copy) {
            memcpy(copy, buttonIds, count * sizeof(int));
            copy[count] = 0;
            SetPropA(hDlg, PROP_BUTTON_IDS, (HANDLE)copy);
        }
    }
    SetWindowSubclass(hDlg, DialogColorSubclass, 0, 0);
}

void DisableDialogColors(HWND hDlg) {
    RemoveWindowSubclass(hDlg, DialogColorSubclass, 0);
    HANDLE hMem = RemovePropA(hDlg, PROP_BUTTON_IDS);
    if (hMem) HeapFree(GetProcessHeap(), 0, hMem);
}


static void trim_whitespace(char *str) {
    char *start = str;
    while (isspace((unsigned char)*start)) start++;
    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';
    if (start != str) memmove(str, start, end - start + 1);
}

void safe_strcpy(char *dest, size_t dest_size, const char *src)
{
    if (dest_size == 0) return;
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

void safe_strcat(char *dest, size_t dest_size, const char *src)
{
    size_t used = strlen(dest);
    size_t remaining = dest_size - used;
    if (remaining <= 1) return;
    strncat(dest, src, remaining - 1);
    dest[dest_size - 1] = '\0';
}

static int ShowModalMessage(HWND hParent, const char* text, const char* caption, UINT type)
{
    if (!hParent && g_MainWin)
        hParent = (HWND)puGetAttribute(g_MainWin, PUA_WINDOW_HANDLE);
    
    BOOL wasTopmost = FALSE;
    if (hParent) {
        LONG exStyle = GetWindowLong(hParent, GWL_EXSTYLE);
        if (exStyle & WS_EX_TOPMOST) {
            wasTopmost = TRUE;
            SetWindowPos(hParent, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        }
    }
    
    int result = MessageBox(hParent, text, caption, type | MB_SYSTEMMODAL);
    
    if (wasTopmost && hParent) {
        SetWindowPos(hParent, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
    return result;
}

void BuildItemString(char *dest, size_t destSize,
                     const char *itemName,
                     int disabled,
                     int forceAccept,
                     int quantityLimit,
                     const char *excludeWords);

void ParseItemString(const char *src,
                     char *itemName, int itemNameSize,
                     int *disabled,
                     int *forceAccept,
                     int *quantityLimit,
                     char *excludeWords, int excludeSize);

void FormatItemForDisplay(const char *raw, char *out, size_t outSize);
void MakeTableEntry(char *dest, size_t destSize, const char *raw);

#define TIMER_BUYINGAGENT 1
#define TIMER_RESPONSE_WATCHDOG 2
static UINT_PTR g_TimerID = 0;
static int g_PendingAttemptNumber = 0;

typedef struct {
    char itemName[256];
    int  limit;
    int  disabled;
    int  force;
    char exclude[256];
    int  isAdd;
} ItemEditData;

typedef struct {
    const char **matches;
    int count;
    char selected[256];
    char originalSearch[256];
    char excludeWords[256];
} MatchListData;

static void UrlEncode(const char *src, char *dst, size_t dstSize)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i = 0;
    while (*src && i + 3 < dstSize)
    {
        if (isalnum((unsigned char)*src) || *src == '-' || *src == '_' || *src == '.' || *src == '~')
            dst[i++] = *src;
        else if (*src == ' ')
            dst[i++] = '+';
        else
        {
            dst[i++] = '%';
            dst[i++] = hex[(*src >> 4) & 0x0F];
            dst[i++] = hex[*src & 0x0F];
        }
        src++;
    }
    dst[i] = '\0';
}

INT_PTR CALLBACK MatchListDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    MatchListData *pMatchData = (MatchListData*)GetWindowLongPtr(hDlg, DWLP_USER);

    switch (msg) {
        case WM_INITDIALOG: {
            pMatchData = (MatchListData*)lParam;
            SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)pMatchData);
            HWND hList = GetDlgItem(hDlg, IDC_MATCH_LIST);
            for (int i = 0; i < pMatchData->count; i++) {
                SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)pMatchData->matches[i]);
            }
            char hint[512];
            if (pMatchData->excludeWords[0] != '\0') {
                sprintf(hint, "Hint: Double-click an item to select it.\n\nUsing original term \"%s\" with exclusions (%s) matches %d item(s).",
                        pMatchData->originalSearch, pMatchData->excludeWords, pMatchData->count);
            } else {
                sprintf(hint, "Hint: Double-click an item to select it.\n\nUsing original term \"%s\" matches %d item(s).",
                        pMatchData->originalSearch, pMatchData->count);
            }
            SetDlgItemTextA(hDlg, IDC_MATCH_HINT, hint);
            SetFocus(GetDlgItem(hDlg, IDC_MATCH_EDIT));
            static const int buttons[] = { IDC_USE_ORIGINAL, IDC_USE_TYPED, IDC_LOOKUP_AUNO, IDCANCEL, 0 };
            EnableDialogColors(hDlg, buttons);
            return FALSE;
        }

        case WM_DESTROY:
            DisableDialogColors(hDlg);
            break;

        case WM_COMMAND: {
            WORD wID = LOWORD(wParam);
            if (wID == IDC_USE_ORIGINAL) {
                pMatchData->selected[0] = '\0';
                EndDialog(hDlg, IDC_USE_ORIGINAL);
                return TRUE;
            } else if (wID == IDC_USE_TYPED) {
                char typed[256];
                GetDlgItemTextA(hDlg, IDC_MATCH_EDIT, typed, sizeof(typed));
                if (strlen(typed) == 0) {
                    MessageBoxA(hDlg, "Please enter a name or click Cancel.", "Empty Name", MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                int newMatchCount = 0;
                const char **newMatches = NULL;
                GetFilteredMatchingItems(typed, pMatchData->excludeWords, &newMatches, &newMatchCount);
                free((void*)newMatches);
                char msg[512];
                sprintf(msg, "Your typed name \"%s\" would match %d item(s).\n\nUse this name?", typed, newMatchCount);
                if (MessageBoxA(hDlg, msg, "Confirm Typed Name", MB_YESNO | MB_ICONQUESTION) == IDYES) {
					safe_strcpy(pMatchData->selected, sizeof(pMatchData->selected), typed);
                    EndDialog(hDlg, IDOK);
                }
                return TRUE;
            } else if (wID == IDCANCEL) {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            } else if (wID == IDC_MATCH_LIST && HIWORD(wParam) == LBN_DBLCLK) {
                HWND hList = GetDlgItem(hDlg, IDC_MATCH_LIST);
                int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR) {
                    SendMessageA(hList, LB_GETTEXT, sel, (LPARAM)pMatchData->selected);
                    EndDialog(hDlg, IDOK);
                }
                return TRUE;
            } else if (wID == IDC_LOOKUP_AUNO) {
                char searchTerm[256] = {0};
                HWND hList = GetDlgItem(hDlg, IDC_MATCH_LIST);
                int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR) {
                    SendMessageA(hList, LB_GETTEXT, sel, (LPARAM)searchTerm);
                } else {
                    GetDlgItemTextA(hDlg, IDC_MATCH_EDIT, searchTerm, sizeof(searchTerm));
                }
                if (searchTerm[0] == '\0') {
                    MessageBoxA(hDlg, "No item selected or typed.", "Lookup", MB_OK | MB_ICONINFORMATION);
                    return TRUE;
                }
                if (g_MainWin) {
                    HWND hMain = (HWND)puGetAttribute(g_MainWin, PUA_WINDOW_HANDLE);
                    if (hMain && IsWindow(hMain)) {
                        ShowWindow(hMain, SW_MINIMIZE);
                    }
                }
                char encoded[512];
                UrlEncode(searchTerm, encoded, sizeof(encoded));
                char url[1024];
                sprintf(url, "https://auno.org/ao/db.php?cmd=search&name=%s", encoded);
                HINSTANCE result = ShellExecuteA(hDlg, "open", url, NULL, NULL, SW_SHOWNORMAL);
                if ((int)result <= 32) {
                    char errMsg[256];
                    sprintf(errMsg, "Failed to open browser for:\n%s", url);
                    MessageBoxA(hDlg, errMsg, "Lookup Error", MB_OK | MB_ICONERROR);
                }
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

INT_PTR CALLBACK ItemEditDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    ItemEditData *pData = (ItemEditData*)GetWindowLongPtr(hDlg, DWLP_USER);

    switch (msg) {
        case WM_INITDIALOG: {
            pData = (ItemEditData*)lParam;
            SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)pData);

            if (pData->isAdd)
                SetWindowTextA(hDlg, "Add Item");
            else
                SetWindowTextA(hDlg, "Edit Item");

            SetDlgItemTextA(hDlg, IDC_ITEM_NAME, pData->itemName);
            SetDlgItemInt(hDlg, IDC_LIMIT, pData->limit, FALSE);
            CheckDlgButton(hDlg, IDC_FORCE, pData->force ? BST_CHECKED : BST_UNCHECKED);
            SetDlgItemTextA(hDlg, IDC_EXCLUDE, pData->exclude);

            static const int buttons[] = { IDOK, IDCANCEL, 0 };
            EnableDialogColors(hDlg, buttons);
            return TRUE;
        }

        case WM_DESTROY:
            DisableDialogColors(hDlg);
            break;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK: {
                    char enteredName[256];
                    GetDlgItemTextA(hDlg, IDC_ITEM_NAME, enteredName, sizeof(enteredName));
                    char *start = enteredName;
                    while (*start == ' ') start++;
                    char *end = start + strlen(start) - 1;
                    while (end > start && *end == ' ') end--;
                    *(end + 1) = '\0';
                    if (strlen(start) == 0) {
                        MessageBoxA(hDlg, "Item name cannot be empty.", "Validation", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }

                    char excludeTemp[256];
                    GetDlgItemTextA(hDlg, IDC_EXCLUDE, excludeTemp, sizeof(excludeTemp));

                    if (!pData->isAdd && strcmp(start, pData->itemName) == 0) {
                        safe_strcpy(pData->itemName, sizeof(pData->itemName), start);
                        pData->limit = GetDlgItemInt(hDlg, IDC_LIMIT, NULL, FALSE);
                        pData->force = (IsDlgButtonChecked(hDlg, IDC_FORCE) == BST_CHECKED);
                        GetDlgItemTextA(hDlg, IDC_EXCLUDE, pData->exclude, sizeof(pData->exclude));
                        EndDialog(hDlg, IDOK);
                        return TRUE;
                    }

                    char searchName[256];
                    safe_strcpy(searchName, sizeof(searchName), start);
                    int hasQuotes = 0;
                    size_t len = strlen(searchName);
                    if (len >= 2 && searchName[0] == '"' && searchName[len-1] == '"') {
                        hasQuotes = 1;
                        memmove(searchName, searchName + 1, len - 2);
                        searchName[len - 2] = '\0';
                        char *qstart = searchName;
                        while (*qstart == ' ') qstart++;
                        char *qend = qstart + strlen(qstart) - 1;
                        while (qend > qstart && *qend == ' ') qend--;
                        *(qend + 1) = '\0';
                        if (qstart != searchName) memmove(searchName, qstart, qend - qstart + 2);
                    }

                    char normalized[256];
                    safe_strcpy(normalized, sizeof(normalized), searchName);
                    for (char *p = normalized; *p; p++) {
                        if (*p == '-') {
                            if ((p == normalized || *(p-1) == ' ') && (*(p+1) == ' ' || *(p+1) == '\0'))
                                *p = ' ';
                        }
                    }
                    char *dst = normalized;
                    int space = 0;
                    for (char *src = normalized; *src; src++) {
                        if (*src == ' ') {
                            if (!space) *dst++ = ' ';
                            space = 1;
                        } else {
                            *dst++ = *src;
                            space = 0;
                        }
                    }
                    *dst = '\0';

                    int matchCount = 0;
                    const char **matches = NULL;
                    GetFilteredMatchingItems(normalized, excludeTemp, &matches, &matchCount);

                    if (matchCount == 0) {
                        char msg[512];
                        sprintf(msg, "Item \"%s\" does not match any known item.\n\nAdd it anyway?", start);
                        if (MessageBoxA(hDlg, msg, "Unknown Item", MB_YESNO | MB_ICONQUESTION) != IDYES) {
                            free((void*)matches);
                            return TRUE;
                        }
                    } else if (matchCount > MATCH_AUTO_THRESHOLD) {
                        int savedLimit = GetDlgItemInt(hDlg, IDC_LIMIT, NULL, FALSE);
                        BOOL savedForce = IsDlgButtonChecked(hDlg, IDC_FORCE);
                        char savedExclude[256];
                        GetDlgItemTextA(hDlg, IDC_EXCLUDE, savedExclude, sizeof(savedExclude));

                        MatchListData data;
                        data.matches = matches;
                        data.count = matchCount;
                        data.selected[0] = '\0';
                        strcpy(data.originalSearch, start);
                        strcpy(data.excludeWords, excludeTemp);

                        INT_PTR result = DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_MATCH_LIST),
                                                        hDlg, MatchListDlgProc, (LPARAM)&data);
                        if (result == IDOK && data.selected[0] != '\0') {
                            safe_strcpy(start, 256, data.selected);
                            hasQuotes = 0;
                            SetDlgItemTextA(hDlg, IDC_ITEM_NAME, start);
                            SetDlgItemInt(hDlg, IDC_LIMIT, savedLimit, FALSE);
                            CheckDlgButton(hDlg, IDC_FORCE, savedForce ? BST_CHECKED : BST_UNCHECKED);
                            SetDlgItemTextA(hDlg, IDC_EXCLUDE, savedExclude);
                        } else if (result != IDC_USE_ORIGINAL) {
                            free((void*)matches);
                            return TRUE;
                        }
                    }
                    free((void*)matches);

                    if (hasQuotes && strlen(start) > 0 && start[0] != '"') {
                        char quoted[256];
                        snprintf(quoted, sizeof(quoted), "\"%s\"", start);
                        safe_strcpy(pData->itemName, sizeof(pData->itemName), quoted);
                    } else {
                        safe_strcpy(pData->itemName, sizeof(pData->itemName), start);
                    }
                    pData->limit = GetDlgItemInt(hDlg, IDC_LIMIT, NULL, FALSE);
                    pData->force = (IsDlgButtonChecked(hDlg, IDC_FORCE) == BST_CHECKED);
                    GetDlgItemTextA(hDlg, IDC_EXCLUDE, pData->exclude, sizeof(pData->exclude));
                    EndDialog(hDlg, IDOK);
                    return TRUE;
                }

                case IDCANCEL:
                    EndDialog(hDlg, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

INT_PTR CALLBACK MassAddDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG:
            SetFocus(GetDlgItem(hDlg, IDC_MASS_EDIT));
            static const int buttons[] = { IDOK, IDCANCEL, 0 };
            EnableDialogColors(hDlg, buttons);
            return FALSE;

        case WM_DESTROY:
            DisableDialogColors(hDlg);
            break;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK: {
                    char text[65536];
                    GetDlgItemTextA(hDlg, IDC_MASS_EDIT, text, sizeof(text));
                    char *p = text;
                    char line[1024] = {0};
                    int lineIdx;
                    while (*p) {
                        lineIdx = 0;
                        while (*p && *p != '\r' && *p != '\n') {
                            if (lineIdx < (int)sizeof(line)-1)
                                line[lineIdx++] = *p;
                            p++;
                        }
                        line[lineIdx] = '\0';
                        if (lineIdx == 0) {
                            while (*p == '\r' || *p == '\n') p++;
                            continue;
                        }
                        char *start = line;
                        while (*start == ' ' || *start == '\t') start++;
                        char *end = start + strlen(start) - 1;
                        while (end > start && (*end == ' ' || *end == '\t')) end--;
                        *(end + 1) = '\0';
                        if (start != line) memmove(line, start, end - start + 2);
                        if (strlen(line) == 0) {
                            while (*p == '\r' || *p == '\n') p++;
                            continue;
                        }
                        char *ptr = line;
                        int disabled = 0, force = 0;
                        if (*ptr == '#') { disabled = 1; ptr++; while (*ptr == ' ' || *ptr == '\t') ptr++; }
                        if (*ptr == '~') { force = 1; ptr++; while (*ptr == ' ' || *ptr == '\t') ptr++; }
                        char itemName[256] = {0};
                        int limit = 1;
                        char excludeWords[256] = {0};
                        char *nameStart = ptr;
                        char *nameEnd = nameStart;
                        while (*nameEnd && *nameEnd != ';' && *nameEnd != '^') nameEnd++;
                        int nameLen = (int)(nameEnd - nameStart);
                        if (nameLen >= (int)sizeof(itemName)) nameLen = sizeof(itemName)-1;
                        strncpy(itemName, nameStart, nameLen);
                        itemName[nameLen] = '\0';
                        char *trimEnd = itemName + strlen(itemName) - 1;
                        while (trimEnd >= itemName && (*trimEnd == ' ' || *trimEnd == '\t'))
                            *trimEnd-- = '\0';
                        ptr = nameEnd;
                        if (*ptr == ';') {
                            ptr++;
                            limit = atoi(ptr);
                            if (limit < 0) limit = 0;
                            if (limit == 0) limit = 1;
                            while (*ptr && *ptr != ' ' && *ptr != '^') ptr++;
                        }
                        while (*ptr) {
                            while (*ptr == ' ') ptr++;
                            if (*ptr == '^') {
                                ptr++;
                                while (*ptr == ' ') ptr++;
                                char word[128] = {0};
                                int wlen = 0;
                                while (*ptr && *ptr != ' ' && *ptr != '^') {
                                    if (wlen < (int)sizeof(word)-1)
                                        word[wlen++] = *ptr;
                                    ptr++;
                                }
                                word[wlen] = '\0';
                                if (wlen > 0) {
                                    if (excludeWords[0] != '\0')
                                        strcat(excludeWords, " ");
                                    strcat(excludeWords, word);
                                }
                            } else break;
                        }
                        if (strlen(itemName) == 0) continue;
                        char raw[512];
                        BuildItemString(raw, sizeof(raw), itemName, disabled, force, limit, excludeWords);
                        char display[1024];
                        FormatItemForDisplay(raw, display, sizeof(display));
                        puDoMethod(g_ItemWatchList, PUM_TABLE_NEWRECORD, 0, 0);
                        puDoMethod(g_ItemWatchList, PUM_TABLE_ADDRECORD, 0, 0);
                        puDoMethod(g_ItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)display, 0);
                        while (*p == '\r' || *p == '\n') p++;
                    }
                    EndDialog(hDlg, IDOK);
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(hDlg, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

int ShowItemEditDialog(HWND hParent, ItemEditData *pData, int bIsAddMode)
{
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hParent, GWLP_HINSTANCE);
    if (!hInst) hInst = GetModuleHandle(NULL);
    
    INT_PTR result = DialogBoxParamA(hInst, MAKEINTRESOURCEA(IDD_ITEM_EDIT),
                                     hParent, ItemEditDlgProc, (LPARAM)pData);
    return (result == IDOK) ? 1 : 0;
}

LRESULT CALLBACK MainWndProcHook( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData );

static LRESULT CALLBACK BAWndProcHook(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_MOVE || uMsg == WM_MOVING)
    {
        RECT r;
        GetWindowRect(hWnd, &r);
        g_BAWindowX = r.left;
        g_BAWindowY = r.top;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void BuildItemString(char *dest, size_t destSize, const char *itemName,
                     int disabled, int forceAccept, int quantityLimit,
                     const char *excludeWords)
{
    dest[0] = '\0';
    int len = 0;

    if (disabled) {
        len = snprintf(dest, destSize, "#");
        if (len < 0 || (size_t)len >= destSize) goto trunc;
    }
    if (forceAccept) {
        len += snprintf(dest + len, destSize - len, "~");
        if (len < 0 || (size_t)len >= destSize) goto trunc;
    }
    len += snprintf(dest + len, destSize - len, "%s", itemName);
    if (len < 0 || (size_t)len >= destSize) goto trunc;

    if (quantityLimit > 0) {
        len += snprintf(dest + len, destSize - len, ";%d", quantityLimit);
        if (len < 0 || (size_t)len >= destSize) goto trunc;
    }

    if (excludeWords && *excludeWords) {
        char *tmp = _strdup(excludeWords);
        if (tmp) {
            char *token = strtok(tmp, ", ");
            while (token) {
                len += snprintf(dest + len, destSize - len, " ^%s", token);
                if (len < 0 || (size_t)len >= destSize) {
                    free(tmp);
                    goto trunc;
                }
                token = strtok(NULL, ", ");
            }
            free(tmp);
        }
    }
    return;
trunc:
    dest[destSize - 1] = '\0';
}

void ParseItemString(const char *src,
                     char *itemName, int itemNameSize,
                     int *disabled,
                     int *forceAccept,
                     int *quantityLimit,
                     char *excludeWords, int excludeSize)
{
    *disabled = 0;
    *forceAccept = 0;
    *quantityLimit = 0;
    if (excludeWords) excludeWords[0] = '\0';
    itemName[0] = '\0';
    if (!src) return;

    const char *p = src;

    while (*p == '#' || *p == '~') {
        if (*p == '#') *disabled = 1;
        if (*p == '~') *forceAccept = 1;
        p++;
    }

    const char *nameStart = p;
    const char *nameEnd = nameStart;
    while (*nameEnd && *nameEnd != ';' && *nameEnd != '^')
        nameEnd++;

    while (nameEnd > nameStart && *(nameEnd - 1) == ' ')
        nameEnd--;

    int nameLen = (int)(nameEnd - nameStart);
    if (nameLen >= itemNameSize) nameLen = itemNameSize - 1;
    strncpy(itemName, nameStart, nameLen);
    itemName[nameLen] = '\0';

    p = nameEnd;

    if (*p == ';') {
        p++;
        *quantityLimit = atoi(p);
        while (*p && *p != ' ' && *p != '^') p++;
    }

    while (*p == ' ') p++;

    if (excludeWords && excludeSize > 0) {
        excludeWords[0] = '\0';
        while (*p == '^') {
            p++;
            while (*p == ' ') p++;

            const char *start = p;
            while (*p && *p != '^' && *p != ';' && *p != ' ')
                p++;

            int len = (int)(p - start);
            if (len > 0) {
                if (excludeWords[0] != '\0') {
                    strncat(excludeWords, " ", excludeSize - strlen(excludeWords) - 1);
                }
                strncat(excludeWords, start, len);
            }
            while (*p == ' ') p++;
        }
    }
}

void FormatItemForDisplay(const char *raw, char *out, size_t outSize)
{
    char itemName[256];
    int disabled = 0, force = 0, limit = 0;
    char exclude[256];
    ParseItemString(raw, itemName, sizeof(itemName), &disabled, &force, &limit, exclude, sizeof(exclude));

    out[0] = '\0';
    int len = 0;

    len = snprintf(out, outSize, "%s", itemName);
    if (len < 0 || (size_t)len >= outSize) goto truncation;

    if (disabled) {
        len += snprintf(out + len, outSize - len, " [disabled]");
        if (len < 0 || (size_t)len >= outSize) goto truncation;
    }
    if (force) {
        len += snprintf(out + len, outSize - len, " [force accept]");
        if (len < 0 || (size_t)len >= outSize) goto truncation;
    }
    if (limit > 0) {
        len += snprintf(out + len, outSize - len, " [qty %d]", limit);
        if (len < 0 || (size_t)len >= outSize) goto truncation;
    }
    if (exclude[0]) {
        char buf[512];
        char tmp[256];
        safe_strcpy(tmp, sizeof(tmp), exclude);
        char *tok = strtok(tmp, " ");
        char formatted[256] = "";
        while (tok) {
            if (formatted[0]) safe_strcat(formatted, sizeof(formatted), ", ");
            safe_strcat(formatted, sizeof(formatted), tok);
            tok = strtok(NULL, " ");
        }
        snprintf(buf, sizeof(buf), " [exclude: %s]", formatted);
        len += snprintf(out + len, outSize - len, "%s", buf);
        if (len < 0 || (size_t)len >= outSize) goto truncation;
    }
    return;

truncation:
    out[outSize - 1] = '\0';
}

void MakeTableEntry(char *dest, size_t destSize, const char *raw)
{
    FormatItemForDisplay(raw, dest, destSize);
}

static int ParseDisplayString(const char *display, char *itemName, size_t itemNameSize,
                              int *disabled, int *forceAccept, int *quantityLimit,
                              char *excludeWords, size_t excludeSize)
{
    *disabled = 0;
    *forceAccept = 0;
    *quantityLimit = 0;
    if (excludeWords) excludeWords[0] = '\0';
    if (itemName) itemName[0] = '\0';

    if (!display || !*display) return -1;

    char buf[1024];
    strncpy(buf, display, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';

    char *p = buf;
    char *nameEnd = strchr(p, '[');
    if (!nameEnd) nameEnd = p + strlen(p);
    size_t nameLen = nameEnd - p;
    while (nameLen > 0 && p[nameLen-1] == ' ') nameLen--;
    if (nameLen >= itemNameSize) nameLen = itemNameSize-1;
    strncpy(itemName, p, nameLen);
    itemName[nameLen] = '\0';

    p = nameEnd;
    while (*p) {
        while (*p == ' ' || *p == '[') p++;
        if (!*p) break;

        if (strncmp(p, "disabled]", 9) == 0) {
            *disabled = 1;
            p += 9;
        }
        else if (strncmp(p, "force accept]", 13) == 0) {
            *forceAccept = 1;
            p += 13;
        }
        else if (strncmp(p, "qty ", 4) == 0) {
            p += 4;
            *quantityLimit = atoi(p);
            while (*p && *p != ']') p++;
            if (*p == ']') p++;
        }
        else if (strncmp(p, "exclude: ", 9) == 0) {
            p += 9;
            char *end = strchr(p, ']');
            if (!end) end = p + strlen(p);
            size_t len = end - p;
            if (len > 0 && excludeWords && excludeSize > 0) {
                strncpy(excludeWords, p, (len < excludeSize-1) ? len : excludeSize-1);
                excludeWords[len] = '\0';
                for (char *c = excludeWords; *c; c++)
                    if (*c == ',') *c = ' ';
                char *trim = excludeWords + strlen(excludeWords) - 1;
                while (trim >= excludeWords && *trim == ' ') *trim-- = '\0';
            }
            p = end;
            if (*p == ']') p++;
        }
        else {
            while (*p && *p != ']') p++;
            if (*p == ']') p++;
        }
    }
    return 0;
}

static void MoveCurrentActiveToDisabled(void)
{
    PULID listView = puGetObjectFromCollection(g_pCol, CS_ITEMWATCH_LISTVIEW);
    int selectedIndex = (int)puGetAttribute(listView, PUA_LISTVIEW_SELECTED);
    if (selectedIndex < 0) {
        ShowModalMessage(NULL, "No item selected.", "ClickSaver", MB_OK | MB_ICONINFORMATION);
        return;
    }

    PUU32 record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    for (int i = 0; i < selectedIndex && record; i++)
        record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETNEXTRECORD, record, 0);
    if (!record) return;

    PUU8 *display = (PUU8*)puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIELDVAL, record, 0);
    if (!display) return;

    puDoMethod(g_DisabledItemWatchList, PUM_TABLE_NEWRECORD, 0, 0);
    puDoMethod(g_DisabledItemWatchList, PUM_TABLE_ADDRECORD, 0, 0);
    puDoMethod(g_DisabledItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)display, 0);

    puDoMethod(listView, PUM_LISTVIEW_REMOVE, 0, 0);

    int numRows = puGetAttribute(g_ItemWatchList, PUA_TABLE_NUMRECORDS);
    if (numRows > 0) {
        int newIndex = (selectedIndex < numRows) ? selectedIndex : numRows - 1;
        puSetAttribute(listView, PUA_LISTVIEW_SELECTED, newIndex);
        PULID table = puGetAttribute(listView, PUA_LISTVIEW_TABLE);
        if (table) {
            puSetAttribute(listView, PUA_LISTVIEW_TABLE, 0);
            puSetAttribute(listView, PUA_LISTVIEW_TABLE, table);
        }
        puDoMethod(listView, PUM_CONTROL_RELAYOUT, 0, 0);
    } else {
        puSetAttribute(listView, PUA_LISTVIEW_SELECTED, -1);
    }
}

static void MoveCurrentDisabledToActive(void)
{
    PULID listView = puGetObjectFromCollection(g_pCol, CS_DISABLED_ITEMWATCH_LISTVIEW);
    int selectedIndex = (int)puGetAttribute(listView, PUA_LISTVIEW_SELECTED);
    if (selectedIndex < 0) {
        ShowModalMessage(NULL, "No item selected in disabled list.", "ClickSaver", MB_OK | MB_ICONINFORMATION);
        return;
    }

    PUU32 record = puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    for (int i = 0; i < selectedIndex && record; i++)
        record = puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETNEXTRECORD, record, 0);
    if (!record) return;

    PUU8 *display = (PUU8*)puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETFIELDVAL, record, 0);
    if (!display) return;

    puDoMethod(g_ItemWatchList, PUM_TABLE_NEWRECORD, 0, 0);
    puDoMethod(g_ItemWatchList, PUM_TABLE_ADDRECORD, 0, 0);
    puDoMethod(g_ItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)display, 0);

    puDoMethod(listView, PUM_LISTVIEW_REMOVE, 0, 0);

    int numRows = puGetAttribute(g_DisabledItemWatchList, PUA_TABLE_NUMRECORDS);
    if (numRows > 0) {
        int newIndex = (selectedIndex < numRows) ? selectedIndex : numRows - 1;
        puSetAttribute(listView, PUA_LISTVIEW_SELECTED, newIndex);
        PULID table = puGetAttribute(listView, PUA_LISTVIEW_TABLE);
        if (table) {
            puSetAttribute(listView, PUA_LISTVIEW_TABLE, 0);
            puSetAttribute(listView, PUA_LISTVIEW_TABLE, table);
        }
        puDoMethod(listView, PUM_CONTROL_RELAYOUT, 0, 0);
    } else {
        puSetAttribute(listView, PUA_LISTVIEW_SELECTED, -1);
    }
}

static void EditActiveItem(void)
{
    PULID listView = puGetObjectFromCollection(g_pCol, CS_ITEMWATCH_LISTVIEW);
    int selectedIndex = (int)puGetAttribute(listView, PUA_LISTVIEW_SELECTED);
    if (selectedIndex < 0) {
        ShowModalMessage(NULL, "No item selected.", "ClickSaver", MB_OK | MB_ICONINFORMATION);
        return;
    }

    PUU32 recordKey = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    for (int i = 0; i < selectedIndex && recordKey; i++)
        recordKey = puDoMethod(g_ItemWatchList, PUM_TABLE_GETNEXTRECORD, recordKey, 0);
    if (!recordKey) return;

    PUU8* oldStr = (PUU8*)puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIELDVAL, recordKey, 0);
    if (!oldStr || !*oldStr) return;

    ItemEditData data;
    memset(&data, 0, sizeof(data));
    ParseDisplayString((char*)oldStr, data.itemName, sizeof(data.itemName),
                       &data.disabled, &data.force, &data.limit, data.exclude, sizeof(data.exclude));
    data.isAdd = 0;

    HWND hMainWnd = (HWND)puGetAttribute(g_MainWin, PUA_WINDOW_HANDLE);
    if (ShowItemEditDialog(hMainWnd, &data, 0)) {
        char rawStr[512], newDisplay[1024];
        BuildItemString(rawStr, sizeof(rawStr), data.itemName, data.disabled, data.force, data.limit, data.exclude);
        FormatItemForDisplay(rawStr, newDisplay, sizeof(newDisplay));
        puSetAttribute(g_ItemWatchList, PUA_TABLE_CURRENTFIELD, 0);
        puSetAttribute(g_ItemWatchList, PUA_TABLE_CURRENTRECORD, recordKey);
        puDoMethod(g_ItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)newDisplay, 0);

        PULID table = puGetAttribute(listView, PUA_LISTVIEW_TABLE);
        if (table) {
            puSetAttribute(listView, PUA_LISTVIEW_TABLE, 0);
            puSetAttribute(listView, PUA_LISTVIEW_TABLE, table);
        }
    }
}

static void EditDisabledItem(void)
{
    PULID listView = puGetObjectFromCollection(g_pCol, CS_DISABLED_ITEMWATCH_LISTVIEW);
    int selectedIndex = (int)puGetAttribute(listView, PUA_LISTVIEW_SELECTED);
    if (selectedIndex < 0) {
        ShowModalMessage(NULL, "No item selected in disabled list.", "ClickSaver", MB_OK | MB_ICONINFORMATION);
        return;
    }

    PUU32 recordKey = puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    for (int i = 0; i < selectedIndex && recordKey; i++)
        recordKey = puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETNEXTRECORD, recordKey, 0);
    if (!recordKey) return;

    PUU8* oldStr = (PUU8*)puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETFIELDVAL, recordKey, 0);
    if (!oldStr || !*oldStr) return;

    ItemEditData data;
    memset(&data, 0, sizeof(data));
    ParseDisplayString((char*)oldStr, data.itemName, sizeof(data.itemName),
                       &data.disabled, &data.force, &data.limit, data.exclude, sizeof(data.exclude));
    data.isAdd = 0;

    HWND hMainWnd = (HWND)puGetAttribute(g_MainWin, PUA_WINDOW_HANDLE);
    if (ShowItemEditDialog(hMainWnd, &data, 0)) {
        char rawStr[512], newDisplay[1024];
        BuildItemString(rawStr, sizeof(rawStr), data.itemName, data.disabled, data.force, data.limit, data.exclude);
        FormatItemForDisplay(rawStr, newDisplay, sizeof(newDisplay));
        puSetAttribute(g_DisabledItemWatchList, PUA_TABLE_CURRENTFIELD, 0);
        puSetAttribute(g_DisabledItemWatchList, PUA_TABLE_CURRENTRECORD, recordKey);
        puDoMethod(g_DisabledItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)newDisplay, 0);
        PULID table = puGetAttribute(listView, PUA_LISTVIEW_TABLE);
        if (table) {
            puSetAttribute(listView, PUA_LISTVIEW_TABLE, 0);
            puSetAttribute(listView, PUA_LISTVIEW_TABLE, table);
        }
    }
}

static void RemoveDuplicateItems(void) {
    PUU32 record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    PUU32 prevRecord = 0;
    int removed = 0;

    char **unique = NULL;
    int uniqueCount = 0;
    record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    while (record) {
        PUU8 *display = (PUU8*)puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIELDVAL, record, 0);
        if (display) {
            int already = 0;
            for (int i = 0; i < uniqueCount; i++) {
                if (strcmp(unique[i], (char*)display) == 0) {
                    already = 1;
                    break;
                }
            }
            if (!already) {
                unique = realloc(unique, (uniqueCount + 1) * sizeof(char*));
                unique[uniqueCount] = _strdup((char*)display);
                uniqueCount++;
            } else {
                removed++;
            }
        }
        record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETNEXTRECORD, record, 0);
    }

    if (removed == 0) {
        free(unique);
        ShowModalMessage(NULL, "No duplicate items found.", "Remove Duplicates", MB_OK);
        return;
    }

    record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    while (record) {
        puDoMethod(g_ItemWatchList, PUM_TABLE_REMRECORD, record, 0);
        record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    }
    for (int i = 0; i < uniqueCount; i++) {
        puDoMethod(g_ItemWatchList, PUM_TABLE_NEWRECORD, 0, 0);
        puDoMethod(g_ItemWatchList, PUM_TABLE_ADDRECORD, 0, 0);
        puDoMethod(g_ItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)unique[i], 0);
        free(unique[i]);
    }
    free(unique);

    PUU32 listView = puGetObjectFromCollection(g_pCol, CS_ITEMWATCH_LISTVIEW);
    puSetAttribute(listView, PUA_LISTVIEW_SELECTED, -1);

    char msg[64];
    sprintf(msg, "Removed %d duplicate entries.", removed);
    ShowModalMessage(NULL, msg, "Remove Duplicates", MB_OK);
}

static int ItemExistsInActiveList(const char *displayString) {
    PUU32 record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    while (record) {
        PUU8 *existing = (PUU8*)puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIELDVAL, record, 0);
        if (existing && strcmp((char*)existing, displayString) == 0) {
            return 1;
        }
        record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETNEXTRECORD, record, 0);
    }
    return 0;
}

static void ImportItemsFromFile(const char *filename, int replaceMode) {
    if (replaceMode) {
        if (ShowModalMessage(NULL, "Replace will delete all current items. Continue?", 
                       "Confirm Replace", MB_YESNO) != IDYES)
            return;
        PUU32 record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
        while (record) {
            puDoMethod(g_ItemWatchList, PUM_TABLE_REMRECORD, record, 0);
            record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
        }
        PUU32 listView = puGetObjectFromCollection(g_pCol, CS_ITEMWATCH_LISTVIEW);
        puSetAttribute(listView, PUA_LISTVIEW_SELECTED, -1);
    }

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        DisplayErrorMessage("Cannot open file.", TRUE);
        return;
    }

    char line[1024];
    int inItemSection = 0;
    int addedCount = 0;
    int duplicateCount = 0;

	while (fgets(line, sizeof(line), fp)) {
		line[strcspn(line, "\r\n")] = '\0';
		trim_whitespace(line);
	
		if (strcmp(line, "::ItemWatch::") == 0) {
			inItemSection = 1;
			continue;
		}
		if (strcmp(line, "::END::") == 0 || strncmp(line, "::", 2) == 0)
			break;
		if (!inItemSection) continue;
	
		if (strlen(line) == 0) continue;
		
		char itemName[256];
		int disabled = 0, force = 0, limit = 0;
		char exclude[256];
		ParseItemString(line, itemName, sizeof(itemName),
						&disabled, &force, &limit, exclude, sizeof(exclude));
		
		if (limit == 0) {
			limit = 1;
		}
		
		char rawWithLimit[512];
		BuildItemString(rawWithLimit, sizeof(rawWithLimit),
						itemName, disabled, force, limit, exclude);
		
		char display[1024];
		MakeTableEntry(display, sizeof(display), rawWithLimit);
		
		if (!replaceMode && ItemExistsInActiveList(display)) {
			duplicateCount++;
			continue;
		}
		
		puDoMethod(g_ItemWatchList, PUM_TABLE_NEWRECORD, 0, 0);
		puDoMethod(g_ItemWatchList, PUM_TABLE_ADDRECORD, 0, 0);
		puDoMethod(g_ItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)display, 0);
		addedCount++;
    }
    fclose(fp);

    char msg[256];
    sprintf(msg, "Imported %d items. Duplicates skipped: %d", addedCount, duplicateCount);
    ShowModalMessage(NULL, msg, "Import Complete", MB_OK);
}

static void ExportItemsOnly(const char *filename)
{
    char fullpath[MAX_PATH];
    safe_strcpy(fullpath, sizeof(fullpath), filename);
    
    size_t len = strlen(fullpath);
    if (len < 3 || _stricmp(fullpath + len - 3, ".cs") != 0)
        safe_strcat(fullpath, sizeof(fullpath), ".cs");
    
    FILE *fp = fopen(fullpath, "w");
    if (!fp) {
        char err[256];
        sprintf(err, "Cannot create file:\n%s", fullpath);
        DisplayErrorMessage(err, TRUE);
        return;
    }
    
    fprintf(fp, "::ItemWatch::\n");
    PUU32 record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    while (record) {
        PUU8 *display = (PUU8*)puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIELDVAL, record, 0);
        if (display && *display) {
            char itemName[256] = {0}, exclude[256] = {0};
            int disabled = 0, force = 0, limit = 0;
            ParseDisplayString((char*)display, itemName, sizeof(itemName),
                               &disabled, &force, &limit, exclude, sizeof(exclude));
            char raw[512];
            BuildItemString(raw, sizeof(raw), itemName, disabled, force, limit, exclude);
            fprintf(fp, "%s\n", raw);
        }
        record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETNEXTRECORD, record, 0);
    }
    fprintf(fp, "::END::\n");
    fclose(fp);
}

typedef struct ItemCounter {
    char *itemName;
    int limit;
    int accepted;
    struct ItemCounter *next;
} ItemCounter;

static ItemCounter *g_ItemCounters = NULL;

ItemCounter* FindItemCounter(const char *name) {
    ItemCounter *cur = g_ItemCounters;
    while (cur) {
        if (strcmp(cur->itemName, name) == 0) return cur;
        cur = cur->next;
    }
    return NULL;
}

void AddItemCounter(const char *name, int limit) {
    if (limit <= 0) return;
    ItemCounter *new = (ItemCounter*)malloc(sizeof(ItemCounter));
    if (!new) {
        DisplayErrorMessage("Out of memory in AddItemCounter", TRUE);
        return;
    }
    new->itemName = _strdup(name);
    if (!new->itemName) {
        free(new);
        DisplayErrorMessage("Out of memory in AddItemCounter (strdup)", TRUE);
        return;
    }
    new->limit = limit;
    new->accepted = 0;
    new->next = g_ItemCounters;
    g_ItemCounters = new;
}

static void ClearItemCounters() {
    if (g_bBuyingAgentActive) {
        return;
    }
    if (g_BuyingAgentCount > 0 || g_BuyingAgentMissions > 0) {
        return;
    }

    ItemCounter *cur = g_ItemCounters;
    while (cur) {
        ItemCounter *next = cur->next;
        free(cur->itemName);
        free(cur);
        cur = next;
    }
    g_ItemCounters = NULL;
}

typedef enum ImportSettingsMode
{
    ISM_CONFIG,
    ISM_LOCWATCH,
    ISM_ITEMWATCH,
	ISM_DISABLED_ITEMWATCH,
    ISM_SLIDERS,
    ISM_DONE,
} ImportSettingsMode;

/* int OpenLocalDB()
{
	char DBPath[MAX_PATH];
	sprintf(DBPath, "%s\\cd_image\\rdb.db", g_AODir);

if (sqlite3_open_v2(DBPath, &g_pSQLite, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
    return FALSE;
}

	sqlite3_prepare_v2(g_pSQLite, "SELECT data FROM rdb_1000020 WHERE id = ?;", -1, &g_stmtItem, NULL);
	sqlite3_prepare_v2(g_pSQLite, "SELECT data FROM rdb_1010008 WHERE id = ?;", -1, &g_stmtIcon, NULL);
	sqlite3_prepare_v2(g_pSQLite, "SELECT data FROM rdb_1000001 WHERE id = ?;", -1, &g_stmtPF, NULL);

	return TRUE;
} */

int OpenLocalDB()
{
    char DBPath[MAX_PATH];
    sprintf(DBPath, "%s\\cd_image\\rdb.db", g_AODir);

    if (sqlite3_open_v2(DBPath, &g_pSQLite, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
    {
        return FALSE;
    }
	
    if (sqlite3_exec(g_pSQLite, "PRAGMA quick_check;", NULL, NULL, NULL) != SQLITE_OK)
    {
        sqlite3_close(g_pSQLite);
        g_pSQLite = NULL;
        return FALSE;
    }

    if (sqlite3_prepare_v2(g_pSQLite, "SELECT data FROM rdb_1000020 WHERE id = ?;", -1, &g_stmtItem, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(g_pSQLite, "SELECT data FROM rdb_1010008 WHERE id = ?;", -1, &g_stmtIcon, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(g_pSQLite, "SELECT data FROM rdb_1000001 WHERE id = ?;", -1, &g_stmtPF, NULL) != SQLITE_OK)
    {
        sqlite3_finalize(g_stmtItem);
        sqlite3_finalize(g_stmtIcon);
        sqlite3_finalize(g_stmtPF);
        sqlite3_close(g_pSQLite);
        g_pSQLite = NULL;
        g_stmtItem = NULL;
        g_stmtIcon = NULL;
        g_stmtPF = NULL;
        return FALSE;
    }

    return TRUE;
}

void* GetDataChunk(PUU32 _KeyHi, PUU32 _KeyLo, PUU32* _pSize)
{
	sqlite3_stmt* pStmt = NULL;
	void* pReturnData = NULL;

	switch (_KeyHi) {
	case AODB_TYP_ITEM: pStmt = g_stmtItem; break;
	case AODB_TYP_ICON: pStmt = g_stmtIcon; break;
	case AODB_TYP_PF:   pStmt = g_stmtPF;   break;
	default: return NULL;
	}

	if (!pStmt) return NULL;

	sqlite3_reset(pStmt);
	
	sqlite3_bind_int(pStmt, 1, (int)_KeyLo);

	if (sqlite3_step(pStmt) == SQLITE_ROW) {
		const unsigned char* blob = (const unsigned char*)sqlite3_column_blob(pStmt, 0);
		int blobSize = sqlite3_column_bytes(pStmt, 0);

		if (!blob || blobSize <= 0) return NULL;

		if (_KeyHi == AODB_TYP_ITEM) {
			MissionItem* pItem = (MissionItem*)malloc(sizeof(MissionItem));
			if (!pItem) return NULL;
			memset(pItem, 0, sizeof(MissionItem));

			for (int i = 4; i + 8 <= blobSize; i += 8) {
				PUU32 tag = *(PUU32*)(blob + i);
				PUU32 val = *(PUU32*)(blob + i + 4);
				switch (tag) {
				case 0x36: pItem->QL = val; break;
				case 0x4F: pItem->IconKey = val; break;
				case 0x4A: pItem->Value = val; break;
				}
			}

			for (int i = 0; i + 12 <= blobSize; i++) {
				if (*(PUU32*)(blob + i) == 0x15 && *(PUU32*)(blob + i + 4) == 0x21) {
					unsigned short nameLen = *(unsigned short*)(blob + i + 8);
					if (i + 12 + nameLen > blobSize) {
						continue;
					}
					if (nameLen > AODB_MAX_NAME_LEN) nameLen = AODB_MAX_NAME_LEN;
					memcpy(pItem->pName, blob + i + 12, nameLen);
					pItem->pName[nameLen] = 0;
					break;
				}
			}

			pReturnData = pItem;
			if (_pSize) *_pSize = sizeof(MissionItem);
		}

		else if (_KeyHi == AODB_TYP_PF) {
			if (blobSize > 8) {
				const char* strData = (const char*)(blob + 8);
				int finalSize = (int)(strlen(strData) + 1);
				pReturnData = malloc(finalSize);
				if (pReturnData) {
					memcpy(pReturnData, strData, finalSize);
					if (_pSize) *_pSize = (PUU32)finalSize;
				}
			}
		}

		else {
			pReturnData = malloc(blobSize);
			if (pReturnData) {
				memcpy(pReturnData, blob, blobSize);
				if (_pSize) *_pSize = (PUU32)blobSize;
			}
		}
	}

	return pReturnData;
}

static int HasActiveWatchlistItems()
{
    PUU32 Record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    while (Record)
    {
        PUU8* pString = (PUU8*)puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIELDVAL, Record, 0);
        if (pString && *pString)
        {
            char itemName[256];
            int disabled = 0, force = 0, limit = 0;
            char exclude[256];
            ParseDisplayString((char*)pString, itemName, sizeof(itemName),
                               &disabled, &force, &limit, exclude, sizeof(exclude));
            if (limit == 0)
                return TRUE;
            else
            {
                ItemCounter *ic = FindItemCounter(itemName);
                if (!ic || ic->accepted < ic->limit)
                    return TRUE;
            }
        }
        Record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETNEXTRECORD, Record, 0);
    }
    return FALSE;
}

LRESULT CALLBACK MainWndProcHook( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
    if (uMsg == WM_TIMER)
    {
        if (wParam == TIMER_BUYINGAGENT)
        {
            KillTimer( hWnd, TIMER_BUYINGAGENT );
            g_TimerID = 0;
            puPostAppMessage( CSAM_BUYINGAGENT_TIMER, 0);
            return 0;
        }
        else if (wParam == TIMER_RESPONSE_WATCHDOG)
        {
            KillTimer( hWnd, TIMER_RESPONSE_WATCHDOG );
            puPostAppMessage( CSAM_NO_MISSION_RESPONSE, 0 );
            return 0;
        }
    }
    return DefSubclassProc( hWnd, uMsg, wParam, lParam );
}

void ReleaseAODatabase(void)
{
	if (g_stmtItem) sqlite3_finalize(g_stmtItem);
	if (g_stmtIcon) sqlite3_finalize(g_stmtIcon);
	if (g_stmtPF)   sqlite3_finalize(g_stmtPF);
	if (g_pSQLite)  sqlite3_close(g_pSQLite);
}

static void ExportLocations(const char *filename)
{
    char fullpath[MAX_PATH];
    safe_strcpy(fullpath, sizeof(fullpath), filename);
    size_t len = strlen(fullpath);
    if (len < 3 || _stricmp(fullpath + len - 3, ".cs") != 0)
        safe_strcat(fullpath, sizeof(fullpath), ".cs");

    FILE *fp = fopen(fullpath, "w");
    if (!fp) {
        char err[256];
        sprintf(err, "Cannot create file:\n%s", fullpath);
        DisplayErrorMessage(err, TRUE);
        return;
    }

    fprintf(fp, "::LocWatch::\n");
    PUU32 record = puDoMethod(g_LocWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    while (record) {
        PUU8 *line = (PUU8*)puDoMethod(g_LocWatchList, PUM_TABLE_GETFIELDVAL, record, 0);
        if (line && *line)
            fprintf(fp, "%s\n", line);
        record = puDoMethod(g_LocWatchList, PUM_TABLE_GETNEXTRECORD, record, 0);
    }
    fprintf(fp, "::END::\n");
    fclose(fp);
}

static void ImportLocations(const char *filename, int replaceMode)
{
    if (replaceMode) {
        if (ShowModalMessage(NULL,
                "Replace will delete all current locations.\nContinue?",
                "Confirm Replace", MB_YESNO) != IDYES)
            return;
        PUU32 record = puDoMethod(g_LocWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
        while (record) {
            puDoMethod(g_LocWatchList, PUM_TABLE_REMRECORD, record, 0);
            record = puDoMethod(g_LocWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
        }
        PULID listView = puGetObjectFromCollection(g_pCol, CS_LOCWATCH_LISTVIEW);
        puSetAttribute(listView, PUA_LISTVIEW_SELECTED, -1);
    }

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        DisplayErrorMessage("Cannot open file.", TRUE);
        return;
    }

    char line[1024];
    int inLocSection = 0;
    int addedCount = 0;
    int duplicateCount = 0;

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        trim_whitespace(line);
        if (strcmp(line, "::LocWatch::") == 0) {
            inLocSection = 1;
            continue;
        }
        if (strcmp(line, "::END::") == 0 || strncmp(line, "::", 2) == 0)
            break;
        if (!inLocSection || strlen(line) == 0)
            continue;

        int duplicate = 0;
        if (!replaceMode) {
            PUU32 rec = puDoMethod(g_LocWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
            while (rec) {
                PUU8 *existing = (PUU8*)puDoMethod(g_LocWatchList, PUM_TABLE_GETFIELDVAL, rec, 0);
                if (existing && strcmp((char*)existing, line) == 0) {
                    duplicate = 1;
                    break;
                }
                rec = puDoMethod(g_LocWatchList, PUM_TABLE_GETNEXTRECORD, rec, 0);
            }
        }
        if (duplicate) {
            duplicateCount++;
            continue;
        }

        puDoMethod(g_LocWatchList, PUM_TABLE_NEWRECORD, 0, 0);
        puDoMethod(g_LocWatchList, PUM_TABLE_ADDRECORD, 0, 0);
        puDoMethod(g_LocWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)line, 0);
        addedCount++;
    }
    fclose(fp);

    PULID listView = puGetObjectFromCollection(g_pCol, CS_LOCWATCH_LISTVIEW);
    PULID table = puGetAttribute(listView, PUA_LISTVIEW_TABLE);
    if (table) {
        puSetAttribute(listView, PUA_LISTVIEW_TABLE, 0);
        puSetAttribute(listView, PUA_LISTVIEW_TABLE, table);
    }
    puSetAttribute(listView, PUA_LISTVIEW_SELECTED, -1);

    char msg[256];
    sprintf(msg, "Imported %d locations. Duplicates skipped: %d", addedCount, duplicateCount);
    ShowModalMessage(NULL, msg, "Import Complete", MB_OK);
}

int main( int argc, char** argv )
{
    pusAppMessage* pAppMsg;
    void* pMissionData;
    PULID MissionControls[5] = {0};
    FILE* fp;
    char AOExePath[ 256 ];
    DWORD dwThreadID;
    HANDLE hOrigDB;

    char DBPath[ 256 * 2 ];

    SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL );

    if( !puInit() )
    {
        return -1;
    }

    if( !RegisterMissionClass() )
    {
        CleanUp();
        return -1;
    }

    if( !( g_pCol = puCreateObjectCollection( g_GUIDef ) ) )
    {
        CleanUp();
        return -1;
    }

    g_MainWin = puGetObjectFromCollection( g_pCol, CS_MAIN_WINDOW );
	g_ItemWatchList = puGetObjectFromCollection( g_pCol, CS_ITEMWATCH_LIST );
	g_DisabledItemWatchList = puGetObjectFromCollection( g_pCol, CS_DISABLED_ITEMWATCH_LIST );
	g_LocWatchList = puGetObjectFromCollection( g_pCol, CS_LOCWATCH_LIST );

    GetCurrentDirectory( MAX_PATH, g_CSDir );

    ImportSettings( "LastSettings.cs" );
	
	// Force "Item name optional" to be disabled on every startup
    PULID itemOptionalCb = puGetObjectFromCollection( g_pCol, CS_ITEMOPTIONAL_CB );
    if (itemOptionalCb) {
        puSetAttribute( itemOptionalCb, PUA_CHECKBOX_CHECKED, FALSE );
    }
	
	ResetAcceptedMissionLog();
	
	PULID delayCtrl = puGetObjectFromCollection(g_pCol, CS_BUYINGAGENTDELAY_ENTRY);
	if (delayCtrl) puSetAttribute(delayCtrl, PUA_TEXTENTRY_VALUE, g_BuyingAgentDelay);
	
	if( puGetAttribute( puGetObjectFromCollection( g_pCol, CS_STARTMIN_CB ), PUA_CHECKBOX_CHECKED ) )
        puSetAttribute( g_MainWin, PUA_WINDOW_ICONIFIED, TRUE );

    sprintf( AOExePath, "%s\\anarchy.exe", g_AODir );
    if( !( fp = fopen( AOExePath, "r" ) ) )
    {
        GetFolder( NULL, "Please locate the PRK folder, where Anarchy.exe resides.", g_AODir );

        if( !g_AODir[ 0 ] )
        {
            CleanUp();
            return -1;
        }

        sprintf( AOExePath, "%s\\anarchy.exe", g_AODir );
        if( !( fp = fopen( AOExePath, "r" ) ) )
        {
            DisplayErrorMessage( "This is not PRK's directory.", FALSE );
            CleanUp();
            return -1;
        }
    }

    fclose( fp );

	sprintf(DBPath, "%s\\cd_image\\rdb.db", g_AODir);
	hOrigDB = CreateFile(DBPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (hOrigDB == INVALID_HANDLE_VALUE) {
		char err[100];
		sprintf(err, "Cannot open rdb.db at:\n%s\nError code: %d", DBPath, GetLastError());
		DisplayErrorMessage(err, FALSE);
		CleanUp();
		return -1;
	}
	CloseHandle(hOrigDB);
	
	if (!OpenLocalDB()) {
		DisplayErrorMessage("Couldn't open the AO database (rdb.db).", FALSE);
		CleanUp();
		return -1;
	}

    if( ( g_Mutex = CreateMutex( NULL, FALSE, "ClickSaver" ) ) == INVALID_HANDLE_VALUE )
    {
        DisplayErrorMessage( "Couldn't create mutex.", FALSE );
        ReleaseAODatabase();
        CleanUp();
        return -1;
    }
	
    if( GetLastError() == ERROR_ALREADY_EXISTS )
    {
        HWND hWnd;
        if( hWnd = FindWindow( "ClickSaverHookWindowClass", "ClickSaverHookWindow" ) )
        {
            return -1;
        }
    }
	
	g_hThreadExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (g_hThreadExitEvent == NULL) {
		DisplayErrorMessage("Failed to create exit event.", FALSE);
		CleanUp();
		return -1;
	}
	
	g_hAbortEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!g_hAbortEvent) {
		DisplayErrorMessage("Failed to create abort event.", FALSE);
		CleanUp();
		return -1;
	}
	
    if( ( g_Thread = CreateThread( NULL, 0, &HookManagerThread, NULL, 0, &dwThreadID ) ) == INVALID_HANDLE_VALUE )
    {
        DisplayErrorMessage( "Couldn't create hook thread.", FALSE );
        ReleaseAODatabase();
        CleanUp();
        return -1;
    }
	
char cachePath[MAX_PATH];
sprintf(cachePath, "%s\\ItemNames.db", g_CSDir);

int needRebuild = 0;
HANDLE hCache = CreateFileA(cachePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
if (hCache == INVALID_HANDLE_VALUE) {
    needRebuild = 1;
} else {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    HANDLE hExe = CreateFileA(exePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hExe != INVALID_HANDLE_VALUE) {
        FILETIME ftCache, ftExe;
        GetFileTime(hCache, NULL, NULL, &ftCache);
        GetFileTime(hExe, NULL, NULL, &ftExe);
        if (CompareFileTime(&ftExe, &ftCache) > 0)
            needRebuild = 1;
        CloseHandle(hExe);
    }
    CloseHandle(hCache);
}

if (needRebuild) {
    DeleteFileA(cachePath);
    BuildItemNameCache(cachePath);
}
if (!LoadItemNameCache(cachePath)) {
    BuildItemNameCache(cachePath);
    LoadItemNameCache(cachePath);
}

    MissionControls[ 0 ] = puGetObjectFromCollection( g_pCol, CS_MISSION1 );
    MissionControls[ 1 ] = puGetObjectFromCollection( g_pCol, CS_MISSION2 );
    MissionControls[ 2 ] = puGetObjectFromCollection( g_pCol, CS_MISSION3 );
    MissionControls[ 3 ] = puGetObjectFromCollection( g_pCol, CS_MISSION4 );
    MissionControls[ 4 ] = puGetObjectFromCollection( g_pCol, CS_MISSION5 );
	
    puSetAttribute( g_MainWin, PUA_WINDOW_OPENED, TRUE );
	
	PULID radiusEntry = puGetObjectFromCollection(g_pCol, CS_EXITS_RADIUS_SLIDER);
	if (radiusEntry) puSetAttribute(radiusEntry, PUA_TEXTENTRY_VALUE, g_ExitProximityRadius);

    HWND hMainWnd = (HWND)puGetAttribute( g_MainWin, PUA_WINDOW_HANDLE );
    SetWindowSubclass( hMainWnd, MainWndProcHook, 0, 0 );

    HICON hIcon = LoadIcon( GetModuleHandle( NULL ), MAKEINTRESOURCE( IDI_ICON1 ) );
    if( hIcon )
    {
        PUU32 uWindowHandle = puGetAttribute( g_MainWin, PUA_WINDOW_HANDLE );
        SendMessage( (HWND)uWindowHandle, WM_SETICON, ICON_BIG,   (LPARAM)hIcon );
        SendMessage( (HWND)uWindowHandle, WM_SETICON, ICON_SMALL, (LPARAM)hIcon );
    }
	
	InitDialogColors();

    do
    {
        pAppMsg = puWaitAppMessages();

        switch( pAppMsg->Message )
        {
		case CSAM_NO_MISSION_RESPONSE:
			if (g_bBuyingAgentActive && g_BuyingAgentCount == 0 && g_BuyingAgentMissions > 0)
			{
				puSetAttribute(puGetObjectFromCollection(g_pCol, CS_BA_PROGRESS), PUA_TEXT_STRING, (PUU32)"");
				PlaySound("notfound.wav", NULL, SND_FILENAME | SND_NODEFAULT);
				puSetAttribute(puGetObjectFromCollection(g_pCol, CS_BA_STATUS), PUA_TEXT_STRING,
							(PUU32)"Stopped: Maximum tries reached.");
				
				g_bBuyingAgentActive = 0;
				g_bPaused = 0;
				g_BuyingAgentCount = 0;
				g_BuyingAgentMissions = 0;
				ClearItemCounters();
				
				PULID pauseButton = puGetObjectFromCollection(g_pCol, CS_BUYINGAGENT_PAUSEBTN);
				if (pauseButton) {
					puSetAttribute(pauseButton, PUA_TEXT_STRING, (PUU32)"Pause");
				}
			}
			break;
		case CSAM_EDIT_ITEM:
			{
				PUU32 listView = puGetObjectFromCollection(g_pCol, CS_ITEMWATCH_LISTVIEW);
				int selectedIndex = (int)puGetAttribute(listView, PUA_LISTVIEW_SELECTED);
			
				if (selectedIndex < 0) {
					ShowModalMessage(NULL, "No item selected.\n\nPlease click on an item first, then click Edit.", 
							"ClickSaver", MB_OK | MB_ICONINFORMATION);
					break;
				}
			
				PUU32 recordKey = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
				for (int i = 0; i < selectedIndex && recordKey; i++)
					recordKey = puDoMethod(g_ItemWatchList, PUM_TABLE_GETNEXTRECORD, recordKey, 0);
			
				if (!recordKey) {
					ShowModalMessage(NULL, "Could not locate selected item in table.\n\nAre you sure you clicked on an item?", "ClickSaver", MB_OK | MB_ICONWARNING);
					break;
				}
			
				PUU8* oldStr = (PUU8*)puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIELDVAL, recordKey, 0);
				if (!oldStr || !*oldStr) break;

				ItemEditData data;
				memset(&data, 0, sizeof(data));
				ParseDisplayString((char*)oldStr, data.itemName, sizeof(data.itemName),
                       &data.disabled, &data.force, &data.limit, data.exclude, sizeof(data.exclude));
					   
				data.isAdd = 0;
			
				HWND hMainWnd = (HWND)puGetAttribute(g_MainWin, PUA_WINDOW_HANDLE);
				if (ShowItemEditDialog(hMainWnd, &data, 0)) {
					char rawStr[512];
					BuildItemString(rawStr, sizeof(rawStr), data.itemName, data.disabled, data.force, data.limit, data.exclude);
					char newDisplay[1024];
					FormatItemForDisplay(rawStr, newDisplay, sizeof(newDisplay));
					puSetAttribute(g_ItemWatchList, PUA_TABLE_CURRENTFIELD, 0);
					puSetAttribute(g_ItemWatchList, PUA_TABLE_CURRENTRECORD, recordKey);
					puDoMethod(g_ItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)newDisplay, 0);
				}
				break;
			}
		case CSAM_DISABLE_ITEM:
            MoveCurrentActiveToDisabled();
            break;

        case CSAM_ENABLE_ITEM:
            MoveCurrentDisabledToActive();
            break;
			
		case CSAM_MASS_ADD_ITEMS:
			{
				HWND hParent = (HWND)puGetAttribute(g_MainWin, PUA_WINDOW_HANDLE);
				DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_MASS_ADD),
							hParent, MassAddDlgProc, 0);
				break;
			}
		case CSAM_REMOVE_ALL_ITEMS:
        {
            if (ShowModalMessage(NULL,
                "Are you sure you want to remove ALL items from the active list?",
                "Confirm Remove All", MB_YESNO | MB_ICONWARNING) == IDYES)
            {
                PUU32 record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
                while (record)
                {
                    puDoMethod(g_ItemWatchList, PUM_TABLE_REMRECORD, record, 0);
                    record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
                }
                PUU32 listView = puGetObjectFromCollection(g_pCol, CS_ITEMWATCH_LISTVIEW);
                puSetAttribute(listView, PUA_LISTVIEW_SELECTED, -1);
            }
            break;
        }

        case CSAM_REMOVE_ALL_DISABLED:
        {
            if (ShowModalMessage(NULL,
                "Are you sure you want to remove ALL items from the disabled list?",
                "Confirm Remove All", MB_YESNO | MB_ICONWARNING) == IDYES)
            {
                PUU32 record = puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
                while (record)
                {
                    puDoMethod(g_DisabledItemWatchList, PUM_TABLE_REMRECORD, record, 0);
                    record = puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
                }
                PUU32 listView = puGetObjectFromCollection(g_pCol, CS_DISABLED_ITEMWATCH_LISTVIEW);
                puSetAttribute(listView, PUA_LISTVIEW_SELECTED, -1);
            }
            break;
        }	
		case CSAM_ITEM_ADD_OK:
			{
				ItemEditData data;
				memset(&data, 0, sizeof(data));
				safe_strcpy(data.itemName, sizeof(data.itemName), "");
				data.limit = 1;
				data.force = 0;
				safe_strcpy(data.exclude, sizeof(data.exclude), "");
				data.isAdd = 1;
			
				HWND hMainWnd = (HWND)puGetAttribute(g_MainWin, PUA_WINDOW_HANDLE);
				if (ShowItemEditDialog(hMainWnd, &data, 1)) {
					char rawStr[512];
					BuildItemString(rawStr, sizeof(rawStr), data.itemName, data.disabled, data.force, data.limit, data.exclude);
					char display[1024];
					FormatItemForDisplay(rawStr, display, sizeof(display));
					puDoMethod(g_ItemWatchList, PUM_TABLE_NEWRECORD, 0, 0);
					puDoMethod(g_ItemWatchList, PUM_TABLE_ADDRECORD, 0, 0);
					puDoMethod(g_ItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)display, 0);
				}
				break;
			}
		
		case CSAM_IMPORT_ITEMS:
			{
				char filename[MAX_PATH];
				HWND hMainWnd = (HWND)puGetAttribute(g_MainWin, PUA_WINDOW_HANDLE);
				if (GetFile(hMainWnd, FALSE, filename, sizeof(filename)))
				{
					int choice = ShowModalMessage(hMainWnd,
						"Import Items:\n\nYes = Append to current list\nNo = Replace current list\nCancel = Cancel",
						"Import Items", MB_YESNOCANCEL | MB_ICONQUESTION);
					if (choice == IDYES)
						ImportItemsFromFile(filename, 0);
					else if (choice == IDNO)
						ImportItemsFromFile(filename, 1);
				}
				break;
			}
		case CSAM_REMOVE_DUPLICATE_ITEMS:
				RemoveDuplicateItems();
				break;
		case CSAM_EXPORT_ITEMS:
			{
				char filename[MAX_PATH];
				HWND hMainWnd = (HWND)puGetAttribute(g_MainWin, PUA_WINDOW_HANDLE);
				if (GetFile(hMainWnd, TRUE, filename, sizeof(filename)))
				{
					ExportItemsOnly(filename);
					char msg[256];
					sprintf(msg, "Exported %d items.", puGetAttribute(g_ItemWatchList, PUA_TABLE_NUMRECORDS));
					ShowModalMessage(hMainWnd, msg, "Export Complete", MB_OK);
				}
				break;
			}
		case CSAM_UPDATE_DELAY:
			{
				PULID delayCtrl = puGetObjectFromCollection( g_pCol, CS_BUYINGAGENTDELAY_ENTRY );
				if (delayCtrl) {
					int newDelay = puGetAttribute( delayCtrl, PUA_TEXTENTRY_VALUE );
					if (newDelay >= 5200 && newDelay <= 15000) {
						g_BuyingAgentDelay = newDelay;
					} else {
						if (newDelay < 5200) newDelay = 5200;
						if (newDelay > 15000) newDelay = 15000;
						g_BuyingAgentDelay = newDelay;
						puSetAttribute(delayCtrl, PUA_TEXTENTRY_VALUE, newDelay);
					}
				}
				break;
			}
			
        case CSAM_STOPBUYINGAGENT:
                if (g_TimerID) {
                    KillTimer( hMainWnd, TIMER_BUYINGAGENT );
                    g_TimerID = 0;
                }
				puSetAttribute(puGetObjectFromCollection(g_pCol, CS_BA_STATUS), PUA_TEXT_STRING,
               (PUU32)"Buying agent stopped by user.");
                g_BuyingAgentCount = 0;
                g_BuyingAgentMissions = 0;
                g_BuyingAgentMaxTries = 0;
                g_BuyingAgentMaxMissions = 0;
                g_TotalAttempts = 0;
                puSetAttribute(puGetObjectFromCollection(g_pCol, CS_BA_PROGRESS), PUA_TEXT_STRING, (PUU32)"");
                EndBuyingAgent(0);
            break;
			
        case CSAM_PAUSEBUYINGAGENT:
			if (!g_bBuyingAgentActive) {
				WriteLog("Pause ignored: buying agent not active.\n");
				break;
			}
		
            if (g_bBuyingAgentActive)
            {
                int wasPaused = g_bPaused;
                g_bPaused = !g_bPaused;
        
                PULID pauseButton = puGetObjectFromCollection(g_pCol, CS_BUYINGAGENT_PAUSEBTN);
                if (pauseButton) {
                    const char* newLabel = g_bPaused ? "Resume" : "Pause";
                    puSetAttribute(pauseButton, PUA_TEXT_STRING, (PUU32)newLabel);
                }
                PULID statusLabel = puGetObjectFromCollection(g_pCol, CS_BA_STATUS);
                if (statusLabel) {
                    const char* status = g_bPaused ? "PAUSED" : "Running...";
                    puSetAttribute(statusLabel, PUA_TEXT_STRING, (PUU32)status);
                }
        
                if (wasPaused && !g_bPaused && g_BuyingAgentCount > 0 && g_TimerID == 0) {
                    BuyingAgent(g_BuyingAgentDelay);
                }
            }
            break;
			
		case CSAM_IMPORT_LOCATIONS:
			{
				char filename[MAX_PATH];
				HWND hMainWnd = (HWND)puGetAttribute(g_MainWin, PUA_WINDOW_HANDLE);
				if (GetFile(hMainWnd, FALSE, filename, sizeof(filename))) {
					int choice = ShowModalMessage(hMainWnd,
						"Import Locations:\n\nYes = Append to current list\nNo = Replace current list\nCancel = Cancel",
						"Import Locations", MB_YESNOCANCEL | MB_ICONQUESTION);
					if (choice == IDYES)
						ImportLocations(filename, 0);
					else if (choice == IDNO)
						ImportLocations(filename, 1);
				}
				break;
			}
			
		case CSAM_EXPORT_LOCATIONS:
			{
				char filename[MAX_PATH];
				HWND hMainWnd = (HWND)puGetAttribute(g_MainWin, PUA_WINDOW_HANDLE);
				if (GetFile(hMainWnd, TRUE, filename, sizeof(filename))) {
					ExportLocations(filename);
					char msg[256];
					sprintf(msg, "Exported %d locations.",
							puGetAttribute(g_LocWatchList, PUA_TABLE_NUMRECORDS));
					ShowModalMessage(hMainWnd, msg, "Export Complete", MB_OK);
				}
				break;
			}
		case CSAM_UPDATE_EXIT_RADIUS:
			{
				PULID radiusEntry = puGetObjectFromCollection(g_pCol, CS_EXITS_RADIUS_SLIDER);
				if (radiusEntry) {
					int newRadius = puGetAttribute(radiusEntry, PUA_TEXTENTRY_VALUE);
					if (newRadius >= 0 && newRadius <= 5000)
						g_ExitProximityRadius = newRadius;
				}
				break;
			}

        case CSAM_BUYINGAGENT_TIMER:
            if (!g_bPaused && g_BuyingAgentCount > 0)
            {
                HWND AOWnd = FindWindow( "Anarchy client", NULL );
                if (AOWnd)
                {
                    SetForegroundWindow( AOWnd );
                    POINT MousePos = { 99, 180 };
                    LPARAM lParam = MousePos.y << 16 | MousePos.x;
                    if( g_bFirstRound )
                    {
                        ClientToScreen( AOWnd, &MousePos );
                        SetCursorPos( MousePos.x, MousePos.y );
                        g_bFirstRound = FALSE;
                    }
                    SendMessage( AOWnd, WM_LBUTTONDOWN, 0, lParam );
                    SendMessage( AOWnd, WM_LBUTTONUP, 0, lParam );
                }

                g_BuyingAgentCount--;
                g_TotalAttempts++;
				
				if (g_BuyingAgentCount == 0 && g_BuyingAgentMissions > 0)
				{
					HWND hWnd = (HWND)puGetAttribute(g_MainWin, PUA_WINDOW_HANDLE);
					SetTimer(hWnd, TIMER_RESPONSE_WATCHDOG, 3000, NULL);
				}

                char buffer[64];
                sprintf( buffer, "Current mission: Attempt %d of %d", g_PendingAttemptNumber, g_BuyingAgentMaxTries );
                puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BA_PROGRESS ), PUA_TEXT_STRING, (PUU32)buffer );

                char totalBuf[64];
                sprintf( totalBuf, "Total attempts: %d", g_TotalAttempts );
                puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BA_TOTAL ), PUA_TEXT_STRING, (PUU32)totalBuf );
            }
            break;

        case CSAM_NEWMISSIONS:
            if( !g_BuyingAgentCount && g_bFullscreen )
            {
                g_BuyingAgentCount = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENTTRIES ), PUA_TEXTENTRY_VALUE );
            }
            if( g_BuyingAgentCount ) {
                if (g_bPaused) break;
                if( PUL_GET_CB(CS_ALERTITEM_CB) && !HasActiveWatchlistItems() ) {
					puSetAttribute(puGetObjectFromCollection(g_pCol, CS_BA_PROGRESS), PUA_TEXT_STRING, (PUU32)"");
					PlaySound( "notfound.wav", NULL, SND_FILENAME | SND_NODEFAULT );
					puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BA_STATUS ), PUA_TEXT_STRING,
									(PUU32)"Stopped: all item limits reached" );
					g_BuyingAgentCount = 0;
					g_BuyingAgentMissions = 0;
					EndBuyingAgent(1);
					break;
				}
                
                HWND hMainWnd = NULL;
                if (!g_bFullscreen) {
                    hMainWnd = (HWND)puGetAttribute(g_MainWin, PUA_WINDOW_HANDLE);
                    if (hMainWnd) SendMessage(hMainWnd, WM_SETREDRAW, FALSE, 0);
                }
                pMissionData = g_CurrentPacket;
                
                g_bForceUIRefresh = 1;

                WaitForSingleObject( g_Mutex, INFINITE );
                g_FoundMish = 255;
                for( g_MishNumber = 0; g_MishNumber < 5; g_MishNumber++ )
                {
                    if( !( pMissionData = (void*)puDoMethod( MissionControls[ g_MishNumber ], CSM_MISSION_PARSEMISSION, (PUU32)pMissionData, 0 ) ) )
                    {
                        break;
                    }
                }
                ReleaseMutex( g_Mutex );
                g_bForceUIRefresh = 0;
                
                if (hMainWnd) {
                    SendMessage(hMainWnd, WM_SETREDRAW, TRUE, 0);
                    InvalidateRect(hMainWnd, NULL, TRUE);
                    UpdateWindow(hMainWnd);
                }
                
                puSetAttribute( g_MainWin, PUA_WINDOW_DEFERUPDATE, TRUE );
                puSetAttribute( g_MainWin, PUA_WINDOW_DEFERUPDATE, FALSE );
                
                puSetAttribute( puGetObjectFromCollection( g_pCol, CS_TABS ), PUA_REGISTER_CURRENTTAB, 0 );
                puSetAttribute( g_MainWin, PUA_WINDOW_ICONIFIED, FALSE );
                
                if( g_BuyingAgentCount && !g_bPaused )
					{
						BuyingAgent(g_BuyingAgentDelay);
					}
					else
					{
						if( g_FoundMish == 255 )
						{
							puSetAttribute(puGetObjectFromCollection(g_pCol, CS_BA_PROGRESS), PUA_TEXT_STRING, (PUU32)"");
							PlaySound( "notfound.wav", NULL, SND_FILENAME | SND_NODEFAULT );
							puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BA_STATUS ), PUA_TEXT_STRING,
											(PUU32)"No mission found within maximum tries" );
						}
						EndBuyingAgent(1);
					}
            }

            if( !g_BuyingAgentCount )
            {
                pMissionData = g_CurrentPacket;
                puSetAttribute( g_MainWin, PUA_WINDOW_DEFERUPDATE, TRUE );

                WaitForSingleObject( g_Mutex, INFINITE );
                g_FoundMish = 255;
                for( g_MishNumber = 0; g_MishNumber < 5; g_MishNumber++ )
                {
                    void *pLastMissionData;
                    pLastMissionData = pMissionData;
                    if( !( pMissionData = (void*)puDoMethod( MissionControls[ g_MishNumber ], CSM_MISSION_PARSEMISSION, (PUU32)pMissionData, 0 ) ) )
                    {
                        pMissionData = pLastMissionData;
                    }
                }

                ReleaseMutex( g_Mutex );

                if( pMissionData && !g_bFullscreen )
                {
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_ERROR_WINDOW ), PUA_WINDOW_OPENED, FALSE );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_TABS ), PUA_REGISTER_CURRENTTAB, 0 );
                    puSetAttribute( g_MainWin, PUA_WINDOW_ICONIFIED, FALSE );
                }

                puSetAttribute( g_MainWin, PUA_WINDOW_DEFERUPDATE, FALSE );

                if( PUL_GET_CB( CS_SOUNDS_CB ) )
                {
                    if( g_FoundMish == 255 )
                        PlaySound( "notfound.wav", NULL, SND_FILENAME | SND_NODEFAULT );
                    else
                        PlaySound( "found.wav", NULL, SND_FILENAME | SND_NODEFAULT );
                }
                if( PUL_GET_CB( CS_MOUSEMOVE_CB ) || g_BuyingAgentMissions )
                {
                    HWND AOWnd;                  
					POINT MousePos = {0, 0};
                    LPARAM lParam;

                    WriteLog( NULL );

                    if( !( AOWnd = FindWindow( "Anarchy client", NULL ) ) )
                    {
                        DisplayErrorMessage( "Anarchy Online is not running.", TRUE );
                        g_BuyingAgentCount = 0;
                    }

                    if( g_FoundMish != 255 && !( pAppMsg->Message == CSAM_STOPBUYINGAGENT ) )
                    {
                        MousePos.x = 44 + ( ( g_FoundMish % 3 ) * 58 );
                        MousePos.y = 57 + ( ( g_FoundMish / 3 ) * 57 );
                        lParam = MousePos.y << 16 | MousePos.x;

                        ClientToScreen( AOWnd, &MousePos );
                        SetCursorPos( MousePos.x, MousePos.y );

                        SendMessage( AOWnd, WM_LBUTTONDOWN, 0, lParam );
                        Sleep( 500 );
                        SendMessage( AOWnd, WM_LBUTTONUP, 0, lParam );

                        Sleep( 710 );

                        MousePos.x = 76; MousePos.y = 321;
                        lParam = MousePos.y << 16 | MousePos.x;
                        ClientToScreen( AOWnd, &MousePos );
                        SetCursorPos( MousePos.x, MousePos.y );
                        if( g_BuyingAgentMissions )
                        {
                            g_BuyingAgentMissions--;
                            
                            int accepted = g_BuyingAgentMaxMissions - g_BuyingAgentMissions;
                            char buf[64];
                            sprintf( buf, "Accepted %d of %d", accepted, g_BuyingAgentMaxMissions );
                            puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BA_ACCEPTED ), PUA_TEXT_STRING, (PUU32)buf );
							
							char buffer[64];
							sprintf( buffer, "Current mission: Attempt %d of %d", g_PendingAttemptNumber, g_BuyingAgentMaxTries );
							puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BA_PROGRESS ), PUA_TEXT_STRING, (PUU32)buffer );
			
							char totalBuf[64];
							sprintf( totalBuf, "Total attempts: %d", g_TotalAttempts );
							puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BA_TOTAL ), PUA_TEXT_STRING, (PUU32)totalBuf );

                            SendMessage( AOWnd, WM_LBUTTONDOWN, 0, lParam );
                            Sleep( 500 );
                            SendMessage( AOWnd, WM_LBUTTONUP, 0, lParam );
                            Sleep( 710 );

                            UpdateAcceptedCountersForMission( g_FoundMish );
                            
                            if( PUL_GET_CB(CS_ALERTITEM_CB) && !HasActiveWatchlistItems() ) {
								puSetAttribute(puGetObjectFromCollection(g_pCol, CS_BA_PROGRESS), PUA_TEXT_STRING, (PUU32)"");
								PlaySound( "notfound.wav", NULL, SND_FILENAME | SND_NODEFAULT );
								puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BA_STATUS ), PUA_TEXT_STRING,
												(PUU32)"Stopped: all item limits reached" );
								g_BuyingAgentMissions = 0;
								g_BuyingAgentCount = 0;
								EndBuyingAgent(1);
								goto stop_buying_agent;
							}

                            if( g_BuyingAgentMissions > 0 )
								{
									SendMessage( AOWnd, WM_KEYDOWN, 0x45, 0 );
									Sleep( 500 );
									SendMessage( AOWnd, WM_KEYUP, 0x45, 0 );
									Sleep( 710 );
									{
										int easy_hard = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_EASY_HARD ), PUA_TEXTENTRY_VALUE );
										int good_bad = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_GOOD_BAD ), PUA_TEXTENTRY_VALUE );
										int order_chaos = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_ORDER_CHAOS ), PUA_TEXTENTRY_VALUE );
										int open_hidden = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_OPEN_HIDDEN ), PUA_TEXTENTRY_VALUE );
										int phys_myst = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_PHYS_MYST ), PUA_TEXTENTRY_VALUE );
										int headon_stealth = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_HEADON_STEALTH ), PUA_TEXTENTRY_VALUE );
										int money_xp = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_MONEY_XP ), PUA_TEXTENTRY_VALUE );
										_setSliders( easy_hard, good_bad, order_chaos, open_hidden, phys_myst, headon_stealth, money_xp );
									}
									
									g_bBuyingAgentActive = 1;
									
									PULID pauseButton = puGetObjectFromCollection( g_pCol, CS_BUYINGAGENT_PAUSEBTN );
									if( pauseButton )
									{
										const char* label = g_bPaused ? "Resume" : "Pause";
										puSetAttribute( pauseButton, PUA_TEXT_STRING, (PUU32)label );
									}
									PULID statusLabel = puGetObjectFromCollection( g_pCol, CS_BA_STATUS );
									if( statusLabel )
									{
										const char* status = g_bPaused ? "PAUSED" : "Running...";
										puSetAttribute( statusLabel, PUA_TEXT_STRING, (PUU32)status );
									}
									
									g_bFirstRound = TRUE;
									g_BuyingAgentCount = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENTTRIES ), PUA_TEXTENTRY_VALUE );
									BuyingAgent(g_BuyingAgentDelay);
								}
								else
								{
									puSetAttribute(puGetObjectFromCollection(g_pCol, CS_BA_PROGRESS), PUA_TEXT_STRING, (PUU32)"");
									PlaySound( "found.wav", NULL, SND_FILENAME | SND_NODEFAULT );
									char statusMsg[128];
									sprintf(statusMsg, "Completed: accepted %d missions", g_BuyingAgentMaxMissions);
									puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BA_STATUS ), PUA_TEXT_STRING,
													(PUU32)statusMsg );
									
									EndBuyingAgent(1);
									g_BuyingAgentCount = 0;
								}
                        }
                    }
                }
                WriteLog( NULL );
            }
            break;

        case CSAM_PRESTARTBUYINGAGENT:
            if( puGetAttribute( puGetObjectFromCollection( g_pCol, CS_BAINFO_CB ), PUA_CHECKBOX_CHECKED ) )
            {
                puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENT_INFOWINDOW ), PUA_WINDOW_OPENED, TRUE );
                break;
            }

        case CSAM_STARTBUYINGAGENT:
            puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENT_INFOWINDOW ), PUA_WINDOW_OPENED, FALSE );

            if( !g_BuyingAgentCount )
            {
                PUU32 bItemListOk = FALSE, bLocListOk = FALSE, bTypeListOk = FALSE;
                PUU32 bWarnItem, bWarnLoc, bWarnType;
                PUU32 bReadyToGo = FALSE;

                puSetAttribute( puGetObjectFromCollection( g_pCol, CS_ERROR_WINDOW ), PUA_WINDOW_OPENED, FALSE );

                bWarnItem = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ALERTITEM_CB ), PUA_CHECKBOX_CHECKED );
                bWarnLoc = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ALERTLOC_CB ), PUA_CHECKBOX_CHECKED );
                bWarnType = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ALERTTYPE_CB ), PUA_CHECKBOX_CHECKED );

                if( puGetAttribute( g_ItemWatchList, PUA_TABLE_NUMRECORDS ) ) bItemListOk = TRUE;
                if( puGetAttribute( g_LocWatchList, PUA_TABLE_NUMRECORDS ) ) bLocListOk = TRUE;
                if( puGetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPEREPAIR_CB ), PUA_CHECKBOX_CHECKED ) ||
                    puGetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPEFINDP_CB ), PUA_CHECKBOX_CHECKED ) ||
                    puGetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPEFINDI_CB ), PUA_CHECKBOX_CHECKED ) ||
                    puGetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPERETURN_CB ), PUA_CHECKBOX_CHECKED ) ||
                    puGetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPEASS_CB ), PUA_CHECKBOX_CHECKED ) )
                    bTypeListOk = TRUE;

                bReadyToGo = bWarnLoc || bWarnItem || bWarnType;
                if( bWarnItem ) bReadyToGo = bReadyToGo && bItemListOk;
                if( bWarnLoc ) bReadyToGo = bReadyToGo && bLocListOk;
                if( bWarnType ) bReadyToGo = bReadyToGo && bTypeListOk;

                if( bReadyToGo )
                {
					StartNewAcceptedMissionSession();
                    ClearItemCounters();
                    g_bBuyingAgentActive = 1;
					g_bPaused = 0; 
					
					PULID pauseButton = puGetObjectFromCollection( g_pCol, CS_BUYINGAGENT_PAUSEBTN );
					if (pauseButton) {
						puSetAttribute(pauseButton, PUA_TEXT_STRING, (PUU32)"Pause");
					}
					PULID statusLabel = puGetObjectFromCollection( g_pCol, CS_BA_STATUS );
					if (statusLabel) {
						puSetAttribute(statusLabel, PUA_TEXT_STRING, (PUU32)"Running...");
					}
                    
                    {
						PUU32 WLRecord = puDoMethod( g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0 );
						while( WLRecord ) {
							PUU8 *pWLStr = (PUU8*)puDoMethod( g_ItemWatchList, PUM_TABLE_GETFIELDVAL, WLRecord, 0 );
							if( pWLStr && *pWLStr ) {
								char wlName[256];
								int wlDisabled = 0, wlForce = 0, wlLimit = 0;
								char wlExclude[256];
								ParseDisplayString((char*)pWLStr, wlName, sizeof(wlName),
												&wlDisabled, &wlForce, &wlLimit, wlExclude, sizeof(wlExclude));
								if( !wlDisabled && wlLimit > 0 ) {
									if( !FindItemCounter( wlName ) )
										AddItemCounter( wlName, wlLimit );
								}
							}
							WLRecord = puDoMethod( g_ItemWatchList, PUM_TABLE_GETNEXTRECORD, WLRecord, 0 );
						}
					}

                    g_BuyingAgentMissions = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENTMISH ), PUA_TEXTENTRY_VALUE );
                    g_BuyingAgentMaxMissions = g_BuyingAgentMissions;
                    g_BuyingAgentMaxTries = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENTTRIES ), PUA_TEXTENTRY_VALUE );
                    g_BuyingAgentCount = g_BuyingAgentMaxTries;
                    g_TotalAttempts = 0;
                    g_bFirstRound = TRUE;
                    
                    char acceptBuf[64];
                    sprintf( acceptBuf, "Accepted 0 of %d", g_BuyingAgentMaxMissions );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BA_ACCEPTED ), PUA_TEXT_STRING, (PUU32)acceptBuf );
                    
                    char totalBuf[64];
                    sprintf(totalBuf, "Total attempts: 0");
                    puSetAttribute(puGetObjectFromCollection(g_pCol, CS_BA_TOTAL), PUA_TEXT_STRING, (PUU32)totalBuf);
                    
                    char buffer[64];
                    sprintf( buffer, "Current mission: Attempt 1 of %d", g_BuyingAgentMaxTries );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BA_PROGRESS ), PUA_TEXT_STRING, (PUU32)buffer );
                    
                    BuyingAgent(g_BuyingAgentDelay);
                }
                else
                {
                    DisplayErrorMessage( "I won't ever find any mission with your current settings and watch lists.", TRUE );
                }
            }
            break;

        case CSAM_EXPORTSETTINGS:
        {
            char buffer[ 2000 ];
            if( GetFile( (HWND)puGetAttribute( puGetObjectFromCollection( g_pCol, CS_MAIN_WINDOW ), PUA_WINDOW_HANDLE )
                , TRUE, buffer, 2000 ) )
            {
                ExportSettings( buffer );
            }
            SetCurrentDirectory( g_CSDir );
        }
        break;

        case CSAM_IMPORTSETTINGS:
        {
            char buffer[ 2000 ];
            if( GetFile( (HWND)puGetAttribute( puGetObjectFromCollection( g_pCol, CS_MAIN_WINDOW ), PUA_WINDOW_HANDLE )
                , FALSE, buffer, 2000 ) )
            {
                ImportSettings( buffer );
            }
            SetCurrentDirectory( g_CSDir );
        }
        break;

        case CSAM_STOPFULLSCREEN:
            g_bFullscreen = 0;
            puSetAttribute( puGetObjectFromCollection( g_pCol, CS_FULLSCREEN_WINDOW ), PUA_WINDOW_OPENED, FALSE );
            puSetAttribute( puGetObjectFromCollection( g_pCol, CS_MAIN_WINDOW ), PUA_WINDOW_OPENED, TRUE );
            break;

        case CSAM_STARTFULLSCREEN:
        {
            PUU32 bItemListOk = FALSE, bLocListOk = FALSE, bTypeListOk = FALSE;
            PUU32 bWarnItem, bWarnLoc, bWarnType;
            PUU32 bReadyToGo = FALSE;

            puSetAttribute( puGetObjectFromCollection( g_pCol, CS_ERROR_WINDOW ), PUA_WINDOW_OPENED, FALSE );

            bWarnItem = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ALERTITEM_CB ), PUA_CHECKBOX_CHECKED );
            bWarnLoc = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ALERTLOC_CB ), PUA_CHECKBOX_CHECKED );
            bWarnType = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ALERTTYPE_CB ), PUA_CHECKBOX_CHECKED );

            if( puGetAttribute( g_ItemWatchList, PUA_TABLE_NUMRECORDS ) ) bItemListOk = TRUE;
            if( puGetAttribute( g_LocWatchList, PUA_TABLE_NUMRECORDS ) ) bLocListOk = TRUE;
            if( puGetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPEREPAIR_CB ), PUA_CHECKBOX_CHECKED ) ||
                puGetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPEFINDP_CB ), PUA_CHECKBOX_CHECKED ) ||
                puGetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPEFINDI_CB ), PUA_CHECKBOX_CHECKED ) ||
                puGetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPERETURN_CB ), PUA_CHECKBOX_CHECKED ) ||
                puGetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPEASS_CB ), PUA_CHECKBOX_CHECKED ) )
                bTypeListOk = TRUE;

            bReadyToGo = bWarnLoc || bWarnItem || bWarnType;
            if( bWarnItem ) bReadyToGo = bReadyToGo && bItemListOk;
            if( bWarnLoc ) bReadyToGo = bReadyToGo && bLocListOk;
            if( bWarnType ) bReadyToGo = bReadyToGo && bTypeListOk;

            if( bReadyToGo )
            {
                puSetAttribute( puGetObjectFromCollection( g_pCol, CS_FULLSCREEN_WINDOW ), PUA_WINDOW_OPENED, TRUE );
                puSetAttribute( puGetObjectFromCollection( g_pCol, CS_MAIN_WINDOW ), PUA_WINDOW_OPENED, FALSE );
                g_bFullscreen = 1;
            }
            else
            {
                DisplayErrorMessage( "I won't ever find any mission with your current settings and watch lists.", TRUE );
            }
        }
        break;

        case CSAM_SET_SLIDERS:
        {
            int easy_hard = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_EASY_HARD ), PUA_TEXTENTRY_VALUE );
            int good_bad = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_GOOD_BAD ), PUA_TEXTENTRY_VALUE );
            int order_chaos = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_ORDER_CHAOS ), PUA_TEXTENTRY_VALUE );
            int open_hidden = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_OPEN_HIDDEN ), PUA_TEXTENTRY_VALUE );
            int phys_myst = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_PHYS_MYST ), PUA_TEXTENTRY_VALUE );
            int headon_stealth = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_HEADON_STEALTH ), PUA_TEXTENTRY_VALUE );
            int money_xp = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_MONEY_XP ), PUA_TEXTENTRY_VALUE );

            _setSliders( easy_hard, good_bad, order_chaos, open_hidden, phys_myst, headon_stealth, money_xp );
        }
		break;
			case 5001:
			{
				PULID listView = (PULID)pAppMsg->Param;
				if (listView == puGetObjectFromCollection(g_pCol, CS_ITEMWATCH_LISTVIEW))
					EditActiveItem();
				else if (listView == puGetObjectFromCollection(g_pCol, CS_DISABLED_ITEMWATCH_LISTVIEW))
					EditDisabledItem();
				break;
			}
		
        }
        stop_buying_agent:;
    }
    while( pAppMsg->Message != CSAM_QUIT );

    WriteDebug( NULL );
    SetCurrentDirectory( g_CSDir );
    ExportSettings( "LastSettings.cs" );
    CleanUp();
    return 0;
}

void CleanUp()
{
    if (g_hThreadExitEvent != NULL) {
        SetEvent(g_hThreadExitEvent);
    }
    
    if( g_Thread != INVALID_HANDLE_VALUE )
    {
        WaitForSingleObject(g_Thread, 5000);
        CloseHandle(g_Thread);
    }

    if( g_Mutex != INVALID_HANDLE_VALUE )
    {
        CloseHandle( g_Mutex );
    }
    
    if (g_hThreadExitEvent != NULL) 
    {
        CloseHandle(g_hThreadExitEvent);
    }
    
    if (g_hAbortEvent) {
        CloseHandle(g_hAbortEvent);
    }
	
	ReleaseAODatabase();
	FreeDialogColors();

	CloseAcceptedMissionLog();
    puDeleteObjectCollection( g_pCol );
    puClear();
	FreeItemNameCache();
}

enum
{
    CFG_AODIR,
    CFG_WINDOWX,
    CFG_WINDOWY,
    CFG_WINDOWWIDTH,
    CFG_STARTMINIMIZED,
    CFG_WATCHMSGBOX,
    CFG_ALERTITEM,
    CFG_ALERTLOC,
    CFG_ALERTTYPE,
    CFG_BUYINGAGENTMAXTRIES,
    CFG_BUYINGAGENTMISH,
    CFG_BUYINGAGENTHIDE,
    CFG_BUYINGAGENTSHOWHELP,
    CFG_MISSIONTYPES,
    CFG_HIGHLIGHTOPTS,
    CFG_SOUNDS,
    CFG_LOG,
    CFG_MOUSEMOVE,
    CFG_EXPAND,
    CFG_ITEMVALUE,

    CFG_SLIDER_EASY_HARD,
    CFG_SLIDER_GOOD_BAD,
    CFG_SLIDER_ORDER_CHAOS,
    CFG_SLIDER_OPEN_HIDDEN,
    CFG_SLIDER_PHYS_MYST,
    CFG_SLIDER_HEADON_STEALTH,
    CFG_SLIDER_MONEY_XP,

    CFG_BUYMOD,
    CFG_BUYINGAGENTDELAY,
    CFG_ITEMOPTIONAL,
	CFG_BAWINDOWX,
    CFG_BAWINDOWY,
};


struct
{
    int id;
    char* keyword;
} CfgKeywords[] =
{
    { CFG_AODIR, "AODIR" },
    { CFG_SOUNDS, "SOUNDS" },
    { CFG_MOUSEMOVE, "MOUSEMOVE" },
    { CFG_LOG, "LOG" },
    { CFG_WINDOWX, "WINDOWX" },
    { CFG_WINDOWY, "WINDOWY" },
    { CFG_WINDOWWIDTH, "WINDOWWIDTH" },
    { CFG_STARTMINIMIZED, "STARTMINIMIZED" },
    { CFG_WATCHMSGBOX, "WATCHMSGBOX" },
    { CFG_BUYINGAGENTSHOWHELP, "BUYINGAGENTSHOWHELP" },
    { CFG_ALERTLOC, "ALERTLOC" },
    { CFG_ALERTITEM, "ALERTITEM" },
    { CFG_ALERTTYPE, "ALERTTYPE" },
    { CFG_BUYINGAGENTMAXTRIES, "BUYINGAGENTMAXTRIES" },
    { CFG_BUYINGAGENTMISH, "BUYINGAGENTMISH" },
    { CFG_BUYINGAGENTHIDE, "BUYINGAGENTHIDE" },
    { CFG_MISSIONTYPES, "MISHTYPES" },
    { CFG_HIGHLIGHTOPTS, "HIGHLIGHTOPTS" },
    { CFG_EXPAND, "EXPAND" },

    { CFG_SLIDER_EASY_HARD, "SLIDER_EASY_HARD" },
    { CFG_SLIDER_GOOD_BAD, "SLIDER_GOOD_BAD" },
    { CFG_SLIDER_ORDER_CHAOS, "SLIDER_ORDER_CHAOS" },
    { CFG_SLIDER_OPEN_HIDDEN, "SLIDER_OPEN_HIDDEN" },
    { CFG_SLIDER_PHYS_MYST, "SLIDER_PHYS_MYST" },
    { CFG_SLIDER_HEADON_STEALTH, "SLIDER_HEADON_STEALTH" },
    { CFG_SLIDER_MONEY_XP, "SLIDER_MONEY_XP" },

    { CFG_ITEMVALUE, "ITEMVALUE" },

    { CFG_BUYMOD, "BUYMOD" },
    { CFG_BUYINGAGENTDELAY, "BUYINGAGENTDELAY" },
    { CFG_ITEMOPTIONAL, "ITEMOPTIONAL" },
	{ CFG_BAWINDOWX, "BAWINDOWX" },
    { CFG_BAWINDOWY, "BAWINDOWY" },
    { 0, NULL }
};

void ImportSettings( char* filename )
{
    FILE* fp;
    PUU32 Record;
    PUU8* pString;
    char buffer[ 1000 ];
    PUU8 Keyword[ 256 ], Value[ 256 ];
    int Id, i;
	int iVal;
    PUU32 Val;
    int mode = ISM_DONE;
    char c;

    Record = puDoMethod( g_LocWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0 );
    while( Record )
    {
        puDoMethod( g_LocWatchList, PUM_TABLE_REMRECORD, Record, 0 );
        Record = puDoMethod( g_LocWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0 );
    }
    Record = puDoMethod( g_DisabledItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0 );
    while( Record )
    {
        puDoMethod( g_DisabledItemWatchList, PUM_TABLE_REMRECORD, Record, 0 );
        Record = puDoMethod( g_DisabledItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0 );
    }
	
    Record = puDoMethod( g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0 );
    while( Record )
    {
        puDoMethod( g_ItemWatchList, PUM_TABLE_REMRECORD, Record, 0 );
        Record = puDoMethod( g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0 );
    }

    if( !( fp = fopen( filename, "r" ) ) )
    {
        return;
    }

    while( fgets( buffer, 1000, fp ) )
    {
        if( sscanf( buffer, "::%s", &buffer ) == 1 )
        {
            strtok( buffer, ":" );
            if( !_stricmp( buffer, "Config" ) ) mode = ISM_CONFIG;
            if( !_stricmp( buffer, "LocWatch" ) ) mode = ISM_LOCWATCH;
            if( !_stricmp( buffer, "ItemWatch" ) ) mode = ISM_ITEMWATCH;
			if( !_stricmp( buffer, "DisabledItemWatch" ) ) mode = ISM_DISABLED_ITEMWATCH;
            if( !_stricmp( buffer, "Sliders" ) ) mode = ISM_SLIDERS;
            if( !_stricmp( buffer, "Done" ) ) mode = ISM_DONE;
            continue;
        }
        switch( mode )
        {
        case ISM_DONE:
            break;

        case ISM_CONFIG:
            if( sscanf( buffer, "%255[^:]::%255[^\n]\n", Keyword, Value ) != EOF )
            {
                i = 0, Id = -1;
                while( CfgKeywords[ i ].keyword )
                {
                    if( !strcmp( Keyword, CfgKeywords[ i ].keyword ) )
                    {
                        Id = CfgKeywords[ i ].id;
                        break;
                    }
                    i++;
                }
				
                switch( Id )
                {
				case CFG_AODIR:
					safe_strcpy(g_AODir, MAX_PATH, Value);
					break;
				case CFG_WINDOWX:
					sscanf( Value, "%d", &iVal );
					puSetAttribute( g_MainWin, PUA_WINDOW_XPOS, iVal );
					break;
				case CFG_WINDOWY:
					sscanf( Value, "%d", &iVal );
					puSetAttribute( g_MainWin, PUA_WINDOW_YPOS, iVal );
					break;
				case CFG_BAWINDOWX:
					sscanf( Value, "%d", &iVal );
					if ( iVal > 0 && iVal < 5000 ) {
						puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENT_WINDOW ), PUA_WINDOW_XPOS, iVal );
						g_BAWindowX = iVal;
					} else {
						puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENT_WINDOW ), PUA_WINDOW_XPOS, g_BAWindowX );
					}
					break;
				case CFG_BAWINDOWY:
					sscanf( Value, "%d", &iVal );
					if ( iVal > 0 && iVal < 5000 ) {
						puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENT_WINDOW ), PUA_WINDOW_YPOS, iVal );
						g_BAWindowY = iVal;
					} else {
						puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENT_WINDOW ), PUA_WINDOW_YPOS, g_BAWindowY );
					}
					break;
                case CFG_WINDOWWIDTH:
                    sscanf( Value, "%u", &Val );
                    puSetAttribute( g_MainWin, PUA_WINDOW_WIDTH, Val );
                    break;
                case CFG_STARTMINIMIZED:
                    sscanf( Value, "%u", &Val );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_STARTMIN_CB ), PUA_CHECKBOX_CHECKED, Val ? TRUE : FALSE );
                    break;
                case CFG_WATCHMSGBOX:
                    sscanf( Value, "%u", &Val );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_MSGBOX_CB ), PUA_CHECKBOX_CHECKED, Val ? TRUE : FALSE );
                    break;
                case CFG_ITEMOPTIONAL:
                    sscanf( Value, "%u", &Val );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMOPTIONAL_CB ), PUA_CHECKBOX_CHECKED, Val ? TRUE : FALSE );
                    break;
                case CFG_BUYINGAGENTSHOWHELP:
                    sscanf( Value, "%u", &Val );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BAINFO_CB ), PUA_CHECKBOX_CHECKED, Val ? TRUE : FALSE );
                    break;
                case CFG_SOUNDS:
                    sscanf( Value, "%u", &Val );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_SOUNDS_CB ), PUA_CHECKBOX_CHECKED, Val ? TRUE : FALSE );
                    break;
                case CFG_MOUSEMOVE:
                    sscanf( Value, "%u", &Val );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_MOUSEMOVE_CB ), PUA_CHECKBOX_CHECKED, Val ? TRUE : FALSE );
                    break;
                case CFG_EXPAND:
                    sscanf( Value, "%u", &Val );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_EXPAND_CB ), PUA_CHECKBOX_CHECKED, Val ? TRUE : FALSE );
                    break;
                case CFG_LOG:
                    sscanf( Value, "%u", &Val );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_LOG_CB ), PUA_CHECKBOX_CHECKED, Val ? TRUE : FALSE );
                    break;
                case CFG_ALERTITEM:
                    sscanf( Value, "%u", &Val );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_ALERTITEM_CB ), PUA_CHECKBOX_CHECKED, Val ? TRUE : FALSE );
                    break;
                case CFG_ALERTLOC:
                    sscanf( Value, "%u", &Val );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_ALERTLOC_CB ), PUA_CHECKBOX_CHECKED, Val ? TRUE : FALSE );
                    break;
                case CFG_ALERTTYPE:
                    sscanf( Value, "%u", &Val );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_ALERTTYPE_CB ), PUA_CHECKBOX_CHECKED, Val ? TRUE : FALSE );
                    break;
                case CFG_BUYINGAGENTMAXTRIES:
                    sscanf( Value, "%d", &Val );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENTTRIES ), PUA_TEXTENTRY_VALUE, Val );
                    break;
                case CFG_BUYINGAGENTMISH:
                    sscanf( Value, "%d", &Val );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENTMISH ), PUA_TEXTENTRY_VALUE, Val );
                    break;
                case CFG_BUYINGAGENTHIDE:
                    sscanf( Value, "%u", &Val );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENTFOLD ), PUA_FOLD_FOLDED, Val );
                    break;
                case CFG_MISSIONTYPES:
                    sscanf( Value, "%u", &Val );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPEREPAIR_CB ), PUA_CHECKBOX_CHECKED, ( Val & 0x01 ) ? TRUE : FALSE );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPERETURN_CB ), PUA_CHECKBOX_CHECKED, ( Val & 0x02 ) ? TRUE : FALSE );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPEFINDP_CB ), PUA_CHECKBOX_CHECKED, ( Val & 0x04 ) ? TRUE : FALSE );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPEFINDI_CB ), PUA_CHECKBOX_CHECKED, ( Val & 0x08 ) ? TRUE : FALSE );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPEASS_CB ), PUA_CHECKBOX_CHECKED, ( Val & 0x10 ) ? TRUE : FALSE );
                    break;
                case CFG_HIGHLIGHTOPTS:
                    sscanf( Value, "%u", &Val );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_HIGHLIGHTITEM_CB ), PUA_CHECKBOX_CHECKED, ( Val & 0x01 ) ? TRUE : FALSE );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_HIGHLIGHTLOC_CB ), PUA_CHECKBOX_CHECKED, ( Val & 0x02 ) ? TRUE : FALSE );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_HIGHLIGHTTYPE_CB ), PUA_CHECKBOX_CHECKED, ( Val & 0x04 ) ? TRUE : FALSE );
                    break;
                case CFG_SLIDER_EASY_HARD:
                case CFG_SLIDER_GOOD_BAD:
                case CFG_SLIDER_ORDER_CHAOS:
                case CFG_SLIDER_OPEN_HIDDEN:
                case CFG_SLIDER_PHYS_MYST:
                case CFG_SLIDER_HEADON_STEALTH:
                case CFG_SLIDER_MONEY_XP:
                    sscanf( Value, "%u", &Val );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_EASY_HARD + ( Id - CFG_SLIDER_EASY_HARD ) ), PUA_TEXTENTRY_VALUE, Val );
                    break;
                case CFG_BUYMOD:
                    sscanf( Value, "%u", &Val );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMVALUE_BUYMOD ), PUA_TEXTENTRY_VALUE, Val );
                    break;
                case CFG_BUYINGAGENTDELAY:
					sscanf( Value, "%u", &Val );
					if (Val > 0) {
						if (Val < 5200) Val = 5200;
						if (Val > 15000) Val = 15000;
						g_BuyingAgentDelay = Val;
					}
					break;
					
                case CFG_ITEMVALUE:
                {
                    PUU32 a, b, c, d;
                    sscanf( Value, "%u::%u::%u::%u", &a, &b, &c, &d );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMVALUE_SINGLE ), PUA_TEXTENTRY_VALUE, a );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMVALUE_TOTAL ), PUA_TEXTENTRY_VALUE, b );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMVALUE_MSINGLE ), PUA_CHECKBOX_CHECKED, c );
                    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMVALUE_MTOTAL ), PUA_CHECKBOX_CHECKED, d );
                    break;
                }
                }
				 if (strcmp(Keyword, "EXIT_RADIUS") == 0) {
					sscanf(Value, "%d", &g_ExitProximityRadius);
				}
					else if (strcmp(Keyword, "EXIT_ALERT") == 0) {
						int val; sscanf(Value, "%d", &val);
						puSetAttribute(puGetObjectFromCollection(g_pCol, CS_ALERTEXIT_CB), PUA_CHECKBOX_CHECKED, val);
					}
					else if (strcmp(Keyword, "EXIT_HIGHLIGHT") == 0) {
						int val; sscanf(Value, "%d", &val);
						puSetAttribute(puGetObjectFromCollection(g_pCol, CS_HIGHLIGHTEXIT_CB), PUA_CHECKBOX_CHECKED, val);
					}
					else if (strncmp(Keyword, "EXIT_CHECKED_", 13) == 0) {
						int idx = atoi(Keyword + 13);
						if (idx >= 0 && idx < g_NumExits) {
							int val;
							sscanf(Value, "%d", &val);
							PULID chk = puGetObjectFromCollection(g_pCol, CS_EXIT_FIRST + idx);
							if (chk) puSetAttribute(chk, PUA_CHECKBOX_CHECKED, val ? TRUE : FALSE);
						}
					}
			}
			break;

		case ISM_DISABLED_ITEMWATCH:
			pString = buffer + strlen( buffer );
			while( pString > buffer )
			{
				c = *--pString;
				if( c != ' ' && c != '\t' && c != '\n' ) break;
			}
			*( pString + 1 ) = 0;
			pString = buffer;
			while( c = *pString++ )
			{
				if( c != ' ' && c != '\t' ) break;
			}
			pString--;
			if( *pString )
			{
				char tableEntry[1024];
				MakeTableEntry(tableEntry, sizeof(tableEntry), pString);
				puDoMethod( g_DisabledItemWatchList, PUM_TABLE_NEWRECORD, 0, 0 );
				puDoMethod( g_DisabledItemWatchList, PUM_TABLE_ADDRECORD, 0, 0 );
				puDoMethod( g_DisabledItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)tableEntry, 0 );
			}
			break;
			
        case ISM_ITEMWATCH:
        case ISM_LOCWATCH:
            pString = buffer + strlen( buffer );
            while( pString > buffer )
            {
                c = *--pString;
                if( c != ' ' && c != '\t' && c != '\n' ) break;
            }
            *( pString + 1 ) = 0;
            pString = buffer;
            while( c = *pString++ )
            {
                if( c != ' ' && c != '\t' ) break;
            }
            pString--;
            if( *pString )
            {
                if( mode == ISM_ITEMWATCH ) {
                    char tableEntry[1024];
                    MakeTableEntry(tableEntry, sizeof(tableEntry), pString);
                    puDoMethod( g_ItemWatchList, PUM_TABLE_NEWRECORD, 0, 0 );
                    puDoMethod( g_ItemWatchList, PUM_TABLE_ADDRECORD, 0, 0 );
                    puDoMethod( g_ItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)tableEntry, 0 );
                } else {
                    puDoMethod( g_LocWatchList, PUM_TABLE_NEWRECORD, 0, 0 );
                    puDoMethod( g_LocWatchList, PUM_TABLE_ADDRECORD, 0, 0 );
                    puDoMethod( g_LocWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)pString, 0 );
                }
            }
            break;
        }
    }

    fclose( fp );
}

void ExportSettings( char* filename )
{
    FILE* fp;
    pusRect Rect = {0};
    PUU32 Record;
    PUU8* pString;
    unsigned int Val = 0;
    char* myfilename;
    size_t myfilename_size = strlen(filename) + 5;

    myfilename = malloc(myfilename_size);
    if (!myfilename) return;

    safe_strcpy(myfilename, myfilename_size, filename);
    if( !strstr( myfilename, ".cs" ) ) safe_strcat(myfilename, myfilename_size, ".cs" );

    if( !( fp = fopen( myfilename, "w" ) ) )
    {
        free( myfilename );
        return;
    }
    free( myfilename );
    fprintf( fp, "::Config::\n" );
    fprintf( fp, "AODIR::%s\n", g_AODir );

    puDoMethod( g_MainWin, PUM_WINDOW_GETRECT, (PUU32)&Rect, 0 );
    fprintf( fp, "WINDOWX::%d\nWINDOWY::%d\nWINDOWWIDTH::%d\n", Rect.X, Rect.Y, Rect.Width );

    fprintf( fp, "STARTMINIMIZED::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_STARTMIN_CB ), PUA_CHECKBOX_CHECKED ) );
    fprintf( fp, "WATCHMSGBOX::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_MSGBOX_CB ), PUA_CHECKBOX_CHECKED ) );
    fprintf( fp, "BUYINGAGENTSHOWHELP::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_BAINFO_CB ), PUA_CHECKBOX_CHECKED ) );
    fprintf( fp, "SOUNDS::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SOUNDS_CB ), PUA_CHECKBOX_CHECKED ) );
    fprintf( fp, "ITEMOPTIONAL::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMOPTIONAL_CB ), PUA_CHECKBOX_CHECKED ) );
    fprintf( fp, "EXPAND::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_EXPAND_CB ), PUA_CHECKBOX_CHECKED ) );
    fprintf( fp, "MOUSEMOVE::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_MOUSEMOVE_CB ), PUA_CHECKBOX_CHECKED ) );
    fprintf( fp, "LOG::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_LOG_CB ), PUA_CHECKBOX_CHECKED ) );
    fprintf( fp, "ALERTITEM::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ALERTITEM_CB ), PUA_CHECKBOX_CHECKED ) );
    fprintf( fp, "ALERTLOC::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ALERTLOC_CB ), PUA_CHECKBOX_CHECKED ) );
    fprintf( fp, "ALERTTYPE::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ALERTTYPE_CB ), PUA_CHECKBOX_CHECKED ) );
    fprintf( fp, "BUYINGAGENTMAXTRIES::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENTTRIES ), PUA_TEXTENTRY_VALUE ) );
    fprintf( fp, "BUYINGAGENTMISH::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENTMISH ), PUA_TEXTENTRY_VALUE ) );
    fprintf( fp, "BUYINGAGENTHIDE::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENTFOLD ), PUA_FOLD_FOLDED ) );

    Val = 0;
    if( puGetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPEREPAIR_CB ), PUA_CHECKBOX_CHECKED ) ) Val |= 0x01;
    if( puGetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPERETURN_CB ), PUA_CHECKBOX_CHECKED ) ) Val |= 0x02;
    if( puGetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPEFINDP_CB ), PUA_CHECKBOX_CHECKED ) ) Val |= 0x04;
    if( puGetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPEFINDI_CB ), PUA_CHECKBOX_CHECKED ) ) Val |= 0x08;
    if( puGetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPEASS_CB ), PUA_CHECKBOX_CHECKED ) ) Val |= 0x10;
    fprintf( fp, "MISHTYPES::%u\n", Val );

    Val = 0;
    if( puGetAttribute( puGetObjectFromCollection( g_pCol, CS_HIGHLIGHTITEM_CB ), PUA_CHECKBOX_CHECKED ) ) Val |= 0x01;
    if( puGetAttribute( puGetObjectFromCollection( g_pCol, CS_HIGHLIGHTLOC_CB ), PUA_CHECKBOX_CHECKED ) ) Val |= 0x02;
    if( puGetAttribute( puGetObjectFromCollection( g_pCol, CS_HIGHLIGHTTYPE_CB ), PUA_CHECKBOX_CHECKED ) ) Val |= 0x04;
    fprintf( fp, "HIGHLIGHTOPTS::%u\n", Val );

    fprintf( fp, "SLIDER_EASY_HARD::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_EASY_HARD ), PUA_TEXTENTRY_VALUE ) );
    fprintf( fp, "SLIDER_GOOD_BAD::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_GOOD_BAD ), PUA_TEXTENTRY_VALUE ) );
    fprintf( fp, "SLIDER_ORDER_CHAOS::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_ORDER_CHAOS ), PUA_TEXTENTRY_VALUE ) );
    fprintf( fp, "SLIDER_OPEN_HIDDEN::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_OPEN_HIDDEN ), PUA_TEXTENTRY_VALUE ) );
    fprintf( fp, "SLIDER_PHYS_MYST::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_PHYS_MYST ), PUA_TEXTENTRY_VALUE ) );
    fprintf( fp, "SLIDER_HEADON_STEALTH::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_HEADON_STEALTH ), PUA_TEXTENTRY_VALUE ) );
    fprintf( fp, "SLIDER_MONEY_XP::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_SLIDER_MONEY_XP ), PUA_TEXTENTRY_VALUE ) );

    fprintf( fp, "BUYMOD::%u\n", puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMVALUE_BUYMOD ), PUA_TEXTENTRY_VALUE ) );
	
    PULID delayCtrl = puGetObjectFromCollection(g_pCol, CS_BUYINGAGENTDELAY_ENTRY);
	int delayValue = g_BuyingAgentDelay;
	if (delayCtrl) {
		HWND hEdit = (HWND)puGetAttribute(delayCtrl, PUA_WINDOW_HANDLE);
		if (hEdit && IsWindow(hEdit)) {
			char buf[32] = {0};
			GetWindowTextA(hEdit, buf, sizeof(buf));
			int val = atoi(buf);
			if (val >= 5200 && val <= 15000)
				delayValue = val;
		}
	}
	fprintf(fp, "BUYINGAGENTDELAY::%u\n", delayValue);
	
	PULID baObj = puGetObjectFromCollection( g_pCol, CS_BUYINGAGENT_WINDOW );
	if ( puGetAttribute( baObj, PUA_WINDOW_OPENED ) )
	{
		HWND baWnd = (HWND)puGetAttribute( baObj, PUA_WINDOW_HANDLE );
		if ( baWnd && IsWindow(baWnd) )
		{
			RECT r;
			GetWindowRect( baWnd, &r );
			g_BAWindowX = r.left;
			g_BAWindowY = r.top;
		}
	}
	fprintf(fp, "BAWINDOWX::%d\nBAWINDOWY::%d\n", g_BAWindowX, g_BAWindowY);
	
	fprintf(fp, "EXIT_RADIUS::%d\n", g_ExitProximityRadius);
	fprintf(fp, "EXIT_ALERT::%d\n", puGetAttribute(puGetObjectFromCollection(g_pCol, CS_ALERTEXIT_CB), PUA_CHECKBOX_CHECKED));
	fprintf(fp, "EXIT_HIGHLIGHT::%d\n", puGetAttribute(puGetObjectFromCollection(g_pCol, CS_HIGHLIGHTEXIT_CB), PUA_CHECKBOX_CHECKED));
	
    fprintf( fp, "ITEMVALUE::%u::%u::%u::%u\n",
             puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMVALUE_SINGLE ), PUA_TEXTENTRY_VALUE ),
             puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMVALUE_TOTAL ), PUA_TEXTENTRY_VALUE ),
             puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMVALUE_MSINGLE ), PUA_CHECKBOX_CHECKED ),
             puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMVALUE_MTOTAL ), PUA_CHECKBOX_CHECKED ) );
			
    for (int i = 0; i < g_NumExits; i++) {
        PULID chk = puGetObjectFromCollection(g_pCol, CS_EXIT_FIRST + i);
        int checked = (chk && puGetAttribute(chk, PUA_CHECKBOX_CHECKED)) ? 1 : 0;
        fprintf(fp, "EXIT_CHECKED_%d::%d\n", i, checked);
    }

    fprintf( fp, "::ItemWatch::\n" );
	Record = puDoMethod( g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0 );
	while( Record )
	{
		PUU8* pDisplay = (PUU8*)puDoMethod( g_ItemWatchList, PUM_TABLE_GETFIELDVAL, Record, 0 );
		if( pDisplay && *pDisplay )
		{
			char itemName[256];
			int disabled = 0, force = 0, limit = 0;
			char exclude[256];
			ParseDisplayString((char*)pDisplay, itemName, sizeof(itemName),
							&disabled, &force, &limit, exclude, sizeof(exclude));
			char raw[512];
			BuildItemString(raw, sizeof(raw), itemName, disabled, force, limit, exclude);
			fprintf( fp, "%s\n", raw );
		}
		Record = puDoMethod( g_ItemWatchList, PUM_TABLE_GETNEXTRECORD, Record, 0 );
	}

    fprintf( fp, "::DisabledItemWatch::\n" );
    Record = puDoMethod( g_DisabledItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0 );
    while( Record )
    {
        PUU8* pDisplay = (PUU8*)puDoMethod( g_DisabledItemWatchList, PUM_TABLE_GETFIELDVAL, Record, 0 );
        if( pDisplay && *pDisplay )
        {
            char itemName[256];
            int disabled = 0, force = 0, limit = 0;
            char exclude[256];
            ParseDisplayString((char*)pDisplay, itemName, sizeof(itemName),
                               &disabled, &force, &limit, exclude, sizeof(exclude));
            char raw[512];
            BuildItemString(raw, sizeof(raw), itemName, disabled, force, limit, exclude);
            fprintf( fp, "%s\n", raw );
        }
        Record = puDoMethod( g_DisabledItemWatchList, PUM_TABLE_GETNEXTRECORD, Record, 0 );
    }
	
    fprintf( fp, "::LocWatch::\n" );
    Record = puDoMethod( g_LocWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0 );
    while( Record )
    {
        if( pString = (PUU8*)puDoMethod( g_LocWatchList, PUM_TABLE_GETFIELDVAL, Record, 0 ) )
            fprintf( fp, "%s\n", pString );
        Record = puDoMethod( g_LocWatchList, PUM_TABLE_GETNEXTRECORD, Record, 0 );
    }
    fprintf( fp, "::END::\n" );
    fclose( fp );
}

void DisplayErrorMessage( PUU8* _pMessage, PUU32 _bAsynchronous )
{
    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_ERROR_TEXT ), PUA_TEXT_STRING, (PUU32)_pMessage );
    puSetAttribute( puGetObjectFromCollection( g_pCol, CS_ERROR_WINDOW ), PUA_WINDOW_OPENED, TRUE );
    if( !_bAsynchronous )
        puWaitAppMessages();
}

void GetFolder( HWND hWndOwner, char *strTitle, char *strPath )
{
    BROWSEINFO udtBI = {0};
    ITEMIDLIST *udtIDList;
    udtBI.hwndOwner = hWndOwner;
    udtBI.pidlRoot = NULL;
    udtBI.pszDisplayName = NULL;
    udtBI.lpszTitle = strTitle;
    udtBI.ulFlags = BIF_RETURNONLYFSDIRS;
    udtBI.lpfn = NULL;
    udtBI.lParam = 0;
    udtBI.iImage = 0;
    udtIDList = SHBrowseForFolder( &udtBI );
    if( !SHGetPathFromIDList( udtIDList, strPath ) )
        strPath[ 0 ] = 0;
}

BOOL GetFile( HWND hWndOwner, BOOL saving, char* buffer, int buffersize )
{
    OPENFILENAME ofn;
    ZeroMemory( &ofn, sizeof( ofn ) );
    ofn.hwndOwner = hWndOwner;
    ofn.lStructSize = sizeof( OPENFILENAME );
    ofn.Flags = saving ? OFN_HIDEREADONLY : ( OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY );
    ofn.lpstrFilter = "Clicksaver Files\0*.CS\0";
	if (saving) ofn.lpstrDefExt = "cs";
    ofn.lpstrFile = buffer;
    ofn.lpstrFile[ 0 ] = '\0';
    ofn.nMaxFile = buffersize;
    ofn.nFilterIndex = 0;
    ofn.lpstrInitialDir = ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    if( saving )
        return GetSaveFileName( &ofn );
    else
        return GetOpenFileName( &ofn );
}

int BuyingAgent( int delay )
{
    HWND AOWnd, BAWnd;

    if( !( AOWnd = FindWindow( "Anarchy client", NULL ) ) )
    {
        DisplayErrorMessage( "Anarchy Online is not running.", TRUE );
        g_BuyingAgentCount = 0;
        g_BuyingAgentMissions = 0;
        return FALSE;
    }

    if( !g_bFullscreen )
    {
        HWND hMainWnd = (HWND)puGetAttribute( g_MainWin, PUA_WINDOW_HANDLE );
        SetWindowPos( hMainWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );
        puGetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENT_WINDOW ), PUA_WINDOW_XPOS );
        puGetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENT_WINDOW ), PUA_WINDOW_YPOS );
        puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENT_WINDOW ), PUA_WINDOW_OPENED, TRUE );
        BAWnd = (HWND)puGetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENT_WINDOW ), PUA_WINDOW_HANDLE );

		RemoveWindowSubclass(BAWnd, BAWndProcHook, 1);
		SetWindowSubclass(BAWnd, BAWndProcHook, 1, 0);
		
        SetFocus( BAWnd );
		
		PULID baWndObj = puGetObjectFromCollection(g_pCol, CS_BUYINGAGENT_WINDOW);
		int savedX = puGetAttribute(baWndObj, PUA_WINDOW_XPOS);
		int savedY = puGetAttribute(baWndObj, PUA_WINDOW_YPOS);
		SetWindowPos(BAWnd, NULL, g_BAWindowX, g_BAWindowY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

    g_PendingAttemptNumber = g_BuyingAgentMaxTries - g_BuyingAgentCount + 1;

    HWND hMainWnd = (HWND)puGetAttribute( g_MainWin, PUA_WINDOW_HANDLE );
	if (g_TimerID) {
		KillTimer(hMainWnd, TIMER_BUYINGAGENT);
		g_TimerID = 0;
	}
	g_TimerID = SetTimer( hMainWnd, TIMER_BUYINGAGENT, delay, NULL );
    if (g_TimerID == 0)
    {
        DisplayErrorMessage( "Failed to create timer.", TRUE );
        return FALSE;
    }

    return TRUE;
}

void EndBuyingAgent(int keepWindow)
{
    g_bPaused = 0;
    ClearItemCounters();

    if (!g_bFullscreen)
    {
        SetFocus(NULL);
        
        PULID baObj = puGetObjectFromCollection(g_pCol, CS_BUYINGAGENT_WINDOW);
        HWND baWnd = (HWND)puGetAttribute(baObj, PUA_WINDOW_HANDLE);
        if (baWnd && IsWindow(baWnd))
        {
            RECT r;
            GetWindowRect(baWnd, &r);
            puSetAttribute(baObj, PUA_WINDOW_XPOS, r.left);
            puSetAttribute(baObj, PUA_WINDOW_YPOS, r.top);
            g_BAWindowX = r.left;
            g_BAWindowY = r.top;
        }
        
        if (!keepWindow)
        {
            // Hide the buying agent window and restore the main window
            puSetAttribute(baObj, PUA_WINDOW_OPENED, FALSE);
            puSetAttribute(g_MainWin, PUA_WINDOW_OPENED, TRUE);
            HWND hMainWnd = (HWND)puGetAttribute(g_MainWin, PUA_WINDOW_HANDLE);
            if (baWnd && IsWindow(baWnd))
                RemoveWindowSubclass(baWnd, BAWndProcHook, 1);
            SetForegroundWindow(hMainWnd);
            SetFocus(hMainWnd);
            SetWindowPos(hMainWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        }
    }
}

static const char* MissionTypeIdToString(PUU32 type) {
    switch (type) {
        case 0x2c4e:  return "Repair";
        case 0x26add: return "Return Item";
        case 0x2c47:  return "Find Person";
        case 0x2c49:  return "Find Item";
        case 0x2c42:  return "Kill Person";
        default:      return "Unknown";
    }
}

static void GetPlayfieldName(int zoneId, char* buf, size_t bufSize) {
    buf[0] = '\0';
    PUU8* pData = GetDataChunk(AODB_TYP_PF, zoneId, NULL);
    if (pData) {
        strncpy(buf, (char*)pData, bufSize - 1);
        buf[bufSize - 1] = '\0';
        free(pData);
    } else {
        snprintf(buf, bufSize, "Zone %d", zoneId);
    }
}

void ResetAcceptedMissionLog(void) {
    if (g_AcceptedLogFile) {
        fclose(g_AcceptedLogFile);
        g_AcceptedLogFile = NULL;
    }
	
	free(g_LoggedMissionKeys);
    g_LoggedMissionKeys = NULL;
    g_LoggedMissionCount = 0;
    g_LoggedMissionCapacity = 0;

    // Open log file in append mode (creates if needed)
    g_AcceptedLogFile = fopen("AcceptedMissions.txt", "a");
    if (!g_AcceptedLogFile) {
        DisplayErrorMessage("Could not open AcceptedMissions.txt for writing.", TRUE);
        return;
    }

    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timeBuf[64];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm_info);
    fprintf(g_AcceptedLogFile, "\n=== New Session: %s ===\n", timeBuf);
    fflush(g_AcceptedLogFile);
}

void StartNewAcceptedMissionSession(void)
{
	g_LastLoggedPlayfield[0] = '\0';
	
    if (!g_AcceptedLogFile) {
        ResetAcceptedMissionLog();
        return;
    }

    // Clear mission keys cache only
    for (int i = 0; i < g_LoggedMissionCount; i++)
        free(g_LoggedMissionKeys[i]);
    free(g_LoggedMissionKeys);
    g_LoggedMissionKeys = NULL;
    g_LoggedMissionCount = g_LoggedMissionCapacity = 0;

    // Write a new session header
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timeBuf[64];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm_info);
    fprintf(g_AcceptedLogFile, "\n=== New Session: %s ===\n", timeBuf);
    fflush(g_AcceptedLogFile);
}

void LogAcceptedMission(int zoneId, float x, float y, PUU32 missionTypeId, const char* findItem, PUU32 mishId, const char* missionTitle){
    if (!g_AcceptedLogFile) return;

    char pfName[256];
    GetPlayfieldName(zoneId, pfName, sizeof(pfName));

    // Write playfield header only when it changes
    if (strcmp(pfName, g_LastLoggedPlayfield) != 0) {
        fprintf(g_AcceptedLogFile, "\n[%s]\n", pfName);
        strcpy(g_LastLoggedPlayfield, pfName);
    }

    const char* missionType = MissionTypeIdToString(missionTypeId);
    char missionDesc[512];
    if (missionTitle && missionTitle[0]) {
        snprintf(missionDesc, sizeof(missionDesc), "%s [%s]", missionType, missionTitle);
    } else {
        snprintf(missionDesc, sizeof(missionDesc), "%s", missionType);
    }

    // Build a unique key to avoid duplicate entries in the same session
    char key[512];
    snprintf(key, sizeof(key), "%u|%s|%s|%.1f|%.1f|%d", mishId, missionDesc, pfName, x, y, zoneId);

    // Session cache (prevents duplicates when the same mission is re‑parsed)
    for (int i = 0; i < g_LoggedMissionCount; i++) {
        if (strcmp(key, g_LoggedMissionKeys[i]) == 0)
            return;
    }

    if (g_LoggedMissionCount >= g_LoggedMissionCapacity) {
        g_LoggedMissionCapacity = g_LoggedMissionCapacity ? g_LoggedMissionCapacity * 2 : 16;
        g_LoggedMissionKeys = realloc(g_LoggedMissionKeys, g_LoggedMissionCapacity * sizeof(char*));
    }
    g_LoggedMissionKeys[g_LoggedMissionCount++] = _strdup(key);

    fprintf(g_AcceptedLogFile, "%s - /waypoint %.1f %.1f %d\n",
            missionDesc, x, y, zoneId);
    fflush(g_AcceptedLogFile);
}

void CloseAcceptedMissionLog(void) {
    if (g_AcceptedLogFile) {
        fclose(g_AcceptedLogFile);
        g_AcceptedLogFile = NULL;
    }

    // Free session cache
    for (int i = 0; i < g_LoggedMissionCount; i++) {
        free(g_LoggedMissionKeys[i]);
    }
    free(g_LoggedMissionKeys);
    g_LoggedMissionKeys = NULL;
    g_LoggedMissionCount = g_LoggedMissionCapacity = 0;
}

void UpdateAcceptedCountersForMission( int mishIndex )
{
    g_bUpdatingCounters = 1;
    PULID MissionControls[5] = {0};
    MissionControls[0] = puGetObjectFromCollection( g_pCol, CS_MISSION1 );
    MissionControls[1] = puGetObjectFromCollection( g_pCol, CS_MISSION2 );
    MissionControls[2] = puGetObjectFromCollection( g_pCol, CS_MISSION3 );
    MissionControls[3] = puGetObjectFromCollection( g_pCol, CS_MISSION4 );
    MissionControls[4] = puGetObjectFromCollection( g_pCol, CS_MISSION5 );

    void *pData = g_CurrentPacket;
    for (int i = 0; i <= mishIndex; i++) {
        if (i == mishIndex) {
            puDoMethod( MissionControls[i], CSM_MISSION_PARSEMISSION, (PUU32)pData, 0 );
            break;
        }
        pData = (void*)puDoMethod( MissionControls[i], CSM_MISSION_PARSEMISSION, (PUU32)pData, 0 );
    }
    g_bUpdatingCounters = 0;
}

void DebugPacket( void* pData, unsigned int length )
{
    unsigned int x;
    unsigned char *data = (char *)pData;
    char ps[70] = {0};
    for( x = 0; x < length; x++ )
    {
        sprintf( &( ps[ x % 16 * 3 ] ), "%02X", data[ x ] );
        ps[ x % 16 * 3 + 2 ] = ' ';
        ps[ x % 16 + 48 ] = ( data[ x ] >= 32 && data[ x ] <= 127 ? data[ x ] : '.' );
        ps[ x % 16 + 49 ] = '\n';
        ps[ x % 16 + 50 ] = 0;
        if( x % 16 == 15 ) WriteDebug( ps );
    }

    if( x % 16 != 0 )
    {
        for( x = x % 16; x < 16; x++ )
        {
            sprintf( &( ps[ x % 16 * 3 ] ), "  " );
            ps[ x % 16 * 3 + 2 ] = ' ';
        }
        WriteDebug( ps );
    }
}

void WriteLog( const char* Format, ... )
{
    va_list argptr;
    static FILE *fp = NULL;
    if( Format == NULL )
    {
        if( fp ) fclose( fp );
        fp = NULL;
        return;
    }
    if( PUL_GET_CB( CS_LOG_CB ) )
    {
        if( !fp ) fp = fopen( "clicksaver.log", "a" );
        va_start( argptr, Format );
        vfprintf( fp, Format, argptr );
        va_end( argptr );
    }
}

void WriteDebug( const char* txt )
{
#ifdef _DEBUG
    static FILE *fp = NULL;
    if( txt == NULL )
    {
        if( fp ) fclose( fp );
        fp = NULL;
        return;
    }
    if( !fp ) fp = fopen( "clicksaver.debug", "a" );
    fprintf( fp, "%s", txt );
#endif
}

static void _dragMouse( int x0, int y0, int x1, int y1 )
{
    POINT MousePos = {0, 0};
    LPARAM lParam;
    HWND AOWnd;

    if( !( AOWnd = FindWindow( "Anarchy client", NULL ) ) )
    {
        DisplayErrorMessage( "Anarchy Online is not running.", TRUE );
        g_BuyingAgentCount = 0;
        g_BuyingAgentMissions = 0;
        return;
    }
    MousePos.x = x0;
    MousePos.y = y0;
    lParam = MousePos.y << 16 | MousePos.x;
    ClientToScreen( AOWnd, &MousePos );
    SetCursorPos( MousePos.x, MousePos.y );
    SendMessage( AOWnd, WM_LBUTTONDOWN, 0, lParam );
    Sleep( 250 );
    MousePos.x = x1;
    MousePos.y = y1;
    lParam = MousePos.y << 16 | MousePos.x;
    ClientToScreen( AOWnd, &MousePos );
    SetCursorPos( MousePos.x, MousePos.y );
    SendMessage( AOWnd, WM_MOUSEMOVE, 0, lParam );
    Sleep( 250 );
    SendMessage( AOWnd, WM_LBUTTONUP, 0, lParam );
    Sleep( 250 );
}

static float _linIinterp( float lo, float hi, float ratio )
{
    return ( hi - lo ) * ratio + lo;
}

void _setSliders( int easy_hard, int good_bad, int order_chaos, int open_hidden, int phys_myst, int headon_stealth, int money_xp )
{
    int ypos = 210;
    if( easy_hard != 50 ) _dragMouse( 102, 160, (int)_linIinterp( 64, 141, easy_hard / 100.0f ), 160 );
    if( good_bad != 50 ) _dragMouse( 102, ypos, (int)_linIinterp( 64, 141, good_bad / 100.0f ), ypos );
    ypos += 18;
    if( order_chaos != 50 ) _dragMouse( 102, ypos, (int)_linIinterp( 64, 141, order_chaos / 100.0f ), ypos );
    ypos += 18;
    if( open_hidden != 50 ) _dragMouse( 102, ypos, (int)_linIinterp( 64, 141, open_hidden / 100.0f ), ypos );
    ypos += 18;
    if( phys_myst != 50 ) _dragMouse( 102, ypos, (int)_linIinterp( 64, 141, phys_myst / 100.0f ), ypos );
    ypos += 18;
    if( headon_stealth != 50 ) _dragMouse( 102, ypos, (int)_linIinterp( 64, 141, headon_stealth / 100.0f ), ypos );
    ypos += 18;
    if( money_xp != 50 ) _dragMouse( 102, ypos, (int)_linIinterp( 64, 141, money_xp / 100.0f ), ypos );
}