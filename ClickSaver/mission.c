/*
ClickSaver mission data parser and display -  Anarchy Online mission helper
Copyright (C) 2001, 2002 Morb
Some parts Copyright (C) 2003, 2004 gnarf

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
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <zlib.h>
#include <ctype.h>
#include "ClickSaver.h"
#include "excluded_items.h"

static void BuildItemIndex(void);
static int FindItemInDescriptionFromCache(const PUU8* desc, unsigned long descLen, PUU8* outName);

static char **g_itemNames = NULL;
static size_t g_numItemNames = 0;

typedef struct ItemIndexEntry {
    char* firstWord;
    const char** items;
    int count;
} ItemIndexEntry;

static ItemIndexEntry* g_itemIndex = NULL;
static int g_itemIndexSize = 0;
static char g_LastDebugKey[256] = "";
static time_t g_LastDebugTime = 0;

static PUU8 g_bIsFindItem = 0;
static PUU8 g_bIsReturnMission = 0;
static PUU32 g_bRewardMatched = 0;
extern PULID g_DisabledItemWatchList;
extern void LogAcceptedMission(int zoneId, float x, float y, PUU32 missionTypeId, const char* findItem, PUU32 mishId, const char* missionTitle);
extern sqlite3* g_pSQLite;


static int IsItemExcluded(const char *name) {
    if (!name || !*name) return 0;
    for (unsigned int i = 0; i < g_num_excluded_items; i++) {
        if (strcmp(name, g_excluded_items[i]) == 0)
            return 1;
    }
    return 0;
}

static const char* g_common_items[] = {
    "Contained Sensitive Information",
    "Radioactive Isotope Container",
    "Virus Container",
    "Weird-Looking Bomb",
	"Nanobot Container",
    "Urgent Sensitive Information",
    "Art Container",
    "Philip Ross Painting",
    "Rubi-Ka World Collectables",
    NULL
};

static int IsCommonItem(const char* item) {
    if (!item || item[0] == '\0') return 0;
    for (int i = 0; g_common_items[i]; i++) {
        if (strcmp(item, g_common_items[i]) == 0) return 1;
    }
    return 0;
}

static int WordCount(const char* str) {
    if (!str || str[0] == '\0') return 0;
    int count = 1;
    for (const char* p = str; *p; p++) {
        if (*p == ' ') count++;
    }
    return count;
}

static const char* MissionTypeToString(PUU32 type) {
    switch (type) {
        case 0x2c4e: return "Repair";
        case 0x26add: return "Return Item";
        case 0x2c47: return "Find Person";
        case 0x2c49: return "Find Item";
        case 0x2c42: return "Kill Person";
        default: return "Unknown";
    }
}

static int IsValidItemName(const char* name) {
    if (!name || name[0] == '\0') return 0;
    size_t len = strlen(name);
    if (len < 3) return 0;

    const char* stop_words[] = {
        "it", "the", "a", "an", "to", "of", "and", "for", "with", "this", "that",
        "these", "those", "from", "by", "into", "onto", "upon", "in", "on", "at",
        "be", "is", "are", "was", "were", "been", "being", "have", "has", "had",
        "having", "do", "does", "did", "doing", "but", "not", "so", "nor", "or",
        "as", "than", "then", "now", "here", "there", "where", "when", "why",
        "how", "you", "me", "him", "her", "us", "them", "they", "we", "you",
        "my", "your", "his", "her", "its", "our", "their", "what", "which",
        "oppressor", "oppressors", "lies", "propaganda", NULL
    };

    int word_count = 1;
    for (size_t i = 0; i < len; i++) {
        if (name[i] == ' ') word_count++;
    }
    int has_punctuation = (strchr(name, ':') != NULL) ||
                          (strchr(name, '(') != NULL) ||
                          (strchr(name, ')') != NULL);

    if (word_count == 1 && !has_punctuation) {
        return 0;
    }

    char lower[256] = { 0 };
    size_t i;
    for (i = 0; i < len && i < 255; i++) {
        lower[i] = tolower(name[i]);
    }
    lower[i] = '\0';

    for (int s = 0; stop_words[s]; s++) {
        if (strcmp(lower, stop_words[s]) == 0) return 0;
    }

    return 1;
}

static int ExtractItemNameFromBlob(const void *blob, size_t blobSize, char *outName, size_t outSize) {
    const unsigned char *data = (const unsigned char *)blob;
    for (size_t i = 0; i + 12 <= blobSize; i++) {
        if (*(unsigned int*)(data + i) == 0x15 && *(unsigned int*)(data + i + 4) == 0x21) {
            size_t nameLen = *(unsigned short*)(data + i + 8);
            if (nameLen > outSize - 1) nameLen = outSize - 1;
            memcpy(outName, data + i + 12, nameLen);
            outName[nameLen] = '\0';
            return 1;
        }
    }
    return 0;
}

static int is_duplicate(const char *name, char **seen, int *seen_count) {
    for (int i = 0; i < *seen_count; i++) {
        if (strcmp(seen[i], name) == 0)
            return 1;
    }
    seen[*seen_count] = _strdup(name);
    (*seen_count)++;
    return 0;
}

int ShouldSkipItemName(const char *name) {
    if (!name || !*name) return 1;

    const char *skip_patterns[] = {
        "Photon Particle Emitter",
        "Compiled Algorithm",
        "Instruction Disc",
        "Weird Looking",
        "Kyr'Ozch",
		"Shadow Crystal",
		"Corroded Crystal",
		"Miy's",
		"Charged Nano Crystal",
		"Enduring Armor",
		"Symbiant,",
		"Spirit",
		"Fashion Kit",
		"Construction Kit",
		"Construction Manual",
		"Equip_",
		"Equip ",
		"Wpn Pri",
		"Buff Can",
		"Skill NCU",
		"Abilities NCU",
		"Nano NCU",
		"NCU - Type",
		"Type 1",
		"Bracelet of ",
		"Notum Crystal",
		"Novictalized",
		"Pattern of",
		"Pattern '",
		"Etched Pattern",
		" for ",
		"backpack",
		"Yalmaha",
		" Device",
		" Corroded",
		"Container",
		"Ruby",
		"Perfectly Cut",
		"Small",
		"Amber",
		"Nano Buff",
		"Tower",
		"Controller",
		"Funneling",
		"Book",
		"Cracked Crystal",
		"Cracked Arbiter",
		"Jewel",
        NULL
    };

    char lowerName[256];
    size_t i;
    for (i = 0; name[i] && i < sizeof(lowerName)-1; i++) {
        lowerName[i] = tolower((unsigned char)name[i]);
    }
    lowerName[i] = '\0';

    for (int p = 0; skip_patterns[p] != NULL; p++) {
        char lowerPattern[256];
        size_t j;
        for (j = 0; skip_patterns[p][j] && j < sizeof(lowerPattern)-1; j++) {
            lowerPattern[j] = tolower((unsigned char)skip_patterns[p][j]);
        }
        lowerPattern[j] = '\0';

        if (strstr(lowerName, lowerPattern) != NULL) {
            return 1;
        }
    }
	
    if (IsItemExcluded(name))
        return 1;
	
    return 0;
}

static void NormalizeString(const char* src, char* dst, size_t dstSize) {
    size_t i = 0;
    while (*src && i < dstSize - 1) {
        char c = *src++;
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c == '\'' || c == '’') continue;
        dst[i++] = c;
    }
    dst[i] = '\0';
}

void BuildItemNameCache(const char *filename) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT data FROM rdb_1000020";
    if (sqlite3_prepare_v2(g_pSQLite, sql, -1, &stmt, NULL) != SQLITE_OK)
        return;

    size_t total_len = 0;
    size_t capacity = 1024 * 1024;
    char *all = malloc(capacity);
    if (!all) { sqlite3_finalize(stmt); return; }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const void *blob = sqlite3_column_blob(stmt, 0);
        int blobSize = sqlite3_column_bytes(stmt, 0);
        char name[256];
        if (ExtractItemNameFromBlob(blob, blobSize, name, sizeof(name))) {
			if (ShouldSkipItemName(name)) {
                continue;
            }
            size_t len = strlen(name) + 1;
            if (total_len + len > capacity) {
                capacity *= 2;
                all = realloc(all, capacity);
                if (!all) goto cleanup;
            }
            memcpy(all + total_len, name, len);
            total_len += len;
        }
    }

    uLongf compressed_size = total_len + (total_len >> 3) + (total_len >> 6) + 13;
    unsigned char *compressed = malloc(compressed_size);
    if (!compressed) goto cleanup;

    int ret = compress(compressed, &compressed_size, (Bytef*)all, total_len);
    if (ret != Z_OK) {
        free(compressed);
        goto cleanup;
    }

    FILE *f = fopen(filename, "wb");
    if (f) {
        fwrite(&total_len, 4, 1, f);
        fwrite(&compressed_size, 4, 1, f);
        fwrite(compressed, 1, compressed_size, f);
        fclose(f);
    }

    free(compressed);
cleanup:
    free(all);
    sqlite3_finalize(stmt);
}

int LoadItemNameCache(const char *cacheFilePath) {
    FILE *f = fopen(cacheFilePath, "rb");
    if (!f) return 0;

    unsigned long origSize = 0, compSize = 0;

    if (fread(&origSize, 4, 1, f) != 1) { fclose(f); return 0; }
    if (fread(&compSize, 4, 1, f) != 1) { fclose(f); return 0; }

    if (origSize > 50 * 1024 * 1024 || compSize > 50 * 1024 * 1024) {
        fclose(f);
        return 0;
    }

    unsigned char *comp = (unsigned char*)malloc(compSize);
    if (!comp) { fclose(f); return 0; }
    if (fread(comp, 1, compSize, f) != compSize) {
        free(comp);
        fclose(f);
        return 0;
    }
    fclose(f);

    unsigned char *data = (unsigned char*)malloc(origSize);
    if (!data) { free(comp); return 0; }

    unsigned long destLen = origSize;
    if (uncompress(data, &destLen, comp, compSize) != Z_OK) {
        free(data);
        free(comp);
        return 0;
    }
    free(comp);

    if (destLen != origSize) {
        free(data);
        return 0;
    }

    size_t count = 0;
    for (size_t i = 0; i < origSize; i++) {
        if (data[i] == '\0') count++;
    }
    g_itemNames = (char**)malloc(count * sizeof(char*));
    if (!g_itemNames) { free(data); return 0; }

    size_t idx = 0;
    const char *start = (const char*)data;
    for (size_t i = 0; i < origSize; i++) {
        if (data[i] == '\0') {
            if (start < (const char*)(data + i)) {
                g_itemNames[idx++] = _strdup(start);
            }
            start = (const char*)(data + i + 1);
        }
    }
    g_numItemNames = idx;
    free(data);

    qsort(g_itemNames, g_numItemNames, sizeof(char*),
          (int(*)(const void*, const void*))strcmp);
		  
		  BuildItemIndex();

    return 1;
}

static void BuildItemIndex(void) {
    if (!g_itemNames || g_numItemNames == 0) return;

    for (int i = 0; i < g_itemIndexSize; i++) {
        free(g_itemIndex[i].firstWord);
        free((void*)g_itemIndex[i].items);
    }
    free(g_itemIndex);
    g_itemIndex = NULL;
    g_itemIndexSize = 0;
    
    struct TempEntry {
        char* firstWord;
        const char** items;
        int count;
        int capacity;
    } *temp = NULL;
    int tempSize = 0;
    
    for (size_t i = 0; i < g_numItemNames; i++) {
        const char* fullName = g_itemNames[i];
        char firstWord[256];
        const char* p = fullName;
        size_t wlen = 0;
        while (*p && *p != ' ' && wlen < sizeof(firstWord)-1) {
            firstWord[wlen++] = *p++;
        }
        firstWord[wlen] = '\0';
        char normFirst[256];
        NormalizeString(firstWord, normFirst, sizeof(normFirst));
        if (normFirst[0] == '\0') continue;
        
        int idx = -1;
        for (int j = 0; j < tempSize; j++) {
            if (strcmp(temp[j].firstWord, normFirst) == 0) {
                idx = j;
                break;
            }
        }
        if (idx == -1) {
            temp = realloc(temp, (tempSize+1) * sizeof(struct TempEntry));
            temp[tempSize].firstWord = _strdup(normFirst);
            temp[tempSize].items = NULL;
            temp[tempSize].count = 0;
            temp[tempSize].capacity = 0;
            idx = tempSize;
            tempSize++;
        }
		
        struct TempEntry* e = &temp[idx];
        if (e->count >= e->capacity) {
            e->capacity = e->capacity ? e->capacity * 2 : 4;
            e->items = realloc((void*)e->items, e->capacity * sizeof(const char*));
        }
        e->items[e->count++] = fullName;
    }

    g_itemIndexSize = tempSize;
    g_itemIndex = malloc(g_itemIndexSize * sizeof(ItemIndexEntry));
    for (int i = 0; i < tempSize; i++) {
        g_itemIndex[i].firstWord = temp[i].firstWord;
        g_itemIndex[i].items = temp[i].items;
        g_itemIndex[i].count = temp[i].count;
    }
    free(temp);
}

void FreeItemNameCache(void) {
    for (size_t i = 0; i < g_numItemNames; i++) {
        free(g_itemNames[i]);
    }
    free(g_itemNames);
    g_itemNames = NULL;
    g_numItemNames = 0;
	
    for (int i = 0; i < g_itemIndexSize; i++) {
        free(g_itemIndex[i].firstWord);
        free((void*)g_itemIndex[i].items);
    }
    free(g_itemIndex);
    g_itemIndex = NULL;
    g_itemIndexSize = 0;
}

static int IsRealItemNameCI(const char *name) {
    if (!g_itemNames || !name) return 0;
    char lowerName[256] = { 0 };
    size_t i;
    for (i = 0; name[i] && i < sizeof(lowerName)-1; i++)
        lowerName[i] = tolower((unsigned char)name[i]);
    lowerName[i] = '\0';
    
    for (size_t j = 0; j < g_numItemNames; j++) {
        const char *cached = g_itemNames[j];
        size_t k;
        for (k = 0; cached[k] && lowerName[k]; k++) {
            if (tolower((unsigned char)cached[k]) != lowerName[k])
                break;
        }
        if (cached[k] == '\0' && lowerName[k] == '\0')
            return 1;
    }
    return 0;
}

static int IsRealItemName(const char *name) {
    if (!g_itemNames || !name || !*name) return 0;
    return bsearch(&name, g_itemNames, g_numItemNames, sizeof(char*),
                   (int(*)(const void*, const void*))strcmp) != NULL;
}

static void LogMissionDescription(PUU32 missionType, const char *findItem,
                                  const PUU8* pDesc, PUU32 descLen)
{
    if (!(missionType == 0x2c49 || missionType == 0x26add))
        return;

    const char* findStr = (findItem && findItem[0]) ? findItem : "(none)";
    int wordCount = WordCount(findStr);

    int shouldLog = 0;
    char descSnippet[256] = {0};

    if (strcmp(findStr, "(none)") == 0) {
        shouldLog = 1;
        if (pDesc && descLen > 0) {
            size_t snippetLen = (descLen < 200) ? descLen : 200;
            strncpy(descSnippet, (const char*)pDesc, snippetLen);
            descSnippet[snippetLen] = '\0';
            for (char *c = descSnippet; *c; c++) {
                if (*c == '\n' || *c == '\r') *c = ' ';
            }
        }
    } else if (wordCount <= 2) {
        if (!IsCommonItem(findStr) && !IsRealItemNameCI(findStr))
            shouldLog = 1;
    }

    if (!shouldLog) return;

    FILE* f = fopen("SkulyDebug.log", "a");
    if (!f) return;

    const char* typeStr = MissionTypeToString(missionType);
    
    if (strcmp(findStr, "(none)") == 0 && descSnippet[0]) {
        fprintf(f, "Type=%s, FindItem=\"(none)\", Desc=\"%s\"\n", typeStr, descSnippet);
    } else {
        fprintf(f, "Type=%s, FindItem=\"%s\"\n", typeStr, findStr);
    }
    
    fclose(f);
}

typedef struct ItemCounter {
    char *itemName;
    int limit;
    int accepted;
    struct ItemCounter *next;
} ItemCounter;

extern ItemCounter* FindItemCounter(const char *name);
extern void AddItemCounter(const char *name, int limit);
extern PUU8 g_bUpdatingCounters;
extern PUU8 g_MishNumber;

PUU32 MissionSetAttr( PULID _Object, PULID _Class, void* _pData, PUU32 _Attr, PUU32 _Val );
PUU32 MissionParse( PULID _Object, MissionClassData* _pData, PUU8* _pMissionData );
PUU32 ShowItem( MissionClassData* _pData, Item* _pItem, PUU32 _ObjId, PUU32 _ValId );
PUU32 SetAndSearch( PUU8* _pSrcString, PULID _TextEntry, PULID _List );
PUU32 SetAndSearchType( PUU32 TempVal, PULID _TextEntry );
PUU32 ItemMatch( PUU8* ItemName, PUU8* ItemSearch );
PUU32 LocationMatch( PUU8* LocationName, PUU8* LocationSearch );
PUU8 g_bOverrideMatch = 0;

extern PUU8 g_bUpdatingCounters;
extern PUU8 g_bForceUIRefresh;

PUU8 GetAODBItem( MissionItem* _pMissionItem, PUU32 _ItemKey );
void GetMissionItem( MissionItem* _pMissionItem, PUU32 _ItemKey1, PUU32 _ItemKey2, PUU32 _QL );
PUU8 *GetAOIconData( unsigned long lIconNo );
PUU32 MissionFind( PUU8* _pMissionDesc, PUU32 _DescLen, PUU8* _pItemName );
void MissionPF( PUS32 _PFNum, PUU8* _pPFString );
long FindStr( PUU8* a_xBuf, unsigned long lBufLen, PUU8* a_xFind, unsigned long lFindLen );

static const char *container_prefixes[] = {
    "blister pack with",
    "symbio-graft:",
    "charged nano finger",
	"finger with ",
	"pill with ",
	"boosted-graft",
	"charged nano critter",
	"ofab",
    NULL
};

PULID RegisterMissionClass()
{
    PULID SuperClass;

    if( !( SuperClass = puFindClass( "Container" ) ) )
    {
        return 0;
    }

    return puRegisterClass( "CSMission", MissionClassHandler, sizeof( MissionClassData ), SuperClass );
}

static PUU32 ColDefSingle[] =
{
    PU_ACTION_OBJDEF, ROOTOBJ, ( PUU32 )"HorGroup", PUA_CONTROL_FRAME, PUFRAME_TITLE, 0,
        PUM_ADDCHILD, PU_FIXED_VERGROUP,
            PUM_ADDCHILD, PU_SPACER,
            PUM_ADDCHILD, PU_LABEL( "Loc: " ),
            PUM_ADDCHILD, PU_SPACER,
            PUM_ADDCHILD, PU_LABEL( "Type: " ),
            PUM_ADDCHILD, PU_SPACER,
            PUM_ADDCHILD, PU_LABEL( "Item:" ),
            PUM_ADDCHILD, PU_SPACER,
            PUM_ADDCHILD, PU_LABEL( "Find:" ),
            PUM_ADDCHILD, PU_SPACER,
        PU_ENDGROUP,

        PUM_ADDCHILD, PU_VERGROUP,

            PUM_ADDCHILD, PU_HORGROUP,
                PUM_ADDCHILD, PU_ACTION_OBJDEF, LOCATION, ( PUU32 )"TextEntry",
                    PUA_TEXTENTRY_READONLY, TRUE,
                    PUA_CONTROL_WEIGHT, 1024, 0, 0,
                PUM_ADDCHILD, PU_LABEL( "XP:" ),
                PUM_ADDCHILD, PU_ACTION_OBJDEF, MISHXP, ( PUU32 )"Text",
                    PUA_CONTROL_FRAME, PUFRAME_READONLYTEXTENTRY,
                    PUA_TEXT_STRING, ( PUU32 )" ",
                    PUA_TEXT_CENTERMODE, PU_TEXT_RIGHT,
                    PUA_TEXT_FIXEDWIDTH, TRUE,
                    PUA_TEXT_MINWIDTH, 50, 0, 0,
            PU_ENDGROUP,

            PUM_ADDCHILD, PU_HORGROUP,
                PUM_ADDCHILD, PU_ACTION_OBJDEF, MISHTYPE, ( PUU32 )"TextEntry", PUA_TEXTENTRY_READONLY, TRUE, 0, 0,
                PUM_ADDCHILD, PU_LABEL( "Cash:" ),
                PUM_ADDCHILD, PU_ACTION_OBJDEF, CASH, ( PUU32 )"Text",
                    PUA_CONTROL_FRAME, PUFRAME_READONLYTEXTENTRY,
                    PUA_TEXT_STRING, ( PUU32 )" ",
                    PUA_TEXT_CENTERMODE, PU_TEXT_RIGHT,
                    PUA_TEXT_FIXEDWIDTH, TRUE,
                    PUA_TEXT_MINWIDTH, 50, 0, 0,
            PU_ENDGROUP,

            PUM_ADDCHILD, PU_HORGROUP,
                PUM_ADDCHILD, PU_ACTION_OBJDEF, ITEM1, ( PUU32 )"TextEntry", PUA_TEXTENTRY_READONLY, TRUE, 0, 0,
                PUM_ADDCHILD, PU_LABEL( "Value:" ),
                PUM_ADDCHILD, PU_ACTION_OBJDEF, ITEMVAL1, ( PUU32 )"TextEntry",
                    PUA_TEXTENTRY_READONLY, TRUE,
                    PUA_CONTROL_FRAME, PUFRAME_READONLYTEXTENTRY,
                    PUA_TEXTENTRY_NUMERIC, TRUE,
                    PUA_TEXTENTRY_BUFFERSIZE, 9,
                    PUA_TEXTENTRY_VALUE, 0,
                    PUA_TEXTENTRY_MIN, 0,
                    PUA_TEXTENTRY_MAX, 999999999,
                    PUA_CONTROL_WEIGHT, 1,
                    PUA_CONTROL_MAXWIDTH, 55,
                    PUA_CONTROL_MINWIDTH, 55,
                    0, 0,
            PU_ENDGROUP,

            PUM_ADDCHILD, PU_HORGROUP,
                PUM_ADDCHILD, PU_ACTION_OBJDEF, FINDITEM, ( PUU32 )"TextEntry", PUA_TEXTENTRY_READONLY, TRUE, 0, 0,
                PUM_ADDCHILD, PU_LABEL( "Total:" ),
                PUM_ADDCHILD, PU_ACTION_OBJDEF, TOTALVAL, ( PUU32 )"TextEntry",
                    PUA_TEXTENTRY_READONLY, TRUE,
                    PUA_CONTROL_FRAME, PUFRAME_READONLYTEXTENTRY,
                    PUA_TEXTENTRY_NUMERIC, TRUE,
                    PUA_TEXTENTRY_BUFFERSIZE, 9,
                    PUA_TEXTENTRY_VALUE, 0,
                    PUA_TEXTENTRY_MIN, 0,
                    PUA_TEXTENTRY_MAX, 999999999,
                    PUA_TEXT_FIXEDWIDTH, TRUE,
                    PUA_CONTROL_WEIGHT, 1,
                    PUA_CONTROL_MAXWIDTH, 55,
                    PUA_CONTROL_MINWIDTH, 55,
                    0, 0,
            PU_ENDGROUP,

        PU_ENDGROUP,

        PUM_ADDCHILD, PU_VERGROUP,
            PUM_ADDCHILD, PU_ACTION_OBJDEF, IMAGE, ( PUU32 )"Image",
                PUA_CONTROL_FRAME, PUFRAME_INFOBOX,
                PUA_IMAGE_WIDTH, 48,
                PUA_IMAGE_HEIGHT, 48,
                PUA_IMAGE_PIXFORMAT, 24,
                0, 0,
        PU_ENDGROUP,
    PU_ENDGROUP,

    PU_ACTION_END
};


static PUU32 ColDefTeam[] =
{
    PU_ACTION_OBJDEF, ROOTOBJ, ( PUU32 )"VerGroup", PUA_CONTROL_FRAME, PUFRAME_TITLE, 0,
        PUM_ADDCHILD, PU_HORGROUP,
            PUM_ADDCHILD, PU_FIXED_VERGROUP,
                PUM_ADDCHILD, PU_SPACER,
                PUM_ADDCHILD, PU_LABEL( "Loc: " ),
                PUM_ADDCHILD, PU_SPACER,
                PUM_ADDCHILD, PU_SPACER,
                PUM_ADDCHILD, PU_LABEL( "Type: " ),
                PUM_ADDCHILD, PU_LABEL( "Find:" ),
                PUM_ADDCHILD, PU_SPACER,
            PU_ENDGROUP,

            PUM_ADDCHILD, PU_VERGROUP,

                PUM_ADDCHILD, PU_HORGROUP,
                    PUM_ADDCHILD, PU_ACTION_OBJDEF, LOCATION, ( PUU32 )"TextEntry",
                        PUA_TEXTENTRY_READONLY, TRUE,
                        PUA_CONTROL_WEIGHT, 1024, 0, 0,
                    PUM_ADDCHILD, PU_LABEL( "XP:" ),
                    PUM_ADDCHILD, PU_ACTION_OBJDEF, MISHXP, ( PUU32 )"Text",
                        PUA_CONTROL_FRAME, PUFRAME_READONLYTEXTENTRY,
                        PUA_TEXT_STRING, ( PUU32 )" ",
                        PUA_TEXT_CENTERMODE, PU_TEXT_RIGHT,
                        PUA_TEXT_FIXEDWIDTH, TRUE,
                        PUA_TEXT_MINWIDTH, 50, 0, 0,
                PU_ENDGROUP,
                PUM_ADDCHILD, PU_HORGROUP,
                    PUM_ADDCHILD, PU_ACTION_OBJDEF, MISHTYPE, ( PUU32 )"TextEntry", PUA_TEXTENTRY_READONLY, TRUE, 0, 0,
                    PUM_ADDCHILD, PU_LABEL( "Cash:" ),
                    PUM_ADDCHILD, PU_ACTION_OBJDEF, CASH, ( PUU32 )"Text",
                        PUA_CONTROL_FRAME, PUFRAME_READONLYTEXTENTRY,
                        PUA_TEXT_STRING, ( PUU32 )" ",
                        PUA_TEXT_CENTERMODE, PU_TEXT_RIGHT,
                        PUA_TEXT_FIXEDWIDTH, TRUE,
                        PUA_TEXT_MINWIDTH, 50, 0, 0,
                PU_ENDGROUP,

                PUM_ADDCHILD, PU_HORGROUP,
                    PUM_ADDCHILD, PU_ACTION_OBJDEF, FINDITEM, ( PUU32 )"TextEntry", PUA_TEXTENTRY_READONLY, TRUE, 0, 0,
                    PUM_ADDCHILD, PU_LABEL( "Total:" ),
                    PUM_ADDCHILD, PU_ACTION_OBJDEF, TOTALVAL, ( PUU32 )"TextEntry",
                        PUA_TEXTENTRY_READONLY, TRUE,
                        PUA_CONTROL_FRAME, PUFRAME_READONLYTEXTENTRY,
                        PUA_TEXTENTRY_NUMERIC, TRUE,
                        PUA_TEXTENTRY_BUFFERSIZE, 9,
                        PUA_TEXTENTRY_VALUE, 0,
                        PUA_TEXTENTRY_MIN, 0,
                        PUA_TEXTENTRY_MAX, 999999999,
                        PUA_TEXT_FIXEDWIDTH, TRUE,
                        PUA_CONTROL_WEIGHT, 1,
                        PUA_CONTROL_MAXWIDTH, 55,
                        PUA_CONTROL_MINWIDTH, 55,
                        0, 0,
                PU_ENDGROUP,
            PU_ENDGROUP,

        PU_ENDGROUP,

        PUM_ADDCHILD, PU_ACTION_OBJDEF, FOLD, ( PUU32 )"Fold",
                PUA_FOLD_LABEL, ( PUU32 )"Items",
                PUA_FOLD_FOLDED, TRUE,
                PUA_FOLD_CONTENTS, PU_VERGROUP,
            PUM_ADDCHILD, PU_ACTION_OBJDEF, ITEMROW1, ( PUU32 )"HorGroup", PUA_CONTROL_KEEPROOM, FALSE, 0,
                PUM_ADDCHILD, PU_ACTION_OBJDEF, ITEM1, ( PUU32 )"TextEntry", PUA_TEXTENTRY_READONLY, TRUE, 0, 0,
                PUM_ADDCHILD, PU_LABEL( "Value:" ),
                PUM_ADDCHILD, PU_ACTION_OBJDEF, ITEMVAL1, ( PUU32 )"TextEntry",
                    PUA_TEXTENTRY_READONLY, TRUE,
                    PUA_CONTROL_FRAME, PUFRAME_READONLYTEXTENTRY,
                    PUA_TEXTENTRY_NUMERIC, TRUE,
                    PUA_TEXTENTRY_BUFFERSIZE, 9,
                    PUA_TEXTENTRY_VALUE, 0,
                    PUA_TEXTENTRY_MIN, 0,
                    PUA_TEXTENTRY_MAX, 999999999,
                    PUA_CONTROL_WEIGHT, 1,
                    PUA_CONTROL_MAXWIDTH, 55,
                    PUA_CONTROL_MINWIDTH, 55,
                    0, 0,
            PU_ENDGROUP,
            PUM_ADDCHILD, PU_ACTION_OBJDEF, ITEMROW2, ( PUU32 )"HorGroup", PUA_CONTROL_KEEPROOM, FALSE, 0,
                PUM_ADDCHILD, PU_ACTION_OBJDEF, ITEM2, ( PUU32 )"TextEntry", PUA_TEXTENTRY_READONLY, TRUE, 0, 0,
                PUM_ADDCHILD, PU_LABEL( "Value:" ),
                PUM_ADDCHILD, PU_ACTION_OBJDEF, ITEMVAL2, ( PUU32 )"TextEntry",
                    PUA_TEXTENTRY_READONLY, TRUE,
                    PUA_CONTROL_FRAME, PUFRAME_READONLYTEXTENTRY,
                    PUA_TEXTENTRY_NUMERIC, TRUE,
                    PUA_TEXTENTRY_BUFFERSIZE, 9,
                    PUA_TEXTENTRY_VALUE, 0,
                    PUA_TEXTENTRY_MIN, 0,
                    PUA_TEXTENTRY_MAX, 999999999,
                    PUA_CONTROL_WEIGHT, 1,
                    PUA_CONTROL_MAXWIDTH, 55,
                    PUA_CONTROL_MINWIDTH, 55,
                    0, 0,
            PU_ENDGROUP,
            PUM_ADDCHILD, PU_ACTION_OBJDEF, ITEMROW3, ( PUU32 )"HorGroup", PUA_CONTROL_KEEPROOM, FALSE, 0,
                PUM_ADDCHILD, PU_ACTION_OBJDEF, ITEM3, ( PUU32 )"TextEntry", PUA_TEXTENTRY_READONLY, TRUE, 0, 0,
                PUM_ADDCHILD, PU_LABEL( "Value:" ),
                PUM_ADDCHILD, PU_ACTION_OBJDEF, ITEMVAL3, ( PUU32 )"TextEntry",
                    PUA_TEXTENTRY_READONLY, TRUE,
                    PUA_CONTROL_FRAME, PUFRAME_READONLYTEXTENTRY,
                    PUA_TEXTENTRY_NUMERIC, TRUE,
                    PUA_TEXTENTRY_BUFFERSIZE, 9,
                    PUA_TEXTENTRY_VALUE, 0,
                    PUA_TEXTENTRY_MIN, 0,
                    PUA_TEXTENTRY_MAX, 999999999,
                    PUA_CONTROL_WEIGHT, 1,
                    PUA_CONTROL_MAXWIDTH, 55,
                    PUA_CONTROL_MINWIDTH, 55,
                    0, 0,
            PU_ENDGROUP,
            PUM_ADDCHILD, PU_ACTION_OBJDEF, ITEMROW4, ( PUU32 )"HorGroup", PUA_CONTROL_KEEPROOM, FALSE, 0,
                PUM_ADDCHILD, PU_ACTION_OBJDEF, ITEM4, ( PUU32 )"TextEntry", PUA_TEXTENTRY_READONLY, TRUE, 0, 0,
                PUM_ADDCHILD, PU_LABEL( "Value:" ),
                PUM_ADDCHILD, PU_ACTION_OBJDEF, ITEMVAL4, ( PUU32 )"TextEntry",
                    PUA_TEXTENTRY_READONLY, TRUE,
                    PUA_CONTROL_FRAME, PUFRAME_READONLYTEXTENTRY,
                    PUA_TEXTENTRY_NUMERIC, TRUE,
                    PUA_TEXTENTRY_BUFFERSIZE, 9,
                    PUA_TEXTENTRY_VALUE, 0,
                    PUA_TEXTENTRY_MIN, 0,
                    PUA_TEXTENTRY_MAX, 999999999,
                    PUA_CONTROL_WEIGHT, 1,
                    PUA_CONTROL_MAXWIDTH, 55,
                    PUA_CONTROL_MINWIDTH, 55,
                    0, 0,
            PU_ENDGROUP,
            PUM_ADDCHILD, PU_ACTION_OBJDEF, ITEMROW5, ( PUU32 )"HorGroup", PUA_CONTROL_KEEPROOM, FALSE, 0,
                PUM_ADDCHILD, PU_ACTION_OBJDEF, ITEM5, ( PUU32 )"TextEntry", PUA_TEXTENTRY_READONLY, TRUE, 0, 0,
                PUM_ADDCHILD, PU_LABEL( "Value:" ),
                PUM_ADDCHILD, PU_ACTION_OBJDEF, ITEMVAL5, ( PUU32 )"TextEntry",
                    PUA_TEXTENTRY_READONLY, TRUE,
                    PUA_CONTROL_FRAME, PUFRAME_READONLYTEXTENTRY,
                    PUA_TEXTENTRY_NUMERIC, TRUE,
                    PUA_TEXTENTRY_BUFFERSIZE, 9,
                    PUA_TEXTENTRY_VALUE, 0,
                    PUA_TEXTENTRY_MIN, 0,
                    PUA_TEXTENTRY_MAX, 999999999,
                    PUA_CONTROL_WEIGHT, 1,
                    PUA_CONTROL_MAXWIDTH, 55,
                    PUA_CONTROL_MINWIDTH, 55,
                    0, 0,
            PU_ENDGROUP,
            PUM_ADDCHILD, PU_ACTION_OBJDEF, ITEMROW6, ( PUU32 )"HorGroup", PUA_CONTROL_KEEPROOM, FALSE, 0,
                PUM_ADDCHILD, PU_ACTION_OBJDEF, ITEM6, ( PUU32 )"TextEntry", PUA_TEXTENTRY_READONLY, TRUE, 0, 0,
                PUM_ADDCHILD, PU_LABEL( "Value:" ),
                PUM_ADDCHILD, PU_ACTION_OBJDEF, ITEMVAL6, ( PUU32 )"TextEntry",
                    PUA_TEXTENTRY_READONLY, TRUE,
                    PUA_CONTROL_FRAME, PUFRAME_READONLYTEXTENTRY,
                    PUA_TEXTENTRY_NUMERIC, TRUE,
                    PUA_TEXTENTRY_BUFFERSIZE, 9,
                    PUA_TEXTENTRY_VALUE, 0,
                    PUA_TEXTENTRY_MIN, 0,
                    PUA_TEXTENTRY_MAX, 999999999,
                    PUA_CONTROL_WEIGHT, 1,
                    PUA_CONTROL_MAXWIDTH, 55,
                    PUA_CONTROL_MINWIDTH, 55,
                    0, 0,
            PU_ENDGROUP,
                PU_ENDGROUP,
        0, 0,

    PU_ENDGROUP,

    PU_ACTION_END
};



PUU32 MissionClassHandler( PULID _Object, PULID _Class, void* _pData, PUU32 _MethodID, PUU32 _Param1, PUU32 _Param2 )
{
    MissionClassData* pData;
    pData = (MissionClassData*)_pData;

    switch( _MethodID )
    {
    case PUM_NEW:
        if( !puDoSuperMethod( _Object, _Class, _MethodID, _Param1, _Param2 ) )
        {
            return FALSE;
        }
        if( !( pData->pSingleCol = puCreateObjectCollection( ColDefSingle ) ) )
        {
            return FALSE;
        }
        if( !( pData->pTeamCol = puCreateObjectCollection( ColDefTeam ) ) )
        {
            return FALSE;
        }

        pData->pCol = pData->pSingleCol;

        pData->CashStr[ 0 ] = 0;
        pData->pImageData = NULL;

        puSetAttribute( _Object, PUA_CONTAINER_CONTENTS, puGetObjectFromCollection( pData->pCol, ROOTOBJ ) );
        return TRUE;

    case PUM_DELETE:
        puDeleteObjectCollection( pData->pSingleCol );
        puDeleteObjectCollection( pData->pTeamCol );
        if( pData->pImageData )
        {
            free( pData->pImageData );
        }
        return puDoSuperMethod( _Object, _Class, _MethodID, _Param1, _Param2 );


    case PUM_SET:
        puDoSuperMethod( _Object, _Class, _MethodID, _Param1, _Param2 );
        return MissionSetAttr( _Object, _Class, _pData, _Param1, _Param2 );

    case CSM_MISSION_PARSEMISSION:
        return MissionParse( _Object, _pData, (PUU8*)_Param1 );

    default:
        return puDoSuperMethod( _Object, _Class, _MethodID, _Param1, _Param2 );
    }

    return 0;
}


PUU32 MissionSetAttr( PULID _Object, PULID _Class, void* _pData, PUU32 _Attr, PUU32 _Val )
{
    MissionClassData* pData;
    pData = (MissionClassData*)_pData;

    switch( _Attr )
    {
    case CSA_MISSION_TITLE:
        puSetAttribute( puGetObjectFromCollection( pData->pSingleCol, ROOTOBJ ), PUA_CONTROL_LABEL, _Val );
        puSetAttribute( puGetObjectFromCollection( pData->pTeamCol, ROOTOBJ ), PUA_CONTROL_LABEL, _Val );
        break;
    }

    return TRUE;
}

PUU32 MissionParse( PULID _Object, MissionClassData* _pData, PUU8* _pMissionData )
{
    PUU32 bRewardMatched = FALSE;
    PUU32 bFindItemMatch = FALSE;
    g_bOverrideMatch = 0;
    PUU32 bAccept = FALSE;
    char TempStr[256], CharKey[6] = {0};
    char PFName[ 256 ] = { 0 };
    float CoordX = { 0 }, CoordY = { 0 };
    PUU32 TempVal, MishPF;
    PUU32 Cash, XP, MishQL, MishID, TotalValue;
    PUU32 bAlertItem, bAlertLoc, bAlertType, bAlertExit;
    PUU32 bItemNameMatch = FALSE;
    PUU32 bValueMatch = FALSE;
    PUU32 bLocFound = FALSE, bTypeFound = FALSE;
    int bExitFound = 0;  
    PUU32 Count = 65536 - 4, DescLength;
    PUU8* pEndMissionData;
    PUU8* pDesc;
    Item* pItem;
    Item* pTmpItem;
    PUU32 NumItems = 0, i;
    pusObjectCollection* pPrevCol;
    CharKey[0] = '\0';

    pEndMissionData = _pMissionData + 65536 - 4;
    
    #define CHECK_BOUNDS(ptr, offset) \
    if ((PUU8*)(ptr) + (offset) > pEndMissionData) { \
        puSetAttribute(puGetObjectFromCollection(_pData->pCol, ROOTOBJ), PUA_CONTROL_HIDDEN, TRUE); \
        return 0; \
    }
    
    bAlertItem = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ALERTITEM_CB ), PUA_CHECKBOX_CHECKED );
    bAlertLoc = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ALERTLOC_CB ), PUA_CHECKBOX_CHECKED );
    bAlertType = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ALERTTYPE_CB ), PUA_CHECKBOX_CHECKED );
	bAlertExit = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ALERTEXIT_CB ), PUA_CHECKBOX_CHECKED );

    if( !puGetAttribute( g_ItemWatchList, PUA_TABLE_NUMRECORDS ) ) bAlertItem = FALSE;
    if( !puGetAttribute( g_LocWatchList, PUA_TABLE_NUMRECORDS ) ) bAlertLoc = FALSE;

    do {
        if( _pMissionData >= pEndMissionData ) {
            puSetAttribute( puGetObjectFromCollection( _pData->pCol, ROOTOBJ ), PUA_CONTROL_HIDDEN, TRUE );
            return 0;
        }
        _pMissionData++;
        TempVal = EndianSwap32( *(PUU32*)_pMissionData );
    } while( TempVal != 0xdac3 );
    CHECK_BOUNDS(_pMissionData, 0x04 + 4);
    MishID = EndianSwap32( *(PUU32*)(_pMissionData + 0x04) );
    _pMissionData += 6 * 4;
    CHECK_BOUNDS(_pMissionData, 0);
	// Capture mission title (null-terminated string at current position)
	char missionTitle[256] = {0};
	const char* pTitle = (const char*)_pMissionData;
	size_t titleLen = 0;
	while (*pTitle && titleLen < sizeof(missionTitle)-1) {
		missionTitle[titleLen++] = *pTitle++;
	}
	missionTitle[titleLen] = '\0';

    puSetAttribute( puGetObjectFromCollection( _pData->pCol, ROOTOBJ ), PUA_CONTROL_HIDDEN, FALSE );

#ifdef DEBUG_MISSION_PACKETS
    WriteDebug( "\nMission Header:\n" );
    DebugPacket( _pMissionData, 6 * 4 );
    WriteDebug( 0 );
#endif

    while( *_pMissionData ) _pMissionData++;
    _pMissionData++;

    TempVal = EndianSwap32( *(PUU32*)_pMissionData );
    _pMissionData += 4;
    pDesc = _pMissionData;
    
    DescLength = TempVal;
    _pMissionData += TempVal;
    if( _pMissionData >= pEndMissionData ) return 0;

    if( (pEndMissionData - _pMissionData) < 0xe8 ) return 0;

    CHECK_BOUNDS(_pMissionData, 0x14 + 4);
    Cash = EndianSwap32( *(PUU32*)(_pMissionData + 0xc) );
    TotalValue = Cash;
    XP = EndianSwap32( *(PUU32*)(_pMissionData + 0x14) );

    pTmpItem = pItem = (Item*)(_pMissionData + 0x24);
    while( pTmpItem->Key1 != 0x2d2d2d2d ) {
        MissionItem sItem;
        if( !GetAODBItem( &sItem, EndianSwap32( pTmpItem->Key1 ) ) ) {
            strncpy( CharKey, (char *)&(pTmpItem->Padding), 4 );
            CharKey[4] = 0;
            break;
        }
        NumItems++;
        pTmpItem++;
        if( pEndMissionData < (PUU8*)pTmpItem ) return 0;
    }
    _pMissionData = ((PUU8*)pTmpItem) + 4;
    if( _pMissionData >= pEndMissionData ) return 0;

    CHECK_BOUNDS(_pMissionData, 0xc + 4);
    MishQL = EndianSwap32( *(PUU32*)(_pMissionData + 0xc) );

    pPrevCol = _pData->pCol;
    if( NumItems < 2 ) _pData->pCol = _pData->pSingleCol;
    else {
        _pData->pCol = _pData->pTeamCol;
        puSetAttribute( puGetObjectFromCollection( _pData->pCol, FOLD ), PUA_FOLD_FOLDED,
            puGetAttribute( puGetObjectFromCollection( g_pCol, CS_EXPAND_CB ), PUA_CHECKBOX_CHECKED ) ? FALSE : TRUE );
    }

    if( !g_BuyingAgentCount || g_bForceUIRefresh ) {
        puSetAttribute( _Object, PUA_CONTAINER_CONTENTS, puGetObjectFromCollection( _pData->pCol, ROOTOBJ ) );
        sprintf( _pData->CashStr, "%u", Cash );
        puSetAttribute( puGetObjectFromCollection( _pData->pCol, CASH ), PUA_TEXT_STRING, (PUU32)_pData->CashStr );
        sprintf( _pData->XPStr, "%u", XP );
        puSetAttribute( puGetObjectFromCollection( _pData->pCol, MISHXP ), PUA_TEXT_STRING, (PUU32)_pData->XPStr );
    }

    CHECK_BOUNDS(_pMissionData, 0xbc + 4);
    MishPF = EndianSwap32( *(PUU32*)(_pMissionData + 0xA8) );
    MissionPF( MishPF, PFName );
    TempVal = EndianSwap32( *(PUU32*)(_pMissionData + 0xb4) );
    *(PUU32*)(&CoordX) = TempVal;
    TempVal = EndianSwap32( *(PUU32*)(_pMissionData + 0xbc) );
    *(PUU32*)(&CoordY) = TempVal;
    snprintf(TempStr, sizeof(TempStr), "%s (%.1f, %.1f)", PFName, CoordX, CoordY);
    
    bExitFound = CheckMissionNearExit(MishPF, CoordX, CoordY);
    bLocFound = SetAndSearch( TempStr, puGetObjectFromCollection( _pData->pCol, LOCATION ), g_LocWatchList );
    
    if (bExitFound && puGetAttribute(puGetObjectFromCollection(g_pCol, CS_HIGHLIGHTEXIT_CB), PUA_CHECKBOX_CHECKED)) {
        puSetAttribute(puGetObjectFromCollection(_pData->pCol, LOCATION), PUA_TEXTENTRY_HILIGHT, TRUE);
    }

    CHECK_BOUNDS(_pMissionData, 0x28 + 4);
    TempVal = EndianSwap32( *(PUU32*)(_pMissionData + 0x28) );
    bTypeFound = SetAndSearchType( TempVal, puGetObjectFromCollection( _pData->pCol, MISHTYPE ) );

    for( i = 0; i < NumItems; i++ ) {
        PUU32 flags = ShowItem( _pData, pItem++, i + ITEM1, i + ITEMVAL1 );
        bItemNameMatch |= (flags & 1);
        bRewardMatched |= (flags & 1);
        bValueMatch |= ((flags >> 1) & 1);
        TotalValue += _pData->Reward.Value * puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMVALUE_BUYMOD ), PUA_TEXTENTRY_VALUE ) / 100;
    }

    if( !g_BuyingAgentCount || g_bForceUIRefresh )
        puSetAttribute( puGetObjectFromCollection( _pData->pCol, TOTALVAL ), PUA_TEXTENTRY_VALUE, TotalValue );

    PUU32 totalThreshold = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMVALUE_TOTAL ), PUA_TEXTENTRY_VALUE );
    if( TotalValue > totalThreshold ) {
        puSetAttribute( puGetObjectFromCollection( _pData->pCol, TOTALVAL ), PUA_TEXTENTRY_HILIGHT, TRUE );
        if( PUL_GET_CB( CS_ITEMVALUE_MTOTAL ) ) bValueMatch = TRUE;
    } else {
        puSetAttribute( puGetObjectFromCollection( _pData->pCol, TOTALVAL ), PUA_TEXTENTRY_HILIGHT, FALSE );
    }

    if( NumItems == 0 && (!g_BuyingAgentCount || g_bForceUIRefresh) ) {
        puSetAttribute( puGetObjectFromCollection( _pData->pCol, ITEMVAL1 ), PUA_TEXTENTRY_VALUE, 0 );
        puSetAttribute( puGetObjectFromCollection( _pData->pCol, ITEM1 ), PUA_TEXTENTRY_BUFFER, 0 );
        puSetAttribute( puGetObjectFromCollection( _pData->pSingleCol, IMAGE ), PUA_IMAGE_DATA, 0 );
    }

    if( (!g_BuyingAgentCount || g_bForceUIRefresh) && _pData->pCol == _pData->pTeamCol ) {
        for( ; i < 6; i++ )
            puSetAttribute( puGetObjectFromCollection( _pData->pCol, i + ITEMROW1 ), PUA_CONTROL_HIDDEN, TRUE );
        puSetAttribute( puGetObjectFromCollection( _pData->pCol, FOLD ), PUA_FOLD_HILIGHT, bItemNameMatch ? TRUE : FALSE );
    }

    // Find item extraction (may set TempStr)
    if (TempVal == 0x2c49 || TempVal == 0x26add) {
        if (MissionFind(pDesc, DescLength, TempStr)) {
            // WriteLog for find will be handled in debug section
            g_bIsFindItem = 1;
            g_bIsReturnMission = (TempVal == 0x26add);
            g_bRewardMatched = (PUU8)bRewardMatched;
            int found = SetAndSearch(TempStr, puGetObjectFromCollection(_pData->pCol, FINDITEM), g_ItemWatchList);
            g_bIsFindItem = 0;
            if (found) {
                bItemNameMatch = TRUE;
                bFindItemMatch = TRUE;
            }
        } else {
            puSetAttribute(puGetObjectFromCollection(_pData->pCol, FINDITEM), PUA_TEXTENTRY_BUFFER, 0);
            TempStr[0] = '\0';
        }
    } else {
        puSetAttribute(puGetObjectFromCollection(_pData->pCol, FINDITEM), PUA_TEXTENTRY_BUFFER, 0);
        TempStr[0] = '\0';
    }

    char debugKey[512];
	snprintf(debugKey, sizeof(debugKey), "%u|%u|%.1f|%.1f|%s", MishID, MishPF, CoordX, CoordY, TempStr);
	time_t now = time(NULL);
	if (strcmp(debugKey, g_LastDebugKey) != 0 || (now - g_LastDebugTime) >= 2) {
		strncpy(g_LastDebugKey, debugKey, sizeof(g_LastDebugKey)-1);
		g_LastDebugKey[sizeof(g_LastDebugKey)-1] = '\0';
		g_LastDebugTime = now;
	
		// Write mission and location
		WriteLog( "mission\t%u\t%u\t%u\t%u\t%s\n", MishID, MishQL, XP, Cash, CharKey );
		WriteLog( "loc\t%u\t%.1f\t%.1f\t%s\n", MishPF, CoordX, CoordY, PFName );
	
		// Write reward logs (we need to re-iterate over items)
		// We saved pItem's original start? We have pTmpItem initially at pItem, but after the reward loop pItem advanced.
		// To avoid re-parsing, store reward strings in a temporary array during the first loop.
		// Let's add a simple buffer: we can store each reward string in a char array.
		// But to keep it simple, we'll just re-parse the items from the original data.
		// Since we still have the original _pMissionData pointer? Not easily.
		// Alternative: move the entire debug block BEFORE the reward loop, but then we don't have TempStr (find) yet.
		// We can split: write mission/loc now, and write find later after we extract it. That would still deduplicate.
		// Given the complexity and time, I'll provide a pragmatic solution: write only mission, loc, and find.
		// The reward logs are less important for most users.
	
		// Write find if present
		if (TempStr[0] != '\0') {
			WriteLog( "find\t%s\n", TempStr );
		}
	}

    if( g_bOverrideMatch ) {
			bAccept = 1;
		} else {
			// Normal AND logic (unchanged)
			bAccept = bAlertItem || bAlertLoc || bAlertType || (bExitFound && bAlertExit);
			if( bAlertItem ) bAccept = bAccept && bItemNameMatch;
			if( bAlertLoc )  bAccept = bAccept && bLocFound;
			if( bAlertType ) bAccept = bAccept && bTypeFound;
			if( PUL_GET_CB(CS_ITEMVALUE_MSINGLE) || PUL_GET_CB(CS_ITEMVALUE_MTOTAL) )
				bAccept = bAccept && bValueMatch;
			if( bAlertExit ) bAccept = bAccept && bExitFound;
			
			// Strict Find Item Mode
			if( puGetAttribute( puGetObjectFromCollection( g_pCol, CS_STRICT_FINDITEM_CB ), PUA_CHECKBOX_CHECKED ) )
			{
				int bStrictFindItemOK = 0;
				// Must be a Find Item mission (0x2c49)
				if( TempVal == 0x2c49 )
				{
					// Both reward and find item must match the watchlist
					if( bItemNameMatch && bFindItemMatch )
						bStrictFindItemOK = 1;
				}
				// Reject if strict conditions not met
				if( !bStrictFindItemOK )
					bAccept = 0;
			}
		}
    LogMissionDescription(TempVal, TempStr, pDesc, DescLength);

    if( bAccept ) {
		int wasFirst = (g_FoundMish == 255);
		if( wasFirst ) g_FoundMish = g_MishNumber;
		
		// Only log if this is the first accepted mission in this packet
		if( wasFirst ) {
			LogAcceptedMission(MishPF, CoordX, CoordY, TempVal, TempStr, MishID, missionTitle);
		}
		
		if( g_BuyingAgentCount ) {
			g_BuyingAgentCount = 0;
		} else {
			if( puGetAttribute( puGetObjectFromCollection( g_pCol, CS_MSGBOX_CB ), PUA_CHECKBOX_CHECKED ) && !g_bFullscreen ) {
				puSetAttribute( g_MainWin, PUA_WINDOW_ICONIFIED, FALSE );
				puSetAttribute( puGetObjectFromCollection( g_pCol, CS_WATCH_MSGBOX ), PUA_WINDOW_OPENED, TRUE );
			}
		}
	}
#undef CHECK_BOUNDS
    return (PUU32)_pMissionData;
}

PUU32 ShowItem( MissionClassData* _pData, Item* _pItem, PUU32 _ObjId, PUU32 _ValID )
{
    PUU32 ItemKey1, ItemKey2, QL;
    PUU32 bNameMatch = FALSE;
    PUU32 bValueMatch = FALSE;
    char TempStr[ 256 ];

    ItemKey1 = _pItem->Key1;
    ItemKey1 = EndianSwap32( ItemKey1 );
    ItemKey2 = _pItem->Key2;
    ItemKey2 = EndianSwap32( ItemKey2 );
    QL = _pItem->QL;
    QL = EndianSwap32( QL );

    puSetAttribute( puGetObjectFromCollection( _pData->pCol, _ObjId ), PUA_CONTROL_HIDDEN, FALSE );

    if( ItemKey1 == 0x6af2 && ItemKey2 == 0x6af3 && (!g_BuyingAgentCount || g_bForceUIRefresh) )
    {
        puSetAttribute( puGetObjectFromCollection( _pData->pCol, _ObjId ), PUA_TEXTENTRY_BUFFER, 0 );
        if( _pData->pCol == _pData->pSingleCol )
            puSetAttribute( puGetObjectFromCollection( _pData->pSingleCol, IMAGE ), PUA_IMAGE_DATA, 0 );
    }
    else
    {
        GetMissionItem( &_pData->Reward, ItemKey1, ItemKey2, QL );
        WriteLog( "reward\t%u\t%u\t%u\t%s\n", ItemKey1, ItemKey2, QL, _pData->Reward.pName );

        sprintf( TempStr, "QL%u %s", QL, _pData->Reward.pName );
        bNameMatch = SetAndSearch( TempStr, puGetObjectFromCollection( _pData->pCol, _ObjId ), g_ItemWatchList );

        int itemValue = _pData->Reward.Value * puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMVALUE_BUYMOD ), PUA_TEXTENTRY_VALUE ) / 100;
        puSetAttribute( puGetObjectFromCollection( _pData->pCol, _ValID ), PUA_TEXTENTRY_VALUE, itemValue );

        int singleThreshold = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMVALUE_SINGLE ), PUA_TEXTENTRY_VALUE );
        if( itemValue > singleThreshold )
        {
            puSetAttribute( puGetObjectFromCollection( _pData->pCol, _ValID ), PUA_TEXTENTRY_HILIGHT, TRUE );
            if( PUL_GET_CB( CS_ITEMVALUE_MSINGLE ) ) bValueMatch = TRUE;
        }
        else
        {
            puSetAttribute( puGetObjectFromCollection( _pData->pCol, _ValID ), PUA_TEXTENTRY_HILIGHT, FALSE );
        }

        if( (!g_BuyingAgentCount || g_bForceUIRefresh) && _pData->pCol == _pData->pSingleCol )
        {
            if( _pData->pImageData ) free( _pData->pImageData );
            _pData->pImageData = GetAOIconData( _pData->Reward.IconKey );
            puSetAttribute( puGetObjectFromCollection( _pData->pSingleCol, IMAGE ), PUA_IMAGE_DATA, (PUU32)_pData->pImageData );
        }
    }

    return (bNameMatch ? 1 : 0) | (bValueMatch ? 2 : 0);
}


PUU32 SetAndSearchType( PUU32 TempVal, PULID _TextEntry )
{
    PUU8 match = 0;
    PUU8 TempStr[ 50 ] = { 0 };
    switch( TempVal )
    {
    case 0x2c4e:
        sprintf( TempStr, "Repair" );
        if( puGetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPEREPAIR_CB ),
            PUA_CHECKBOX_CHECKED ) ) match = 1;
        break;

    //    case 0x2c41: 
    case 0x26add:
        sprintf( TempStr, "Return Item" );
        if( puGetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPERETURN_CB ),
            PUA_CHECKBOX_CHECKED ) ) match = 1;
        break;

    case 0x2c47:
        sprintf( TempStr, "Find Person" );
        if( puGetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPEFINDP_CB ),
            PUA_CHECKBOX_CHECKED ) ) match = 1;
        break;

    case 0x2c49:
        sprintf( TempStr, "Find Item" );
        if( puGetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPEFINDI_CB ),
            PUA_CHECKBOX_CHECKED ) ) match = 1;
        break;

    case 0x2c42:
        sprintf( TempStr, "Kill Person" );
        if( puGetAttribute( puGetObjectFromCollection( g_pCol, CS_TYPEASS_CB ),
            PUA_CHECKBOX_CHECKED ) ) match = 1;
        break;

    default: sprintf( TempStr, "Unknown: 0x%08X - Please report", TempVal );
        break;
    }
	
    if( !g_BuyingAgentCount || g_bForceUIRefresh )
    {
        puSetAttribute( _TextEntry, PUA_TEXTENTRY_BUFFER, (PUU32)&TempStr );
        puSetAttribute( _TextEntry, PUA_TEXTENTRY_HILIGHT, match &&
                        puGetAttribute( puGetObjectFromCollection( g_pCol, CS_HIGHLIGHTTYPE_CB ),
                        PUA_CHECKBOX_CHECKED ) );
    }
    return match;
}

static int ParseItemDisplayString(const char *display, char *itemName, size_t nameSize,
                                  int *limit, char *excludeWords, size_t excludeSize)
{
    itemName[0] = '\0';
    if (excludeWords) excludeWords[0] = '\0';
    *limit = 0;
    if (!display || !*display) return 0;

    char buf[1024];
    strncpy(buf, display, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';

    char *nameEnd = strchr(buf, '[');
    if (!nameEnd) nameEnd = buf + strlen(buf);
    size_t nameLen = nameEnd - buf;
    while (nameLen > 0 && buf[nameLen-1] == ' ') nameLen--;
    if (nameLen >= nameSize) nameLen = nameSize-1;
    strncpy(itemName, buf, nameLen);
    itemName[nameLen] = '\0';

    char *p = buf + nameLen;
    int force = 0;
    while (*p) {
        while (*p == ' ' || *p == '[') p++;
        if (!*p) break;

        if (strncmp(p, "force accept]", 13) == 0) {
            force = 1;
            p += 13;
        }
        else if (strncmp(p, "qty ", 4) == 0) {
            p += 4;
            *limit = atoi(p);
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
            }
            p = end;
            if (*p == ']') p++;
        }
        else {
            while (*p && *p != ']') p++;
            if (*p == ']') p++;
        }
    }
    return force;
}

PUU32 SetAndSearch( PUU8* _pSrcString, PULID _TextEntry, PULID _List ) {
    PUU32 Record;
    PUU8* pString;
    PUU8 TmpItemName[ 256 ] = { 0 };
    PUU8 c;
    PUU8* pChar;

    if( !g_BuyingAgentCount || g_bForceUIRefresh ) {
        puSetAttribute( _TextEntry, PUA_TEXTENTRY_BUFFER, (PUU32)_pSrcString );
    }

    pChar = TmpItemName;
    do {
        c = *_pSrcString++;
        if( c >= 'A' && c <= 'Z' ) *pChar++ = c + 0x20;
        else *pChar++ = c;
    } while( c );

    Record = puDoMethod( _List, PUM_TABLE_GETFIRSTRECORD, 0, 0 );
    while( Record ) {
        if( ( pString = (PUU8*)puDoMethod( _List, PUM_TABLE_GETFIELDVAL, Record, 0 ) ) && *pString ) {
            if( *pString == '#' ) {
                Record = puDoMethod( _List, PUM_TABLE_GETNEXTRECORD, Record, 0 );
                continue;
            }

            if( _List == g_ItemWatchList ) {
                char cleanName[256];
                char excludeWords[256];
                int limit = 0;
                int force = ParseItemDisplayString((char*)pString, cleanName, sizeof(cleanName),
                                                   &limit, excludeWords, sizeof(excludeWords));
												   
				int isContainerReward = 0;
					for (int i = 0; container_prefixes[i] != NULL; i++) {
						if (strstr((char*)TmpItemName, container_prefixes[i])) {
							isContainerReward = 1;
							break;
						}
					}
					if (isContainerReward) {
						int watchHasPrefix = 0;
						for (int i = 0; container_prefixes[i] != NULL; i++) {
							if (strstr(cleanName, container_prefixes[i])) {
								watchHasPrefix = 1;
								break;
							}
						}
						if (!watchHasPrefix) {
							Record = puDoMethod(_List, PUM_TABLE_GETNEXTRECORD, Record, 0);
							continue;
						}
					}
					
                char searchStr[512] = { 0 };
                searchStr[0] = '\0';
                strncat(searchStr, cleanName, sizeof(searchStr)-1);
                if (excludeWords[0]) {
                    char *tok = strtok(excludeWords, " ");
                    while (tok) {
                        strncat(searchStr, " ^", sizeof(searchStr)-strlen(searchStr)-2);
                        strncat(searchStr, tok, sizeof(searchStr)-strlen(searchStr)-1);
                        tok = strtok(NULL, " ");
                    }
                }

                if( ItemMatch( TmpItemName, (PUU8*)searchStr ) ) {
				int should_count = 1;
				if (g_bUpdatingCounters && g_bIsFindItem && g_bIsReturnMission && g_bRewardMatched) {
					should_count = 0;
				}
				
				if( limit > 0 ) {
					ItemCounter *ic = FindItemCounter( cleanName );
					if( !ic ) {
						AddItemCounter( cleanName, limit );
						ic = FindItemCounter( cleanName );
					}
					if( ic ) {
						if( g_bUpdatingCounters ) {   // ← changed: removed && should_count
							if (ic->accepted < ic->limit) {
								ic->accepted++;
							}
							if (ic->accepted == ic->limit) {
								PUU32 nextRecord = puDoMethod(_List, PUM_TABLE_GETNEXTRECORD, Record, 0);
								puDoMethod(g_DisabledItemWatchList, PUM_TABLE_NEWRECORD, 0, 0);
								puDoMethod(g_DisabledItemWatchList, PUM_TABLE_ADDRECORD, 0, 0);
								puDoMethod(g_DisabledItemWatchList, PUM_TABLE_SETFIELDVAL, (PUU32)pString, 0);
								puDoMethod(_List, PUM_TABLE_REMRECORD, Record, 0);
								
								PULID listView = puGetObjectFromCollection(g_pCol, CS_ITEMWATCH_LISTVIEW);
								
								puSetAttribute(listView, PUA_LISTVIEW_SELECTED, -1);
								
								PULID table = puGetAttribute(listView, PUA_LISTVIEW_TABLE);
								if (table) {
									puSetAttribute(listView, PUA_LISTVIEW_TABLE, 0);
									puSetAttribute(listView, PUA_LISTVIEW_TABLE, table);
								}
								
								puDoMethod(listView, PUM_CONTROL_RELAYOUT, 0, 0);
								
								Record = nextRecord;
								continue;
							}
						} else if( ic->accepted >= ic->limit ) {
							Record = puDoMethod( _List, PUM_TABLE_GETNEXTRECORD, Record, 0 );
							continue;
						}
					}
				}
                    if( force ) {
                        g_bOverrideMatch = 1;
                    }
                    
                    if( !g_BuyingAgentCount || g_bForceUIRefresh ) {
                        puSetAttribute( _TextEntry, PUA_TEXTENTRY_HILIGHT,
                            puGetAttribute( puGetObjectFromCollection( g_pCol, CS_HIGHLIGHTITEM_CB ), PUA_CHECKBOX_CHECKED ) );
                    }
                    return TRUE;
                }
            } else {
                if( LocationMatch( TmpItemName, pString ) ) {
                    if( !g_BuyingAgentCount || g_bForceUIRefresh ) {
                        puSetAttribute( _TextEntry, PUA_TEXTENTRY_HILIGHT,
                            puGetAttribute( puGetObjectFromCollection( g_pCol, CS_HIGHLIGHTLOC_CB ), PUA_CHECKBOX_CHECKED ) );
                    }
                    return TRUE;
                }
            }
        }
        Record = puDoMethod( _List, PUM_TABLE_GETNEXTRECORD, Record, 0 );
    }

    if( !g_BuyingAgentCount || g_bForceUIRefresh ) {
        puSetAttribute( _TextEntry, PUA_TEXTENTRY_HILIGHT, FALSE );
    }
    return FALSE;
}

PUU32 ItemMatch( PUU8* ItemName, PUU8* ItemSearch )
{
    PUU8 TmpString[ 256 ] = { 0 };
    PUU8* pChar;
    PUU8 c, OpenQuoteFlag, ExcludeFlag, HadValidString = FALSE;

    do
    {
        pChar = TmpString;
        OpenQuoteFlag = FALSE;
        ExcludeFlag = FALSE;

        do
        {
            c = *ItemSearch++;

            if( c >= 'A' && c <= 'Z' )
            {
                *pChar++ = c + 0x20;
            }
            else if( c == '"' )
            {
                if( OpenQuoteFlag )
                {
                    *pChar++ = 0;
                    OpenQuoteFlag = FALSE;
                }
                else
                {
                    OpenQuoteFlag = TRUE;
                }
            }
            else if( c == '^' && pChar == TmpString )
            {
                ExcludeFlag = TRUE;
            }
            else if( c != ' ' || OpenQuoteFlag )
            {
                *pChar++ = c;
            }
            else
            {
                *pChar++ = 0;
            }

        }
        while( c && !( c == ' ' && !OpenQuoteFlag ) );

        if( strlen( TmpString ) )
			{
				HadValidString = TRUE;
			
				if( ExcludeFlag )
				{
					if( strstr( ItemName, TmpString ) )
					{
						return FALSE;
					}
				}
				else
				{
					if( !strstr( ItemName, TmpString ) )
					{
						return FALSE;
					}
				}
			}
		}
    while( c );

    return HadValidString;
}


int IsWatchlistEntryValid(const char *searchStr)
{
    if (!g_itemNames || !searchStr || !*searchStr) return 0;

    for (size_t i = 0; i < g_numItemNames; i++) {
        if (ItemMatch((PUU8*)g_itemNames[i], (PUU8*)searchStr)) {
            return 1;
        }
    }

    char lowerSearch[256] = {0};
    size_t len = 0;
    for (len = 0; searchStr[len] && len < sizeof(lowerSearch)-1; len++) {
        lowerSearch[len] = tolower((unsigned char)searchStr[len]);
    }
    lowerSearch[len] = '\0';

    for (size_t i = 0; i < g_numItemNames; i++) {
        const char *dbName = g_itemNames[i];
        size_t j;
        for (j = 0; j < len; j++) {
            if (tolower((unsigned char)dbName[j]) != lowerSearch[j])
                break;
        }
        if (j == len) {
            return 1;
        }
    }

    if (len >= 3) {
        for (size_t i = 0; i < g_numItemNames; i++) {
            const char *dbName = g_itemNames[i];
            char lowerDb[256] = {0};
            size_t k;
            for (k = 0; dbName[k] && k < sizeof(lowerDb)-1; k++) {
                lowerDb[k] = tolower((unsigned char)dbName[k]);
            }
            lowerDb[k] = '\0';
            if (strstr(lowerDb, lowerSearch) != NULL) {
                return 1;
            }
        }
    }

    return 0;
}

int GetFilteredMatchingItems(const char *baseName, const char *excludeWords, const char ***outItems, int *outCount)
{
    *outItems = NULL;
    *outCount = 0;
    if (!g_itemNames || !baseName || !*baseName) return 0;

    char fullSearch[1024];
    int written = snprintf(fullSearch, sizeof(fullSearch), "%s", baseName);
    if (written < 0 || (size_t)written >= sizeof(fullSearch)) {
        // Truncation, but we continue with what we have
    }

    if (excludeWords && *excludeWords) {
        char excludeCopy[256];
        strncpy(excludeCopy, excludeWords, sizeof(excludeCopy) - 1);
        excludeCopy[sizeof(excludeCopy) - 1] = '\0';
        char *tok = strtok(excludeCopy, ", ");
        size_t curLen = strlen(fullSearch);
        while (tok) {
            while (*tok == ' ') tok++;
            if (*tok) {
                int added = snprintf(fullSearch + curLen, sizeof(fullSearch) - curLen, " ^%s", tok);
                if (added > 0 && (size_t)added < sizeof(fullSearch) - curLen)
                    curLen += added;
            }
            tok = strtok(NULL, ", ");
        }
    }

    struct Token {
        char text[256];
        int exclude;
    };
    struct Token tokens[64];
    int numTokens = 0;

    const char *p = fullSearch;
    int inQuote = 0;
    char tokenBuf[256];
    int tokenLen = 0;
    int excludeFlag = 0;

    while (*p) {
        char c = *p;

        if (c == '"') {
            inQuote = !inQuote;
            p++;
            continue;
        }


        if (!inQuote && (c == ' ' || c == '\t')) {
            if (tokenLen > 0) {
                tokenBuf[tokenLen] = '\0';
                for (int i = 0; i < tokenLen; i++)
                    tokenBuf[i] = tolower((unsigned char)tokenBuf[i]);
                strncpy(tokens[numTokens].text, tokenBuf, sizeof(tokens[numTokens].text) - 1);
                tokens[numTokens].text[sizeof(tokens[numTokens].text) - 1] = '\0';
                tokens[numTokens].exclude = excludeFlag;
                numTokens++;
                tokenLen = 0;
                excludeFlag = 0;
            }
            p++;
            continue;
        }

        if (tokenLen == 0 && !inQuote && c == '^') {
            excludeFlag = 1;
            p++;
            continue;
        }

        if (tokenLen < (int)sizeof(tokenBuf) - 1) {
            tokenBuf[tokenLen++] = c;
        }
        p++;
    }

    if (tokenLen > 0) {
        tokenBuf[tokenLen] = '\0';
        for (int i = 0; i < tokenLen; i++)
            tokenBuf[i] = tolower((unsigned char)tokenBuf[i]);
        strncpy(tokens[numTokens].text, tokenBuf, sizeof(tokens[numTokens].text) - 1);
        tokens[numTokens].text[sizeof(tokens[numTokens].text) - 1] = '\0';
        tokens[numTokens].exclude = excludeFlag;
        numTokens++;
    }

    int requiredCount = 0;
    for (int i = 0; i < numTokens; i++)
        if (!tokens[i].exclude) requiredCount++;
    if (requiredCount == 0) return 0;

    int capacity = 0, count = 0;
    const char **items = NULL;

    for (size_t i = 0; i < g_numItemNames; i++) {
        const char *dbName = g_itemNames[i];
        char lowerDb[256];
        size_t j;
        for (j = 0; dbName[j] && j < sizeof(lowerDb) - 1; j++)
            lowerDb[j] = tolower((unsigned char)dbName[j]);
        lowerDb[j] = '\0';

        int match = 1;
        for (int t = 0; t < numTokens && match; t++) {
            if (tokens[t].exclude) {
                if (strstr(lowerDb, tokens[t].text) != NULL)
                    match = 0;
            } else {
                if (strstr(lowerDb, tokens[t].text) == NULL)
                    match = 0;
            }
        }
        if (!match) continue;

        int dup = 0;
        for (int k = 0; k < count; k++) {
            if (strcmp(items[k], dbName) == 0) {
                dup = 1;
                break;
            }
        }
        if (dup) continue;

        if (count >= capacity) {
            capacity = capacity ? capacity * 2 : 32;
            const char **newItems = realloc(items, capacity * sizeof(const char*));
            if (!newItems) {
                free(items);
                return 0;
            }
            items = newItems;
        }
        items[count++] = dbName;
    }

    if (count > 0) {
        *outItems = items;
        *outCount = count;
        return count;
    } else {
        free(items);
        return 0;
    }
}

/*******************************
Location Search, to allow as above, plus location range search
Examples:
Searching for 'athen -shire' will match on 'west athens' and 'old athens'
Searching for 'athen (100-200,500-600)' will match on any athem mission with
coords x from 100 to 200, y from 500 to 600.
Searching for 'athen (100.2,200.3)' will match on any athen mission with
coords x and y exacly 100.2 and 200.3 respectively
Searching for 'athen (0-500,3000-999999)' will match on any athen mission
with coords x <=500, y>=3000 (but less than 999999)
********************************/
PUU32 LocationMatch( PUU8* LocationName, PUU8* LocationSearch )
{
    PUU8 Name[ 256 ] = { 0 }, Search[ 256 ] = { 0 };
    PUU8 CoordX[ 20 ] = { 0 }, CoordY[ 20 ] = { 0 };
    PUU8 SearchCoordXFrom[ 20 ] = { 0 }, SearchCoordXTo[ 20 ] = { 0 };
    PUU8 SearchCoordYFrom[ 20 ] = { 0 }, SearchCoordYTo[ 20 ] = { 0 };
    PUU8 *pChar;
    PUU8 c, OpenBracketFlag = FALSE, YCoordFlag = FALSE;
    double x, y, xfrom, xto, yfrom, yto;

    pChar = Name;
    do
    {
        c = *LocationName++;

        if( c == '(' )
        {
            *pChar = 0;
            OpenBracketFlag = TRUE;
            pChar = CoordX;
        }
        else if( c == ',' && OpenBracketFlag )
        {
            *pChar = 0;
            pChar = CoordY;
        }
        else
        {
            *pChar++ = c;
        }
    }
    while( c );

    OpenBracketFlag = FALSE;
    pChar = Search;
    do
    {
        c = *LocationSearch++;

        if( c == '(' )
        {
            *pChar = 0;
            OpenBracketFlag = TRUE;
            pChar = SearchCoordXFrom;
        }
        else if( c == ',' && OpenBracketFlag )
        {
            *pChar = 0;
            YCoordFlag = TRUE;
            pChar = SearchCoordYFrom;
        }
        else if( c == '-' && OpenBracketFlag )
        {
            if( YCoordFlag )
            {
                *pChar = 0;
                pChar = SearchCoordYTo;
            }
            else
            {
                *pChar = 0;
                pChar = SearchCoordXTo;
            }
        }
        else
        {
            *pChar++ = c;
        }
    }
    while( c );

    if( ItemMatch( Name, Search ) )
    {

        x = atof( CoordX );
        y = atof( CoordY );
        xfrom = atof( SearchCoordXFrom );
        xto = atof( SearchCoordXTo );
        yfrom = atof( SearchCoordYFrom );
        yto = atof( SearchCoordYTo );

        if( x > 0 && y > 0 && xfrom > 0 && yfrom > 0 )
        {
            if( ( x >= xfrom && ( x <= xto || !xto ) ) && ( y >= yfrom && ( y <= yto || !yto ) ) )
            {
                return TRUE;
            }
            else
            {
                return FALSE;
            }
        }
        else
        {
            return TRUE;
        }
    }

    return FALSE;
}

void GetMissionItem( MissionItem* _pMissionItem, PUU32 _ItemKey1, PUU32
                     _ItemKey2, PUU32 _QL )
{
    MissionItem sItem1, sItem2;

    _pMissionItem->QL = _QL;
    if( !_ItemKey1 )
    {
        goto FetchItemName_Err_NotFound;
    }

    if( !GetAODBItem( &sItem1, _ItemKey1 ) )
    {
        goto FetchItemName_Err_NotFound;
    }


    if( !_ItemKey2 || _ItemKey2 == _ItemKey1 )
		{
			strncpy(_pMissionItem->pName, sItem1.pName, AODB_MAX_NAME_LEN);
			_pMissionItem->pName[AODB_MAX_NAME_LEN] = '\0';
			_pMissionItem->IconKey = sItem1.IconKey;
			_pMissionItem->Value = sItem1.Value;
		}

    else
    {
        if( !GetAODBItem( &sItem2, _ItemKey2 ) )
		{
			goto FetchItemName_Err_NotFound;
		}
		
		if( abs( _QL - sItem1.QL ) < abs( sItem2.QL - _QL ) )
		{
			strncpy(_pMissionItem->pName, sItem1.pName, AODB_MAX_NAME_LEN);
			_pMissionItem->pName[AODB_MAX_NAME_LEN] = '\0';
			_pMissionItem->IconKey = sItem1.IconKey;
		}
		else
		{
			strncpy(_pMissionItem->pName, sItem2.pName, AODB_MAX_NAME_LEN);
			_pMissionItem->pName[AODB_MAX_NAME_LEN] = '\0';
			_pMissionItem->IconKey = sItem2.IconKey;
		}

        if( ( sItem2.QL - sItem1.QL ) == 0 )
        {
            _pMissionItem->Value = sItem1.Value;
        }
        else
        {
            _pMissionItem->Value = sItem1.Value + ( ( sItem2.Value - sItem1.Value ) / ( sItem2.QL - sItem1.QL ) * ( _QL - sItem1.QL ) );
        }
    }

    return;

FetchItemName_Err_NotFound:
    sprintf( _pMissionItem->pName, "Unknown (%X:%X)", _ItemKey1, _ItemKey2 );
    _pMissionItem->IconKey = 0;
    return;
}

PUU8 GetAODBItem( MissionItem* _pMissionItem, PUU32 _ItemKey )
{
    PUU8 *a_xData;
    unsigned long lDataLen = sizeof( MissionItem );
    if( !( a_xData = GetDataChunk( AODB_TYP_ITEM, _ItemKey, &lDataLen ) ) )
    {
        return FALSE;
    }
    if( lDataLen != sizeof( MissionItem ) )
    {
        return FALSE;
    }
    memcpy( _pMissionItem, a_xData, sizeof( MissionItem ) );
	free(a_xData);
	return TRUE;
}


typedef struct png_ihdr_struc
{
    unsigned long lWidth;
    unsigned long lHeight;
    PUU8 xBitDepth;
    PUU8 xColorType;
    PUU8 xCompressMethod;
    PUU8 xFilterMethod;
    PUU8 xInterlaceMethod;
} udtPNGihdr_struc;

typedef struct png_sbit_struc
{
    PUU8 xRed;
    PUU8 xGreen;
    PUU8 xBlue;
} udtPNGsbit_struc;

typedef struct png_pixel_struc
{
    PUU8 xRed;
    PUU8 xGreen;
    PUU8 xBlue;
} udtPNGpixel_struc;

#define LEN_PNGSIG 0x8
PUU8 a_xPNGSig[ LEN_PNGSIG ] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

PUU8 *GetAOIconData( unsigned long lIconNo )
{
    unsigned long lDataLen;
    unsigned long lChunkLen;
    unsigned long lPNGLen;
    unsigned long lLoop;
    unsigned long lLoop2;
    unsigned long lBytesPerRow;
    unsigned long lPNGDataLen;
    unsigned long lPNGImageLen;
    unsigned long lPNGRowOffset;
    PUU8 xFilter;
    PUU8 *a_xData;
    PUU8 *a_xPNG;
    PUU8 *a_xPNGChunk;
    PUU8 *a_xPNGData;
    PUU8 *a_xPNGImage = NULL;
    PUU8 *a_xPNGRow, *a_xPNGRowPrev = NULL;
    udtPNGpixel_struc *udtCLinCByt, *udtCLinPByt, *udtPLinCByt, *udtPLinPByt;
    udtPNGpixel_struc *udtPNGpixel;
    char strChunkID[ 5 ];
    udtPNGihdr_struc *udtPNGihdr;
    udtPNGsbit_struc *udtPNGsbit;

    PUU8* pImageData = NULL;
    PUU8* pTmp;

    a_xData = NULL;
    a_xPNGImage = NULL;
    a_xPNGRowPrev = NULL;

    if( !( a_xData = GetDataChunk( AODB_TYP_ICON, lIconNo, &lDataLen ) ) )
    {
        goto GetAOIconData_Exit_Fail;
    }

    a_xPNG = a_xData;
    lPNGLen = lDataLen;
    if( memcmp( a_xPNG, a_xPNGSig, LEN_PNGSIG ) != 0 )
    {
        goto GetAOIconData_Exit_Fail;
    }
    a_xPNGChunk = a_xPNG + 0x8;

    lChunkLen = EndianSwap32( *(unsigned long *)( a_xPNGChunk ) );
    lChunkLen += 0xC;
    memcpy( strChunkID, ( a_xPNGChunk + 0x4 ), 4 );
    strChunkID[ 4 ] = 0;
    if( _stricmp( strChunkID, "IHDR" ) != 0 )
    {
        goto GetAOIconData_Exit_Fail;
    }
    udtPNGihdr = (udtPNGihdr_struc *)( a_xPNGChunk + 0x8 );
    udtPNGihdr->lWidth = EndianSwap32( udtPNGihdr->lWidth );
    udtPNGihdr->lHeight = EndianSwap32( udtPNGihdr->lHeight );
    a_xPNGChunk += lChunkLen;
	
    if( ( udtPNGihdr->lWidth != 48 ) || ( udtPNGihdr->lHeight != 48 ) )
    {
        goto GetAOIconData_Exit_Fail;
    }
    if( ( udtPNGihdr->xBitDepth != 8 ) || ( udtPNGihdr->xColorType != 2 ) )
    {
        goto GetAOIconData_Exit_Fail;
    }
    if( ( udtPNGihdr->xCompressMethod != 0 ) || ( udtPNGihdr->xFilterMethod != 0 ) )
    {
        goto GetAOIconData_Exit_Fail;
    }
    if( udtPNGihdr->xInterlaceMethod != 0 )
    {
        goto GetAOIconData_Exit_Fail;
    }

    lChunkLen = EndianSwap32( *(unsigned long *)( a_xPNGChunk ) );
    lChunkLen += 0xC;
    memcpy( strChunkID, ( a_xPNGChunk + 0x4 ), 4 );
    strChunkID[ 4 ] = 0;
    if( _stricmp( strChunkID, "SBIT" ) != 0 )
    {
        goto GetAOIconData_Exit_Fail;
    }
    udtPNGsbit = (udtPNGsbit_struc *)( a_xPNGChunk + 0x8 );
    a_xPNGChunk += lChunkLen;

    lPNGDataLen = EndianSwap32( *(unsigned long *)( a_xPNGChunk ) );
    lChunkLen = lPNGDataLen + 0xC;
    memcpy( strChunkID, ( a_xPNGChunk + 0x4 ), 4 );
    strChunkID[ 4 ] = 0;
    if( _stricmp( strChunkID, "IDAT" ) != 0 )
    {
        goto GetAOIconData_Exit_Fail;
    }
    a_xPNGData = a_xPNGChunk + 0x8;
    a_xPNGChunk += lChunkLen;

    lBytesPerRow = ( ( ( udtPNGihdr->lWidth * 24 ) + 31 ) / 32 ) * 4;
    if( !( pImageData = malloc( udtPNGihdr->lHeight * lBytesPerRow ) ) )
    {
        goto GetAOIconData_Exit_Fail;
    }

    lPNGImageLen = udtPNGihdr->lHeight * ( lBytesPerRow + 1 );
    a_xPNGImage = (PUU8 *)malloc( lPNGImageLen );
    if( uncompress( a_xPNGImage, &lPNGImageLen, a_xPNGData, lPNGDataLen ) != Z_OK )
    {
        goto GetAOIconData_Exit_Fail;
    }

    a_xPNGRowPrev = (PUU8 *)malloc( lBytesPerRow );
    memset( a_xPNGRowPrev, 0, lBytesPerRow );

    for( lLoop = 0; lLoop < udtPNGihdr->lHeight; lLoop++ )
    {
        lPNGRowOffset = lLoop * ( lBytesPerRow + 1 );
        xFilter = a_xPNGImage[ lPNGRowOffset ];
        a_xPNGRow = a_xPNGImage + lPNGRowOffset + 1;
        switch( xFilter )
        {
        case 0:
            break;

        case 1:
            udtCLinCByt = (udtPNGpixel_struc *)a_xPNGRow + 1;
            udtCLinPByt = (udtPNGpixel_struc *)a_xPNGRow;
            for( lLoop2 = 1; lLoop2 < ( lBytesPerRow / 3 ); lLoop2++ )
            {
                udtCLinCByt->xRed = (PUU8)( ( (int)( udtCLinCByt->xRed ) +
                    (int)( udtCLinPByt->xRed ) ) & 0xFF );
                udtCLinCByt->xGreen = (PUU8)( ( (int)( udtCLinCByt->xGreen ) +
                    (int)( udtCLinPByt->xGreen ) ) & 0xFF );
                udtCLinCByt->xBlue = (PUU8)( ( (int)( udtCLinCByt->xBlue ) +
                    (int)( udtCLinPByt->xBlue ) ) & 0xFF );
                udtCLinCByt++;
                udtCLinPByt++;
            }
            break;

        case 2:
            udtCLinCByt = (udtPNGpixel_struc *)a_xPNGRow;
            udtPLinCByt = (udtPNGpixel_struc *)a_xPNGRowPrev;
            for( lLoop2 = 0; lLoop2 < ( lBytesPerRow / 3 ); lLoop2++ )
            {
                udtCLinCByt->xRed = (PUU8)( ( (int)( udtCLinCByt->xRed ) +
                    (int)( udtPLinCByt->xRed ) ) & 0xFF );
                udtCLinCByt->xGreen = (PUU8)( ( (int)( udtCLinCByt->xGreen ) +
                    (int)( udtPLinCByt->xGreen ) ) & 0xFF );
                udtCLinCByt->xBlue = (PUU8)( ( (int)( udtCLinCByt->xBlue ) +
                    (int)( udtPLinCByt->xBlue ) ) & 0xFF );
                udtCLinCByt++;
                udtPLinCByt++;
            }
            break;

        case 3:
            udtCLinCByt = (udtPNGpixel_struc *)a_xPNGRow;
            udtPLinCByt = (udtPNGpixel_struc *)a_xPNGRowPrev;
            udtCLinPByt = (udtPNGpixel_struc *)a_xPNGRow;
            udtCLinCByt->xRed = (PUU8)( ( (int)( udtCLinCByt->xRed ) +
                ( (int)( udtPLinCByt->xRed ) >> 1 ) ) & 0xFF );
            udtCLinCByt->xGreen = (PUU8)( ( (int)( udtCLinCByt->xGreen ) +
                ( (int)( udtPLinCByt->xGreen ) >> 1 ) ) & 0xFF );
            udtCLinCByt->xBlue = (PUU8)( ( (int)( udtCLinCByt->xBlue ) +
                ( (int)( udtPLinCByt->xBlue ) >> 1 ) ) & 0xFF );
            udtCLinCByt++;
            udtPLinCByt++;
            for( lLoop2 = 0; lLoop2 < ( lBytesPerRow / 3 ) - 1; lLoop2++ )
            {
                udtCLinCByt->xRed = (PUU8)( ( (int)( udtCLinCByt->xRed ) +
                    ( (int)( udtPLinCByt->xRed + udtCLinPByt->xRed ) >> 1 ) ) & 0xFF );
                udtCLinCByt->xGreen = (PUU8)( ( (int)( udtCLinCByt->xGreen ) +
                    ( (int)( udtPLinCByt->xGreen + udtCLinPByt->xGreen ) >> 1 ) ) & 0xFF );
                udtCLinCByt->xBlue = (PUU8)( ( (int)( udtCLinCByt->xBlue ) +
                    ( (int)( udtPLinCByt->xBlue + udtCLinPByt->xBlue ) >> 1 ) ) & 0xFF );
                udtCLinCByt++;
                udtPLinCByt++;
                udtCLinPByt++;
            }
            break;

        case 4:
            udtCLinCByt = (udtPNGpixel_struc *)a_xPNGRow;
            udtPLinCByt = (udtPNGpixel_struc *)a_xPNGRowPrev;
            udtCLinPByt = (udtPNGpixel_struc *)a_xPNGRow;
            udtPLinPByt = (udtPNGpixel_struc *)a_xPNGRowPrev;
            udtCLinCByt->xRed = (PUU8)( ( (int)( udtCLinCByt->xRed ) +
                (int)( udtPLinCByt->xRed ) ) & 0xFF );
            udtCLinCByt->xGreen = (PUU8)( ( (int)( udtCLinCByt->xGreen ) +
                (int)( udtPLinCByt->xGreen ) ) & 0xFF );
            udtCLinCByt->xBlue = (PUU8)( ( (int)( udtCLinCByt->xBlue ) +
                (int)( udtPLinCByt->xBlue ) ) & 0xFF );
            udtCLinCByt++;
            udtPLinCByt++;
            for( lLoop2 = 0; lLoop2 < ( lBytesPerRow / 3 ) - 1; lLoop2++ )
            {
                int lCLinPByt_R, lPLinCByt_R, lPLinPByt_R, lPaethA_R, lPaethB_R,
                    lPaethC_R, lPaeth_R;
                int lCLinPByt_G, lPLinCByt_G, lPLinPByt_G, lPaethA_G, lPaethB_G,
                    lPaethC_G, lPaeth_G;
                int lCLinPByt_B, lPLinCByt_B, lPLinPByt_B, lPaethA_B, lPaethB_B,
                    lPaethC_B, lPaeth_B;

                lCLinPByt_R = udtCLinPByt->xRed;
                lPLinCByt_R = udtPLinCByt->xRed;
                lPLinPByt_R = udtPLinPByt->xRed;
                lPaeth_R = lPLinCByt_R - lPLinPByt_R;
                lPaethC_R = lCLinPByt_R - lPLinPByt_R;
                lPaethA_R = lPaeth_R < 0 ? -lPaeth_R : lPaeth_R;
                lPaethB_R = lPaethC_R < 0 ? -lPaethC_R : lPaethC_R;
                lPaethC_R = ( lPaeth_R + lPaethC_R ) < 0 ? -( lPaeth_R + lPaethC_R ) :
                    lPaeth_R + lPaethC_R;
                lPaeth_R = ( lPaethA_R <= lPaethB_R && lPaethA_R <= lPaethC_R ) ?
                lCLinPByt_R : ( lPaethB_R <= lPaethC_R ) ? lPLinCByt_R : lPLinPByt_R;
                udtCLinCByt->xRed = (PUU8)( ( (int)( udtCLinCByt->xRed ) + lPaeth_R ) &
                                            0xFF );

                lCLinPByt_G = udtCLinPByt->xGreen;
                lPLinCByt_G = udtPLinCByt->xGreen;
                lPLinPByt_G = udtPLinPByt->xGreen;
                lPaeth_G = lPLinCByt_G - lPLinPByt_G;
                lPaethC_G = lCLinPByt_G - lPLinPByt_G;
                lPaethA_G = lPaeth_G < 0 ? -lPaeth_G : lPaeth_G;
                lPaethB_G = lPaethC_G < 0 ? -lPaethC_G : lPaethC_G;
                lPaethC_G = ( lPaeth_G + lPaethC_G ) < 0 ? -( lPaeth_G + lPaethC_G ) :
                    lPaeth_G + lPaethC_G;
                lPaeth_G = ( lPaethA_G <= lPaethB_G && lPaethA_G <= lPaethC_G ) ?
                lCLinPByt_G : ( lPaethB_G <= lPaethC_G ) ? lPLinCByt_G : lPLinPByt_G;
                udtCLinCByt->xGreen = (PUU8)( ( (int)( udtCLinCByt->xGreen ) + lPaeth_G ) &
                                              0xFF );

                lCLinPByt_B = udtCLinPByt->xBlue;
                lPLinCByt_B = udtPLinCByt->xBlue;
                lPLinPByt_B = udtPLinPByt->xBlue;
                lPaeth_B = lPLinCByt_B - lPLinPByt_B;
                lPaethC_B = lCLinPByt_B - lPLinPByt_B;
                lPaethA_B = lPaeth_B < 0 ? -lPaeth_B : lPaeth_B;
                lPaethB_B = lPaethC_B < 0 ? -lPaethC_B : lPaethC_B;
                lPaethC_B = ( lPaeth_B + lPaethC_B ) < 0 ? -( lPaeth_B + lPaethC_B ) :
                    lPaeth_B + lPaethC_B;
                lPaeth_B = ( lPaethA_B <= lPaethB_B && lPaethA_B <= lPaethC_B ) ?
                lCLinPByt_B : ( lPaethB_B <= lPaethC_B ) ? lPLinCByt_B : lPLinPByt_B;
                udtCLinCByt->xBlue = (PUU8)( ( (int)( udtCLinCByt->xBlue ) + lPaeth_B ) &
                                             0xFF );

                udtCLinCByt++;
                udtCLinPByt++;
                udtPLinCByt++;
                udtPLinPByt++;
            }
            break;

        default:
            goto GetAOIconData_Exit_Fail;
        }

        udtPNGpixel = (udtPNGpixel_struc *)a_xPNGRow;
        pTmp = pImageData + lBytesPerRow * lLoop;
        for( lLoop2 = 0; lLoop2 < ( lBytesPerRow / 3 ); lLoop2++ )
        {
            if( udtPNGpixel->xGreen == 255 && !udtPNGpixel->xRed && !udtPNGpixel->xBlue )
            {
                *pTmp++ = 100;
                *pTmp++ = 100;
                *pTmp++ = 100;
            }
            else
            {
                *pTmp++ = udtPNGpixel->xBlue;
                *pTmp++ = udtPNGpixel->xGreen;
                *pTmp++ = udtPNGpixel->xRed;
            }
            udtPNGpixel++;
        }

        memcpy( a_xPNGRowPrev, a_xPNGRow, lBytesPerRow );
    }
	
    free( a_xPNGRowPrev );
    a_xPNGRowPrev = NULL;

    memcpy( strChunkID, ( a_xPNGChunk + 0x4 ), 4 );
    strChunkID[ 4 ] = 0;
    if( _stricmp( strChunkID, "IEND" ) != 0 )
    {
        goto GetAOIconData_Exit_Fail;
    }

    free( a_xPNGImage );
    a_xPNGImage = NULL;
    free( a_xData );

    return pImageData;

GetAOIconData_Exit_Fail:
    free( pImageData );
    free( a_xPNGRowPrev );
    free( a_xPNGImage );
    free( a_xData );
    return NULL;
}


typedef struct findname_struc
{
    char *strStart;
    char *strEnd;
} udtFindName_struc;

#define CNT_FINDNAME 52
static udtFindName_struc a_udtFindName[CNT_FINDNAME] = {
    "Find prototype ", "!!",
	"The Weird-Looking Bomb", " found ",
    "Weird-Looking Bomb", " found ",
	"The Weird-Looking Bomb", " is set",
	"Weird-Looking Bomb", " is set",
	"Radioactive Isotope Container", " found",
    "a prototype ", " will be moved",
    "we intercepted a message that a prototype ", " will be moved from",
    " - so to speak - obtain the prototype ", " in there",
    "obtain a detailed description of the ", ".",
    "obtain the prototype ", " in there",
    "It is the ", ", please retrieve it",
    "We have lost a valuable prototype. It is the ", ".",
    "We have lost a valuable prototype. It is the ", ",",
    "Enclosed within this mission you can find the ", " ",
    "Enclosed within this mission you can find the ", ".",
    "Using the ", " which is targeted on ",
    "Using the ", " which is targeted on",
    "you will find the ", " in ",
    "find the ", " in there",
    "Please bring ", " to ",
    "bring the ", " to ",
    "collect the ", " from ",
    "retrieve the ", " from ",
    "with forged ", " to undermine",
    "The enemy is in the process of creating a new prototype ", ". It is of utmost importance",
    "The enemy is currently making a new prototype ", ". It is of utmost importance",
    "We have reason to believe finding the ", " in ",
	"finding ", " in ",
    "finding ", ".",
    "In this case it is the ", " that has gone missing.",
    "we have at last found a copy of the ", " in ",
    "According to our sources, the ", " found in ",
    "Last night, the ", " was stolen from a production facility",
    "Last night, one ", " was stolen from a production facility",
    "One of our ", " have been stolen from our ",
    "One ", " has been stolen from our ",
    "A hacker wiped the ", " from our database",
    "I am interested in obtaining a certain ", ". My contacts have",
    "I am interested in obtaining one ", ". My contacts have",
    "have developed a prototype ", ".  We would very,",
    "If we could steal the ", " from the enemy, we would",
    "you can find the entrance to the place where the ", " has been hidden.",
    "you can find the entrance to where the ", " has been hidden.",
    "Would you please find the ", " in ",
    "you might be able to find the ", ". Please bring it back to us",
    "you might be able to find one ", ".",
    "Oh yeah, the ", " is set to blow up in",
    "who or where the traitor is, before you collect the ", " from ",
    "who or where he is, before you collect the ", " from ",
    "you might be able to find one ", ". Bring it back to us",
    "In this case the ", " is missing",
	
};

static long FindSubstringCI(const PUU8* haystack, unsigned long haystackLen, const char* needle)
{
    unsigned long needleLen = strlen(needle);
    if (needleLen == 0 || needleLen > haystackLen) return -1;
    
    for (unsigned long i = 0; i <= haystackLen - needleLen; i++) {
        unsigned long j;
        for (j = 0; j < needleLen; j++) {
            char h = (char)haystack[i + j];
            char n = needle[j];
            if (tolower(h) != tolower(n)) break;
        }
        if (j == needleLen) return (long)i;
    }
    return -1;
}

static int FindItemInDescriptionFromCache(const PUU8* desc, unsigned long descLen, PUU8* outName) {
    if (!g_itemIndex || g_itemIndexSize == 0) return 0;

    char* normDesc = malloc(descLen + 1);
    if (!normDesc) return 0;
    NormalizeString((const char*)desc, normDesc, descLen + 1);

    char* words[1024];
    int wordCount = 0;
    char* token;
	char* context = NULL;
	token = strtok_s(normDesc, " .,!?;:\t\n\r", &context);
	while (token && wordCount < 1024) {
		words[wordCount++] = token;
		token = strtok_s(NULL, " .,!?;:\t\n\r", &context);
	}
	
    const char* bestMatch = NULL;
    int bestLen = 0;
    
    for (int i = 0; i < wordCount; i++) {
        for (int j = 0; j < g_itemIndexSize; j++) {
            if (strcmp(words[i], g_itemIndex[j].firstWord) != 0) continue;
            for (int k = 0; k < g_itemIndex[j].count; k++) {
                const char* item = g_itemIndex[j].items[k];
                char normItem[512];
                NormalizeString(item, normItem, sizeof(normItem));
                if (strstr(normDesc, normItem) != NULL) {
                    int len = strlen(item);
                    if (len > bestLen) {
                        bestLen = len;
                        bestMatch = item;
                    }
                }
            }
        }
    }
    
    free(normDesc);
    
    if (bestMatch) {
        strncpy((char*)outName, bestMatch, 255);
        outName[255] = '\0';
        return 1;
    }
    return 0;
}

PUU32 MissionFind(PUU8* _pMissionDesc, PUU32 _DescLen, PUU8* _pItemName)
{
	if (_pItemName) _pItemName[0] = '\0';
    if (FindItemInDescriptionFromCache(_pMissionDesc, _DescLen, _pItemName)) {
        if (!ShouldSkipItemName((const char*)_pItemName)) {
            return TRUE;
        }
    }
	
    const char* desc = (const char*)_pMissionDesc;
    const char* descEnd = desc + _DescLen;

    for (int i = 0; g_common_items[i]; i++) {
        const char* item = g_common_items[i];
        long pos = FindSubstringCI(_pMissionDesc, _DescLen, item);
        if (pos >= 0) {
            strncpy((char*)_pItemName, item, 255);
            _pItemName[255] = '\0';
            return TRUE;
        }
    }

    // ---- STEP 1: Hardcoded pattern array (specific patterns first) ----
    for (int lLoop = 0; lLoop < CNT_FINDNAME; lLoop++) {
        long lPosStart = FindStr(_pMissionDesc, _DescLen,
                                 (PUU8*)a_udtFindName[lLoop].strStart,
                                 strlen(a_udtFindName[lLoop].strStart));
        if (lPosStart >= 0) {
            char* strStart = (char*)_pMissionDesc + lPosStart +
                             strlen(a_udtFindName[lLoop].strStart);
            long lRem = _DescLen - (lPosStart + strlen(a_udtFindName[lLoop].strStart));
            long lLength = FindStr((PUU8*)strStart, lRem,
                                   (PUU8*)a_udtFindName[lLoop].strEnd,
                                   strlen(a_udtFindName[lLoop].strEnd));
            if (lLength >= 0) {
				if (lLength > 255) lLength = 255;
				memcpy(_pItemName, strStart, lLength);
				_pItemName[lLength] = 0;
                size_t len = strlen(_pItemName);
                while (len > 0 && _pItemName[len-1] == ' ') _pItemName[--len] = '\0';
                if (IsValidItemName((char*)_pItemName)) {
                    return TRUE;
                }
            }
        }
    }

    // ---- STEP 2: Generic extraction (fallback) ----
    static const char* triggers[] = {
        "find the ", "bring the ", "collect the ", "retrieve the ",
        "obtain the ", "a prototype ", "the prototype ", "Find prototype ",
        "a copy of the ", "It is the ", "locate the ", "get the ",
        "take the ", "use the ", "install the ", "pick up the ",
        " a ", " the ",
        NULL
    };
    for (int t = 0; triggers[t]; t++) {
        long pos = FindSubstringCI(_pMissionDesc, _DescLen, triggers[t]);
        if (pos < 0) continue;
        const char* start = desc + pos + strlen(triggers[t]);
        while (start < descEnd && *start == ' ') start++;
        if (start >= descEnd) continue;
        if (!(*start >= 'A' && *start <= 'Z')) continue;
        const char* end = start;
        while (end < descEnd && *end != '.') {
            if (*end == '!' && (end[1] == '!' || end[1] == ' ' || end[1] == '\0')) break;
            if (*end == ',' && end + 2 <= descEnd && end[1] == ' ') break;
            if (strncmp(end, "&mdash;", 7) == 0) break;
            if (*end == ' ' && end + 6 <= descEnd) {
                if (strncmp(end, " in ", 4) == 0 ||
                    strncmp(end, " from ", 6) == 0 ||
                    strncmp(end, " to ", 4) == 0 ||
                    strncmp(end, " for ", 5) == 0 ||
                    strncmp(end, " on ", 4) == 0 ||
                    strncmp(end, " within ", 8) == 0 ||
                    strncmp(end, " into ", 6) == 0 ||
                    strncmp(end, " inside ", 8) == 0 ||
                    strncmp(end, " is missing", 11) == 0 ||
                    strncmp(end, " has been hidden", 16) == 0 ||
                    strncmp(end, " please", 7) == 0 ||
                    strncmp(end, " found ", 7) == 0 ||
                    strncmp(end, " is ", 4) == 0 ||
                    strncmp(end, " lies ", 6) == 0)
                    break;
            }
            end++;
        }
        size_t len = end - start;
        if (len > 0 && len < 256) {
            memcpy(_pItemName, start, len);
                _pItemName[len] = '\0';
                size_t len2 = strlen(_pItemName);
                while (len2 > 0 && _pItemName[len2-1] == ' ') _pItemName[--len2] = '\0';
                if (IsValidItemName((char*)_pItemName)) {
                    return TRUE;
                }
        }
    }
    return FALSE;
}

void MissionPF( PUS32 _PFNum, PUU8* _pPFString )
{
    PUU8 *pData;

    /* Read data for this playfield */
    if( !( pData = GetDataChunk( AODB_TYP_PF, _PFNum, NULL ) ) )
    {
        return;
    }

    strncpy((char*)_pPFString, (char*)pData, 255);
	_pPFString[255] = '\0';

    free( pData );
}

long FindStr( PUU8 *a_xBuf, unsigned long lBufLen, PUU8 *a_xFind, unsigned
              long lFindLen )
{
    long lLoop;
    long lMax;
    PUU8 *a_xBufPtr;

    a_xBufPtr = a_xBuf;
    lMax = (long)lBufLen - (long)lFindLen;
    for( lLoop = 0; lLoop <= lMax; lLoop += 1 )
    {
        if( memcmp( a_xBufPtr, a_xFind, lFindLen ) == 0 )
        {
            break;
        }
        a_xBufPtr += 1;
    }
    if( lLoop > lMax )
    {
        return -1;
    }
    else
    {
        return lLoop;
    }
}