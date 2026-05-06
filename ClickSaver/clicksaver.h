/*
 * ClickSaver - Anarchy Online mission helper
 * ... (copyright header) ...
 */

#ifndef __CLICKSAVER_H__
#define __CLICKSAVER_H__

#define CS_VERSION "1.2"

#include <windows.h>
#include "mission.h"
#include "sqlite3.h"

// AO Resource Type Constants
#define AODB_TYP_ITEM       1000020
#define AODB_TYP_ICON       1010008
#define AODB_TYP_PF         1000001

// GUI object IDs
enum
{
    CS_MAIN_WINDOW = 1,
    CS_TABS,
    CS_OPTIONS_TAB,
    CS_MISSIONS_TAB,

    CS_ITEMWATCH_TAB,
    CS_ITEMWATCH_LIST,
    CS_ITEMWATCH_LISTVIEW,

    CS_LOCWATCH_TAB,
    CS_LOCWATCH_LIST,
    CS_LOCWATCH_LISTVIEW,

    CS_TYPEWATCH_TAB,
    CS_TYPEREPAIR_CB,
    CS_TYPERETURN_CB,
    CS_TYPEFINDP_CB,
    CS_TYPEFINDI_CB,
    CS_TYPEASS_CB,

    CS_ITEMVALUES_TAB,
    CS_ITEMVALUE_BUYMOD,
    CS_ITEMVALUE_SINGLE,
    CS_ITEMVALUE_TOTAL,
    CS_ITEMVALUE_MSINGLE,
    CS_ITEMVALUE_MTOTAL,

    CS_SLIDER_TAB,
    CS_SLIDER_EASY_HARD,
    CS_SLIDER_GOOD_BAD,
    CS_SLIDER_ORDER_CHAOS,
    CS_SLIDER_OPEN_HIDDEN,
    CS_SLIDER_PHYS_MYST,
    CS_SLIDER_HEADON_STEALTH,
    CS_SLIDER_MONEY_XP,

    CS_MISSION1,
    CS_MISSION2,
    CS_MISSION3,
    CS_MISSION4,
    CS_MISSION5,

    CS_ERROR_WINDOW,
    CS_ERROR_TEXT,

    CS_STARTMIN_CB,
    CS_MSGBOX_CB,
    CS_BAINFO_CB,
    CS_BAINFO2_CB,

    CS_ALERTITEM_CB,
    CS_ALERTLOC_CB,
    CS_ALERTTYPE_CB,

    CS_SOUNDS_CB,
    CS_MOUSEMOVE_CB,
    CS_LOG_CB,
    CS_EXPAND_CB,

    CS_HIGHLIGHTITEM_CB,
    CS_HIGHLIGHTLOC_CB,
    CS_HIGHLIGHTTYPE_CB,
    CS_ITEMOPTIONAL_CB,

    CS_BUYINGAGENTFOLD,
    CS_BUYINGAGENTTRIES,
    CS_BUYINGAGENTMISH,
    CS_BUYINGAGENTDELAY_ENTRY,
    CS_BUYINGAGENTDELAY_BTN,

    CS_BUYINGAGENT_INFOWINDOW,
    CS_BUYINGAGENT_WINDOW,
    CS_FULLSCREEN_WINDOW,
    CS_ITEM_EDIT_DLG,
    CS_ITEM_EDIT_NAME,
    CS_ITEM_EDIT_DISABLED,
    CS_ITEM_EDIT_FORCE,
    CS_ITEM_EDIT_LIMIT,
    CS_ITEM_EDIT_EXCLUDE,
    CS_ITEM_ADD_BTN,
    CS_ITEM_EDIT_BTN,

    CS_WATCH_MSGBOX,

    CS_DBCOPYMSGBOX,
    CS_CREATINGDBMSGBOX,

    CS_OPTIONSFOLD3,
    CS_BA_PROGRESS,
    CS_BA_ACCEPTED,
    CS_BA_TOTAL,
    CS_BUYINGAGENT_PAUSEBTN,
    CS_BA_STATUS,

    // Disabled items tab – use WM_USER + numbers to avoid conflicts
    CS_DISABLED_ITEMWATCH_TAB = 1100,
    CS_DISABLED_ITEMWATCH_LIST,
    CS_DISABLED_ITEMWATCH_LISTVIEW,
    CS_DISABLE_BTN,
    CS_ENABLE_BTN
};

// App messages
enum
{
    CSAM_QUIT = 1,
    CSAM_SKIP,
    CSAM_OK,
    CSAM_CANCEL,
    CSAM_NEWMISSIONS,
    CSAM_PRESTARTBUYINGAGENT,
    CSAM_STARTBUYINGAGENT,
    CSAM_STOPBUYINGAGENT,
    CSAM_STARTFULLSCREEN,
    CSAM_STOPFULLSCREEN,
    CSAM_EXPORTSETTINGS,
    CSAM_IMPORTSETTINGS,

    CSAM_SET_SLIDERS,
    CSAM_PAUSEBUYINGAGENT,
    CSAM_UPDATE_DELAY,

    CSAM_BUYINGAGENT_TIMER,
    CSAM_EDIT_ITEM,
    CSAM_ITEM_EDIT_OK,
    CSAM_ITEM_ADD_OK,
    CSAM_ITEM_EDIT_CANCEL,

    CSAM_DISABLE_ITEM,
    CSAM_ENABLE_ITEM,
	CSAM_MASS_ADD_ITEMS,
	CSAM_REMOVE_ALL_ITEMS,
    CSAM_REMOVE_ALL_DISABLED,
	CSAM_IMPORT_ITEMS,
	CSAM_EXPORT_ITEMS,
    CSAM_REMOVE_DUPLICATE_ITEMS
};

// External variables
extern PULID g_ItemWatchList;
extern PULID g_LocWatchList;
extern PULID g_TypeWatchList;
extern pusObjectCollection* g_pCol;
extern PUU32 g_BuyingAgentCount;
extern PULID g_MainWin;
extern PUU8 g_MishNumber, g_FoundMish;
extern PUU8 g_bFullscreen;
extern PUU32 g_GUIDef[];
int IsWatchlistEntryValid(const char *searchStr);
int GetMatchingItems(const char *searchStr, const char ***outItems, int *outCount);
int GetFilteredMatchingItems(const char *baseName, const char *excludeWords, const char ***outItems, int *outCount);

// Endianness macros
#define EndianSwap16(x) ( ( x ) >> 8 | ( x ) << 8 )
#define EndianSwap32(x) ( ( x ) << 24 | ( ( x ) & 0xff00 ) << 8 | ( ( x ) >> 8 ) & 0xff00 | ( x ) >> 24 )

// PUL macros
#define PUL_GET_CB(x) puGetAttribute( puGetObjectFromCollection( g_pCol, (x) ), PUA_CHECKBOX_CHECKED )
#define PUL_SET_CB(x,y) puSetAttribute( puGetObjectFromCollection( g_pCol, (x) ), PUA_CHECKBOX_CHECKED, ( (y) ? 1 : 0) )

// Database functions
int OpenLocalDB();
void ReleaseAODatabase();
void* GetDataChunk( PUU32 _KeyHi, PUU32 _KeyLo, PUU32* _pSize );
void DebugPacket( void* pData, unsigned int length );
void WriteLog( const char* Format, ... );
void WriteDebug( const char* txt );

#endif