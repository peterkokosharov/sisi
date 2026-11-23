/*
 * Public interface of src/ini.c (see the notices there and below).
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
 * This file was copied from Layka (same author); only the include guard was renamed.
 * Original work: Wine misc/profile.c (1995). The rest of Sisi is MIT licensed
 * (see LICENSE); this file is under the Wine license above.
 * ---------------------------------------------------------------------------
 */
/*
 * Tiny INI reader: [section], key = value, ';' and '#' comment lines.
 * ANSI C, callable from C++.
 */
#ifndef SISI_INI_H
#define SISI_INI_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct IniFile IniFile;

/* Returns NULL when the file cannot be opened. Bad lines are ignored. */
IniFile *ini_load(const char *path);

/* Case-insensitive lookup; NULL when absent. The pointer lives until ini_free. */
const char *ini_get(const IniFile *ini, const char *section, const char *key);

void ini_free(IniFile *ini);

#ifdef __cplusplus
}
#endif

#endif
