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

static char **g_itemNames = NULL;
static size_t g_numItemNames = 0;
extern sqlite3* g_pSQLite;

//#define DEBUG_MISSION_PACKETS 1

static const char* g_common_items[] = {
    "Contained Sensitive Information",
    "Radioactive Isotope Container",
    "Virus Container",
    "Weird-Looking Bomb",
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

// Returns TRUE if the extracted item name is valid (not a stop word, length >= 3)
static int IsValidItemName(const char* name) {
    if (!name || name[0] == '\0') return 0;
    size_t len = strlen(name);
    if (len < 3) return 0;

    // Stop words (same as before)
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

    // Reject single‑word items that lack punctuation (likely locations)
    int word_count = 1;
    for (size_t i = 0; i < len; i++) {
        if (name[i] == ' ') word_count++;
    }
    int has_punctuation = (strchr(name, ':') != NULL) ||
                          (strchr(name, '(') != NULL) ||
                          (strchr(name, ')') != NULL);

    if (word_count == 1 && !has_punctuation) {
        return 0; // single word without punctuation -> probably a location
    }

    // Convert to lowercase for stop‑word check
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
    // Scan for the name signature: 0x15 0x00 0x00 0x00 0x21 0x00 0x00 0x00
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

// Helper to check for duplicates while building cache
static int is_duplicate(const char *name, char **seen, int *seen_count) {
    for (int i = 0; i < *seen_count; i++) {
        if (strcmp(seen[i], name) == 0)
            return 1;
    }
    seen[*seen_count] = _strdup(name);
    (*seen_count)++;
    return 0;
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

    // Safe bound for compression (compatible with older zlib)
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

    unsigned long origSize, compSize;
    if (fread(&origSize, 4, 1, f) != 1) { fclose(f); return 0; }
    if (fread(&compSize, 4, 1, f) != 1) { fclose(f); return 0; }

    unsigned char *comp = malloc(compSize);
    if (!comp) { fclose(f); return 0; }
    if (fread(comp, 1, compSize, f) != compSize) {
        free(comp); fclose(f); return 0;
    }
    fclose(f);

    unsigned char *data = malloc(origSize);
    if (!data) { free(comp); return 0; }
    if (uncompress(data, &origSize, comp, compSize) != Z_OK) {
        free(data); free(comp); return 0;
    }
    free(comp);

    // Split by '\0' and fill g_itemNames
    size_t count = 0;
    for (size_t i = 0; i < origSize; i++) {
        if (data[i] == '\0') count++;
    }
    g_itemNames = malloc(count * sizeof(char*));
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

    // Sort for binary search
    qsort(g_itemNames, g_numItemNames, sizeof(char*),
          (int(*)(const void*, const void*))strcmp);

    return 1;
}

void FreeItemNameCache(void) {
    for (size_t i = 0; i < g_numItemNames; i++) {
        free(g_itemNames[i]);
    }
    free(g_itemNames);
    g_itemNames = NULL;
    g_numItemNames = 0;
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

/* static void LogMissionDescription(PUU32 missionType, const char* findItem)
{
    // Only log for Find Item or Return Item
    if (!(missionType == 0x2c49 || missionType == 0x26add)) return;

    const char* findStr = (findItem && findItem[0]) ? findItem : "(none)";
    int wordCount = WordCount(findStr);

    // Conditions to log:
    // 1. Extraction failed (findStr == "(none)")
    // 2. OR (extraction succeeded AND wordCount <= 2 AND not a common item)
    int shouldLog = 0;
    if (findStr[0] == '\0' || strcmp(findStr, "(none)") == 0) {
        shouldLog = 1;
    } else if (wordCount <= 2 && !IsCommonItem(findStr)) {
        shouldLog = 1;
    }

    if (!shouldLog) return;

    FILE* f = fopen("clicksaver_missions.log", "a");
    if (!f) return;

    const char* typeStr = MissionTypeToString(missionType);
    fprintf(f, "Type=%s, FindItem=\"%s\"\n", typeStr, findStr);
    fclose(f);
} */

static void LogMissionDescription(PUU32 missionType, const char *findItem,
                                  const PUU8* pDesc, PUU32 descLen)
{
    // Only log for Find Item or Return Item
    if (!(missionType == 0x2c49 || missionType == 0x26add))
        return;

    const char* findStr = (findItem && findItem[0]) ? findItem : "(none)";
    int wordCount = WordCount(findStr);

    int shouldLog = 0;
    char descSnippet[256] = {0};

    if (strcmp(findStr, "(none)") == 0) {
        shouldLog = 1;  // extraction failed
        // Build description snippet (limit to 200 chars, clean newlines)
        if (pDesc && descLen > 0) {
            size_t snippetLen = (descLen < 200) ? descLen : 200;
            strncpy(descSnippet, (const char*)pDesc, snippetLen);
            descSnippet[snippetLen] = '\0';
            // Replace newlines and carriage returns with spaces
            for (char *c = descSnippet; *c; c++) {
                if (*c == '\n' || *c == '\r') *c = ' ';
            }
        }
    } else if (wordCount <= 2) {
        // Only log if the name is NOT a known real item (case‑insensitive)
        if (!IsCommonItem(findStr) && !IsRealItemNameCI(findStr))
            shouldLog = 1;
    }

    if (!shouldLog) return;

    FILE* f = fopen("SkulyDebug.log", "a");
    if (!f) return;

    const char* typeStr = MissionTypeToString(missionType);
    
    // Write log line – include description snippet only for failed extraction
    if (strcmp(findStr, "(none)") == 0 && descSnippet[0]) {
        fprintf(f, "Type=%s, FindItem=\"(none)\", Desc=\"%s\"\n", typeStr, descSnippet);
    } else {
        fprintf(f, "Type=%s, FindItem=\"%s\"\n", typeStr, findStr);
    }
    
    fclose(f);
}

// ========== FORWARD DECLARATIONS FOR COUNTERS ==========
typedef struct ItemCounter {
    char *itemName;
    int limit;
    int accepted;
    struct ItemCounter *next;
} ItemCounter;

extern ItemCounter* FindItemCounter(const char *name);
extern void AddItemCounter(const char *name, int limit);
extern PUU8 g_bUpdatingCounters;
// =======================================================
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

/* AOMD functions */
PUU8 GetAODBItem( MissionItem* _pMissionItem, PUU32 _ItemKey );
void GetMissionItem( MissionItem* _pMissionItem, PUU32 _ItemKey1, PUU32 _ItemKey2, PUU32 _QL );
PUU8 *GetAOIconData( unsigned long lIconNo );
PUU32 MissionFind( PUU8* _pMissionDesc, PUU32 _DescLen, PUU8* _pItemName );
void MissionPF( PUS32 _PFNum, PUU8* _pPFString );
long FindStr( PUU8* a_xBuf, unsigned long lBufLen, PUU8* a_xFind, unsigned long lFindLen );
/***/

static const char *container_prefixes[] = {
    "blister pack with",
    "symbio-graft:",
    "charged nano finger",
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
	g_bOverrideMatch = 0;
	PUU32 bAccept = FALSE;
    char TempStr[ 256 ], CharKey[ 6 ];
    char PFName[ 256 ] = { 0 };
    float CoordX = { 0 }, CoordY = { 0 };
    PUU32 TempVal, MishPF;
    PUU32 Cash, XP, MishQL, MishID, TotalValue;
    PUU32 bAlertItem, bAlertLoc, bAlertType;
    PUU32 bItemNameMatch = FALSE;    // watchlist name matched
    PUU32 bValueMatch = FALSE;       // any value threshold exceeded
    PUU32 bLocFound = FALSE, bTypeFound = FALSE;
    PUU32 Count = 65536 - 4, DescLength;
    PUU8* pEndMissionData;
    PUU8* pDesc;
    Item* pItem;
    Item* pTmpItem;
    PUU32 NumItems = 0, i;
    pusObjectCollection* pPrevCol;

    pEndMissionData = _pMissionData + 65536 - 4;
    bAlertItem = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ALERTITEM_CB ), PUA_CHECKBOX_CHECKED );
    bAlertLoc = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ALERTLOC_CB ), PUA_CHECKBOX_CHECKED );
    bAlertType = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ALERTTYPE_CB ), PUA_CHECKBOX_CHECKED );

    if( !puGetAttribute( g_ItemWatchList, PUA_TABLE_NUMRECORDS ) ) bAlertItem = FALSE;
    if( !puGetAttribute( g_LocWatchList, PUA_TABLE_NUMRECORDS ) ) bAlertLoc = FALSE;

    // Find start of mission packet (look for 0xDAC3)
    do {
        if( _pMissionData >= pEndMissionData ) {
            puSetAttribute( puGetObjectFromCollection( _pData->pCol, ROOTOBJ ), PUA_CONTROL_HIDDEN, TRUE );
            return 0;
        }
        _pMissionData++;
        TempVal = EndianSwap32( *(PUU32*)_pMissionData );
    } while( TempVal != 0xdac3 );

    puSetAttribute( puGetObjectFromCollection( _pData->pCol, ROOTOBJ ), PUA_CONTROL_HIDDEN, FALSE );

#ifdef DEBUG_MISSION_PACKETS
    WriteDebug( "\nMission Header:\n" );
    DebugPacket( _pMissionData, 6 * 4 );
    WriteDebug( 0 );
#endif

    MishID = EndianSwap32( *(PUU32*)(_pMissionData + 0x04) );
    _pMissionData += 6 * 4;
    if( _pMissionData >= pEndMissionData ) return 0;

    // Skip short description (null terminated)
    while( *_pMissionData ) _pMissionData++;
    _pMissionData++;

    // Get full description length
    TempVal = EndianSwap32( *(PUU32*)_pMissionData );
    _pMissionData += 4;
    pDesc = _pMissionData;
    
    // **
	
    DescLength = TempVal;
    _pMissionData += TempVal;
    if( _pMissionData >= pEndMissionData ) return 0;

    if( (pEndMissionData - _pMissionData) < 0xe8 ) return 0;

    Cash = EndianSwap32( *(PUU32*)(_pMissionData + 0xc) );
    TotalValue = Cash;
    XP = EndianSwap32( *(PUU32*)(_pMissionData + 0x14) );

    // Count items
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

    // Playfield and coordinates
    MishPF = EndianSwap32( *(PUU32*)(_pMissionData + 0xA8) );
    MissionPF( MishPF, PFName );
    TempVal = EndianSwap32( *(PUU32*)(_pMissionData + 0xb4) );
    *(PUU32*)(&CoordX) = TempVal;
    TempVal = EndianSwap32( *(PUU32*)(_pMissionData + 0xbc) );
    *(PUU32*)(&CoordY) = TempVal;
    sprintf( TempStr, "%s (%.1f, %.1f)", PFName, CoordX, CoordY );
    bLocFound = SetAndSearch( TempStr, puGetObjectFromCollection( _pData->pCol, LOCATION ), g_LocWatchList );

    // Mission type
    TempVal = EndianSwap32( *(PUU32*)(_pMissionData + 0x28) );
    bTypeFound = SetAndSearchType( TempVal, puGetObjectFromCollection( _pData->pCol, MISHTYPE ) );

    WriteLog( "mission\t%u\t%u\t%u\t%u\t%s\n", MishID, MishQL, XP, Cash, CharKey );
    WriteLog( "loc\t%u\t%.1f\t%.1f\t%s\n", MishPF, CoordX, CoordY, PFName );

    // Process items – collect name matches and value matches
    for( i = 0; i < NumItems; i++ ) {
        PUU32 flags = ShowItem( _pData, pItem++, i + ITEM1, i + ITEMVAL1 );
        bItemNameMatch |= (flags & 1);
        bValueMatch |= ((flags >> 1) & 1);
        TotalValue += _pData->Reward.Value * puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMVALUE_BUYMOD ), PUA_TEXTENTRY_VALUE ) / 100;
    }

    // Total value highlight and match
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

     // Find item in description – only for Find Item (0x2c49) and Return Item (0x26add)
    if (TempVal == 0x2c49 || TempVal == 0x26add) {
        if (MissionFind(pDesc, DescLength, TempStr)) {
            WriteLog("find\t%s\n", TempStr);
            if (SetAndSearch(TempStr, puGetObjectFromCollection(_pData->pCol, FINDITEM), g_ItemWatchList))
                bItemNameMatch = TRUE;
        } else {
            puSetAttribute(puGetObjectFromCollection(_pData->pCol, FINDITEM), PUA_TEXTENTRY_BUFFER, 0);
            TempStr[0] = '\0';
        }
    } else {
        // For all other mission types, clear the Find: field
        puSetAttribute(puGetObjectFromCollection(_pData->pCol, FINDITEM), PUA_TEXTENTRY_BUFFER, 0);
        TempStr[0] = '\0';
    }
	
        // If any override item (starting with '~') was found, accept immediately
    if( g_bOverrideMatch ) {
        bAccept = 1;
    } else {
        // ---- NEW LOGIC for optional item name (Behavior B) ----
        PUU32 bItemOptional = puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMOPTIONAL_CB ), PUA_CHECKBOX_CHECKED );
        PUU32 bNonItemActive = bAlertLoc || bAlertType || PUL_GET_CB(CS_ITEMVALUE_MSINGLE) || PUL_GET_CB(CS_ITEMVALUE_MTOTAL);

        if( bItemOptional && bNonItemActive ) {
            bAccept = 1;
            if( bAlertLoc && !bLocFound ) bAccept = 0;
            if( bAlertType && !bTypeFound ) bAccept = 0;
            if( (PUL_GET_CB(CS_ITEMVALUE_MSINGLE) || PUL_GET_CB(CS_ITEMVALUE_MTOTAL)) && !bValueMatch ) bAccept = 0;
        } else {
            bAccept = bAlertItem || bAlertLoc || bAlertType;
            if( bAlertItem ) bAccept = bAccept && bItemNameMatch;
            if( bAlertLoc )  bAccept = bAccept && bLocFound;
            if( bAlertType ) bAccept = bAccept && bTypeFound;
            if( PUL_GET_CB(CS_ITEMVALUE_MSINGLE) || PUL_GET_CB(CS_ITEMVALUE_MTOTAL) )
                bAccept = bAccept && bValueMatch;
        }
    }
	
	        LogMissionDescription(TempVal, TempStr, pDesc, DescLength);

    if( bAccept ) {
        if( g_FoundMish == 255 ) g_FoundMish = g_MishNumber;
        if( g_BuyingAgentCount ) {
            g_BuyingAgentCount = 0;
        } else {
            if( puGetAttribute( puGetObjectFromCollection( g_pCol, CS_MSGBOX_CB ), PUA_CHECKBOX_CHECKED ) && !g_bFullscreen ) {
                puSetAttribute( g_MainWin, PUA_WINDOW_ICONIFIED, FALSE );
                puSetAttribute( puGetObjectFromCollection( g_pCol, CS_WATCH_MSGBOX ), PUA_WINDOW_OPENED, TRUE );
            }
        }
    }

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

        // Display item name and ql
        sprintf( TempStr, "QL%u %s", QL, _pData->Reward.pName );
        bNameMatch = SetAndSearch( TempStr, puGetObjectFromCollection( _pData->pCol, _ObjId ), g_ItemWatchList );

        // Calculate value after buy mod
        int itemValue = _pData->Reward.Value * puGetAttribute( puGetObjectFromCollection( g_pCol, CS_ITEMVALUE_BUYMOD ), PUA_TEXTENTRY_VALUE ) / 100;
        puSetAttribute( puGetObjectFromCollection( _pData->pCol, _ValID ), PUA_TEXTENTRY_VALUE, itemValue );

        // Check single‑item value threshold
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

        // Icon handling
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
    case 0x26add:	//PRK new id
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
    // Don't update interface when buying agent is running
    if( !g_BuyingAgentCount || g_bForceUIRefresh )
    {
        puSetAttribute( _TextEntry, PUA_TEXTENTRY_BUFFER, (PUU32)&TempStr );
        puSetAttribute( _TextEntry, PUA_TEXTENTRY_HILIGHT, match &&
                        puGetAttribute( puGetObjectFromCollection( g_pCol, CS_HIGHLIGHTTYPE_CB ),
                        PUA_CHECKBOX_CHECKED ) );
    }
    return match;
}

// Parse a display string like "ItemName [force accept] [qty N] [exclude: words]"
// Returns 1 if force-accept is present, and fills name, limit, and exclude words.
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

    // --- 1. Item name: everything up to first '[' or end ---
    char *nameEnd = strchr(buf, '[');
    if (!nameEnd) nameEnd = buf + strlen(buf);
    size_t nameLen = nameEnd - buf;
    while (nameLen > 0 && buf[nameLen-1] == ' ') nameLen--;
    if (nameLen >= nameSize) nameLen = nameSize-1;
    strncpy(itemName, buf, nameLen);
    itemName[nameLen] = '\0';

    // --- 2. Parse optional tags ---
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
            // Skip unknown tags
            while (*p && *p != ']') p++;
            if (*p == ']') p++;
        }
    }
    return force;
}

/* Set string to a textentry control, search for it in a list,
   and hilight the textentry if the string was found.
*/

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
            if( *pString == '#' ) {   // legacy disabled items (not used anymore)
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
							// Skip this watch item entirely – move to next record
							Record = puDoMethod(_List, PUM_TABLE_GETNEXTRECORD, Record, 0);
							continue;
						}
					}

                // Build search string for ItemMatch: "cleanName -exclude1 -exclude2"
                char searchStr[512] = { 0 };
                searchStr[0] = '\0';
                strncat(searchStr, cleanName, sizeof(searchStr)-1);
                if (excludeWords[0]) {
                    char *tok = strtok(excludeWords, " ");
                    while (tok) {
                        strncat(searchStr, " -", sizeof(searchStr)-strlen(searchStr)-2);
                        strncat(searchStr, tok, sizeof(searchStr)-strlen(searchStr)-1);
                        tok = strtok(NULL, " ");
                    }
                }

                if( ItemMatch( TmpItemName, (PUU8*)searchStr ) ) {
                    // Handle quantity limit
                    if( limit > 0 ) {
                        ItemCounter *ic = FindItemCounter( cleanName );
                        if( !ic ) {
                            AddItemCounter( cleanName, limit );
                            ic = FindItemCounter( cleanName );
                        }
                        if( ic ) {
                            if( g_bUpdatingCounters ) {
                                if (ic->accepted < ic->limit) {
                                    ic->accepted++;
                                }
                            } else if( ic->accepted >= ic->limit ) {
                                Record = puDoMethod( _List, PUM_TABLE_GETNEXTRECORD, Record, 0 );
                                continue;
                            }
                        }
                    }
                    
                    // Force-accept flag
                    if( force ) {
                        g_bOverrideMatch = 1;
                    }
                    
                    // Highlight if not in buying agent
                    if( !g_BuyingAgentCount || g_bForceUIRefresh ) {
                        puSetAttribute( _TextEntry, PUA_TEXTENTRY_HILIGHT,
                            puGetAttribute( puGetObjectFromCollection( g_pCol, CS_HIGHLIGHTITEM_CB ), PUA_CHECKBOX_CHECKED ) );
                    }
                    return TRUE;
                }
            } else { // location matching
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

/*******************************
Item Search, to allow web-search-like constructors "<text>", -<text> and
word-match
Examples:
Searching for 'decus -gloves' will match all decus items except gloves;
Searching for 'decus armor' will match on 'decus body armor', and 'decus
armor boots'
Searching for '"decus armor"' will match on 'decus armor boots' but not on
'decus body armor'
Searching for '"primus decus" -gloves -boots -body' will match on all primus
decus armor except for gloves, boots and body
********************************/
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
            else if( c == '-' && pChar == TmpString )
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

// Returns 1 if the user's watchlist entry would match at least one real item.
int IsWatchlistEntryValid(const char *searchStr)
{
    if (!g_itemNames || !searchStr || !*searchStr) return 0;

    // Step 1: Full watchlist matching logic (supports quotes, exclusions, word order)
    for (size_t i = 0; i < g_numItemNames; i++) {
        if (ItemMatch((PUU8*)g_itemNames[i], (PUU8*)searchStr)) {
            return 1;
        }
    }

    // Step 2: Case‑insensitive prefix match (for missing trailing punctuation)
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
            return 1;   // database name starts with the entered text
        }
    }

    // Step 3: Case‑insensitive substring match (for phrases inside longer names)
    // Skip very short search strings to avoid false positives (e.g., "a", "an")
    if (len >= 3) {
        for (size_t i = 0; i < g_numItemNames; i++) {
            const char *dbName = g_itemNames[i];
            // Convert database name to lowercase once per iteration
            char lowerDb[256] = {0};
            size_t k;
            for (k = 0; dbName[k] && k < sizeof(lowerDb)-1; k++) {
                lowerDb[k] = tolower((unsigned char)dbName[k]);
            }
            lowerDb[k] = '\0';
            if (strstr(lowerDb, lowerSearch) != NULL) {
                return 1;   // search string is a substring of a real item name
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

    // Build the full search string (same as before: baseName + " -" + exclude words)
    char fullSearch[512];
    strcpy(fullSearch, baseName);
    if (excludeWords && *excludeWords) {
        char excludeCopy[256];
        strncpy(excludeCopy, excludeWords, sizeof(excludeCopy)-1);
        excludeCopy[sizeof(excludeCopy)-1] = '\0';
        char *tok = strtok(excludeCopy, ", ");
        while (tok) {
            while (*tok == ' ') tok++;
            if (*tok) {
                strcat(fullSearch, " -");
                strcat(fullSearch, tok);
            }
            tok = strtok(NULL, ", ");
        }
    }

    // Helper: case‑insensitive substring
    #define stristr(haystack, needle) strstr(haystack, needle) // we'll lower case both

    // For each database name, we need to parse the fullSearch into tokens.
    int capacity = 0;
    int count = 0;
    const char **items = NULL;

    // Pre‑process fullSearch into an array of tokens (with exclude flag)
    // We'll parse it once per call, not per item, for efficiency.
    char searchCopy[512];
    strcpy(searchCopy, fullSearch);
    char *tokens[64];
    int excludeFlag[64];
    int numTokens = 0;
    char *p = searchCopy;
    int inQuote = 0;
    char *tokenStart = NULL;
    while (*p) {
        if (*p == '"') {
            inQuote = !inQuote;
            p++;
            continue;
        }
        if (!inQuote && (*p == ' ' || *p == '\t')) {
            if (tokenStart) {
                *p = '\0';
                // token from tokenStart to p-1
                char *tok = tokenStart;
                if (tok[0] == '-') {
                    excludeFlag[numTokens] = 1;
                    tok++; // skip the '-'
                } else {
                    excludeFlag[numTokens] = 0;
                }
                // lower case the token for comparison
                for (char *c = tok; *c; c++) *c = tolower((unsigned char)*c);
                tokens[numTokens++] = tok;
                tokenStart = NULL;
            }
            p++;
            continue;
        }
        if (!tokenStart) tokenStart = p;
        p++;
    }
    if (tokenStart) {
        char *tok = tokenStart;
        if (tok[0] == '-') {
            excludeFlag[numTokens] = 1;
            tok++;
        } else {
            excludeFlag[numTokens] = 0;
        }
        for (char *c = tok; *c; c++) *c = tolower((unsigned char)*c);
        tokens[numTokens++] = tok;
    }

    // If nothing to search, return 0
    if (numTokens == 0) return 0;

    // Now iterate through database items
    for (size_t i = 0; i < g_numItemNames; i++) {
        const char *dbName = g_itemNames[i];
        char lowerDb[256];
        size_t k;
        for (k = 0; dbName[k] && k < sizeof(lowerDb)-1; k++)
            lowerDb[k] = tolower((unsigned char)dbName[k]);
        lowerDb[k] = '\0';

        int match = 1;
        for (int t = 0; t < numTokens; t++) {
            if (excludeFlag[t]) {
                // Exclusion token must NOT be found
                if (strstr(lowerDb, tokens[t]) != NULL) {
                    match = 0;
                    break;
                }
            } else {
                // Normal token must be found
                if (strstr(lowerDb, tokens[t]) == NULL) {
                    match = 0;
                    break;
                }
            }
        }
        if (!match) continue;

        // Duplicate check
        int dup = 0;
        for (int j = 0; j < count; j++) {
            if (strcmp(items[j], dbName) == 0) { dup = 1; break; }
        }
        if (dup) continue;

        if (count >= capacity) {
            capacity = capacity ? capacity * 2 : 32;
            const char **newItems = realloc(items, capacity * sizeof(const char*));
            if (!newItems) { free(items); return 0; }
            items = newItems;
        }
        items[count++] = dbName;
    }

    if (count > 0) {
        *outItems = items;
        *outCount = count;
    } else {
        free(items);
    }
    return count;
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

    // pull Name, CoordX, CoordY
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

    // pull Search, SearchCoordXFrom, SearchCoordXTo, SearchCoordYFrom, SearchCoordYTo
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

    // compare LocationName to LocationSearch
    if( ItemMatch( Name, Search ) )
    {

        // if matched, compare coordtinates
        x = atof( CoordX );
        y = atof( CoordY );
        xfrom = atof( SearchCoordXFrom );
        xto = atof( SearchCoordXTo );
        yfrom = atof( SearchCoordYFrom );
        yto = atof( SearchCoordYTo );

        if( x > 0 && y > 0 && xfrom > 0 && yfrom > 0 )
        { // carry on only if we have mission location
            if( ( x >= xfrom && ( x <= xto || !xto ) ) && ( y >= yfrom && ( y <= yto || !yto ) ) )
            {
                return TRUE; // loc name matched, coords matched
            }
            else
            {
                return FALSE; // loc name matched; coonds wrong
            }
        }
        else
        {
            return TRUE; // loc name matched; won't compare coords
        }
    }

    return FALSE; // loc name didn't match
}


/*******************************
Various parts borrowed from AOMD
(database access, PNG unpacking, playfield names, find item name finder)
********************************/
void GetMissionItem( MissionItem* _pMissionItem, PUU32 _ItemKey1, PUU32
                     _ItemKey2, PUU32 _QL )
{
    MissionItem sItem1, sItem2;

    _pMissionItem->QL = _QL;
    if( !_ItemKey1 )
    {
        goto FetchItemName_Err_NotFound;
    }

    /* Get description for item number 1 */
    if( !GetAODBItem( &sItem1, _ItemKey1 ) )
    {
        goto FetchItemName_Err_NotFound;
    }

    /* If no item number 2, then just keep the first description */
    if( !_ItemKey2 || _ItemKey2 == _ItemKey1 )
    {
        strcpy( _pMissionItem->pName, sItem1.pName );
        _pMissionItem->IconKey = sItem1.IconKey;
        _pMissionItem->Value = sItem1.Value;
    }
    /* Item number 2 exists, must interpolate */
    else
    {
        if( !GetAODBItem( &sItem2, _ItemKey2 ) )
        {
            goto FetchItemName_Err_NotFound;
        }

        if( abs( _QL - sItem1.QL ) < abs( sItem2.QL - _QL ) )
        {
            strcpy( _pMissionItem->pName, sItem1.pName );
            _pMissionItem->IconKey = sItem1.IconKey;
        }
        else
        {
            strcpy( _pMissionItem->pName, sItem2.pName );
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

    /* Success */
    return;

FetchItemName_Err_NotFound:
    sprintf( _pMissionItem->pName, "Unknown (%X:%X)", _ItemKey1, _ItemKey2 );
    _pMissionItem->IconKey = 0;
    return;
}


/* Get item Data from AO Database */
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


/* Get item icon from AO Database */
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
    //FILE *fpDebug;
    PUU8* pImageData = NULL;
    PUU8* pTmp;

    /* Initialise */
    a_xData = NULL;
    a_xPNGImage = NULL;
    a_xPNGRowPrev = NULL;

    /* Read data for this item */
    if( !( a_xData = GetDataChunk( AODB_TYP_ICON, lIconNo, &lDataLen ) ) )
    {
        goto GetAOIconData_Exit_Fail;
    }

    /* Check it contains a valid PNG */
    a_xPNG = a_xData; // + 0x18;
    lPNGLen = lDataLen; // - 0x18;
    if( memcmp( a_xPNG, a_xPNGSig, LEN_PNGSIG ) != 0 )
    {
        goto GetAOIconData_Exit_Fail;
    }
    a_xPNGChunk = a_xPNG + 0x8;     // Point to first chunk

    /* Write PNG icon to file */
    /*
    if( lDebug & DBG_ICN )
    {
        sprintf( strDebugFile, "%sDebug_IconsPNG.DAT", strAOMDPath );
        fpDebug = fopen( strDebugFile, "a+b" );
        fwrite( a_xPNG, sizeof( PUU8 ), lPNGLen, fpDebug );
        fwrite( "****************", sizeof( char ), 0x10 - ( lPNGLen % 0x10 ),
                fpDebug );
        fwrite( "****************", sizeof( char ), 0x10, fpDebug );
        fclose( fpDebug );
    }
    */

    /* Check IHDR chunk - Start of PNG, contains icon properties */
    lChunkLen = EndianSwap32( *(unsigned long *)( a_xPNGChunk ) );
    lChunkLen += 0xC;
    memcpy( strChunkID, ( a_xPNGChunk + 0x4 ), 4 );
    strChunkID[ 4 ] = 0;
    if( _stricmp( strChunkID, "IHDR" ) != 0 )
    {
        goto GetAOIconData_Exit_Fail;
    }
    udtPNGihdr = (udtPNGihdr_struc *)( a_xPNGChunk + 0x8 );
    udtPNGihdr->lWidth = EndianSwap32( udtPNGihdr->lWidth );  // Fix endian
    udtPNGihdr->lHeight = EndianSwap32( udtPNGihdr->lHeight );    // Fix endian
    a_xPNGChunk += lChunkLen;   // Bump to next chunk

    /* Ensure PNG properties are what we expect from AO */
    if( ( udtPNGihdr->lWidth != 48 ) || ( udtPNGihdr->lHeight != 48 ) )
    {
        goto GetAOIconData_Exit_Fail;   // not a 48x48 image
    }
    if( ( udtPNGihdr->xBitDepth != 8 ) || ( udtPNGihdr->xColorType != 2 ) )
    {
        goto GetAOIconData_Exit_Fail;   // not 24bit RGB
    }
    if( ( udtPNGihdr->xCompressMethod != 0 ) || ( udtPNGihdr->xFilterMethod != 0 ) )
    {
        goto GetAOIconData_Exit_Fail;   // non-standard compression or filter
    }
    if( udtPNGihdr->xInterlaceMethod != 0 )
    {
        goto GetAOIconData_Exit_Fail;   // must not be interlaced
    }

    /* Check SBIT chunk - Significant bits */
    lChunkLen = EndianSwap32( *(unsigned long *)( a_xPNGChunk ) );
    lChunkLen += 0xC;
    memcpy( strChunkID, ( a_xPNGChunk + 0x4 ), 4 );
    strChunkID[ 4 ] = 0;
    if( _stricmp( strChunkID, "SBIT" ) != 0 )
    {
        goto GetAOIconData_Exit_Fail;
    }
    udtPNGsbit = (udtPNGsbit_struc *)( a_xPNGChunk + 0x8 );
    a_xPNGChunk += lChunkLen;   // Bump to next chunk

    /* Check IDAT chunk - Contains the icon data*/
    lPNGDataLen = EndianSwap32( *(unsigned long *)( a_xPNGChunk ) );
    lChunkLen = lPNGDataLen + 0xC;
    memcpy( strChunkID, ( a_xPNGChunk + 0x4 ), 4 );
    strChunkID[ 4 ] = 0;
    if( _stricmp( strChunkID, "IDAT" ) != 0 )
    {
        goto GetAOIconData_Exit_Fail;
    }
    a_xPNGData = a_xPNGChunk + 0x8;
    a_xPNGChunk += lChunkLen;   // Bump to next chunk

    /* Allocate bitmap */
    lBytesPerRow = ( ( ( udtPNGihdr->lWidth * 24 ) + 31 ) / 32 ) * 4;
    if( !( pImageData = malloc( udtPNGihdr->lHeight * lBytesPerRow ) ) )
    {
        goto GetAOIconData_Exit_Fail;
    }


    /* Decompress the PNG image data using ZLib */
    lPNGImageLen = udtPNGihdr->lHeight * ( lBytesPerRow + 1 );
    a_xPNGImage = (PUU8 *)malloc( lPNGImageLen );
    if( uncompress( a_xPNGImage, &lPNGImageLen, a_xPNGData, lPNGDataLen ) != Z_OK )
    {
        goto GetAOIconData_Exit_Fail;
    }

    /* Allocate previous row buffer and init to zero */
    a_xPNGRowPrev = (PUU8 *)malloc( lBytesPerRow );
    memset( a_xPNGRowPrev, 0, lBytesPerRow );

    /* Filter each row and copy to bitmap */
    for( lLoop = 0; lLoop < udtPNGihdr->lHeight; lLoop++ )
    {
        lPNGRowOffset = lLoop * ( lBytesPerRow + 1 );
        xFilter = a_xPNGImage[ lPNGRowOffset ];
        a_xPNGRow = a_xPNGImage + lPNGRowOffset + 1;
        switch( xFilter )
        {
            /* Filter 0 - None */
        case 0:
            break;

            /* Filter 1 - Sub */
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

            /* Filter 2 - Up */
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

            /* Filter 3 - Average */
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

            /* Filter 4 - Paeth */
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

            /* Unknown filter value */
        default:
            goto GetAOIconData_Exit_Fail;
        }

        /* Copy processed row to bitmap (have to do this pixel by pixel because
PNG is RGB but DIB is BGR) */
        udtPNGpixel = (udtPNGpixel_struc *)a_xPNGRow;
        pTmp = pImageData + lBytesPerRow * lLoop;
        //      rgbDIBpixel = (RGBTRIPLE *)((PUU8 *)(bmiDIB->bmiColors) + (((udtPNGihdr->lHeight - 1) - lLoop) * lBytesPerRow));
        for( lLoop2 = 0; lLoop2 < ( lBytesPerRow / 3 ); lLoop2++ )
        {
            // PUL doesn't handle color key on images yet and I don't have MSDNs at hand
            // to implement it, so in the meantime... :)
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

        /* Copy processed row to previous row buffer */
        memcpy( a_xPNGRowPrev, a_xPNGRow, lBytesPerRow );
    }

    /* Release previous row buffer */
    free( a_xPNGRowPrev );
    a_xPNGRowPrev = NULL;

    /* Check IEND chunk - This marks the end of PNG */
    memcpy( strChunkID, ( a_xPNGChunk + 0x4 ), 4 );
    strChunkID[ 4 ] = 0;
    if( _stricmp( strChunkID, "IEND" ) != 0 )
    {
        goto GetAOIconData_Exit_Fail;
    }

    /* Release the PNG image and data chunk */
    free( a_xPNGImage );
    a_xPNGImage = NULL;
    free( a_xData );

    /* Write icon bitmap to file */
    /*
    if( lDebug & DBG_ICN )
    {
        sprintf( strDebugFile, "%sDebug_IconsBMP.DAT", strAOMDPath );
        fpDebug = fopen( strDebugFile, "a+b" );
        fwrite( &bmiBMPhdr, sizeof( BITMAPFILEHEADER ), 1, fpDebug );
        fwrite( bmiDIB, sizeof( PUU8 ), lDIBsize, fpDebug );
        fwrite( "****************", sizeof( char ), 0x10 - ( ( sizeof( BITMAPFILEHEADER )
            + lDIBsize ) % 0x10 ), fpDebug );
        fwrite( "****************", sizeof( char ), 0x10, fpDebug );
        fclose( fpDebug );
    }
    */

    /* Success - return the bitmap */
    return pImageData;

GetAOIconData_Exit_Fail:    // Cleanup
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



// Helper: case‑insensitive substring search with length limit
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

// Main extraction function
PUU32 MissionFind(PUU8* _pMissionDesc, PUU32 _DescLen, PUU8* _pItemName)
{
    // Safety: convert to char pointers for easier arithmetic
    const char* desc = (const char*)_pMissionDesc;
    const char* descEnd = desc + _DescLen;

    for (int i = 0; g_common_items[i]; i++) {
        const char* item = g_common_items[i];
        long pos = FindSubstringCI(_pMissionDesc, _DescLen, item);
        if (pos >= 0) {
            // Copy the exact common item name (not the whole sub‑string)
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
                memcpy(_pItemName, strStart, lLength);
                _pItemName[lLength] = 0;
                // Trim trailing spaces
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
        // Require that the first character is uppercase (A-Z)
        if (!(*start >= 'A' && *start <= 'Z')) continue;
        const char* end = start;
        while (end < descEnd && *end != '.') {
            // Stop at !! or !
            if (*end == '!' && (end[1] == '!' || end[1] == ' ' || end[1] == '\0')) break;
            // Stop at ", " (comma then space)
            if (*end == ',' && end + 2 <= descEnd && end[1] == ' ') break;
			// Stop at &mdash;
            if (strncmp(end, "&mdash;", 7) == 0) break;
            // Stop at space followed by certain words
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
        if (len > 0 && len < 255) {
            memcpy(_pItemName, start, len);
                _pItemName[len] = '\0';
                // Trim trailing spaces
                size_t len2 = strlen(_pItemName);  // use different variable name to avoid shadowing
                while (len2 > 0 && _pItemName[len2-1] == ' ') _pItemName[--len2] = '\0';
                if (IsValidItemName((char*)_pItemName)) {
                    return TRUE;
                }
        }
    }
    return FALSE;
}

/* Return mission PlayField */
void MissionPF( PUS32 _PFNum, PUU8* _pPFString )
{
    PUU8 *pData;

    /* Read data for this playfield */
    if( !( pData = GetDataChunk( AODB_TYP_PF, _PFNum, NULL ) ) )
    {
        return;
    }

    strcpy( _pPFString, pData );

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