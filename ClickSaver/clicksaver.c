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
#include <commctrl.h>
#include <windowsx.h>
#include "clicksaver.h"
#include "resource.h"

//#include "BerkeleyDB/db.h"
#include "sqlite3.h"
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")

// SQLite Globals
sqlite3*      g_pSQLite = NULL;
sqlite3_stmt* g_stmtItem = NULL;
sqlite3_stmt* g_stmtIcon = NULL;
sqlite3_stmt* g_stmtPF = NULL;

// Forward declarations for item name cache
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

extern PUU8 g_bForceUIRefresh;
extern PUU32 g_GUIDef[];
pusObjectCollection* g_pCol;
PULID g_ItemWatchList, g_LocWatchList, g_MainWin;
PULID g_DisabledItemWatchList;

void _setSliders( int easy_hard, int good_bad, int order_chaos, int open_hidden, int phys_myst, int headon_stealth, int money_xp );

PUU32 g_BuyingAgentCount = 0;
PUU32 g_BuyingAgentDelay = 5010;
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

char g_CurrentPacket[ 65536 ];

char g_AODir[ MAX_PATH ] = { 0 };
char g_CSDir[ MAX_PATH ] = { 0 };

HANDLE g_Mutex = INVALID_HANDLE_VALUE;
HANDLE g_Thread = INVALID_HANDLE_VALUE;
HANDLE g_hThreadExitEvent = NULL;
HANDLE g_hAbortEvent = NULL;      // kept for compatibility, not used in timer version
DWORD WINAPI HookManagerThread( void *pParam );

//DB* g_pDB = NULL;

// Helper: Show a modal message box that appears on top of the topmost main window
static int ShowModalMessage(HWND hParent, const char* text, const char* caption, UINT type)
{
    // If no parent given, use the main window
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

// Forward declarations for item string helpers
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

// ========== TIMER BASED BUYING AGENT ==========
#define TIMER_BUYINGAGENT 1
static UINT_PTR g_TimerID = 0;
static int g_PendingAttemptNumber = 0;

// Data exchange for the native dialog
typedef struct {
    char itemName[256];
    int  limit;
    int  disabled;
    int  force;
    char exclude[256];
    int  isAdd;
} ItemEditData;

// Structure to pass data to the match list dialog
typedef struct {
    const char **matches;
    int count;
    char selected[256];
    char originalSearch[256];
    char excludeWords[256];
} MatchListData;

static INT_PTR CALLBACK MatchListDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    MatchListData *pData = (MatchListData*)GetWindowLongPtr(hDlg, DWLP_USER);

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        pData = (MatchListData*)lParam;
        SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)pData);

        HWND hList = GetDlgItem(hDlg, IDC_MATCH_LIST);
        for (int i = 0; i < pData->count; i++) {
            SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)pData->matches[i]);
        }

        // Build hint text
        char hint[512];
        if (pData->excludeWords[0] != '\0') {
            sprintf(hint, "Hint: You can Double click an item in the lsit to select it.\n\nHint: Using the original search term \"%s\" with exclusions (%s) will match %d item(s).",
                    pData->originalSearch, pData->excludeWords, pData->count);
        } else {
            sprintf(hint, "Hint: You can Double click an item in the lsit to select it.\n\nHint: Using the original search term \"%s\" will match %d item(s).",
                    pData->originalSearch, pData->count);
        }
        SetDlgItemTextA(hDlg, IDC_MATCH_HINT, hint);

        SetFocus(GetDlgItem(hDlg, IDC_MATCH_EDIT));
        return FALSE;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDC_USE_ORIGINAL) {
            pData->selected[0] = '\0';
            EndDialog(hDlg, IDC_USE_ORIGINAL);
            return TRUE;
        }

        if (LOWORD(wParam) == IDC_USE_TYPED) {
            char typed[256];
            GetDlgItemTextA(hDlg, IDC_MATCH_EDIT, typed, sizeof(typed));
            if (strlen(typed) == 0) {
                MessageBox(hDlg, "Please enter a name or click Cancel.", "Empty Name", MB_OK | MB_ICONWARNING);
                return TRUE;
            }

            // Check how many items this typed name would match (using same exclude words)
            int newMatchCount = 0;
            const char **newMatches = NULL;
            GetFilteredMatchingItems(typed, pData->excludeWords, &newMatches, &newMatchCount);
            free((void*)newMatches);

            char msg[512];
            sprintf(msg, "Your typed name \"%s\" would match %d item(s).\n\nUse this name?", typed, newMatchCount);
            if (MessageBox(hDlg, msg, "Confirm Typed Name", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                strcpy(pData->selected, typed);
                EndDialog(hDlg, IDOK);
            }
            return TRUE;
        }

        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }

        if (LOWORD(wParam) == IDC_MATCH_LIST && HIWORD(wParam) == LBN_DBLCLK) {
            HWND hList = GetDlgItem(hDlg, IDC_MATCH_LIST);
            int sel = SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                SendMessageA(hList, LB_GETTEXT, sel, (LPARAM)pData->selected);
                EndDialog(hDlg, IDOK);
            }
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

// Forward declarations
INT_PTR CALLBACK ItemEditDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
int ShowItemEditDialog(HWND hParent, ItemEditData *pData, int bIsAddMode);

// Dialog procedure for the native edit/add dialog
INT_PTR CALLBACK ItemEditDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ItemEditData *pData = (ItemEditData*)GetWindowLongPtr(hDlg, DWLP_USER);
    static HBRUSH hBrush = NULL;

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        pData = (ItemEditData*)lParam;
        SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)pData);

        // Set the dialog title
        if (pData->isAdd)
            SetWindowTextA(hDlg, "Add Item");
        else
            SetWindowTextA(hDlg, "Edit Item");

        SetDlgItemTextA(hDlg, IDC_ITEM_NAME, pData->itemName);
        SetDlgItemInt(hDlg, IDC_LIMIT, pData->limit, FALSE);
        CheckDlgButton(hDlg, IDC_FORCE, pData->force ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemTextA(hDlg, IDC_EXCLUDE, pData->exclude);

        hBrush = CreateSolidBrush(RGB(240, 240, 240));
        return TRUE;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
        if (hBrush)
        {
            SetBkMode((HDC)wParam, TRANSPARENT);
            SetTextColor((HDC)wParam, RGB(0, 0, 0));
            return (INT_PTR)hBrush;
        }
        break;

    case WM_DESTROY:
			if (hBrush) DeleteObject(hBrush);
		break;

    case WM_COMMAND:
    {
        WORD wID = LOWORD(wParam);
        WORD wNotify = HIWORD(wParam);
        HWND hCtrl = (HWND)lParam;

        switch (wID)
        {
        case IDOK:
			{
				char enteredName[256];
				GetDlgItemTextA(hDlg, IDC_ITEM_NAME, enteredName, sizeof(enteredName));
			
				// Trim leading/trailing spaces
				char *start = enteredName;
				while (*start == ' ') start++;
				char *end = start + strlen(start) - 1;
				while (end > start && *end == ' ') end--;
				*(end + 1) = '\0';
			
				if (strlen(start) == 0) {
					MessageBox(hDlg, "Item name cannot be empty.", "Validation", MB_OK | MB_ICONWARNING);
					return TRUE;
				}
			
				// Get exclude words from the dialog
				char excludeTemp[256];
				GetDlgItemTextA(hDlg, IDC_EXCLUDE, excludeTemp, sizeof(excludeTemp));
			
				// Get filtered list of matching items (respects exclusions)
				int matchCount = 0;
				const char **matches = NULL;
				GetFilteredMatchingItems(start, excludeTemp, &matches, &matchCount);
			
				if (matchCount > 0) {
					char msg[256];
					sprintf(msg, "That would match %d item(s).\n\nBefore we add/update it, would you like to see a list of those possible matches?", matchCount);
					int answer = MessageBox(hDlg, msg, "Multiple Matches", MB_YESNO | MB_ICONQUESTION);
			
					if (answer == IDYES) {
						MatchListData data;
						data.matches = matches;
						data.count = matchCount;
						data.selected[0] = '\0';
						strcpy(data.originalSearch, start);
						strcpy(data.excludeWords, excludeTemp);
			
						INT_PTR result = DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_MATCH_LIST),
														hDlg, MatchListDlgProc, (LPARAM)&data);
						if (result == IDOK && data.selected[0] != '\0') {
							// User double‑clicked an item – replace the name
							strcpy(start, data.selected);
							SetDlgItemTextA(hDlg, IDC_ITEM_NAME, start);
						} else if (result == IDC_USE_ORIGINAL) {
							// User wants to keep the original name
						} else {
							// User cancelled
							free((void*)matches);
							return TRUE;
						}
					}
					free((void*)matches);
				} else {
					// No matches – existing warning
					char msg[512];
					sprintf(msg, "Item \"%s\" would not match any known item.\n\nAdd it anyway?", start);
					if (MessageBox(hDlg, msg, "Unknown Item", MB_YESNO | MB_ICONQUESTION) != IDYES)
						return TRUE;
				}
			
				// Copy trimmed name back
				strcpy(pData->itemName, start);
			
				// Read other fields
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
    }
    return FALSE;
}

static INT_PTR CALLBACK MassAddDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        SetFocus(GetDlgItem(hDlg, IDC_MASS_EDIT));
        return FALSE;   // let system set focus

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
			
        case IDOK:
			{
				char text[65536];
				GetDlgItemTextA(hDlg, IDC_MASS_EDIT, text, sizeof(text));
			
				char *p = text;
                char line[1024] = { 0 };
				int lineIdx;
			
				while (*p)
				{
					// Extract a line
					lineIdx = 0;
					while (*p && *p != '\r' && *p != '\n')
					{
						if (lineIdx < (int)sizeof(line)-1)
							line[lineIdx++] = *p;
						p++;
					}
					line[lineIdx] = '\0';
			
					// Skip empty lines
					if (lineIdx == 0)
					{
						while (*p == '\r' || *p == '\n') p++;
						continue;
					}
			
					// Trim leading/trailing spaces from the line
					char *start = line;
					while (*start == ' ' || *start == '\t') start++;
					char *end = start + strlen(start) - 1;
					while (end > start && (*end == ' ' || *end == '\t')) end--;
					*(end + 1) = '\0';
					if (start != line) memmove(line, start, end - start + 2);
			
					if (strlen(line) == 0)
					{
						while (*p == '\r' || *p == '\n') p++;
						continue;
					}
			
					// ---------- Parse the line ----------
					char *ptr = line;
					int disabled = 0, force = 0;
			
					// Optional leading '#'
					if (*ptr == '#')
					{
						disabled = 1;
						ptr++;
						while (*ptr == ' ' || *ptr == '\t') ptr++;
					}
					// Optional leading '~'
					if (*ptr == '~')
					{
						force = 1;
						ptr++;
						while (*ptr == ' ' || *ptr == '\t') ptr++;
					}
			
					// Now parse: item name, then optional ';limit', then optional '^exclude' tokens
					char itemName[256] = { 0 };
					itemName[0] = '\0';
					int limit = 1;          // default limit
					char excludeWords[256] = { 0 };
					excludeWords[0] = '\0';
			
					// ---- Collect item name: stop at ';' or '^' or end of string ----
					char *nameStart = ptr;
					char *nameEnd = nameStart;
					while (*nameEnd && *nameEnd != ';' && *nameEnd != '^')
						nameEnd++;
			
					// Copy the name
					int nameLen = (int)(nameEnd - nameStart);
					if (nameLen >= (int)sizeof(itemName)) nameLen = sizeof(itemName)-1;
					strncpy(itemName, nameStart, nameLen);
					itemName[nameLen] = '\0';
			
					// Trim trailing spaces from name
					char *trimEnd = itemName + strlen(itemName) - 1;
					while (trimEnd >= itemName && (*trimEnd == ' ' || *trimEnd == '\t'))
						*trimEnd-- = '\0';
			
					// Move ptr to after the name
					ptr = nameEnd;
			
					// ---- Parse quantity limit if ';' ----
					if (*ptr == ';')
					{
						ptr++;
						limit = atoi(ptr);
						if (limit < 0) limit = 0;
						// Advance ptr past the number
						while (*ptr && *ptr != ' ' && *ptr != '^') ptr++;
					}
			
					// ---- Parse exclude words (each starts with '^') ----
					// They appear as " ^word1 ^word2 ..."
					while (*ptr)
					{
						// Skip spaces
						while (*ptr == ' ') ptr++;
						if (*ptr == '^')
						{
							ptr++; // skip '^'
							// Skip any spaces after caret (optional)
							while (*ptr == ' ') ptr++;
							// Collect the exclude word (until next space or end)
							char word[128] = { 0 };
							int wlen = 0;
							while (*ptr && *ptr != ' ' && *ptr != '^')
							{
								if (wlen < (int)sizeof(word)-1)
									word[wlen++] = *ptr;
								ptr++;
							}
							word[wlen] = '\0';
							if (wlen > 0)
							{
								// Add to excludeWords list (space separated)
								if (excludeWords[0] != '\0')
									strcat(excludeWords, " ");
								strcat(excludeWords, word);
							}
						}
						else
						{
							// Unexpected character – break to avoid infinite loop
							break;
						}
					}
			
					// If the item name is empty, skip this line
					if (strlen(itemName) == 0)
					{
						while (*p == '\r' || *p == '\n') p++;
						continue;
					}
			
					// Build the raw string using the new '^' exclude marker
					char raw[512];
					BuildItemString(raw, sizeof(raw), itemName, disabled, force, limit, excludeWords);
			
					// Format for display (shows exclude words prefixed with "exclude: ...")
					char display[1024];
					FormatItemForDisplay(raw, display, sizeof(display));
			
					// Add to active watchlist
					puDoMethod(g_ItemWatchList, PUM_TABLE_NEWRECORD, 0, 0);
					puDoMethod(g_ItemWatchList, PUM_TABLE_ADDRECORD, 0, 0);
					puDoMethod(g_ItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)display, 0);
			
					// Skip any trailing newline characters
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

// Show the dialog and return 1 if OK, 0 if cancelled
int ShowItemEditDialog(HWND hParent, ItemEditData *pData, int bIsAddMode)
{
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hParent, GWLP_HINSTANCE);
    // If we can't get instance from parent, fallback to the global one
    if (!hInst) hInst = GetModuleHandle(NULL);
    
    INT_PTR result = DialogBoxParamA(hInst, MAKEINTRESOURCEA(IDD_ITEM_EDIT),
                                     hParent, ItemEditDlgProc, (LPARAM)pData);
    return (result == IDOK) ? 1 : 0;
}

// Forward declaration for the subclass procedure
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
// ===============================================


// ========== Helper functions for item string format ==========
void BuildItemString(char *dest, size_t destSize,
                     const char *itemName,
                     int disabled,
                     int forceAccept,
                     int quantityLimit,
                     const char *excludeWords)
{
    dest[0] = '\0';
    if (disabled) strncat(dest, "#", destSize - strlen(dest) - 1);
    if (forceAccept) strncat(dest, "~", destSize - strlen(dest) - 1);
    strncat(dest, itemName, destSize - strlen(dest) - 1);
    if (quantityLimit > 0) {
        char buf[16];
        sprintf(buf, ";%d", quantityLimit);
        strncat(dest, buf, destSize - strlen(dest) - 1);
    }
    if (excludeWords && *excludeWords) {
        char *tmp = _strdup(excludeWords);
        char *token = strtok(tmp, ", ");
        while (token) {
            strncat(dest, " ^", destSize - strlen(dest) - 2);
            strncat(dest, token, destSize - strlen(dest) - 1);
            token = strtok(NULL, ", ");
        }
        free(tmp);
    }
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

    // Skip leading # and ~
    while (*p == '#' || *p == '~') {
        if (*p == '#') *disabled = 1;
        if (*p == '~') *forceAccept = 1;
        p++;
    }

    // Find end of item name – stop at ';' or '^' (was '-')
    const char *nameStart = p;
    const char *nameEnd = nameStart;
    while (*nameEnd && *nameEnd != ';' && *nameEnd != '^')
        nameEnd++;

    // Trim trailing spaces from the name
    while (nameEnd > nameStart && *(nameEnd - 1) == ' ')
        nameEnd--;

    int nameLen = (int)(nameEnd - nameStart);
    if (nameLen >= itemNameSize) nameLen = itemNameSize - 1;
    strncpy(itemName, nameStart, nameLen);
    itemName[nameLen] = '\0';

    p = nameEnd;

    // Parse quantity limit if present (;N)
    if (*p == ';') {
        p++;
        *quantityLimit = atoi(p);
        while (*p && *p != ' ' && *p != '^') p++;
    }

    while (*p == ' ') p++;

    // Parse exclude words – each starts with '^' (was '-')
    if (excludeWords && excludeSize > 0) {
        excludeWords[0] = '\0';
        while (*p == '^') {
            p++; // skip the caret
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

// Convert raw item string to a human-readable display label.
// Format: ItemName [disabled] [force accept] [qty N] [exclude: word1, word2]
// Only the tags that are actually set are shown.
void FormatItemForDisplay(const char *raw, char *out, size_t outSize)
{
    char itemName[256];
    int disabled = 0, force = 0, limit = 0;
    char exclude[256];

    ParseItemString(raw, itemName, sizeof(itemName), &disabled, &force, &limit, exclude, sizeof(exclude));

    out[0] = '\0';
    strncat(out, itemName, outSize - strlen(out) - 1);

    if (disabled)
        strncat(out, " [disabled]", outSize - strlen(out) - 1);
    if (force)
        strncat(out, " [force accept]", outSize - strlen(out) - 1);
    if (limit > 0) {
        char buf[32];
        sprintf(buf, " [qty %d]", limit);
        strncat(out, buf, outSize - strlen(out) - 1);
    }
    if (exclude[0]) {
        // Build comma-separated list from space-separated exclude words
        char buf[512];
        char tmp[256];
        strncpy(tmp, exclude, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';

        char *tok = strtok(tmp, " ");
        char formatted[256] = "";
        while (tok) {
            if (formatted[0]) strncat(formatted, ", ", sizeof(formatted) - strlen(formatted) - 1);
            strncat(formatted, tok, sizeof(formatted) - strlen(formatted) - 1);
            tok = strtok(NULL, " ");
        }
        sprintf(buf, " [exclude: %s]", formatted);
        strncat(out, buf, outSize - strlen(out) - 1);
    }
}

// Build the string stored in the item watchlist table.
// The visible portion (before \x01) is the formatted display label.
// The raw portion (after \x01) is the original encoded string used by all logic.
// PUL renders up to the first non-printable char, so \x01 acts as a hidden separator.
// Build only the formatted display string (no \x01, no raw)
// ========== NEW: Store only formatted display string ==========
void MakeTableEntry(char *dest, size_t destSize, const char *raw)
{
    // Just format the raw string as human-readable display
    FormatItemForDisplay(raw, dest, destSize);
}

// Parse a display string (e.g. "Staff [qty 3] [exclude: rotten]") back into fields
// Returns 0 on success, -1 if parsing fails
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

    // Work on a copy
    char buf[1024];
    strncpy(buf, display, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';

    // --- 1. Item name: everything up to first '[' or end ---
    char *nameEnd = strchr(buf, '[');
    if (!nameEnd) nameEnd = buf + strlen(buf);
    size_t nameLen = nameEnd - buf;
    // trim trailing spaces
    while (nameLen > 0 && buf[nameLen-1] == ' ') nameLen--;
    if (nameLen >= itemNameSize) nameLen = itemNameSize-1;
    strncpy(itemName, buf, nameLen);
    itemName[nameLen] = '\0';

    // --- 2. Parse optional tags ---
    char *p = buf + nameLen;
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
                // Convert commas to spaces (our internal format uses space separated)
                for (char *c = excludeWords; *c; c++)
                    if (*c == ',') *c = ' ';
            }
            p = end;
            if (*p == ']') p++;
        }
        else {
            // Unknown tag – stop
            break;
        }
    }
    return 0;
}

// Move the currently selected item from active list to disabled list
static void MoveCurrentActiveToDisabled(void)
{
    PUU32 listView = puGetObjectFromCollection(g_pCol, CS_ITEMWATCH_LISTVIEW);
    int selectedIndex = (int)puGetAttribute(listView, PUA_LISTVIEW_SELECTED);
    if (selectedIndex < 0) {
        ShowModalMessage(NULL, "No item selected.", "ClickSaver", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // Get the record from the active table using the selected index
    PUU32 record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    for (int i = 0; i < selectedIndex && record; i++)
        record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETNEXTRECORD, record, 0);
    if (!record) {
        ShowModalMessage(NULL, "No item selected or could not find the selected record.", "ClickSaver", MB_OK | MB_ICONWARNING);
        return;
    }

    // Get the display string
    PUU8 *display = (PUU8*)puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIELDVAL, record, 0);
    if (!display || !*display) return;

    // Add a copy to the disabled table
    puDoMethod(g_DisabledItemWatchList, PUM_TABLE_NEWRECORD, 0, 0);
    puDoMethod(g_DisabledItemWatchList, PUM_TABLE_ADDRECORD, 0, 0);
    puDoMethod(g_DisabledItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)display, 0);

    // Remove the selected row from the active listview (also deletes the table record)
    puDoMethod(listView, PUM_LISTVIEW_REMOVE, 0, 0);

    // Determine the new selection index
    // Get the number of rows by checking the last possible selected index
    int maxRows = (int)puGetAttribute(listView, PUA_LISTVIEW_SELECTED);
    // If the listview is empty, set to -1
    if (maxRows == -1) {
        puSetAttribute(listView, PUA_LISTVIEW_SELECTED, -1);
        return;
    }
    // The new index is the same as the original if it's still within range, else the last row
    int newIndex = selectedIndex;
    if (newIndex >= maxRows) {
        newIndex = maxRows - 1;
    }
    puSetAttribute(listView, PUA_LISTVIEW_SELECTED, newIndex);
}

static void MoveCurrentDisabledToActive(void)
{
    PUU32 listView = puGetObjectFromCollection(g_pCol, CS_DISABLED_ITEMWATCH_LISTVIEW);
    int selectedIndex = (int)puGetAttribute(listView, PUA_LISTVIEW_SELECTED);
    if (selectedIndex < 0) {
        ShowModalMessage(NULL, "No item selected in disabled list.", "ClickSaver", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // Get the record from the disabled table
    PUU32 record = puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    for (int i = 0; i < selectedIndex && record; i++)
        record = puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETNEXTRECORD, record, 0);
    if (!record) {
        ShowModalMessage(NULL, "No item selected or could not find the selected disabled record.", "ClickSaver", MB_OK | MB_ICONWARNING);
        return;
    }

    PUU8 *display = (PUU8*)puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETFIELDVAL, record, 0);
    if (!display || !*display) return;

    // Add to active table
    puDoMethod(g_ItemWatchList, PUM_TABLE_NEWRECORD, 0, 0);
    puDoMethod(g_ItemWatchList, PUM_TABLE_ADDRECORD, 0, 0);
    puDoMethod(g_ItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)display, 0);

    // Remove from disabled listview (and its table)
    puDoMethod(listView, PUM_LISTVIEW_REMOVE, 0, 0);

    // Determine new selection in the disabled list
    int maxRows = (int)puGetAttribute(listView, PUA_LISTVIEW_SELECTED);
    if (maxRows == -1) {
        puSetAttribute(listView, PUA_LISTVIEW_SELECTED, -1);
        return;
    }
    int newIndex = selectedIndex;
    if (newIndex >= maxRows) {
        newIndex = maxRows - 1;
    }
    puSetAttribute(listView, PUA_LISTVIEW_SELECTED, newIndex);
}

static void RemoveDuplicateItems(void) {
    // We'll build a hash set of display strings, keeping first occurrence.
    // Simple O(n^2) for small lists is fine; if list large, use temporary array.
    PUU32 record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    PUU32 prevRecord = 0;
    int removed = 0;

    // We'll traverse and remove duplicates by comparing each record with all previous ones.
    // Simpler: collect all display strings into an array, then rebuild table.
    // But rebuilding might lose order and cause UI issues. Instead, two-pass.

    // First pass: collect unique display strings
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

    // Rebuild the table from unique list
    // Clear table
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

    // Refresh listview
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
        // Confirm? Already handled in UI, but safe to double-check
        if (ShowModalMessage(NULL, "Replace will delete all current items. Continue?", 
                       "Confirm Replace", MB_YESNO) != IDYES)
            return;
        // Clear active watchlist
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
        // Trim newline
        line[strcspn(line, "\r\n")] = 0;

        if (strcmp(line, "::ItemWatch::") == 0) {
            inItemSection = 1;
            continue;
        }
        if (strcmp(line, "::END::") == 0 || strncmp(line, "::", 2) == 0)
            break;
        if (!inItemSection) continue;

        // Skip empty lines
        if (strlen(line) == 0) continue;

        // Convert raw line to display string
        char display[1024];
        MakeTableEntry(display, sizeof(display), line);

        // Duplicate check (only for append mode; replace already cleared list)
        if (!replaceMode && ItemExistsInActiveList(display)) {
            duplicateCount++;
            continue;
        }

        // Add item
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
    strcpy(fullpath, filename);
    
    // Append .cs if not already present (case-insensitive)
    size_t len = strlen(fullpath);
    if (len < 3 || _stricmp(fullpath + len - 3, ".cs") != 0)
        strcat(fullpath, ".cs");
    
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

// ========== ADDED FOR QUANTITY LIMITS ==========
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
    new->itemName = _strdup(name);
    new->limit = limit;
    new->accepted = 0;
    new->next = g_ItemCounters;
    g_ItemCounters = new;
}

static void ClearItemCounters() {
    // Never clear counters while a buying session is active
    if (g_bBuyingAgentActive) {
        return;
    }
    // Additional guard: if we still have tries or missions pending, don't clear
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
// ===============================================

typedef enum ImportSettingsMode
{
    ISM_CONFIG,
    ISM_LOCWATCH,
    ISM_ITEMWATCH,
	ISM_DISABLED_ITEMWATCH,
    ISM_SLIDERS,
    ISM_DONE,
} ImportSettingsMode;

int OpenLocalDB()
{
	char DBPath[MAX_PATH];
	sprintf(DBPath, "%s\\cd_image\\rdb.db", g_AODir);

	// 1. Open SQLite handle
if (sqlite3_open_v2(DBPath, &g_pSQLite, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
    // This will succeed even if AO is running
    return FALSE;
}


	// 2. Pre-prepare statements for the 3 specific tables
	sqlite3_prepare_v2(g_pSQLite, "SELECT data FROM rdb_1000020 WHERE id = ?;", -1, &g_stmtItem, NULL);
	sqlite3_prepare_v2(g_pSQLite, "SELECT data FROM rdb_1010008 WHERE id = ?;", -1, &g_stmtIcon, NULL);
	sqlite3_prepare_v2(g_pSQLite, "SELECT data FROM rdb_1000001 WHERE id = ?;", -1, &g_stmtPF, NULL);

	return TRUE;
}

void* GetDataChunk(PUU32 _KeyHi, PUU32 _KeyLo, PUU32* _pSize)
{
	sqlite3_stmt* pStmt = NULL;
	void* pReturnData = NULL;

	// 1. Select the correct prepared statement
	// Tables: Items (1000020), Icons (1010008), Playfields (1000001)
	switch (_KeyHi) {
	case AODB_TYP_ITEM: pStmt = g_stmtItem; break;
	case AODB_TYP_ICON: pStmt = g_stmtIcon; break;
	case AODB_TYP_PF:   pStmt = g_stmtPF;   break;
	default: return NULL;
	}

	if (!pStmt) return NULL;

	// 2. Reset and Bind Key
	sqlite3_reset(pStmt);
	// Use _KeyLo directly as an integer. SQLite handles the byte order.
	sqlite3_bind_int(pStmt, 1, (int)_KeyLo);

	// 3. Execute
	if (sqlite3_step(pStmt) == SQLITE_ROW) {
		const unsigned char* blob = (const unsigned char*)sqlite3_column_blob(pStmt, 0);
		int blobSize = sqlite3_column_bytes(pStmt, 0);

		if (!blob || blobSize <= 0) return NULL;

		// --- TYPE 1: ITEMS (Parse into MissionItem struct) ---
		if (_KeyHi == AODB_TYP_ITEM) {
			MissionItem* pItem = (MissionItem*)malloc(sizeof(MissionItem));
			if (!pItem) return NULL;
			memset(pItem, 0, sizeof(MissionItem));

			// Scan tags for QL, Icon, and Value
			for (int i = 4; i + 8 <= blobSize; i += 8) {
				PUU32 tag = *(PUU32*)(blob + i);
				PUU32 val = *(PUU32*)(blob + i + 4);
				switch (tag) {
				case 0x36: pItem->QL = val; break;
				case 0x4F: pItem->IconKey = val; break;
				case 0x4A: pItem->Value = val; break;
				}
			}

			// Find Name Signature: 15 00 00 00 21 00 00 00
			for (int i = 0; i + 12 <= blobSize; i++) {
				if (*(PUU32*)(blob + i) == 0x15 && *(PUU32*)(blob + i + 4) == 0x21) {
					unsigned short nameLen = *(unsigned short*)(blob + i + 8);
					if (nameLen > AODB_MAX_NAME_LEN) nameLen = AODB_MAX_NAME_LEN;
					memcpy(pItem->pName, blob + i + 12, nameLen);
					pItem->pName[nameLen] = 0;
					break;
				}
			}

			pReturnData = pItem;
			if (_pSize) *_pSize = sizeof(MissionItem);
		}

		// --- TYPE 2: PLAYFIELDS (Skip 8-byte header) ---
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

		// --- TYPE 3: ICONS (Return raw PNG) ---
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
            // Note: 'disabled' flag is now only set if the raw string contains '#'
            // But since we separate physically, we ignore it here. Keep the limit logic.
            if (limit == 0)
                return TRUE;    // unlimited active item
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

// ========== WINDOW SUBCLASS FOR TIMER HANDLING ==========
LRESULT CALLBACK MainWndProcHook( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
    if (uMsg == WM_TIMER && wParam == TIMER_BUYINGAGENT)
    {
        // Timer expired – kill it and post an application message to the main loop
        KillTimer( hWnd, TIMER_BUYINGAGENT );
        g_TimerID = 0;
        puPostAppMessage( CSAM_BUYINGAGENT_TIMER, 0);
        return 0;
    }
    return DefSubclassProc( hWnd, uMsg, wParam, lParam );
}
// ========================================================

void ReleaseAODatabase(void)
{
	if (g_stmtItem) sqlite3_finalize(g_stmtItem);
	if (g_stmtIcon) sqlite3_finalize(g_stmtIcon);
	if (g_stmtPF)   sqlite3_finalize(g_stmtPF);
	if (g_pSQLite)  sqlite3_close(g_pSQLite);
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
    //int bUpdateDB = FALSE;
    char DBPath[ 256 * 2 ];

    // Set main thread of clicksaver on a priority above normal
    // Helps a lot. Refreshing of missions infos is much faster.
    SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL );

    // Initialise PUL
    if( !puInit() )
    {
        return -1;
    }

    // Register mission control class
    if( !RegisterMissionClass() )
    {
        CleanUp();
        return -1;
    }

    // Create the windows
    if( !( g_pCol = puCreateObjectCollection( g_GUIDef ) ) )
    {
        CleanUp();
        return -1;
    }

    g_MainWin = puGetObjectFromCollection( g_pCol, CS_MAIN_WINDOW );
	g_ItemWatchList = puGetObjectFromCollection( g_pCol, CS_ITEMWATCH_LIST );
	g_DisabledItemWatchList = puGetObjectFromCollection( g_pCol, CS_DISABLED_ITEMWATCH_LIST );
	g_LocWatchList = puGetObjectFromCollection( g_pCol, CS_LOCWATCH_LIST );

    // Get current directory
    GetCurrentDirectory( MAX_PATH, g_CSDir );

    ImportSettings( "LastSettings.cs" );
	
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

	// Construct the path to check if rdb.db exists
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

    // Create mutex
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
            // send some message
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
	
    // Starts dll hook management thread
    if( ( g_Thread = CreateThread( NULL, 0, &HookManagerThread, NULL, 0, &dwThreadID ) ) == INVALID_HANDLE_VALUE )
    {
        DisplayErrorMessage( "Couldn't create hook thread.", FALSE );
        ReleaseAODatabase();
        CleanUp();
        return -1;
    }
	
char cachePath[MAX_PATH];
sprintf(cachePath, "%s\\ItemNames.db", g_CSDir);
if (!LoadItemNameCache(cachePath)) {
    //MessageBox(NULL, "Building item name cache (one-time operation).\nPlease wait...", "ClickSaver", MB_OK);
    BuildItemNameCache(cachePath);
    if (!LoadItemNameCache(cachePath)) {
        //MessageBox(NULL, "Warning: Could not build or load item name cache.\nShort item names will not be filtered.", "ClickSaver", MB_OK);
    }
}

    MissionControls[ 0 ] = puGetObjectFromCollection( g_pCol, CS_MISSION1 );
    MissionControls[ 1 ] = puGetObjectFromCollection( g_pCol, CS_MISSION2 );
    MissionControls[ 2 ] = puGetObjectFromCollection( g_pCol, CS_MISSION3 );
    MissionControls[ 3 ] = puGetObjectFromCollection( g_pCol, CS_MISSION4 );
    MissionControls[ 4 ] = puGetObjectFromCollection( g_pCol, CS_MISSION5 );
    //puSetAttribute( puGetObjectFromCollection( g_pCol, CS_OPTIONSFOLD3 ), PUA_FOLD_FOLDED, TRUE);
    puSetAttribute( g_MainWin, PUA_WINDOW_OPENED, TRUE );

    // Subclass the main window to catch WM_TIMER
    HWND hMainWnd = (HWND)puGetAttribute( g_MainWin, PUA_WINDOW_HANDLE );
    SetWindowSubclass( hMainWnd, MainWndProcHook, 0, 0 );

    HICON hIcon = LoadIcon( GetModuleHandle( NULL ), MAKEINTRESOURCE( IDI_ICON1 ) );
    if( hIcon )
    {
        PUU32 uWindowHandle = puGetAttribute( g_MainWin, PUA_WINDOW_HANDLE );
        SendMessage( (HWND)uWindowHandle, WM_SETICON, ICON_BIG,   (LPARAM)hIcon );
        SendMessage( (HWND)uWindowHandle, WM_SETICON, ICON_SMALL, (LPARAM)hIcon );
    }

    do
    {
        pAppMsg = puWaitAppMessages();

        switch( pAppMsg->Message )
        {
		case CSAM_EDIT_ITEM:
			{
				// Get selected row
				PUU32 listView = puGetObjectFromCollection(g_pCol, CS_ITEMWATCH_LISTVIEW);
				int selectedIndex = (int)puGetAttribute(listView, PUA_LISTVIEW_SELECTED);
			
				if (selectedIndex < 0) {
					ShowModalMessage(NULL, "No item selected.\n\nPlease click on an item first, then click Edit.", 
							"ClickSaver", MB_OK | MB_ICONINFORMATION);
					break;
				}
			
				// Walk the table to get the record key
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
					   
				data.isAdd = 0;   // editing existing item
			
				// Show native dialog
				HWND hMainWnd = (HWND)puGetAttribute(g_MainWin, PUA_WINDOW_HANDLE);
				if (ShowItemEditDialog(hMainWnd, &data, 0)) {
					char rawStr[512];
					BuildItemString(rawStr, sizeof(rawStr), data.itemName, data.disabled, data.force, data.limit, data.exclude);
					char newDisplay[1024];
					// **** Store only the formatted display string ****
					FormatItemForDisplay(rawStr, newDisplay, sizeof(newDisplay));
					puSetAttribute(g_ItemWatchList, PUA_TABLE_CURRENTFIELD, 0);
					puSetAttribute(g_ItemWatchList, PUA_TABLE_CURRENTRECORD, recordKey);
					puDoMethod(g_ItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)newDisplay, 0);
				}
				break;
			}
		case CSAM_DISABLE_ITEM:
            MoveCurrentActiveToDisabled();
            puSetAttribute(puGetObjectFromCollection(g_pCol, CS_ITEMWATCH_LISTVIEW), PUA_LISTVIEW_SELECTED, -1);
            break;

        case CSAM_ENABLE_ITEM:
            MoveCurrentDisabledToActive();
            puSetAttribute(puGetObjectFromCollection(g_pCol, CS_DISABLED_ITEMWATCH_LISTVIEW), PUA_LISTVIEW_SELECTED, -1);
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
                // Clear the table by removing every record
                PUU32 record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
                while (record)
                {
                    puDoMethod(g_ItemWatchList, PUM_TABLE_REMRECORD, record, 0);
                    record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
                }
                // Update the listview (it should refresh automatically via PUL)
                // Force a redraw of the listview control
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
				// Prepare empty data
				ItemEditData data;
				memset(&data, 0, sizeof(data));
				strcpy(data.itemName, "");
				data.limit = 1;
				data.force = 0;
				strcpy(data.exclude, "");
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
					if (newDelay >= 5010 && newDelay <= 10000) {   // changed lower bound to 5010
						g_BuyingAgentDelay = newDelay;
					}
				}
				break;
			}
			
        case CSAM_STOPBUYINGAGENT:
                // Kill any pending timer
                if (g_TimerID) {
                    KillTimer( hMainWnd, TIMER_BUYINGAGENT );
                    g_TimerID = 0;
                }
                g_BuyingAgentCount = 0;
                g_BuyingAgentMissions = 0;
                g_BuyingAgentMaxTries = 0;
                g_BuyingAgentMaxMissions = 0;
                g_TotalAttempts = 0;
                puSetAttribute(puGetObjectFromCollection(g_pCol, CS_BA_PROGRESS), PUA_TEXT_STRING, (PUU32)"");
                puSetAttribute(puGetObjectFromCollection(g_pCol, CS_BA_TOTAL), PUA_TEXT_STRING, (PUU32)"");
                puSetAttribute(puGetObjectFromCollection(g_pCol, CS_BA_ACCEPTED), PUA_TEXT_STRING, (PUU32)"");
                EndBuyingAgent();   // This will clear counters and set g_bBuyingAgentActive = 0
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
        
                // Update button text and status label
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
        
                // If resuming and there are remaining attempts, start a new timer (if not already pending)
                if (wasPaused && !g_bPaused && g_BuyingAgentCount > 0 && g_TimerID == 0) {
                    BuyingAgent(g_BuyingAgentDelay);
                }
            }
            break;

        case CSAM_BUYINGAGENT_TIMER:
            // Timer expired – send the click if not paused
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

                // Decrement counters after sending click
                g_BuyingAgentCount--;
                g_TotalAttempts++;

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
                // If paused, do nothing this cycle
                if (g_bPaused) break;
                // Before rolling, check if there are still active items
                if( PUL_GET_CB(CS_ALERTITEM_CB) && !HasActiveWatchlistItems() ) {
					PlaySound( "notfound.wav", NULL, SND_FILENAME | SND_NODEFAULT );
					MessageBox( NULL, "All watched items have reached their quantity limits. Stopping.", "ClickSaver", MB_OK | MB_ICONINFORMATION | MB_SYSTEMMODAL );
					g_BuyingAgentCount = 0;
					g_BuyingAgentMissions = 0;
					EndBuyingAgent();
					break;
				}
                
                // Disable redraw on main window (only if not in fullscreen)
                HWND hMainWnd = NULL;
                if (!g_bFullscreen) {
                    hMainWnd = (HWND)puGetAttribute(g_MainWin, PUA_WINDOW_HANDLE);
                    if (hMainWnd) SendMessage(hMainWnd, WM_SETREDRAW, FALSE, 0);
                }
                pMissionData = g_CurrentPacket;
                
                // Force UI refresh for this parse
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
                
                // Re-enable redraw and refresh the whole window
                if (hMainWnd) {
                    SendMessage(hMainWnd, WM_SETREDRAW, TRUE, 0);
                    InvalidateRect(hMainWnd, NULL, TRUE);
                    UpdateWindow(hMainWnd);
                }
                
                // Force main window to redraw its mission controls
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
                    // No more tries left
                    if( g_FoundMish == 255 )
                    {
                        PlaySound( "notfound.wav", NULL, SND_FILENAME | SND_NODEFAULT );
                        MessageBox( NULL, "No mission found within maximum tries.", "ClickSaver", MB_OK | MB_ICONINFORMATION | MB_SYSTEMMODAL );
                    }
                    EndBuyingAgent();
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
								PlaySound( "notfound.wav", NULL, SND_FILENAME | SND_NODEFAULT );
								MessageBox( NULL, "All watched items have reached their quantity limits. Stopping.", "ClickSaver", MB_OK | MB_ICONINFORMATION | MB_SYSTEMMODAL );
								g_BuyingAgentMissions = 0;
								g_BuyingAgentCount = 0;
								EndBuyingAgent();
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
									
									// ========== ADD THE FOLLOWING LINES HERE ==========
									// Ensure the buying agent is still marked active
									g_bBuyingAgentActive = 1;
									
									// Refresh the pause button text and status label to match current paused state
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
									// =================================================
									
									g_bFirstRound = TRUE;
									g_BuyingAgentCount = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENTTRIES ), PUA_TEXTENTRY_VALUE );
									BuyingAgent(g_BuyingAgentDelay);
								}
                             else
								{
									PlaySound( "found.wav", NULL, SND_FILENAME | SND_NODEFAULT );
									char msg[128];
									sprintf(msg, "Accepted %d of %d missions", g_BuyingAgentMaxMissions, g_BuyingAgentMaxMissions);
									MessageBox( NULL, msg, "ClickSaver", MB_OK | MB_ICONINFORMATION | MB_SYSTEMMODAL );
									EndBuyingAgent();
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
            // Fall through

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
                    ClearItemCounters();
                    g_bBuyingAgentActive = 1;
					g_bPaused = 0; 
					
					// Reset pause button and status label
					PULID pauseButton = puGetObjectFromCollection( g_pCol, CS_BUYINGAGENT_PAUSEBTN );
					if (pauseButton) {
						puSetAttribute(pauseButton, PUA_TEXT_STRING, (PUU32)"Pause");
					}
					PULID statusLabel = puGetObjectFromCollection( g_pCol, CS_BA_STATUS );
					if (statusLabel) {
						puSetAttribute(statusLabel, PUA_TEXT_STRING, (PUU32)"Running...");
					}
                    
                    // Pre-register all limited watchlist items
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
	 // Clear disabled table
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
            if( sscanf( buffer, "%[^:]::%[^\n]\n", Keyword, Value ) != EOF )
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
					strcpy( g_AODir, Value );
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
					if ( iVal > 0 && iVal < 5000 ) {   // 0 is not a valid saved position
						puSetAttribute( puGetObjectFromCollection( g_pCol, CS_BUYINGAGENT_WINDOW ), PUA_WINDOW_XPOS, iVal );
						g_BAWindowX = iVal;
					} else {
						// keep default
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
						if (Val < 5010) Val = 5010;   // enforce minimum
						if (Val > 10000) Val = 10000;
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
                    // Wrap raw string with formatted display label
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

    myfilename = malloc( strlen( filename ) + 5 );
    strcpy( myfilename, filename );
    if( !strstr( myfilename, ".cs" ) ) strcat( myfilename, ".cs" );

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
    fprintf( fp, "BUYINGAGENTDELAY::%u\n", g_BuyingAgentDelay );
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
    fprintf( fp, "ITEMVALUE::%u::%u::%u::%u\n",
             puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMVALUE_SINGLE ), PUA_TEXTENTRY_VALUE ),
             puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMVALUE_TOTAL ), PUA_TEXTENTRY_VALUE ),
             puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMVALUE_MSINGLE ), PUA_CHECKBOX_CHECKED ),
             puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMVALUE_MTOTAL ), PUA_CHECKBOX_CHECKED ) );

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
			// Parse the display string back to fields
			ParseDisplayString((char*)pDisplay, itemName, sizeof(itemName),
							&disabled, &force, &limit, exclude, sizeof(exclude));
			// Build the raw string for saving
			char raw[512];
			BuildItemString(raw, sizeof(raw), itemName, disabled, force, limit, exclude);
			fprintf( fp, "%s\n", raw );
		}
		Record = puDoMethod( g_ItemWatchList, PUM_TABLE_GETNEXTRECORD, Record, 0 );
	}
	
	    // Write disabled items
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
            // Build raw string for saving (same format as active items)
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

// Timer‑based BuyingAgent – only starts the timer, does not block
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
		SetWindowSubclass(BAWnd, BAWndProcHook, 1, 0);
        SetFocus( BAWnd );
		// Force the saved position (in case PUL lost it)
		PULID baWndObj = puGetObjectFromCollection(g_pCol, CS_BUYINGAGENT_WINDOW);
		int savedX = puGetAttribute(baWndObj, PUA_WINDOW_XPOS);
		int savedY = puGetAttribute(baWndObj, PUA_WINDOW_YPOS);
		SetWindowPos(BAWnd, NULL, g_BAWindowX, g_BAWindowY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

    g_PendingAttemptNumber = g_BuyingAgentMaxTries - g_BuyingAgentCount + 1;

    // Set a timer that will post a WM_TIMER message to the main window after 'delay' ms
    HWND hMainWnd = (HWND)puGetAttribute( g_MainWin, PUA_WINDOW_HANDLE );
    g_TimerID = SetTimer( hMainWnd, TIMER_BUYINGAGENT, delay, NULL );
    if (g_TimerID == 0)
    {
        DisplayErrorMessage( "Failed to create timer.", TRUE );
        return FALSE;
    }

    return TRUE;
}

void EndBuyingAgent()
{
    g_bBuyingAgentActive = 0;
	g_bPaused = 0; 
    ClearItemCounters();
	
	 // Update UI elements to reflect non‑paused state
    PULID pauseButton = puGetObjectFromCollection( g_pCol, CS_BUYINGAGENT_PAUSEBTN );
    if (pauseButton) {
        puSetAttribute(pauseButton, PUA_TEXT_STRING, (PUU32)"Pause");
    }
    PULID statusLabel = puGetObjectFromCollection( g_pCol, CS_BA_STATUS );
    if (statusLabel) {
        puSetAttribute(statusLabel, PUA_TEXT_STRING, (PUU32)"Running...");
    }

    if( !g_bFullscreen )
		{
			SetFocus( NULL );
			
			// === Save BA window position before it's destroyed ===
			PULID baObj = puGetObjectFromCollection( g_pCol, CS_BUYINGAGENT_WINDOW );
			HWND baWnd = (HWND)puGetAttribute( baObj, PUA_WINDOW_HANDLE );
			if ( baWnd && IsWindow(baWnd) )
			{
				RECT r;
				GetWindowRect( baWnd, &r );
				puSetAttribute( baObj, PUA_WINDOW_XPOS, r.left );
				puSetAttribute( baObj, PUA_WINDOW_YPOS, r.top );
				g_BAWindowX = r.left;
				g_BAWindowY = r.top;
			}
			// ====================================================
			
			puSetAttribute( baObj, PUA_WINDOW_OPENED, FALSE );
			puSetAttribute( g_MainWin, PUA_WINDOW_OPENED, TRUE );
			HWND hMainWnd = (HWND)puGetAttribute( g_MainWin, PUA_WINDOW_HANDLE );
			SetForegroundWindow( hMainWnd );
			SetFocus( hMainWnd );
			SetWindowPos( hMainWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );
		}
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