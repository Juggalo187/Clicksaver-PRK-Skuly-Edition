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

#include "sqlite3.h"
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")

// SQLite Globals
sqlite3*      g_pSQLite = NULL;
sqlite3_stmt* g_stmtItem = NULL;
sqlite3_stmt* g_stmtIcon = NULL;
sqlite3_stmt* g_stmtPF = NULL;

// ========== FORWARD DECLARATIONS OF FUNCTIONS DEFINED AFTER main() ==========
void BuildItemNameCache(const char *filename);
int LoadItemNameCache(const char *cacheFilePath);
void FreeItemNameCache(void);
void CleanUp();
void ImportSettings(char* filename);
void ExportSettings(char* filename);
void DisplayErrorMessage(PUU8* _pMessage, PUU32 _bAsynchronous);
void GetFolder(HWND hWndOwner, char *strTitle, char *strPath);
BOOL GetFile(HWND hWndOwner, BOOL saving, char *buffer, int buffersize);
int BuyingAgent(int delay);
void EndBuyingAgent();
void UpdateAcceptedCountersForMission(int mishIndex);
void _setSliders(int easy_hard, int good_bad, int order_chaos, int open_hidden, int phys_myst, int headon_stealth, int money_xp);

// Forward declarations for item string helpers (defined after main)
void BuildItemString(char *dest, size_t destSize, const char *itemName, int disabled, int forceAccept, int quantityLimit, const char *excludeWords);
void FormatItemForDisplay(const char *raw, char *out, size_t outSize);
//int ParseDisplayString(const char *display, char *itemName, size_t itemNameSize, int *disabled, int *forceAccept, int *quantityLimit, char *excludeWords, size_t excludeSize);
void MakeTableEntry(char *dest, size_t destSize, const char *raw);

// Forward declarations for item list operations (defined after main)
static void MoveCurrentActiveToDisabled(void);
static void MoveCurrentDisabledToActive(void);
static void RemoveDuplicateItems(void);
static int ItemExistsInActiveList(const char *displayString);
static void ImportItemsFromFile(const char *filename, int replaceMode);
static void ExportItemsOnly(const char *filename);

typedef struct ItemCounter ItemCounter;

// Forward declarations for item counter and watchlist functions
int HasActiveWatchlistItems(void);
void ClearItemCounters(void);
ItemCounter* FindItemCounter(const char *name);
void AddItemCounter(const char *name, int limit);

// Forward declaration for the edit dialog
typedef struct ItemEditData ItemEditData;   // opaque forward declaration
int ShowItemEditDialog(HWND hParent, ItemEditData *pData, int bIsAddMode);
// ============================================================================

extern PUU8 g_bForceUIRefresh;
extern PUU32 g_GUIDef[];
pusObjectCollection* g_pCol;
PULID g_ItemWatchList, g_LocWatchList, g_MainWin;
PULID g_DisabledItemWatchList;

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

HWND g_hHiddenTabsWnd = NULL;

char g_CurrentPacket[65536];

char g_AODir[MAX_PATH] = {0};
char g_CSDir[MAX_PATH] = {0};

HANDLE g_Mutex = INVALID_HANDLE_VALUE;
HANDLE g_Thread = INVALID_HANDLE_VALUE;
HANDLE g_hThreadExitEvent = NULL;
HANDLE g_hAbortEvent = NULL;
DWORD WINAPI HookManagerThread(void *pParam);

typedef enum ImportSettingsMode
{
    ISM_CONFIG,
    ISM_LOCWATCH,
    ISM_ITEMWATCH,
    ISM_DISABLED_ITEMWATCH,
    ISM_SLIDERS,
    ISM_DONE,
} ImportSettingsMode;

// ========== STRUCTURE FOR ITEM EDIT DIALOG (must be defined before use) ==========
typedef struct ItemEditData {
    char itemName[256];
    int  limit;
    int  disabled;
    int  force;
    char exclude[256];
    int  isAdd;
} ItemEditData;

// Parse raw item string format: [ # ][ ~ ]ItemName[ ;limit][ ^exclude1 ^exclude2 ...]
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
    if (itemName) itemName[0] = '\0';
    if (!src) return;

    const char *p = src;

    // Skip leading # and ~
    while (*p == '#' || *p == '~') {
        if (*p == '#') *disabled = 1;
        if (*p == '~') *forceAccept = 1;
        p++;
    }

    // Find end of item name – stop at ';' or '^'
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

    // Parse exclude words – each starts with '^'
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
                if (excludeWords[0] != '\0')
                    strncat(excludeWords, " ", excludeSize - strlen(excludeWords) - 1);
                strncat(excludeWords, start, len);
            }
            while (*p == ' ') p++;
        }
    }
}

// ========== SUPPORT FUNCTIONS FOR HIDDEN TABS WINDOW ==========
static void PopulateItemList(HWND hList, PULID table)
{
    ListView_DeleteAllItems(hList);
    PUU32 record = puDoMethod(table, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    int idx = 0;
    while (record) {
        PUU8 *str = (PUU8*)puDoMethod(table, PUM_TABLE_GETFIELDVAL, record, 0);
        if (str && *str) {
            char itemName[256] = {0}, exclude[256] = {0};
            int disabled = 0, force = 0, limit = 0;

            // Check if string looks like raw format (contains ';' or '^')
            if (strchr((char*)str, ';') || strchr((char*)str, '^')) {
                // Raw format: parse with ParseItemString
                ParseItemString((char*)str, itemName, sizeof(itemName),
                                &disabled, &force, &limit, exclude, sizeof(exclude));
            } else {
                // Formatted display: parse with ParseDisplayString
                ParseDisplayString((char*)str, itemName, sizeof(itemName),
                                   &disabled, &force, &limit, exclude, sizeof(exclude));
            }

            LVITEM item = {0};
            item.mask = LVIF_TEXT;
            item.iItem = idx;
            item.pszText = itemName;
            ListView_InsertItem(hList, &item);

            char qtyStr[16];
            if (limit > 0)
                sprintf(qtyStr, "%d", limit);
            else
                strcpy(qtyStr, "unlimited");
            ListView_SetItemText(hList, idx, 1, qtyStr);
            // For active items, only show force accept if item is not disabled (disabled items are separate)
            ListView_SetItemText(hList, idx, 2, (force && !disabled) ? "Yes" : "");
            // Clean exclude words (replace commas with spaces)
            for (char *c = exclude; *c; c++) if (*c == ',') *c = ' ';
            ListView_SetItemText(hList, idx, 3, exclude);
            idx++;
        }
        record = puDoMethod(table, PUM_TABLE_GETNEXTRECORD, record, 0);
    }
}

static void PopulateDisabledList(HWND hList)
{
    ListView_DeleteAllItems(hList);
    PUU32 record = puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    int idx = 0;
    while (record) {
        PUU8 *str = (PUU8*)puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETFIELDVAL, record, 0);
        if (str && *str) {
            char itemName[256] = {0}, exclude[256] = {0};
            int disabled = 0, force = 0, limit = 0;

            if (strchr((char*)str, ';') || strchr((char*)str, '^')) {
                ParseItemString((char*)str, itemName, sizeof(itemName),
                                &disabled, &force, &limit, exclude, sizeof(exclude));
            } else {
                ParseDisplayString((char*)str, itemName, sizeof(itemName),
                                   &disabled, &force, &limit, exclude, sizeof(exclude));
            }

            LVITEM item = {0};
            item.mask = LVIF_TEXT;
            item.iItem = idx;
            item.pszText = itemName;
            ListView_InsertItem(hList, &item);

            char qtyStr[16];
            if (limit > 0)
                sprintf(qtyStr, "%d", limit);
            else
                strcpy(qtyStr, "unlimited");
            ListView_SetItemText(hList, idx, 1, qtyStr);
            ListView_SetItemText(hList, idx, 2, force ? "Yes" : "");
            for (char *c = exclude; *c; c++) if (*c == ',') *c = ' ';
            ListView_SetItemText(hList, idx, 3, exclude);
            idx++;
        }
        record = puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETNEXTRECORD, record, 0);
    }
}

static void PopulateLocationList(HWND hList)
{
    ListView_DeleteAllItems(hList);
    PUU32 record = puDoMethod(g_LocWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    int idx = 0;
    while (record) {
        PUU8 *str = (PUU8*)puDoMethod(g_LocWatchList, PUM_TABLE_GETFIELDVAL, record, 0);
        if (str && *str) {
            LVITEM item = {0};
            item.mask = LVIF_TEXT;
            item.iItem = idx++;
            item.pszText = (char*)str;
            ListView_InsertItem(hList, &item);
        }
        record = puDoMethod(g_LocWatchList, PUM_TABLE_GETNEXTRECORD, record, 0);
    }
}

static void SyncTypeCheckboxes(HWND hDlg)
{
    CheckDlgButton(hDlg, IDC_TYPE_REPAIR, PUL_GET_CB(CS_TYPEREPAIR_CB) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_TYPE_RETURN, PUL_GET_CB(CS_TYPERETURN_CB) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_TYPE_FINDP,  PUL_GET_CB(CS_TYPEFINDP_CB)  ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_TYPE_FINDI,  PUL_GET_CB(CS_TYPEFINDI_CB)  ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_TYPE_ASS,    PUL_GET_CB(CS_TYPEASS_CB)    ? BST_CHECKED : BST_UNCHECKED);
}

static void SyncValueControls(HWND hDlg)
{
    char buf[32];
    sprintf(buf, "%d", puGetAttribute(puGetObjectFromCollection(g_pCol, CS_ITEMVALUE_BUYMOD), PUA_TEXTENTRY_VALUE));
    SetDlgItemTextA(hDlg, IDC_VALUE_BUYMOD, buf);
    sprintf(buf, "%d", puGetAttribute(puGetObjectFromCollection(g_pCol, CS_ITEMVALUE_SINGLE), PUA_TEXTENTRY_VALUE));
    SetDlgItemTextA(hDlg, IDC_VALUE_SINGLE_EDIT, buf);
    CheckDlgButton(hDlg, IDC_VALUE_SINGLE_ENABLE, PUL_GET_CB(CS_ITEMVALUE_MSINGLE) ? BST_CHECKED : BST_UNCHECKED);
    sprintf(buf, "%d", puGetAttribute(puGetObjectFromCollection(g_pCol, CS_ITEMVALUE_TOTAL), PUA_TEXTENTRY_VALUE));
    SetDlgItemTextA(hDlg, IDC_VALUE_TOTAL_EDIT, buf);
    CheckDlgButton(hDlg, IDC_VALUE_TOTAL_ENABLE, PUL_GET_CB(CS_ITEMVALUE_MTOTAL) ? BST_CHECKED : BST_UNCHECKED);
}

static void SyncSliderValues(HWND hDlg)
{
    SetDlgItemInt(hDlg, IDC_SLIDER_EASY_HARD,   puGetAttribute(puGetObjectFromCollection(g_pCol, CS_SLIDER_EASY_HARD), PUA_TEXTENTRY_VALUE), FALSE);
    SetDlgItemInt(hDlg, IDC_SLIDER_GOOD_BAD,    puGetAttribute(puGetObjectFromCollection(g_pCol, CS_SLIDER_GOOD_BAD), PUA_TEXTENTRY_VALUE), FALSE);
    SetDlgItemInt(hDlg, IDC_SLIDER_ORDER_CHAOS, puGetAttribute(puGetObjectFromCollection(g_pCol, CS_SLIDER_ORDER_CHAOS), PUA_TEXTENTRY_VALUE), FALSE);
    SetDlgItemInt(hDlg, IDC_SLIDER_OPEN_HIDDEN, puGetAttribute(puGetObjectFromCollection(g_pCol, CS_SLIDER_OPEN_HIDDEN), PUA_TEXTENTRY_VALUE), FALSE);
    SetDlgItemInt(hDlg, IDC_SLIDER_PHYS_MYST,   puGetAttribute(puGetObjectFromCollection(g_pCol, CS_SLIDER_PHYS_MYST), PUA_TEXTENTRY_VALUE), FALSE);
    SetDlgItemInt(hDlg, IDC_SLIDER_HEADON_STEALTH, puGetAttribute(puGetObjectFromCollection(g_pCol, CS_SLIDER_HEADON_STEALTH), PUA_TEXTENTRY_VALUE), FALSE);
    SetDlgItemInt(hDlg, IDC_SLIDER_MONEY_XP,    puGetAttribute(puGetObjectFromCollection(g_pCol, CS_SLIDER_MONEY_XP), PUA_TEXTENTRY_VALUE), FALSE);
}

typedef struct {
    char* buffer;
    const char* prompt;
} InputBoxParams;

static INT_PTR CALLBACK InputBoxProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static InputBoxParams* params = NULL;
    switch (msg) {
    case WM_INITDIALOG:
        params = (InputBoxParams*)lParam;
        SetDlgItemTextA(hDlg, IDC_STATIC, params->prompt);
        SetFocus(GetDlgItem(hDlg, IDC_EDIT));
        return FALSE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            GetDlgItemTextA(hDlg, IDC_EDIT, params->buffer, 512);
            EndDialog(hDlg, IDOK);
            return TRUE;
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static int InputBox(HWND hParent, const char* prompt, char* outBuf, int bufSize)
{
    InputBoxParams params;
    params.buffer = outBuf;
    params.prompt = prompt;
    outBuf[0] = '\0';
    return (DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_INPUTBOX),
                           hParent, InputBoxProc, (LPARAM)&params) == IDOK);
}

static void SyncHiddenTabCheckboxes(HWND hDlg)
{
    // Items
    CheckDlgButton(hDlg, IDC_ITEM_MATCH_CB, PUL_GET_CB(CS_ALERTITEM_CB) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_ITEM_HIGHLIGHT_CB, PUL_GET_CB(CS_HIGHLIGHTITEM_CB) ? BST_CHECKED : BST_UNCHECKED);
    // Locations
    CheckDlgButton(hDlg, IDC_LOC_MATCH_CB, PUL_GET_CB(CS_ALERTLOC_CB) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_LOC_HIGHLIGHT_CB, PUL_GET_CB(CS_HIGHLIGHTLOC_CB) ? BST_CHECKED : BST_UNCHECKED);
    // Types
    CheckDlgButton(hDlg, IDC_TYPE_MATCH_CB, PUL_GET_CB(CS_ALERTTYPE_CB) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_TYPE_HIGHLIGHT_CB, PUL_GET_CB(CS_HIGHLIGHTTYPE_CB) ? BST_CHECKED : BST_UNCHECKED);
}

// ========== HIDDEN TABS DIALOG PROCEDURE (WIN32) ==========
static INT_PTR CALLBACK HiddenTabsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HWND hTab;
    static HWND hItemList, hDisabledList, hLocationList;
    static HWND hTypeRepair, hTypeReturn, hTypeFindP, hTypeFindI, hTypeAss;
    static HWND hValueBuyMod, hValueSingleEnable, hValueSingleEdit, hValueTotalEnable, hValueTotalEdit;
	static HWND hItemValueHint1, hItemValueHint2;
	static HWND hSlidersHint1, hSlidersHint2;
    static HWND hSliderEasyHard, hSliderGoodBad, hSliderOrderChaos, hSliderOpenHidden, hSliderPhysMyst, hSliderHeadonStealth, hSliderMoneyXp;
    static HWND hBtnSetSliders;
    // Labels
    static HWND hLabelBuyMod, hLabelBuyModHint, hLabelSingleValue, hLabelTotalValue;
    static HWND hLabelEasyHard, hLabelGoodBad, hLabelOrderChaos, hLabelOpenHidden, hLabelPhysMyst, hLabelHeadonStealth, hLabelMoneyXp;
	static HWND hItemMatchLabel, hItemHighlightLabel;
	static HWND hLocMatchLabel, hLocHighlightLabel;
	static HWND hTypeMatchLabel, hTypeHighlightLabel;
    static int currentTab = 0;


    switch (msg) {
		case WM_INITDIALOG:
			{
				hTab = GetDlgItem(hDlg, IDC_HIDDEN_TAB);
				if (!hTab) return FALSE;
			
				TCITEM tie = {0};
				tie.mask = TCIF_TEXT;
				tie.pszText = "Items";      TabCtrl_InsertItem(hTab, 0, &tie);
				tie.pszText = "Disabled";   TabCtrl_InsertItem(hTab, 1, &tie);
				tie.pszText = "Locations";  TabCtrl_InsertItem(hTab, 2, &tie);
				tie.pszText = "Mission Types"; TabCtrl_InsertItem(hTab, 3, &tie);
				tie.pszText = "Item Values"; TabCtrl_InsertItem(hTab, 4, &tie);
				tie.pszText = "Sliders";    TabCtrl_InsertItem(hTab, 5, &tie);
			
				// Get control handles
				hItemList = GetDlgItem(hDlg, IDC_ITEM_LIST);
				hDisabledList = GetDlgItem(hDlg, IDC_DISABLED_LIST);
				hLocationList = GetDlgItem(hDlg, IDC_LOCATION_LIST);
				hTypeRepair = GetDlgItem(hDlg, IDC_TYPE_REPAIR);
				hTypeReturn = GetDlgItem(hDlg, IDC_TYPE_RETURN);
				hTypeFindP  = GetDlgItem(hDlg, IDC_TYPE_FINDP);
				hTypeFindI  = GetDlgItem(hDlg, IDC_TYPE_FINDI);
				hTypeAss    = GetDlgItem(hDlg, IDC_TYPE_ASS);
				hValueBuyMod = GetDlgItem(hDlg, IDC_VALUE_BUYMOD);
				hValueSingleEnable = GetDlgItem(hDlg, IDC_VALUE_SINGLE_ENABLE);
				hValueSingleEdit   = GetDlgItem(hDlg, IDC_VALUE_SINGLE_EDIT);
				hValueTotalEnable  = GetDlgItem(hDlg, IDC_VALUE_TOTAL_ENABLE);
				hValueTotalEdit    = GetDlgItem(hDlg, IDC_VALUE_TOTAL_EDIT);
				hItemValueHint1 = GetDlgItem(hDlg, IDC_ITEMVALUE_HINT1);
				hItemValueHint2 = GetDlgItem(hDlg, IDC_ITEMVALUE_HINT2);
				hSlidersHint1 = GetDlgItem(hDlg, IDC_SLIDERS_HINT1);
				hSlidersHint2 = GetDlgItem(hDlg, IDC_SLIDERS_HINT2);
				hSliderEasyHard     = GetDlgItem(hDlg, IDC_SLIDER_EASY_HARD);
				hSliderGoodBad      = GetDlgItem(hDlg, IDC_SLIDER_GOOD_BAD);
				hSliderOrderChaos   = GetDlgItem(hDlg, IDC_SLIDER_ORDER_CHAOS);
				hSliderOpenHidden   = GetDlgItem(hDlg, IDC_SLIDER_OPEN_HIDDEN);
				hSliderPhysMyst     = GetDlgItem(hDlg, IDC_SLIDER_PHYS_MYST);
				hSliderHeadonStealth = GetDlgItem(hDlg, IDC_SLIDER_HEADON_STEALTH);
				hSliderMoneyXp      = GetDlgItem(hDlg, IDC_SLIDER_MONEY_XP);
				hBtnSetSliders      = GetDlgItem(hDlg, IDC_BTN_SET_SLIDERS);
				// Labels
				hLabelBuyMod        = GetDlgItem(hDlg, IDC_LABEL_BUYMOD);
				hLabelBuyModHint    = GetDlgItem(hDlg, IDC_LABEL_BUYMOD_HINT);
				hLabelSingleValue   = GetDlgItem(hDlg, IDC_LABEL_SINGLE_VALUE);
				hLabelTotalValue    = GetDlgItem(hDlg, IDC_LABEL_TOTAL_VALUE);
				hLabelEasyHard      = GetDlgItem(hDlg, IDC_LABEL_EASY_HARD);
				hLabelGoodBad       = GetDlgItem(hDlg, IDC_LABEL_GOOD_BAD);
				hLabelOrderChaos    = GetDlgItem(hDlg, IDC_LABEL_ORDER_CHAOS);
				hLabelOpenHidden    = GetDlgItem(hDlg, IDC_LABEL_OPEN_HIDDEN);
				hLabelPhysMyst      = GetDlgItem(hDlg, IDC_LABEL_PHYS_MYST);
				hLabelHeadonStealth = GetDlgItem(hDlg, IDC_LABEL_HEADON_STEALTH);
				hLabelMoneyXp       = GetDlgItem(hDlg, IDC_LABEL_MONEY_XP);
				hItemMatchLabel = GetDlgItem(hDlg, IDC_ITEM_MATCH_LABEL);
				hItemHighlightLabel = GetDlgItem(hDlg, IDC_ITEM_HIGHLIGHT_LABEL);
				hLocMatchLabel = GetDlgItem(hDlg, IDC_LOC_MATCH_LABEL);
				hLocHighlightLabel = GetDlgItem(hDlg, IDC_LOC_HIGHLIGHT_LABEL);
				hTypeMatchLabel = GetDlgItem(hDlg, IDC_TYPE_MATCH_LABEL);
				hTypeHighlightLabel = GetDlgItem(hDlg, IDC_TYPE_HIGHLIGHT_LABEL);
			
				// Set listview styles
				ListView_SetExtendedListViewStyle(hItemList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
				ListView_SetExtendedListViewStyle(hDisabledList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
				ListView_SetExtendedListViewStyle(hLocationList, LVS_EX_FULLROWSELECT);
			
				LVCOLUMN col = {0};
				col.mask = LVCF_TEXT | LVCF_WIDTH;
			
				col.pszText = "Item Name";
				col.cx = 160;
				ListView_InsertColumn(hItemList, 0, &col);
				ListView_InsertColumn(hDisabledList, 0, &col);
			
				col.pszText = "Qty";
				col.cx = 35;
				ListView_InsertColumn(hItemList, 1, &col);
				ListView_InsertColumn(hDisabledList, 1, &col);
			
				col.pszText = "Force";
				col.cx = 40;
				ListView_InsertColumn(hItemList, 2, &col);
				ListView_InsertColumn(hDisabledList, 2, &col);
			
				col.pszText = "Exclude";
				col.cx = 120;
				ListView_InsertColumn(hItemList, 3, &col);
				ListView_InsertColumn(hDisabledList, 3, &col);
			
				// Locations listview
				col.pszText = "Location";
				col.cx = 320;
				ListView_InsertColumn(hLocationList, 0, &col);
			
				// ========== INITIALISE FIRST TAB (ITEMS) – HIDE ALL OTHER CONTROLS ==========
				ShowWindow(hItemList, SW_SHOW);
				ShowWindow(hDisabledList, SW_HIDE);
				ShowWindow(hLocationList, SW_HIDE);
				ShowWindow(hTypeRepair, SW_HIDE);
				ShowWindow(hTypeReturn, SW_HIDE);
				ShowWindow(hTypeFindP, SW_HIDE);
				ShowWindow(hTypeFindI, SW_HIDE);
				ShowWindow(hTypeAss, SW_HIDE);
				ShowWindow(hValueBuyMod, SW_HIDE);
				ShowWindow(hValueSingleEnable, SW_HIDE);
				ShowWindow(hValueSingleEdit, SW_HIDE);
				ShowWindow(hValueTotalEnable, SW_HIDE);
				ShowWindow(hValueTotalEdit, SW_HIDE);
				ShowWindow(hLabelBuyMod, SW_HIDE);
				ShowWindow(hLabelBuyModHint, SW_HIDE);
				ShowWindow(hLabelSingleValue, SW_HIDE);
				ShowWindow(hLabelTotalValue, SW_HIDE);
				ShowWindow(hSliderEasyHard, SW_HIDE);
				ShowWindow(hSliderGoodBad, SW_HIDE);
				ShowWindow(hSliderOrderChaos, SW_HIDE);
				ShowWindow(hSliderOpenHidden, SW_HIDE);
				ShowWindow(hSliderPhysMyst, SW_HIDE);
				ShowWindow(hSliderHeadonStealth, SW_HIDE);
				ShowWindow(hSliderMoneyXp, SW_HIDE);
				ShowWindow(hLabelEasyHard, SW_HIDE);
				ShowWindow(hLabelGoodBad, SW_HIDE);
				ShowWindow(hLabelOrderChaos, SW_HIDE);
				ShowWindow(hLabelOpenHidden, SW_HIDE);
				ShowWindow(hLabelPhysMyst, SW_HIDE);
				ShowWindow(hLabelHeadonStealth, SW_HIDE);
				ShowWindow(hLabelMoneyXp, SW_HIDE);
				ShowWindow(hBtnSetSliders, SW_HIDE);
				ShowWindow(hItemValueHint1, SW_HIDE);
				ShowWindow(hItemValueHint2, SW_HIDE);
				ShowWindow(hSlidersHint1, SW_HIDE);
				ShowWindow(hSlidersHint2, SW_HIDE);
			
				// Items tab buttons
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_ADD), SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_REMOVE), SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_EDIT), SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_DISABLE), SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_IMPORT), SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_EXPORT), SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_REMOVE_DUPLICATES), SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_REMOVE_ALL), SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_ENABLE), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_REMOVE_DISABLED), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_REMOVE_ALL_DISABLED), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_LOCATION_ADD), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_LOCATION_REMOVE), SW_HIDE);
			
				// ========== CRITICAL: Show the Items tab’s own checkboxes ==========
				ShowWindow(GetDlgItem(hDlg, IDC_ITEM_MATCH_CB), SW_SHOW);
				ShowWindow(GetDlgItem(hDlg, IDC_ITEM_HIGHLIGHT_CB), SW_SHOW);
				// Also ensure the Locations and Types tab checkboxes are hidden (they already are)
				ShowWindow(GetDlgItem(hDlg, IDC_LOC_MATCH_CB), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_LOC_HIGHLIGHT_CB), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_TYPE_MATCH_CB), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_TYPE_HIGHLIGHT_CB), SW_HIDE);
			
				PopulateItemList(hItemList, g_ItemWatchList);
				PopulateDisabledList(hDisabledList);
				PopulateLocationList(hLocationList);
				ListView_SetColumnWidth(hLocationList, 0, LVSCW_AUTOSIZE_USEHEADER);
				SyncTypeCheckboxes(hDlg);
				SyncValueControls(hDlg);
				SyncSliderValues(hDlg);
				SyncHiddenTabCheckboxes(hDlg);
			
				return TRUE;
			}

    case WM_NOTIFY:
			{
				NMHDR *pnmh = (NMHDR*)lParam;
			
				// Tab change handling
				if (pnmh->idFrom == IDC_HIDDEN_TAB && pnmh->code == TCN_SELCHANGE) {
				int newTab = TabCtrl_GetCurSel(hTab);
				if (newTab == currentTab) break;
				
				// Hide ALL tab-specific controls (including all checkboxes)
				ShowWindow(hItemList, SW_HIDE);
				ShowWindow(hDisabledList, SW_HIDE);
				ShowWindow(hLocationList, SW_HIDE);
				ShowWindow(hTypeRepair, SW_HIDE);
				ShowWindow(hTypeReturn, SW_HIDE);
				ShowWindow(hTypeFindP, SW_HIDE);
				ShowWindow(hTypeFindI, SW_HIDE);
				ShowWindow(hTypeAss, SW_HIDE);
				ShowWindow(hValueBuyMod, SW_HIDE);
				ShowWindow(hValueSingleEnable, SW_HIDE);
				ShowWindow(hValueSingleEdit, SW_HIDE);
				ShowWindow(hValueTotalEnable, SW_HIDE);
				ShowWindow(hValueTotalEdit, SW_HIDE);
				ShowWindow(hLabelBuyMod, SW_HIDE);
				ShowWindow(hLabelBuyModHint, SW_HIDE);
				ShowWindow(hLabelSingleValue, SW_HIDE);
				ShowWindow(hLabelTotalValue, SW_HIDE);
				ShowWindow(hSliderEasyHard, SW_HIDE);
				ShowWindow(hSliderGoodBad, SW_HIDE);
				ShowWindow(hSliderOrderChaos, SW_HIDE);
				ShowWindow(hSliderOpenHidden, SW_HIDE);
				ShowWindow(hSliderPhysMyst, SW_HIDE);
				ShowWindow(hSliderHeadonStealth, SW_HIDE);
				ShowWindow(hSliderMoneyXp, SW_HIDE);
				ShowWindow(hLabelEasyHard, SW_HIDE);
				ShowWindow(hLabelGoodBad, SW_HIDE);
				ShowWindow(hLabelOrderChaos, SW_HIDE);
				ShowWindow(hLabelOpenHidden, SW_HIDE);
				ShowWindow(hLabelPhysMyst, SW_HIDE);
				ShowWindow(hLabelHeadonStealth, SW_HIDE);
				ShowWindow(hLabelMoneyXp, SW_HIDE);
				ShowWindow(hBtnSetSliders, SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_ADD), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_REMOVE), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_EDIT), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_DISABLE), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_IMPORT), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_EXPORT), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_REMOVE_DUPLICATES), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_REMOVE_ALL), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_ENABLE), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_REMOVE_DISABLED), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_BTN_REMOVE_ALL_DISABLED), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_LOCATION_ADD), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_LOCATION_REMOVE), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_ITEM_MATCH_CB), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_ITEM_HIGHLIGHT_CB), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_LOC_MATCH_CB), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_LOC_HIGHLIGHT_CB), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_TYPE_MATCH_CB), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDC_TYPE_HIGHLIGHT_CB), SW_HIDE);
				ShowWindow(hSlidersHint1, SW_HIDE);
				ShowWindow(hSlidersHint2, SW_HIDE);
				
				// Show only the controls for the selected tab
				switch (newTab) {
					case 0: // Items
						ShowWindow(hItemList, SW_SHOW);
						ShowWindow(GetDlgItem(hDlg, IDC_BTN_ADD), SW_SHOW);
						ShowWindow(GetDlgItem(hDlg, IDC_BTN_REMOVE), SW_SHOW);
						ShowWindow(GetDlgItem(hDlg, IDC_BTN_EDIT), SW_SHOW);
						ShowWindow(GetDlgItem(hDlg, IDC_BTN_DISABLE), SW_SHOW);
						ShowWindow(GetDlgItem(hDlg, IDC_BTN_IMPORT), SW_SHOW);
						ShowWindow(GetDlgItem(hDlg, IDC_BTN_EXPORT), SW_SHOW);
						ShowWindow(GetDlgItem(hDlg, IDC_BTN_REMOVE_DUPLICATES), SW_SHOW);
						ShowWindow(GetDlgItem(hDlg, IDC_BTN_REMOVE_ALL), SW_SHOW);
						ShowWindow(GetDlgItem(hDlg, IDC_ITEM_MATCH_CB), SW_SHOW);
						ShowWindow(GetDlgItem(hDlg, IDC_ITEM_HIGHLIGHT_CB), SW_SHOW);
						 ShowWindow(hItemMatchLabel, SW_SHOW);
						ShowWindow(hItemHighlightLabel, SW_SHOW);
						// Explicitly hide mission type checkboxes (safety)
						ShowWindow(hTypeRepair, SW_HIDE);
						ShowWindow(hTypeReturn, SW_HIDE);
						ShowWindow(hTypeFindP, SW_HIDE);
						ShowWindow(hTypeFindI, SW_HIDE);
						ShowWindow(hTypeAss, SW_HIDE);
						ShowWindow(hItemValueHint1, SW_HIDE);
						ShowWindow(hItemValueHint2, SW_HIDE);
						ShowWindow(hLocMatchLabel, SW_HIDE);
						ShowWindow(hLocHighlightLabel, SW_HIDE);
						ShowWindow(hTypeMatchLabel, SW_HIDE);
						ShowWindow(hTypeHighlightLabel, SW_HIDE);
						PopulateItemList(hItemList, g_ItemWatchList);
						break;
					
						case 1: // Disabled
							ShowWindow(hDisabledList, SW_SHOW);
							ShowWindow(GetDlgItem(hDlg, IDC_BTN_ENABLE), SW_SHOW);
							ShowWindow(GetDlgItem(hDlg, IDC_BTN_REMOVE_DISABLED), SW_SHOW);
							ShowWindow(GetDlgItem(hDlg, IDC_BTN_REMOVE_ALL_DISABLED), SW_SHOW);
							// Disabled tab – no checkboxes – hide all
							ShowWindow(GetDlgItem(hDlg, IDC_ITEM_MATCH_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_ITEM_HIGHLIGHT_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_LOC_MATCH_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_LOC_HIGHLIGHT_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_TYPE_MATCH_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_TYPE_HIGHLIGHT_CB), SW_HIDE);
							ShowWindow(hItemValueHint1, SW_HIDE);
							ShowWindow(hItemValueHint2, SW_HIDE);
							ShowWindow(hItemMatchLabel, SW_HIDE);
							ShowWindow(hItemHighlightLabel, SW_HIDE);
							ShowWindow(hLocMatchLabel, SW_HIDE);
							ShowWindow(hLocHighlightLabel, SW_HIDE);
							ShowWindow(hTypeMatchLabel, SW_HIDE);
							ShowWindow(hTypeHighlightLabel, SW_HIDE);
							PopulateDisabledList(hDisabledList);
							break;
					
						case 2: // Locations
							ShowWindow(hLocationList, SW_SHOW);
							ShowWindow(GetDlgItem(hDlg, IDC_LOCATION_ADD), SW_SHOW);
							ShowWindow(GetDlgItem(hDlg, IDC_LOCATION_REMOVE), SW_SHOW);
							// Locations checkboxes
							ShowWindow(GetDlgItem(hDlg, IDC_LOC_MATCH_CB), SW_SHOW);
							ShowWindow(GetDlgItem(hDlg, IDC_LOC_HIGHLIGHT_CB), SW_SHOW);
							ShowWindow(GetDlgItem(hDlg, IDC_LOC_MATCH_LABEL), SW_SHOW);
							ShowWindow(GetDlgItem(hDlg, IDC_LOC_HIGHLIGHT_LABEL), SW_SHOW);
							// Hide others
							ShowWindow(GetDlgItem(hDlg, IDC_ITEM_MATCH_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_ITEM_HIGHLIGHT_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_TYPE_MATCH_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_TYPE_HIGHLIGHT_CB), SW_HIDE);
							ShowWindow(hItemValueHint1, SW_HIDE);
							ShowWindow(hItemValueHint2, SW_HIDE);
							ShowWindow(hItemMatchLabel, SW_HIDE);
							ShowWindow(hItemHighlightLabel, SW_HIDE);
							ShowWindow(hTypeMatchLabel, SW_HIDE);
							ShowWindow(hTypeHighlightLabel, SW_HIDE);
							PopulateLocationList(hLocationList);
							break;
					
						case 3: // Mission Types
							ShowWindow(GetDlgItem(hDlg, IDC_TYPE_REPAIR), SW_SHOW);
							ShowWindow(GetDlgItem(hDlg, IDC_TYPE_RETURN), SW_SHOW);
							ShowWindow(GetDlgItem(hDlg, IDC_TYPE_FINDP), SW_SHOW);
							ShowWindow(GetDlgItem(hDlg, IDC_TYPE_FINDI), SW_SHOW);
							ShowWindow(GetDlgItem(hDlg, IDC_TYPE_ASS), SW_SHOW);
							// Types checkboxes
							ShowWindow(GetDlgItem(hDlg, IDC_TYPE_MATCH_CB), SW_SHOW);
							ShowWindow(GetDlgItem(hDlg, IDC_TYPE_HIGHLIGHT_CB), SW_SHOW);
							ShowWindow(GetDlgItem(hDlg, IDC_TYPE_MATCH_LABEL), SW_SHOW);
							ShowWindow(GetDlgItem(hDlg, IDC_TYPE_HIGHLIGHT_LABEL), SW_SHOW);
							// Hide others
							ShowWindow(GetDlgItem(hDlg, IDC_ITEM_MATCH_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_ITEM_HIGHLIGHT_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_LOC_MATCH_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_LOC_HIGHLIGHT_CB), SW_HIDE);
							ShowWindow(hItemValueHint1, SW_HIDE);
							ShowWindow(hItemValueHint2, SW_HIDE);
							
							ShowWindow(hItemMatchLabel, SW_HIDE);
							ShowWindow(hItemHighlightLabel, SW_HIDE);
							ShowWindow(hLocMatchLabel, SW_HIDE);
							ShowWindow(hLocHighlightLabel, SW_HIDE);
							
							SyncTypeCheckboxes(hDlg);
							break;
					
						case 4: // Item Values
							ShowWindow(hValueBuyMod, SW_SHOW);
							ShowWindow(hValueSingleEnable, SW_SHOW);
							ShowWindow(hValueSingleEdit, SW_SHOW);
							ShowWindow(hValueTotalEnable, SW_SHOW);
							ShowWindow(hValueTotalEdit, SW_SHOW);
							ShowWindow(hLabelBuyMod, SW_SHOW);
							ShowWindow(hLabelBuyModHint, SW_SHOW);
							ShowWindow(hLabelSingleValue, SW_SHOW);
							ShowWindow(hLabelTotalValue, SW_SHOW);
							ShowWindow(hItemValueHint1, SW_SHOW);
							ShowWindow(hItemValueHint2, SW_SHOW);
							ShowWindow(hSlidersHint1, SW_HIDE);
							ShowWindow(hSlidersHint2, SW_HIDE);
							// Hide all generic checkboxes
							ShowWindow(GetDlgItem(hDlg, IDC_ITEM_MATCH_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_ITEM_HIGHLIGHT_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_LOC_MATCH_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_LOC_HIGHLIGHT_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_TYPE_MATCH_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_TYPE_HIGHLIGHT_CB), SW_HIDE);
							ShowWindow(hItemMatchLabel, SW_HIDE);
							ShowWindow(hItemHighlightLabel, SW_HIDE);
							ShowWindow(hLocMatchLabel, SW_HIDE);
							ShowWindow(hLocHighlightLabel, SW_HIDE);
							ShowWindow(hTypeMatchLabel, SW_HIDE);
							ShowWindow(hTypeHighlightLabel, SW_HIDE);
							SyncValueControls(hDlg);
							break;
					
						case 5: // Sliders
							ShowWindow(hSliderEasyHard, SW_SHOW);
							ShowWindow(hSliderGoodBad, SW_SHOW);
							ShowWindow(hSliderOrderChaos, SW_SHOW);
							ShowWindow(hSliderOpenHidden, SW_SHOW);
							ShowWindow(hSliderPhysMyst, SW_SHOW);
							ShowWindow(hSliderHeadonStealth, SW_SHOW);
							ShowWindow(hSliderMoneyXp, SW_SHOW);
							ShowWindow(hLabelEasyHard, SW_SHOW);
							ShowWindow(hLabelGoodBad, SW_SHOW);
							ShowWindow(hLabelOrderChaos, SW_SHOW);
							ShowWindow(hLabelOpenHidden, SW_SHOW);
							ShowWindow(hLabelPhysMyst, SW_SHOW);
							ShowWindow(hLabelHeadonStealth, SW_SHOW);
							ShowWindow(hLabelMoneyXp, SW_SHOW);
							ShowWindow(hBtnSetSliders, SW_SHOW);
							ShowWindow(hSlidersHint1, SW_SHOW);
							ShowWindow(hSlidersHint2, SW_SHOW);
							ShowWindow(hItemValueHint1, SW_HIDE);
							ShowWindow(hItemValueHint2, SW_HIDE);
							// Hide all generic checkboxes
							ShowWindow(GetDlgItem(hDlg, IDC_ITEM_MATCH_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_ITEM_HIGHLIGHT_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_LOC_MATCH_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_LOC_HIGHLIGHT_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_TYPE_MATCH_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_TYPE_HIGHLIGHT_CB), SW_HIDE);
							ShowWindow(hItemMatchLabel, SW_HIDE);
							ShowWindow(hItemHighlightLabel, SW_HIDE);
							ShowWindow(hLocMatchLabel, SW_HIDE);
							ShowWindow(hLocHighlightLabel, SW_HIDE);
							ShowWindow(hTypeMatchLabel, SW_HIDE);
							ShowWindow(hTypeHighlightLabel, SW_HIDE);
							SyncSliderValues(hDlg);
							break;
					
						default:
							// Fallback – hide all generic checkboxes
							ShowWindow(GetDlgItem(hDlg, IDC_ITEM_MATCH_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_ITEM_HIGHLIGHT_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_LOC_MATCH_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_LOC_HIGHLIGHT_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_TYPE_MATCH_CB), SW_HIDE);
							ShowWindow(GetDlgItem(hDlg, IDC_TYPE_HIGHLIGHT_CB), SW_HIDE);
							break;
					}
					currentTab = newTab;
				}
				// Double-click handling for listviews (outside the tab change block)
				else if (pnmh->idFrom == IDC_ITEM_LIST && pnmh->code == NM_DBLCLK) {
					PostMessage(hDlg, WM_COMMAND, MAKEWPARAM(IDC_BTN_EDIT, BN_CLICKED), 0);
					return TRUE;
				}
				else if (pnmh->idFrom == IDC_DISABLED_LIST && pnmh->code == NM_DBLCLK) {
						int sel = ListView_GetNextItem(hDisabledList, -1, LVNI_SELECTED);
						if (sel < 0) return TRUE;
						PUU32 record = puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
						for (int i = 0; i < sel && record; i++)
							record = puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETNEXTRECORD, record, 0);
						if (!record) return TRUE;
						PUU8* oldStr = (PUU8*)puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETFIELDVAL, record, 0);
						if (!oldStr) return TRUE;
					
						ItemEditData data;
						memset(&data, 0, sizeof(data));
						const char* str = (const char*)oldStr;
						if (strchr(str, ';') || strchr(str, '^')) {
							ParseItemString(str, data.itemName, sizeof(data.itemName),
											&data.disabled, &data.force, &data.limit, data.exclude, sizeof(data.exclude));
						} else {
							ParseDisplayString(str, data.itemName, sizeof(data.itemName),
											&data.disabled, &data.force, &data.limit, data.exclude, sizeof(data.exclude));
						}
						data.isAdd = 0;
					
						if (ShowItemEditDialog(hDlg, &data, 0)) {
							char raw[512], newDisplay[1024];
							BuildItemString(raw, sizeof(raw), data.itemName, data.disabled, data.force, data.limit, data.exclude);
							FormatItemForDisplay(raw, newDisplay, sizeof(newDisplay));
							puSetAttribute(g_DisabledItemWatchList, PUA_TABLE_CURRENTFIELD, 0);
							puSetAttribute(g_DisabledItemWatchList, PUA_TABLE_CURRENTRECORD, record);
							puDoMethod(g_DisabledItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)newDisplay, 0);
							PopulateDisabledList(hDisabledList);
						}
						return TRUE;
					}
				else if (pnmh->idFrom == IDC_LOCATION_LIST && pnmh->code == NM_DBLCLK) {
					// Optional: edit location (not implemented yet)
					return TRUE;
				}
				break;
			}
	case WM_CTLCOLORDLG:
		{
			HDC hdc = (HDC)wParam;
			static HBRUSH hBrush = NULL;
			if (!hBrush)
				hBrush = CreateSolidBrush(RGB(170, 170, 170));
			return (INT_PTR)hBrush;
		}
		
	case WM_DRAWITEM:
		{
			DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT*)lParam;
			if (dis->CtlType != ODT_BUTTON) break;
		
			HDC hdc = dis->hDC;
			RECT rc = dis->rcItem;
		
			// Colors
			COLORREF bgNormal   = RGB(104, 137, 161);
			COLORREF bgHover    = RGB(70, 70, 90);
			COLORREF bgPressed  = RGB(30, 30, 40);
			COLORREF borderCol  = RGB(100, 100, 140);
			COLORREF textCol    = RGB(0, 0, 0);
		
			// Pick bg based on state
			COLORREF bg = bgNormal;
			if (dis->itemState & ODS_SELECTED)
				bg = bgPressed;
		
			// Fill background
			HBRUSH hBrush = CreateSolidBrush(bg);
			FillRect(hdc, &rc, hBrush);
			DeleteObject(hBrush);
		
			// Draw rounded border
			HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
			HPEN hOldPen = SelectObject(hdc, hPen);
			HBRUSH hOldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
			RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);  // 6,6 controls corner radius
			SelectObject(hdc, hOldPen);
			SelectObject(hdc, hOldBrush);
			DeleteObject(hPen);
		
			// Draw text
			char text[64] = "";
			GetWindowTextA(dis->hwndItem, text, sizeof(text));
			SetTextColor(hdc, textCol);
			SetBkMode(hdc, TRANSPARENT);
			DrawTextA(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		
			return TRUE;
		}
		
	case WM_MOVING:
		{
			HWND hMain = (HWND)puGetAttribute(g_MainWin, PUA_WINDOW_HANDLE);
			RECT rcMain;
			GetWindowRect(hMain, &rcMain);
		
			RECT *rcDlg = (RECT*)lParam;
			int dlgW = rcDlg->right - rcDlg->left;
			int dlgH = rcDlg->bottom - rcDlg->top;
		
			rcDlg->left   = rcMain.right - 16;
			rcDlg->top    = rcMain.top;
			rcDlg->right  = rcMain.right - 16 + dlgW;
			rcDlg->bottom = rcMain.top + dlgH;
		
			return TRUE;
		}
		
	case WM_CLOSE:
			DestroyWindow(hDlg);
		return TRUE;

    case WM_COMMAND:
        {
            WORD id = LOWORD(wParam);
            // === Items tab buttons ===
            if (id == IDC_BTN_ADD) {
                ItemEditData data = {0};
                data.limit = 1;
                data.isAdd = 1;
                if (ShowItemEditDialog(hDlg, &data, 1)) {
                    char raw[512], display[1024];
                    BuildItemString(raw, sizeof(raw), data.itemName, data.disabled, data.force, data.limit, data.exclude);
                    FormatItemForDisplay(raw, display, sizeof(display));
                    puDoMethod(g_ItemWatchList, PUM_TABLE_NEWRECORD, 0, 0);
                    puDoMethod(g_ItemWatchList, PUM_TABLE_ADDRECORD, 0, 0);
                    puDoMethod(g_ItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)display, 0);
                    PopulateItemList(hItemList, g_ItemWatchList);
                }
            }
            else if (id == IDC_BTN_REMOVE) {
					// Items tab removal
					if (currentTab == 0) {
						int sel = ListView_GetNextItem(hItemList, -1, LVNI_SELECTED);
						if (sel >= 0) {
							PUU32 record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
							for (int i = 0; i < sel && record; i++)
								record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETNEXTRECORD, record, 0);
							if (record)
								puDoMethod(g_ItemWatchList, PUM_TABLE_REMRECORD, record, 0);
							PopulateItemList(hItemList, g_ItemWatchList);
						}
					}
				}
				else if (id == IDC_BTN_REMOVE_DISABLED) {
					// Disabled tab removal
					if (currentTab == 1) {
						int sel = ListView_GetNextItem(hDisabledList, -1, LVNI_SELECTED);
						if (sel >= 0) {
							PUU32 record = puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
							for (int i = 0; i < sel && record; i++)
								record = puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETNEXTRECORD, record, 0);
							if (record)
								puDoMethod(g_DisabledItemWatchList, PUM_TABLE_REMRECORD, record, 0);
							PopulateDisabledList(hDisabledList);
						}
					}
				}
            else if (id == IDC_BTN_EDIT) {
						int sel = ListView_GetNextItem(hItemList, -1, LVNI_SELECTED);
						if (sel < 0) {
							MessageBox(hDlg, "No item selected.", "Edit", MB_OK);
							break;
						}
						PUU32 record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
						for (int i = 0; i < sel && record; i++)
							record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETNEXTRECORD, record, 0);
						if (!record) break;
						PUU8* oldStr = (PUU8*)puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIELDVAL, record, 0);
						if (!oldStr) break;
					
						ItemEditData data;
						memset(&data, 0, sizeof(data));
					
						const char *str = (const char*)oldStr;
						// Check if the string looks like raw format (contains ';' or '^')
						if (strchr(str, ';') || strchr(str, '^')) {
							// Raw format: parse with ParseItemString
							ParseItemString(str, data.itemName, sizeof(data.itemName),
											&data.disabled, &data.force, &data.limit, data.exclude, sizeof(data.exclude));
						} else {
							// Formatted display: parse with ParseDisplayString
							ParseDisplayString(str, data.itemName, sizeof(data.itemName),
											&data.disabled, &data.force, &data.limit, data.exclude, sizeof(data.exclude));
						}
						data.isAdd = 0;
					
						if (ShowItemEditDialog(hDlg, &data, 0)) {
							char raw[512], newDisplay[1024];
							BuildItemString(raw, sizeof(raw), data.itemName, data.disabled, data.force, data.limit, data.exclude);
							FormatItemForDisplay(raw, newDisplay, sizeof(newDisplay));
							puSetAttribute(g_ItemWatchList, PUA_TABLE_CURRENTFIELD, 0);
							puSetAttribute(g_ItemWatchList, PUA_TABLE_CURRENTRECORD, record);
							puDoMethod(g_ItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)newDisplay, 0);
							PopulateItemList(hItemList, g_ItemWatchList);
						}
					}
            else if (id == IDC_BTN_DISABLE) {
                int sel = ListView_GetNextItem(hItemList, -1, LVNI_SELECTED);
                if (sel >= 0) {
                    PUU32 record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
                    for (int i = 0; i < sel && record; i++)
                        record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETNEXTRECORD, record, 0);
                    if (record) {
                        PUU8 *display = (PUU8*)puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIELDVAL, record, 0);
                        if (display) {
                            puDoMethod(g_DisabledItemWatchList, PUM_TABLE_NEWRECORD, 0, 0);
                            puDoMethod(g_DisabledItemWatchList, PUM_TABLE_ADDRECORD, 0, 0);
                            puDoMethod(g_DisabledItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)display, 0);
                            puDoMethod(g_ItemWatchList, PUM_TABLE_REMRECORD, record, 0);
                            PopulateItemList(hItemList, g_ItemWatchList);
                            PopulateDisabledList(hDisabledList);
                        }
                    }
                }
            }
            else if (id == IDC_BTN_ENABLE) {
                int sel = ListView_GetNextItem(hDisabledList, -1, LVNI_SELECTED);
                if (sel >= 0) {
                    PUU32 record = puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
                    for (int i = 0; i < sel && record; i++)
                        record = puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETNEXTRECORD, record, 0);
                    if (record) {
                        PUU8 *display = (PUU8*)puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETFIELDVAL, record, 0);
                        if (display) {
                            puDoMethod(g_ItemWatchList, PUM_TABLE_NEWRECORD, 0, 0);
                            puDoMethod(g_ItemWatchList, PUM_TABLE_ADDRECORD, 0, 0);
                            puDoMethod(g_ItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)display, 0);
                            puDoMethod(g_DisabledItemWatchList, PUM_TABLE_REMRECORD, record, 0);
                            PopulateItemList(hItemList, g_ItemWatchList);
                            PopulateDisabledList(hDisabledList);
                        }
                    }
                }
            }
            else if (id == IDC_BTN_REMOVE_ALL) {
                if (MessageBox(hDlg, "Remove ALL items from active list?", "Confirm", MB_YESNO | MB_ICONWARNING) == IDYES) {
                    PUU32 record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
                    while (record) {
                        puDoMethod(g_ItemWatchList, PUM_TABLE_REMRECORD, record, 0);
                        record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
                    }
                    PopulateItemList(hItemList, g_ItemWatchList);
                }
            }
            else if (id == IDC_BTN_REMOVE_ALL_DISABLED) {
                if (MessageBox(hDlg, "Remove ALL items from disabled list?", "Confirm", MB_YESNO | MB_ICONWARNING) == IDYES) {
                    PUU32 record = puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
                    while (record) {
                        puDoMethod(g_DisabledItemWatchList, PUM_TABLE_REMRECORD, record, 0);
                        record = puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
                    }
                    PopulateDisabledList(hDisabledList);
                }
            }
            else if (id == IDC_BTN_IMPORT) {
                char filename[MAX_PATH];
                if (GetFile(hDlg, FALSE, filename, sizeof(filename))) {
                    int choice = MessageBox(hDlg, "Import Items:\nYes = Append\nNo = Replace\nCancel = Cancel",
                                            "Import", MB_YESNOCANCEL | MB_ICONQUESTION);
                    if (choice == IDYES) ImportItemsFromFile(filename, 0);
                    else if (choice == IDNO) ImportItemsFromFile(filename, 1);
                    PopulateItemList(hItemList, g_ItemWatchList);
                }
            }
            else if (id == IDC_BTN_EXPORT) {
                char filename[MAX_PATH];
                if (GetFile(hDlg, TRUE, filename, sizeof(filename))) {
                    ExportItemsOnly(filename);
                    MessageBox(hDlg, "Export complete.", "Export", MB_OK);
                }
            }
            else if (id == IDC_BTN_REMOVE_DUPLICATES) {
                RemoveDuplicateItems();
                PopulateItemList(hItemList, g_ItemWatchList);
            }
            // === Locations tab ===
            else if (id == IDC_LOCATION_ADD) {
                char loc[256] = "";
                if (InputBox(hDlg, "Enter location name or coordinates:", loc, sizeof(loc))) {
                    puDoMethod(g_LocWatchList, PUM_TABLE_NEWRECORD, 0, 0);
                    puDoMethod(g_LocWatchList, PUM_TABLE_ADDRECORD, 0, 0);
                    puDoMethod(g_LocWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)loc, 0);
                    PopulateLocationList(hLocationList);
					ListView_SetColumnWidth(hLocationList, 0, LVSCW_AUTOSIZE_USEHEADER);
                }
            }
            else if (id == IDC_LOCATION_REMOVE) {
                int sel = ListView_GetNextItem(hLocationList, -1, LVNI_SELECTED);
                if (sel >= 0) {
                    PUU32 record = puDoMethod(g_LocWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
                    for (int i = 0; i < sel && record; i++)
                        record = puDoMethod(g_LocWatchList, PUM_TABLE_GETNEXTRECORD, record, 0);
                    if (record)
                        puDoMethod(g_LocWatchList, PUM_TABLE_REMRECORD, record, 0);
                    PopulateLocationList(hLocationList);
                }
            }
            // === Mission Types checkboxes ===
            else if (id == IDC_TYPE_REPAIR || id == IDC_TYPE_RETURN || id == IDC_TYPE_FINDP ||
                     id == IDC_TYPE_FINDI || id == IDC_TYPE_ASS) {
                int checked = (IsDlgButtonChecked(hDlg, id) == BST_CHECKED);
                switch (id) {
                    case IDC_TYPE_REPAIR: PUL_SET_CB(CS_TYPEREPAIR_CB, checked); break;
                    case IDC_TYPE_RETURN: PUL_SET_CB(CS_TYPERETURN_CB, checked); break;
                    case IDC_TYPE_FINDP:  PUL_SET_CB(CS_TYPEFINDP_CB, checked); break;
                    case IDC_TYPE_FINDI:  PUL_SET_CB(CS_TYPEFINDI_CB, checked); break;
                    case IDC_TYPE_ASS:    PUL_SET_CB(CS_TYPEASS_CB, checked); break;
                }
            }
            // === Item Values tab ===
            else if (id == IDC_VALUE_BUYMOD) {
                if (HIWORD(wParam) == EN_CHANGE) {
                    char buf[16];
                    GetDlgItemTextA(hDlg, IDC_VALUE_BUYMOD, buf, sizeof(buf));
                    int val = atoi(buf);
                    puSetAttribute(puGetObjectFromCollection(g_pCol, CS_ITEMVALUE_BUYMOD), PUA_TEXTENTRY_VALUE, val);
                }
            }
            else if (id == IDC_VALUE_SINGLE_ENABLE) {
                PUL_SET_CB(CS_ITEMVALUE_MSINGLE, IsDlgButtonChecked(hDlg, id) == BST_CHECKED);
            }
            else if (id == IDC_VALUE_SINGLE_EDIT && HIWORD(wParam) == EN_CHANGE) {
                char buf[32];
                GetDlgItemTextA(hDlg, IDC_VALUE_SINGLE_EDIT, buf, sizeof(buf));
                int val = atoi(buf);
                puSetAttribute(puGetObjectFromCollection(g_pCol, CS_ITEMVALUE_SINGLE), PUA_TEXTENTRY_VALUE, val);
            }
            else if (id == IDC_VALUE_TOTAL_ENABLE) {
                PUL_SET_CB(CS_ITEMVALUE_MTOTAL, IsDlgButtonChecked(hDlg, id) == BST_CHECKED);
            }
            else if (id == IDC_VALUE_TOTAL_EDIT && HIWORD(wParam) == EN_CHANGE) {
                char buf[32];
                GetDlgItemTextA(hDlg, IDC_VALUE_TOTAL_EDIT, buf, sizeof(buf));
                int val = atoi(buf);
                puSetAttribute(puGetObjectFromCollection(g_pCol, CS_ITEMVALUE_TOTAL), PUA_TEXTENTRY_VALUE, val);
            }
            // === Sliders tab ===
            else if (id == IDC_SLIDER_EASY_HARD && HIWORD(wParam) == EN_CHANGE) {
                int val = GetDlgItemInt(hDlg, IDC_SLIDER_EASY_HARD, NULL, FALSE);
                puSetAttribute(puGetObjectFromCollection(g_pCol, CS_SLIDER_EASY_HARD), PUA_TEXTENTRY_VALUE, val);
            }
            else if (id == IDC_SLIDER_GOOD_BAD && HIWORD(wParam) == EN_CHANGE) {
                int val = GetDlgItemInt(hDlg, IDC_SLIDER_GOOD_BAD, NULL, FALSE);
                puSetAttribute(puGetObjectFromCollection(g_pCol, CS_SLIDER_GOOD_BAD), PUA_TEXTENTRY_VALUE, val);
            }
            else if (id == IDC_SLIDER_ORDER_CHAOS && HIWORD(wParam) == EN_CHANGE) {
                int val = GetDlgItemInt(hDlg, IDC_SLIDER_ORDER_CHAOS, NULL, FALSE);
                puSetAttribute(puGetObjectFromCollection(g_pCol, CS_SLIDER_ORDER_CHAOS), PUA_TEXTENTRY_VALUE, val);
            }
            else if (id == IDC_SLIDER_OPEN_HIDDEN && HIWORD(wParam) == EN_CHANGE) {
                int val = GetDlgItemInt(hDlg, IDC_SLIDER_OPEN_HIDDEN, NULL, FALSE);
                puSetAttribute(puGetObjectFromCollection(g_pCol, CS_SLIDER_OPEN_HIDDEN), PUA_TEXTENTRY_VALUE, val);
            }
            else if (id == IDC_SLIDER_PHYS_MYST && HIWORD(wParam) == EN_CHANGE) {
                int val = GetDlgItemInt(hDlg, IDC_SLIDER_PHYS_MYST, NULL, FALSE);
                puSetAttribute(puGetObjectFromCollection(g_pCol, CS_SLIDER_PHYS_MYST), PUA_TEXTENTRY_VALUE, val);
            }
            else if (id == IDC_SLIDER_HEADON_STEALTH && HIWORD(wParam) == EN_CHANGE) {
                int val = GetDlgItemInt(hDlg, IDC_SLIDER_HEADON_STEALTH, NULL, FALSE);
                puSetAttribute(puGetObjectFromCollection(g_pCol, CS_SLIDER_HEADON_STEALTH), PUA_TEXTENTRY_VALUE, val);
            }
            else if (id == IDC_SLIDER_MONEY_XP && HIWORD(wParam) == EN_CHANGE) {
                int val = GetDlgItemInt(hDlg, IDC_SLIDER_MONEY_XP, NULL, FALSE);
                puSetAttribute(puGetObjectFromCollection(g_pCol, CS_SLIDER_MONEY_XP), PUA_TEXTENTRY_VALUE, val);
            }
            else if (id == IDC_BTN_SET_SLIDERS) {
                int easy_hard = GetDlgItemInt(hDlg, IDC_SLIDER_EASY_HARD, NULL, FALSE);
                int good_bad = GetDlgItemInt(hDlg, IDC_SLIDER_GOOD_BAD, NULL, FALSE);
                int order_chaos = GetDlgItemInt(hDlg, IDC_SLIDER_ORDER_CHAOS, NULL, FALSE);
                int open_hidden = GetDlgItemInt(hDlg, IDC_SLIDER_OPEN_HIDDEN, NULL, FALSE);
                int phys_myst = GetDlgItemInt(hDlg, IDC_SLIDER_PHYS_MYST, NULL, FALSE);
                int headon_stealth = GetDlgItemInt(hDlg, IDC_SLIDER_HEADON_STEALTH, NULL, FALSE);
                int money_xp = GetDlgItemInt(hDlg, IDC_SLIDER_MONEY_XP, NULL, FALSE);
                _setSliders(easy_hard, good_bad, order_chaos, open_hidden, phys_myst, headon_stealth, money_xp);
            }
			  // === New checkboxes for Match and Highlight ===
			else if (id == IDC_ITEM_MATCH_CB) {
				PUL_SET_CB(CS_ALERTITEM_CB, IsDlgButtonChecked(hDlg, IDC_ITEM_MATCH_CB) == BST_CHECKED);
			}
			else if (id == IDC_ITEM_HIGHLIGHT_CB) {
				PUL_SET_CB(CS_HIGHLIGHTITEM_CB, IsDlgButtonChecked(hDlg, IDC_ITEM_HIGHLIGHT_CB) == BST_CHECKED);
			}
			else if (id == IDC_LOC_MATCH_CB) {
				PUL_SET_CB(CS_ALERTLOC_CB, IsDlgButtonChecked(hDlg, IDC_LOC_MATCH_CB) == BST_CHECKED);
			}
			else if (id == IDC_LOC_HIGHLIGHT_CB) {
				PUL_SET_CB(CS_HIGHLIGHTLOC_CB, IsDlgButtonChecked(hDlg, IDC_LOC_HIGHLIGHT_CB) == BST_CHECKED);
			}
			else if (id == IDC_TYPE_MATCH_CB) {
				PUL_SET_CB(CS_ALERTTYPE_CB, IsDlgButtonChecked(hDlg, IDC_TYPE_MATCH_CB) == BST_CHECKED);
			}
			else if (id == IDC_TYPE_HIGHLIGHT_CB) {
				PUL_SET_CB(CS_HIGHLIGHTTYPE_CB, IsDlgButtonChecked(hDlg, IDC_TYPE_HIGHLIGHT_CB) == BST_CHECKED);
			}
			else if (id == IDC_VALUE_HIGHLIGHT_CB) {
				PUL_SET_CB(CS_HIGHLIGHTTYPE_CB, IsDlgButtonChecked(hDlg, IDC_VALUE_HIGHLIGHT_CB) == BST_CHECKED);
			}
        }
        break;

    case WM_DESTROY:
        g_hHiddenTabsWnd = NULL;
        break;
    }
    return FALSE;
}

// ========== Helper: ShowModalMessage (used by multiple places) ==========
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

// ========== TIMER BASED BUYING AGENT (declarations) ==========
#define TIMER_BUYINGAGENT 1
static UINT_PTR g_TimerID = 0;
static int g_PendingAttemptNumber = 0;

// ========== FORWARD DECLARATIONS FOR MATCH LIST AND OTHER DIALOGS ==========
typedef struct {
    const char **matches;
    int count;
    char selected[256];
    char originalSearch[256];
    char excludeWords[256];
} MatchListData;

typedef struct ItemCounter {
    char *itemName;
    int limit;
    int accepted;
    struct ItemCounter *next;
} ItemCounter;

ItemCounter *g_ItemCounters = NULL;

static INT_PTR CALLBACK MatchListDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK ItemEditDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK MassAddDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

// Forward declaration for the subclass procedure
LRESULT CALLBACK MainWndProcHook(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
static LRESULT CALLBACK BAWndProcHook(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);


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
	
	INITCOMMONCONTROLSEX icc = { sizeof(INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES };
	InitCommonControlsEx(&icc);

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
			
		case CSAM_SHOW_HIDDEN_TABS:
			if (!g_hHiddenTabsWnd || !IsWindow(g_hHiddenTabsWnd)) {
				g_hHiddenTabsWnd = CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_HIDDEN_TABS),
												(HWND)puGetAttribute(g_MainWin, PUA_WINDOW_HANDLE),
												HiddenTabsDlgProc);
			}
		
			if (IsWindowVisible(g_hHiddenTabsWnd)) {
				ShowWindow(g_hHiddenTabsWnd, SW_HIDE);
			}
			else {
				// Always reposition before showing
				HWND hMain = (HWND)puGetAttribute(g_MainWin, PUA_WINDOW_HANDLE);
				RECT rcMain;
				GetWindowRect(hMain, &rcMain);
				RECT rcDlg;
				GetWindowRect(g_hHiddenTabsWnd, &rcDlg);
				int dlgW = rcDlg.right - rcDlg.left;
				int dlgH = rcDlg.bottom - rcDlg.top;
				SetWindowPos(g_hHiddenTabsWnd, NULL,
					rcMain.right - 16,
					rcMain.top,
					dlgW, dlgH,
					SWP_NOZORDER | SWP_NOSIZE);
		
				ShowWindow(g_hHiddenTabsWnd, SW_SHOW);
				SetForegroundWindow(g_hHiddenTabsWnd);
			}
			break;
			
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

int OpenLocalDB()
{
    char DBPath[MAX_PATH];
    sprintf(DBPath, "%s\\cd_image\\rdb.db", g_AODir);
    if (sqlite3_open_v2(DBPath, &g_pSQLite, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
        return FALSE;
    sqlite3_prepare_v2(g_pSQLite, "SELECT data FROM rdb_1000020 WHERE id = ?;", -1, &g_stmtItem, NULL);
    sqlite3_prepare_v2(g_pSQLite, "SELECT data FROM rdb_1010008 WHERE id = ?;", -1, &g_stmtIcon, NULL);
    sqlite3_prepare_v2(g_pSQLite, "SELECT data FROM rdb_1000001 WHERE id = ?;", -1, &g_stmtPF, NULL);
    return TRUE;
}

void ReleaseAODatabase()
{
    if (g_stmtItem) sqlite3_finalize(g_stmtItem);
    if (g_stmtIcon) sqlite3_finalize(g_stmtIcon);
    if (g_stmtPF)   sqlite3_finalize(g_stmtPF);
    if (g_pSQLite)  sqlite3_close(g_pSQLite);
}

static void RemoveDuplicateItems(void)
{
    // We'll build a hash set of display strings, keeping first occurrence.
    // Simple O(n^2) for small lists is fine; if list large, use temporary array.
    PUU32 record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    PUU32 prevRecord = 0;
    int removed = 0;

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

static void MoveCurrentActiveToDisabled(void)
{
    PUU32 listView = puGetObjectFromCollection(g_pCol, CS_ITEMWATCH_LISTVIEW);
    int selectedIndex = (int)puGetAttribute(listView, PUA_LISTVIEW_SELECTED);
    if (selectedIndex < 0) {
        ShowModalMessage(NULL, "No item selected.", "ClickSaver", MB_OK | MB_ICONINFORMATION);
        return;
    }

    PUU32 record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    for (int i = 0; i < selectedIndex && record; i++)
        record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETNEXTRECORD, record, 0);
    if (!record) {
        ShowModalMessage(NULL, "No item selected or could not find the selected record.", "ClickSaver", MB_OK | MB_ICONWARNING);
        return;
    }

    PUU8 *display = (PUU8*)puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIELDVAL, record, 0);
    if (!display || !*display) return;

    puDoMethod(g_DisabledItemWatchList, PUM_TABLE_NEWRECORD, 0, 0);
    puDoMethod(g_DisabledItemWatchList, PUM_TABLE_ADDRECORD, 0, 0);
    puDoMethod(g_DisabledItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)display, 0);

    puDoMethod(listView, PUM_LISTVIEW_REMOVE, 0, 0);

    int maxRows = (int)puGetAttribute(listView, PUA_LISTVIEW_SELECTED);
    if (maxRows == -1) {
        puSetAttribute(listView, PUA_LISTVIEW_SELECTED, -1);
        return;
    }
    int newIndex = selectedIndex;
    if (newIndex >= maxRows) newIndex = maxRows - 1;
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

    PUU32 record = puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    for (int i = 0; i < selectedIndex && record; i++)
        record = puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETNEXTRECORD, record, 0);
    if (!record) {
        ShowModalMessage(NULL, "No item selected or could not find the selected disabled record.", "ClickSaver", MB_OK | MB_ICONWARNING);
        return;
    }

    PUU8 *display = (PUU8*)puDoMethod(g_DisabledItemWatchList, PUM_TABLE_GETFIELDVAL, record, 0);
    if (!display || !*display) return;

    puDoMethod(g_ItemWatchList, PUM_TABLE_NEWRECORD, 0, 0);
    puDoMethod(g_ItemWatchList, PUM_TABLE_ADDRECORD, 0, 0);
    puDoMethod(g_ItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)display, 0);

    puDoMethod(listView, PUM_LISTVIEW_REMOVE, 0, 0);

    int maxRows = (int)puGetAttribute(listView, PUA_LISTVIEW_SELECTED);
    if (maxRows == -1) {
        puSetAttribute(listView, PUA_LISTVIEW_SELECTED, -1);
        return;
    }
    int newIndex = selectedIndex;
    if (newIndex >= maxRows) newIndex = maxRows - 1;
    puSetAttribute(listView, PUA_LISTVIEW_SELECTED, newIndex);
}

static int ItemExistsInActiveList(const char *displayString)
{
    PUU32 record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    while (record) {
        PUU8 *existing = (PUU8*)puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIELDVAL, record, 0);
        if (existing && strcmp((char*)existing, displayString) == 0)
            return 1;
        record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETNEXTRECORD, record, 0);
    }
    return 0;
}

static void ImportItemsFromFile(const char *filename, int replaceMode)
{
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
        line[strcspn(line, "\r\n")] = 0;
        if (strcmp(line, "::ItemWatch::") == 0) {
            inItemSection = 1;
            continue;
        }
        if (strcmp(line, "::END::") == 0 || strncmp(line, "::", 2) == 0)
            break;
        if (!inItemSection) continue;
        if (strlen(line) == 0) continue;

        char display[1024];
        MakeTableEntry(display, sizeof(display), line);
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
    strcpy(fullpath, filename);
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

ItemCounter* FindItemCounter(const char *name)
{
    ItemCounter *cur = g_ItemCounters;
    while (cur) {
        if (strcmp(cur->itemName, name) == 0) return cur;
        cur = cur->next;
    }
    return NULL;
}

void AddItemCounter(const char *name, int limit)
{
    if (limit <= 0) return;
    ItemCounter *new_item = (ItemCounter*)malloc(sizeof(ItemCounter));
    new_item->itemName = _strdup(name);
    new_item->limit = limit;
    new_item->accepted = 0;
    new_item->next = g_ItemCounters;
    g_ItemCounters = new_item;
}

void ClearItemCounters(void)
{
    if (g_bBuyingAgentActive) return;
    if (g_BuyingAgentCount > 0 || g_BuyingAgentMissions > 0) return;
    ItemCounter *cur = g_ItemCounters;
    while (cur) {
        ItemCounter *next = cur->next;
        free(cur->itemName);
        free(cur);
        cur = next;
    }
    g_ItemCounters = NULL;
}

static int HasActiveWatchlistItems(void)
{
    PUU32 Record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIRSTRECORD, 0, 0);
    while (Record) {
        PUU8* pString = (PUU8*)puDoMethod(g_ItemWatchList, PUM_TABLE_GETFIELDVAL, Record, 0);
        if (pString && *pString) {
            char itemName[256];
            int disabled = 0, force = 0, limit = 0;
            char exclude[256];
            ParseDisplayString((char*)pString, itemName, sizeof(itemName),
                               &disabled, &force, &limit, exclude, sizeof(exclude));
            if (limit == 0)
                return 1;
            else {
                ItemCounter *ic = FindItemCounter(itemName);
                if (!ic || ic->accepted < ic->limit)
                    return 1;
            }
        }
        Record = puDoMethod(g_ItemWatchList, PUM_TABLE_GETNEXTRECORD, Record, 0);
    }
    return 0;
}


static INT_PTR CALLBACK MatchListDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    MatchListData *pData = (MatchListData*)GetWindowLongPtr(hDlg, DWLP_USER);

    switch (msg) {
    case WM_INITDIALOG:
        pData = (MatchListData*)lParam;
        SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)pData);
        HWND hList = GetDlgItem(hDlg, IDC_MATCH_LIST);
        for (int i = 0; i < pData->count; i++)
            SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)pData->matches[i]);
        char hint[512];
        if (pData->excludeWords[0] != '\0') {
            sprintf(hint, "Hint: Double click an item to select it.\n\nUsing \"%s\" with exclusions (%s) matches %d item(s).",
                    pData->originalSearch, pData->excludeWords, pData->count);
        } else {
            sprintf(hint, "Hint: Double click an item to select it.\n\nUsing \"%s\" matches %d item(s).",
                    pData->originalSearch, pData->count);
        }
        SetDlgItemTextA(hDlg, IDC_MATCH_HINT, hint);
        SetFocus(GetDlgItem(hDlg, IDC_MATCH_EDIT));
        return FALSE;

    case WM_COMMAND:
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
    return FALSE;
}

static INT_PTR CALLBACK ItemEditDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ItemEditData *pData = (ItemEditData*)GetWindowLongPtr(hDlg, DWLP_USER);
    static HBRUSH hBrush = NULL;

    switch (msg) {
    case WM_INITDIALOG:
        pData = (ItemEditData*)lParam;
        SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)pData);
        SetWindowTextA(hDlg, pData->isAdd ? "Add Item" : "Edit Item");
        SetDlgItemTextA(hDlg, IDC_ITEM_NAME, pData->itemName);
        SetDlgItemInt(hDlg, IDC_LIMIT, pData->limit, FALSE);
        CheckDlgButton(hDlg, IDC_FORCE, pData->force ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemTextA(hDlg, IDC_EXCLUDE, pData->exclude);
        hBrush = CreateSolidBrush(RGB(170, 170, 170));
        return TRUE;

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
        if (hBrush) {
            SetBkMode((HDC)wParam, TRANSPARENT);
            SetTextColor((HDC)wParam, RGB(0, 0, 0));
            return (INT_PTR)hBrush;
        }
        break;

    case WM_DRAWITEM:
        {
            DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT*)lParam;
            if (dis->CtlType != ODT_BUTTON) break;

            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;

            // Colors (same as hidden tabs)
            COLORREF bgNormal   = RGB(104, 137, 161);
            COLORREF bgPressed  = RGB(30, 30, 40);
            COLORREF borderCol  = RGB(100, 100, 140);
            COLORREF textCol    = RGB(0, 0, 0);

            // Pick bg based on state (pressed only, no hover for simplicity)
            COLORREF bg = bgNormal;
            if (dis->itemState & ODS_SELECTED)
                bg = bgPressed;

            // Fill background
            HBRUSH hBrushBtn = CreateSolidBrush(bg);
            FillRect(hdc, &rc, hBrushBtn);
            DeleteObject(hBrushBtn);

            // Draw rounded border
            HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
            HPEN hOldPen = SelectObject(hdc, hPen);
            HBRUSH hOldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
            SelectObject(hdc, hOldPen);
            SelectObject(hdc, hOldBrush);
            DeleteObject(hPen);

            // Draw text
            char text[64] = "";
            GetWindowTextA(dis->hwndItem, text, sizeof(text));
            SetTextColor(hdc, textCol);
            SetBkMode(hdc, TRANSPARENT);
            DrawTextA(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            return TRUE;
        }

    case WM_DESTROY:
        if (hBrush) DeleteObject(hBrush);
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            {
                char enteredName[256];
                GetDlgItemTextA(hDlg, IDC_ITEM_NAME, enteredName, sizeof(enteredName));
                char *start = enteredName;
                while (*start == ' ') start++;
                char *end = start + strlen(start) - 1;
                while (end > start && *end == ' ') end--;
                *(end + 1) = '\0';
                if (strlen(start) == 0) {
                    MessageBox(hDlg, "Item name cannot be empty.", "Validation", MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                char excludeTemp[256];
                GetDlgItemTextA(hDlg, IDC_EXCLUDE, excludeTemp, sizeof(excludeTemp));
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
                        if (result == IDOK && data.selected[0] != '\0')
                            strcpy(start, data.selected);
                        else if (result != IDC_USE_ORIGINAL) {
                            free((void*)matches);
                            return TRUE;
                        }
                    }
                    free((void*)matches);
                } else {
                    char msg[512];
                    sprintf(msg, "Item \"%s\" would not match any known item.\n\nAdd it anyway?", start);
                    if (MessageBox(hDlg, msg, "Unknown Item", MB_YESNO | MB_ICONQUESTION) != IDYES)
                        return TRUE;
                }
                strcpy(pData->itemName, start);
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

static INT_PTR CALLBACK MassAddDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
        SetFocus(GetDlgItem(hDlg, IDC_MASS_EDIT));
        return FALSE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            {
                char text[65536];
                GetDlgItemTextA(hDlg, IDC_MASS_EDIT, text, sizeof(text));
                char *p = text;
                char line[1024];
                int lineIdx;
                while (*p) {
                    lineIdx = 0;
                    while (*p && *p != '\r' && *p != '\n') {
                        if (lineIdx < (int)sizeof(line)-1) line[lineIdx++] = *p;
                        p++;
                    }
                    line[lineIdx] = '\0';
                    if (lineIdx == 0) { while (*p == '\r' || *p == '\n') p++; continue; }
                    char *start = line;
                    while (*start == ' ' || *start == '\t') start++;
                    char *end = start + strlen(start) - 1;
                    while (end > start && (*end == ' ' || *end == '\t')) end--;
                    *(end + 1) = '\0';
                    if (*start == '\0') { while (*p == '\r' || *p == '\n') p++; continue; }
                    // Parse line
                    char *ptr = start;
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
                    while (trimEnd >= itemName && (*trimEnd == ' ' || *trimEnd == '\t')) *trimEnd-- = '\0';
                    ptr = nameEnd;
                    if (*ptr == ';') {
                        ptr++;
                        limit = atoi(ptr);
                        if (limit < 0) limit = 0;
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
                                if (wlen < (int)sizeof(word)-1) word[wlen++] = *ptr;
                                ptr++;
                            }
                            word[wlen] = '\0';
                            if (wlen > 0) {
                                if (excludeWords[0] != '\0') strcat(excludeWords, " ");
                                strcat(excludeWords, word);
                            }
                        } else break;
                    }
                    if (strlen(itemName) == 0) { while (*p == '\r' || *p == '\n') p++; continue; }
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

int ParseDisplayString(const char *display, char *itemName, size_t itemNameSize,
                              int *disabled, int *forceAccept, int *quantityLimit,
                              char *excludeWords, size_t excludeSize)
{
    *disabled = 0;
    *forceAccept = 0;
    *quantityLimit = 0;
    if (excludeWords) excludeWords[0] = '\0';
    if (itemName) itemName[0] = '\0';
    if (!display || !*display) return -1;

    // Copy to buffer
    char buf[1024];
    strncpy(buf, display, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';

    // Look for the first '[' to separate name from tags
    char *p = buf;
    char *nameEnd = strchr(p, '[');
    if (nameEnd) {
        // Name is before first '['
        size_t nameLen = nameEnd - p;
        // trim trailing spaces
        while (nameLen > 0 && p[nameLen-1] == ' ') nameLen--;
        if (nameLen >= itemNameSize) nameLen = itemNameSize-1;
        strncpy(itemName, p, nameLen);
        itemName[nameLen] = '\0';
        p = nameEnd;
    } else {
        // No brackets, treat whole string as name
        strncpy(itemName, p, itemNameSize-1);
        return 0;
    }

    // Parse tags inside brackets
    while (*p) {
        // skip to next '['
        while (*p && *p != '[') p++;
        if (!*p) break;
        p++; // skip '['
        // find closing ']'
        char *tagEnd = strchr(p, ']');
        if (!tagEnd) break;
        int tagLen = tagEnd - p;
        char tag[256];
        strncpy(tag, p, tagLen);
        tag[tagLen] = '\0';
        p = tagEnd + 1;

        // check tag content
        if (strcmp(tag, "disabled") == 0) {
            *disabled = 1;
        } else if (strcmp(tag, "force accept") == 0) {
            *forceAccept = 1;
        } else if (strncmp(tag, "qty ", 4) == 0) {
            *quantityLimit = atoi(tag + 4);
        } else if (strncmp(tag, "exclude: ", 9) == 0) {
            const char *excl = tag + 9;
            // copy exclude words, replace commas with spaces
            char exclBuf[256];
            strncpy(exclBuf, excl, sizeof(exclBuf)-1);
            exclBuf[sizeof(exclBuf)-1] = '\0';
            for (char *c = exclBuf; *c; c++) if (*c == ',') *c = ' ';
            strncpy(excludeWords, exclBuf, excludeSize-1);
            excludeWords[excludeSize-1] = '\0';
        }
    }
    return 0;
}

LRESULT CALLBACK MainWndProcHook(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_TIMER && wParam == TIMER_BUYINGAGENT)
    {
        KillTimer(hWnd, TIMER_BUYINGAGENT);
        g_TimerID = 0;
        puPostAppMessage(CSAM_BUYINGAGENT_TIMER, 0);
        return 0;
    }

    if (uMsg == WM_MOVE || uMsg == WM_MOVING)
    {
        if (g_hHiddenTabsWnd && IsWindow(g_hHiddenTabsWnd) && IsWindowVisible(g_hHiddenTabsWnd)) {
            RECT r;
            GetWindowRect(hWnd, &r);
            RECT rcDlg;
            GetWindowRect(g_hHiddenTabsWnd, &rcDlg);
            int dlgW = rcDlg.right - rcDlg.left;
            int dlgH = rcDlg.bottom - rcDlg.top;
            SetWindowPos(g_hHiddenTabsWnd, NULL,
				r.right - 16,
				r.top,
				dlgW, dlgH,
				SWP_NOZORDER | SWP_NOSIZE);
        }
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

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

void FormatItemForDisplay(const char *raw, char *out, size_t outSize)
{
    char itemName[256];
    int disabled = 0, force = 0, limit = 0;
    char exclude[256];

    // Parse the raw format (supports #, ~, ;N, ^exclude)
    ParseItemString(raw, itemName, sizeof(itemName),
                    &disabled, &force, &limit, exclude, sizeof(exclude));

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
        char tmp[256];
        strncpy(tmp, exclude, sizeof(tmp) - 1);
        tmp[sizeof(tmp)-1] = '\0';
        char *tok = strtok(tmp, " ");
        char formatted[256] = "";
        while (tok) {
            if (formatted[0])
                strncat(formatted, ", ", sizeof(formatted)-strlen(formatted)-1);
            strncat(formatted, tok, sizeof(formatted)-strlen(formatted)-1);
            tok = strtok(NULL, " ");
        }
        char buf[512];
        sprintf(buf, " [exclude: %s]", formatted);
        strncat(out, buf, outSize - strlen(out) - 1);
    }
}

void MakeTableEntry(char *dest, size_t destSize, const char *raw)
{
    FormatItemForDisplay(raw, dest, destSize);
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