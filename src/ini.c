/*
 * Initialization-file parser.
 *
 * Copyright (c) 1993 Miguel de Icaza
 *
 * You may without charge, royalty or other payment, copy and
 * distribute copies of this work and derivative works of this work
 * in source or binary form provided that: (1)
 * you appropriately publish on each copy an appropriate copyright
 * notice; (2) faithfully reproduce all prior copyright notices
 * included in the original work (you may also add your own
 * copyright notice); and (3) agree to indemnify and hold all prior
 * authors, copyright holders and licensors of the work harmless
 * from and against all damages arising from use of the work.
 *
 * You may distribute sources of derivative works of the work
 * provided that (1) (a) all source files of the original work that
 * have been modified, (b) all source files of the derivative work
 * that contain any party of the original work, and (c) all source
 * files of the derivative work that are necessary to compile, link
 * and run the derivative work without unresolved external calls and
 * with the same functionality of the original work ("Necessary
 * Sources") carry a prominent notice explaining the nature and date
 * of the modification and/or creation.  You are encouraged to make
 * the Necessary Sources available under this license in order to
 * further the development and acceptance of the work.
 *
 * EXCEPT AS OTHERWISE RESTRICTED BY LAW, THIS WORK IS PROVIDED
 * WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES OF ANY KIND, INCLUDING
 * BUT NOT LIMITED TO, ANY IMPLIED WARRANTIES OF FITNESS FOR A
 * PARTICULAR PURPOSE, MERCHANTABILITY OR TITLE.  EXCEPT AS
 * OTHERWISE PROVIDED BY LAW, NO AUTHOR, COPYRIGHT HOLDER OR
 * LICENSOR SHALL BE LIABLE TO YOU FOR DAMAGES OF ANY KIND, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 *
 * ---------------------------------------------------------------------------
 * MODIFICATION NOTICE (required by the license above)
 * Modified 2026-11-23 by Peter Kokosharov <peter.kokosharov@gmail.com>:
 * extracted the INI file loader from Wine misc/profile.c (wine-950727),
 * removed the Win16 API glue, made the buffer handling bounds-safe, and added
 * '#' comments, whitespace trimming, skipping of bad lines, CRLF and UTF-8 BOM
 * handling, case-insensitive lookup and a small ini_load/ini_get/ini_free API.
 * This file was copied from Layka (same author).
 * Original work: Wine misc/profile.c (1995). The rest of Sisi is MIT licensed
 * (see LICENSE); this file is under the Wine license above.
 * ---------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ini.h"

#define STRSIZE 1024

enum { KeyDef, OnSecHeader, IgnoreToEOL, KeyValue };

typedef struct TKeys {
    char *KeyName;
    char *Value;
    struct TKeys *link;
} TKeys;

typedef struct TSecHeader {
    char *AppName;
    TKeys *Keys;
    struct TSecHeader *link;
} TSecHeader;

struct IniFile {
    TSecHeader *Sections;
};

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static int ci_equal(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return 0;
    }
    return *a == *b;
}

/* Trim leading and trailing blanks in place; returns the start. */
static char *trim(char *s)
{
    size_t n;
    while (*s == ' ' || *s == '\t') s++;
    n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
    return s;
}

IniFile *ini_load(const char *path)
{
    FILE *f;
    IniFile *ini;
    TSecHeader *cur = NULL;
    TKeys *curkey = NULL;
    char CharBuffer[STRSIZE];
    size_t len = 0;
    int state = KeyDef;
    int c;

    f = fopen(path, "r");
    if (!f) return NULL;
    ini = (IniFile *)malloc(sizeof(IniFile));
    if (!ini) { fclose(f); return NULL; }
    ini->Sections = NULL;

    /* skip a UTF-8 byte order mark */
    c = fgetc(f);
    if (c == 0xEF) {
        if (fgetc(f) != 0xBB || fgetc(f) != 0xBF) rewind(f);
    } else if (c != EOF) {
        rewind(f);
    }

    for (;;) {
        c = fgetc(f);
        if (c == '\r') continue;          /* ignore carriage return */
        if (c == EOF) {
            if (state == KeyValue && curkey) {
                CharBuffer[len] = '\0';
                free(curkey->Value);
                curkey->Value = dup_str(trim(CharBuffer));
            }
            break;
        }

        switch (state) {

        case OnSecHeader:
            if (c == ']') {
                TSecHeader *s;
                CharBuffer[len] = '\0';
                s = (TSecHeader *)malloc(sizeof(TSecHeader));
                if (s) {
                    s->AppName = dup_str(trim(CharBuffer));
                    s->Keys = NULL;
                    s->link = ini->Sections;
                    ini->Sections = s;
                    cur = s;
                }
                state = IgnoreToEOL;
            } else if (c == '\n' || len >= STRSIZE - 1) {
                cur = NULL;               /* bad header: skip its keys */
                state = (c == '\n') ? KeyDef : IgnoreToEOL;
                len = 0;
            } else
                CharBuffer[len++] = (char)c;
            break;

        case IgnoreToEOL:
            if (c == '\n') {
                state = KeyDef;
                len = 0;
            }
            break;

        case KeyDef:
            if (len == 0) {
                if (c == ' ' || c == '\t' || c == '\n') break;  /* leading blanks */
                if (c == '[') {
                    state = OnSecHeader;
                    break;
                }
                if (c == ';' || c == '#') {
                    state = IgnoreToEOL;
                    break;
                }
            }
            if (c == '\n') {              /* line without '=': bad, ignore */
                len = 0;
                break;
            }
            if (c == '=') {
                char *name;
                CharBuffer[len] = '\0';
                name = trim(CharBuffer);
                len = 0;
                if (!cur || !*name) {
                    state = IgnoreToEOL;
                    break;
                }
                curkey = (TKeys *)malloc(sizeof(TKeys));
                if (!curkey) { state = IgnoreToEOL; break; }
                curkey->KeyName = dup_str(name);
                curkey->Value = dup_str("");
                curkey->link = cur->Keys;
                cur->Keys = curkey;
                state = KeyValue;
                break;
            }
            if (len >= STRSIZE - 1) {     /* too long: abort the line */
                len = 0;
                state = IgnoreToEOL;
                break;
            }
            CharBuffer[len++] = (char)c;
            break;

        case KeyValue:
            if (c == '\n' || len >= STRSIZE - 1) {
                CharBuffer[len] = '\0';
                free(curkey->Value);
                curkey->Value = dup_str(trim(CharBuffer));
                state = (c == '\n') ? KeyDef : IgnoreToEOL;
                len = 0;
            } else
                CharBuffer[len++] = (char)c;
            break;
        }
    }
    fclose(f);
    return ini;
}

const char *ini_get(const IniFile *ini, const char *section, const char *key)
{
    const TSecHeader *s;
    const TKeys *k;
    if (!ini) return NULL;
    for (s = ini->Sections; s; s = s->link) {
        if (!s->AppName || !ci_equal(s->AppName, section)) continue;
        for (k = s->Keys; k; k = k->link)
            if (ci_equal(k->KeyName, key)) return k->Value;
    }
    return NULL;
}

void ini_free(IniFile *ini)
{
    TSecHeader *s, *sn;
    TKeys *k, *kn;
    if (!ini) return;
    for (s = ini->Sections; s; s = sn) {
        sn = s->link;
        for (k = s->Keys; k; k = kn) {
            kn = k->link;
            free(k->KeyName);
            free(k->Value);
            free(k);
        }
        free(s->AppName);
        free(s);
    }
    free(ini);
}
