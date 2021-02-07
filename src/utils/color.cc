/*
 *
 * Copyright (c) 2020 The Raptor Authors. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "raptor-lite/utils/color.h"

#include <stdint.h>
#include <stdio.h>

#ifdef _WIN32
#include <Windows.h>
#include <WinCon.h>

namespace {
enum ConsoleColor : uint16_t {
    fBlue      = FOREGROUND_BLUE,
    fGreen     = FOREGROUND_GREEN,
    fRed       = FOREGROUND_RED,
    fIntensity = FOREGROUND_INTENSITY,

    bBlue      = BACKGROUND_BLUE,       // 0x10
    bGreen     = BACKGROUND_GREEN,      // 0x20
    bRed       = BACKGROUND_RED,        // 0x40
    bIntensity = BACKGROUND_INTENSITY,  // 0x80

    // Foreground color
    fYellow  = fRed | fGreen,
    fCyan    = fGreen | fBlue,
    fMagenta = fBlue | fRed,
    fWhite   = fRed | fBlue | fGreen,
    fDefault = fWhite,

    // Background color
    bYellow  = bRed | bGreen,
    bCyan    = bGreen | bBlue,
    bMagenta = bBlue | bRed,
    bWhite   = bRed | bBlue | bGreen,
    bDefault = 0,  // 0 black

    defColor = fDefault | bDefault,
};

uint16_t ForegroundColorTable[] = {
    fWhite | fIntensity,   0,
    fRed | fIntensity,     fGreen | fIntensity,
    fYellow | fIntensity,  fBlue | fIntensity,
    fMagenta | fIntensity, fCyan | fIntensity,
    fWhite | fIntensity,
};

uint16_t BackgroundColorTable[] = {
    0,
    0,
    bRed | bIntensity,
    bGreen | bIntensity,
    bYellow | bIntensity,
    bBlue | bIntensity,
    bMagenta | bIntensity,
    bCyan | bIntensity,
    bWhite | bIntensity,
};

static int win32_reset_color = -1;
}  // namespace

namespace raptor {

WORD GetConsoleColor(HANDLE h) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    BOOL ret = GetConsoleScreenBufferInfo(h, &csbi);
    if (!ret) {
        return 0;
    }
    return csbi.wAttributes;
}

void SetConsoleColor(FILE *f, Color foreground, Color background) {
    HANDLE handle = NULL;
    if (f == stdout) {
        handle = GetStdHandle(STD_OUTPUT_HANDLE);
    } else {
        handle = GetStdHandle(STD_ERROR_HANDLE);
    }
    WORD attr = ForegroundColorTable[foreground] + BackgroundColorTable[background];
    if (win32_reset_color == -1) {
        win32_reset_color = GetConsoleColor(handle);
    }
    SetConsoleTextAttribute(handle, attr);
}

void ResetConsoleColor(FILE *) {
    if (win32_reset_color != -1) {
        HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
        SetConsoleTextAttribute(handle, static_cast<WORD>(win32_reset_color));
    }
}
}  // namespace raptor

#else
namespace {

const char *fBlack   = "\033[30m";
const char *fRed     = "\033[31m";
const char *fGreen   = "\033[32m";
const char *fYellow  = "\033[33m";
const char *fBlue    = "\033[34m";
const char *fMagenta = "\033[35m";
const char *fCyan    = "\033[36m";
const char *fWhite   = "\033[37m";
const char *fDefault = "\033[39m";  // default foreground color

const char *bBlack   = "\033[40m";
const char *bRed     = "\033[41m";
const char *bGreen   = "\033[42m";
const char *bYellow  = "\033[43m";
const char *bBlue    = "\033[44m";
const char *bMagenta = "\033[45m";
const char *bCyan    = "\033[46m";
const char *bWhite   = "\033[47m";
const char *bDefault = "\033[49m";  // default background color

const char *colorReset = "\033[0m";

const char *ForegroundColorTable[] = {
    fDefault, fBlack, fRed, fGreen, fYellow, fBlue, fMagenta, fCyan, fWhite,
};

const char *BackgroundColorTable[] = {
    bDefault, bBlack, bRed, bGreen, bYellow, bBlue, bMagenta, bCyan, bWhite,
};
}  // namespace

namespace raptor {
void SetConsoleColor(FILE *f, Color foreground, Color background) {
    fprintf(f, "%s%s", ForegroundColorTable[foreground], BackgroundColorTable[background]);
}

void ResetConsoleColor(FILE *f) {
    fprintf(f, colorReset);
}
}  // namespace raptor

#endif
