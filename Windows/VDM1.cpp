// -----------------------------------------------------------------------------
// Processor Technology VDM-1 emulation for Windows
// Copyright (C) 2018 David Hansel
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
// -----------------------------------------------------------------------------

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <ws2tcpip.h>
#include <wingdi.h>
#include <setupapi.h>
#include <Shlwapi.h>

#define REG_FOLDER    L"Software\\VDM1Display"

#define HPIX 576
#define VPIX 416

#define VDM_MEMBYTE   0x10
#define VDM_FULLFRAME 0x20
#define VDM_CTRL      0x30
#define VDM_DIP       0x40

#define VDM_CONNECT   0x10
#define VDM_KEY       0x30


// DIP switches (SW1-6 = bit 0-5):
// bit 0-1: off/off: all blank
//          off/on : normal video
//          on /off: inverse video
//          on /on : illegal
// bit 2-3: off/off: cursor characters (bit 7 on) shown regular
//          off/on : cursor characters blink at 2 Hz
//          on /off: cursor characters shown inverted
//          on /on : illegal
// bit 4-5: off/off: all characters blanked (cursor characters shown as blocks)
//          off/on : control characters (0-31) blanked, CR/VT blanking enabled
//          on /off: all characters shown, CR/VT blanking enabled
//          on /on : all characters shown, CR/VT blanking disabled
byte dip = 2+4+16;


// 4 upper bits define the first line shown, all lines above this are blanked
// 4 lower bits define the top line on the screen. e.g. if 3 then the order
//              of lines displayed on the screen is 3-15, 0, 1, 2
byte ctrl = 0, ctrl_prev = 0;


// video memory
byte mem[1024];

int    g_com_port = -1;
int    g_com_baud = 1050000;
HANDLE serial_conn = INVALID_HANDLE_VALUE;

wchar_t *peer = NULL;
SOCKET server_socket = INVALID_SOCKET;

HANDLE draw_mutex = INVALID_HANDLE_VALUE;

int colCR[16], colVT, rowVT;
int scaling = 1, border_left = 10, border_top = 10;
COLORREF bgColor, fgColor;
HBRUSH bgBrush, fgBrush;

bool blinkOn = false, goSend;
int delay_char = 0, delay_line = 0, delay_times[13] = {0, 1, 2, 5, 10, 20, 30, 40, 50, 75, 100, 200, 500};

HDC memDC;
HBITMAP charsNormal[128], charsInverse[128];


enum
  {
    ID_SOCKET = WM_USER,
    ID_COPY,
    ID_PASTE,
    ID_FULLSCREEN,
    ID_ABOUT,
    ID_EXIT,
    ID_SEND,
    ID_SEND_STOP,
    ID_BAUD_9600,
    ID_BAUD_38400,
    ID_BAUD_115200,
    ID_BAUD_250000,
    ID_BAUD_525000,
    ID_BAUD_750000,
    ID_BAUD_1050000,
    ID_DELAY_CHAR_0,
    ID_DELAY_CHAR_1,
    ID_DELAY_CHAR_2,
    ID_DELAY_CHAR_5,
    ID_DELAY_CHAR_10,
    ID_DELAY_CHAR_20,
    ID_DELAY_CHAR_30,
    ID_DELAY_CHAR_40,
    ID_DELAY_CHAR_50,
    ID_DELAY_CHAR_75,
    ID_DELAY_CHAR_100,
    ID_DELAY_CHAR_200,
    ID_DELAY_CHAR_500,
    ID_DELAY_LINE_0,
    ID_DELAY_LINE_1,
    ID_DELAY_LINE_2,
    ID_DELAY_LINE_5,
    ID_DELAY_LINE_10,
    ID_DELAY_LINE_20,
    ID_DELAY_LINE_30,
    ID_DELAY_LINE_40,
    ID_DELAY_LINE_50,
    ID_DELAY_LINE_75,
    ID_DELAY_LINE_100,
    ID_DELAY_LINE_200,
    ID_DELAY_LINE_500,
    ID_COLOR_FG,
    ID_COLOR_BG,
    ID_PORT_NONE, // must be before ID_PORT
    ID_PORT,      // must be last
  };


// MCM6475 ROM character set
const char charset_7bit[128][12] = 
 {{0x7f,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x7f,0x00,0x00,0x00},
  {0x7f,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x00,0x00,0x00},
  {0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x7f,0x00,0x00,0x00},
  {0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x7f,0x00,0x00,0x00},
  {0x20,0x10,0x08,0x04,0x3e,0x10,0x08,0x04,0x02,0x00,0x00,0x00},
  {0x7f,0x41,0x63,0x55,0x49,0x55,0x63,0x41,0x7f,0x00,0x00,0x00},
  {0x00,0x01,0x02,0x04,0x48,0x50,0x60,0x40,0x00,0x00,0x00,0x00},
  {0x1c,0x22,0x41,0x41,0x41,0x7f,0x14,0x14,0x77,0x00,0x00,0x00},
  {0x10,0x20,0x7c,0x22,0x11,0x01,0x01,0x01,0x01,0x00,0x00,0x00},
  {0x00,0x08,0x04,0x02,0x7f,0x02,0x04,0x08,0x00,0x00,0x00,0x00},
  {0x7f,0x00,0x00,0x00,0x7f,0x00,0x00,0x00,0x7f,0x00,0x00,0x00},
  {0x00,0x08,0x08,0x08,0x49,0x2a,0x1c,0x08,0x00,0x00,0x00,0x00},
  {0x08,0x08,0x2a,0x1c,0x08,0x49,0x2a,0x1c,0x08,0x00,0x00,0x00},
  {0x00,0x08,0x10,0x20,0x7f,0x20,0x10,0x08,0x00,0x00,0x00,0x00},
  {0x1c,0x22,0x63,0x55,0x49,0x55,0x63,0x22,0x1c,0x00,0x00,0x00},
  {0x1c,0x22,0x41,0x41,0x49,0x41,0x41,0x22,0x1c,0x00,0x00,0x00},
  {0x7f,0x41,0x41,0x41,0x7f,0x41,0x41,0x41,0x7f,0x00,0x00,0x00},
  {0x1c,0x2a,0x49,0x49,0x4f,0x41,0x41,0x22,0x1c,0x00,0x00,0x00},
  {0x1c,0x22,0x41,0x41,0x4f,0x49,0x49,0x2a,0x1c,0x00,0x00,0x00},
  {0x1c,0x22,0x41,0x41,0x79,0x49,0x49,0x2a,0x1c,0x00,0x00,0x00},
  {0x1c,0x2a,0x49,0x49,0x79,0x41,0x41,0x22,0x1c,0x00,0x00,0x00},
  {0x00,0x11,0x0a,0x04,0x4a,0x51,0x60,0x40,0x00,0x00,0x00,0x00},
  {0x3e,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x63,0x00,0x00,0x00},
  {0x01,0x01,0x01,0x01,0x7f,0x01,0x01,0x01,0x01,0x00,0x00,0x00},
  {0x7f,0x41,0x22,0x14,0x08,0x14,0x22,0x41,0x7f,0x00,0x00,0x00},
  {0x08,0x08,0x08,0x1c,0x1c,0x08,0x08,0x08,0x08,0x00,0x00,0x00},
  {0x3c,0x42,0x42,0x40,0x30,0x08,0x08,0x00,0x08,0x00,0x00,0x00},
  {0x1c,0x22,0x41,0x41,0x7f,0x41,0x41,0x22,0x1c,0x00,0x00,0x00},
  {0x7f,0x49,0x49,0x49,0x79,0x41,0x41,0x41,0x7f,0x00,0x00,0x00},
  {0x7f,0x41,0x41,0x41,0x79,0x49,0x49,0x49,0x7f,0x00,0x00,0x00},
  {0x7f,0x41,0x41,0x41,0x4f,0x49,0x49,0x49,0x7f,0x00,0x00,0x00},
  {0x7f,0x49,0x49,0x49,0x4f,0x41,0x41,0x41,0x7f,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x08,0x08,0x08,0x08,0x08,0x00,0x00,0x08,0x08,0x00,0x00,0x00},
  {0x24,0x24,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x14,0x14,0x14,0x7f,0x14,0x7f,0x14,0x14,0x14,0x00,0x00,0x00},
  {0x08,0x3f,0x48,0x48,0x3e,0x09,0x09,0x7e,0x08,0x00,0x00,0x00},
  {0x20,0x51,0x22,0x04,0x08,0x10,0x22,0x45,0x02,0x00,0x00,0x00},
  {0x38,0x44,0x44,0x28,0x10,0x29,0x46,0x46,0x39,0x00,0x00,0x00},
  {0x0c,0x0c,0x08,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x04,0x08,0x10,0x10,0x10,0x10,0x10,0x08,0x04,0x00,0x00,0x00},
  {0x10,0x08,0x04,0x04,0x04,0x04,0x04,0x08,0x10,0x00,0x00,0x00},
  {0x00,0x08,0x49,0x2a,0x1c,0x2a,0x49,0x08,0x00,0x00,0x00,0x00},
  {0x00,0x08,0x08,0x08,0x7f,0x08,0x08,0x08,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x10,0x20,0x00},
  {0x00,0x00,0x00,0x00,0x7f,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00},
  {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x00,0x00,0x00,0x00},
  {0x3e,0x41,0x43,0x45,0x49,0x51,0x61,0x41,0x3e,0x00,0x00,0x00},
  {0x08,0x18,0x28,0x08,0x08,0x08,0x08,0x08,0x3e,0x00,0x00,0x00},
  {0x3e,0x41,0x01,0x02,0x1c,0x20,0x40,0x40,0x7f,0x00,0x00,0x00},
  {0x3e,0x41,0x01,0x01,0x1e,0x01,0x01,0x41,0x3e,0x00,0x00,0x00},
  {0x02,0x06,0x0a,0x12,0x22,0x42,0x7f,0x02,0x02,0x00,0x00,0x00},
  {0x7f,0x40,0x40,0x7c,0x02,0x01,0x01,0x42,0x3c,0x00,0x00,0x00},
  {0x1e,0x20,0x40,0x40,0x7e,0x41,0x41,0x41,0x3e,0x00,0x00,0x00},
  {0x7f,0x41,0x02,0x04,0x08,0x10,0x10,0x10,0x10,0x00,0x00,0x00},
  {0x3e,0x41,0x41,0x41,0x3e,0x41,0x41,0x41,0x3e,0x00,0x00,0x00},
  {0x3e,0x41,0x41,0x41,0x3f,0x01,0x01,0x02,0x3c,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x10,0x20,0x00},
  {0x04,0x08,0x10,0x20,0x40,0x20,0x10,0x08,0x04,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x3e,0x00,0x3e,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x10,0x08,0x04,0x02,0x01,0x02,0x04,0x08,0x10,0x00,0x00,0x00},
  {0x1e,0x21,0x21,0x01,0x06,0x08,0x08,0x00,0x08,0x00,0x00,0x00},
  {0x1e,0x21,0x4d,0x55,0x55,0x5e,0x40,0x20,0x1e,0x00,0x00,0x00},
  {0x1c,0x22,0x41,0x41,0x41,0x7f,0x41,0x41,0x41,0x00,0x00,0x00},
  {0x7e,0x21,0x21,0x21,0x3e,0x21,0x21,0x21,0x7e,0x00,0x00,0x00},
  {0x1e,0x21,0x40,0x40,0x40,0x40,0x40,0x21,0x1e,0x00,0x00,0x00},
  {0x7c,0x22,0x21,0x21,0x21,0x21,0x21,0x22,0x7c,0x00,0x00,0x00},
  {0x7f,0x40,0x40,0x40,0x78,0x40,0x40,0x40,0x7f,0x00,0x00,0x00},
  {0x7f,0x40,0x40,0x40,0x78,0x40,0x40,0x40,0x40,0x00,0x00,0x00},
  {0x1e,0x21,0x40,0x40,0x40,0x4f,0x41,0x21,0x1e,0x00,0x00,0x00},
  {0x41,0x41,0x41,0x41,0x7f,0x41,0x41,0x41,0x41,0x00,0x00,0x00},
  {0x3e,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x3e,0x00,0x00,0x00},
  {0x1f,0x04,0x04,0x04,0x04,0x04,0x04,0x44,0x38,0x00,0x00,0x00},
  {0x41,0x42,0x44,0x48,0x50,0x68,0x44,0x42,0x41,0x00,0x00,0x00},
  {0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x7f,0x00,0x00,0x00},
  {0x41,0x63,0x55,0x49,0x49,0x41,0x41,0x41,0x41,0x00,0x00,0x00},
  {0x41,0x61,0x51,0x49,0x45,0x43,0x41,0x41,0x41,0x00,0x00,0x00},
  {0x1c,0x22,0x41,0x41,0x41,0x41,0x41,0x22,0x1c,0x00,0x00,0x00},
  {0x7e,0x41,0x41,0x41,0x7e,0x40,0x40,0x40,0x40,0x00,0x00,0x00},
  {0x1c,0x22,0x41,0x41,0x41,0x49,0x45,0x22,0x1d,0x00,0x00,0x00},
  {0x7e,0x41,0x41,0x41,0x7e,0x48,0x44,0x42,0x41,0x00,0x00,0x00},
  {0x3e,0x41,0x40,0x40,0x3e,0x01,0x01,0x41,0x3e,0x00,0x00,0x00},
  {0x7f,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x00,0x00,0x00},
  {0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x3e,0x00,0x00,0x00},
  {0x41,0x41,0x41,0x22,0x22,0x14,0x14,0x08,0x08,0x00,0x00,0x00},
  {0x41,0x41,0x41,0x41,0x49,0x49,0x55,0x63,0x41,0x00,0x00,0x00},
  {0x41,0x41,0x22,0x14,0x08,0x14,0x22,0x41,0x41,0x00,0x00,0x00},
  {0x41,0x41,0x22,0x14,0x08,0x08,0x08,0x08,0x08,0x00,0x00,0x00},
  {0x7f,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x7f,0x00,0x00,0x00},
  {0x3c,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x3c,0x00,0x00,0x00},
  {0x00,0x40,0x20,0x10,0x08,0x04,0x02,0x01,0x00,0x00,0x00,0x00},
  {0x3c,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x3c,0x00,0x00,0x00},
  {0x08,0x14,0x22,0x41,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7f,0x00,0x00,0x00},
  {0x18,0x18,0x08,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x3c,0x02,0x3e,0x42,0x42,0x3d,0x00,0x00,0x00},
  {0x40,0x40,0x40,0x5c,0x62,0x42,0x42,0x62,0x5c,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x3c,0x42,0x40,0x40,0x42,0x3c,0x00,0x00,0x00},
  {0x02,0x02,0x02,0x3a,0x46,0x42,0x42,0x46,0x3a,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x3c,0x42,0x7e,0x40,0x40,0x3c,0x00,0x00,0x00},
  {0x0c,0x12,0x10,0x10,0x7c,0x10,0x10,0x10,0x10,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x3a,0x46,0x42,0x46,0x3a,0x02,0x02,0x42,0x3c},
  {0x40,0x40,0x40,0x5c,0x62,0x42,0x42,0x42,0x42,0x00,0x00,0x00},
  {0x00,0x08,0x00,0x18,0x08,0x08,0x08,0x08,0x1c,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x06,0x02,0x02,0x02,0x02,0x02,0x02,0x22,0x1c},
  {0x40,0x40,0x40,0x44,0x48,0x50,0x68,0x44,0x42,0x00,0x00,0x00},
  {0x18,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x1c,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x76,0x49,0x49,0x49,0x49,0x49,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x5c,0x62,0x42,0x42,0x42,0x42,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x3c,0x42,0x42,0x42,0x42,0x3c,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x5c,0x62,0x42,0x42,0x62,0x5c,0x40,0x40,0x40},
  {0x00,0x00,0x00,0x3a,0x46,0x42,0x42,0x46,0x3a,0x02,0x02,0x02},
  {0x00,0x00,0x00,0x5c,0x62,0x40,0x40,0x40,0x40,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x3c,0x42,0x30,0x0c,0x42,0x3c,0x00,0x00,0x00},
  {0x00,0x10,0x10,0x7c,0x10,0x10,0x10,0x12,0x0c,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x42,0x42,0x42,0x42,0x46,0x3a,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x41,0x41,0x41,0x22,0x14,0x08,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x41,0x49,0x49,0x49,0x49,0x36,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x42,0x24,0x18,0x18,0x24,0x42,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x42,0x42,0x42,0x42,0x46,0x3a,0x02,0x42,0x3c},
  {0x00,0x00,0x00,0x7e,0x04,0x08,0x10,0x20,0x7e,0x00,0x00,0x00},
  {0x0e,0x10,0x10,0x10,0x20,0x10,0x10,0x10,0x0e,0x00,0x00,0x00},
  {0x08,0x08,0x08,0x00,0x00,0x08,0x08,0x08,0x00,0x00,0x00,0x00},
  {0x18,0x04,0x04,0x04,0x02,0x04,0x04,0x04,0x18,0x00,0x00,0x00},
  {0x30,0x49,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x24,0x49,0x12,0x24,0x49,0x12,0x24,0x49,0x12,0x00,0x00,0x00}};


static HBITMAP create_char_bitmap(HDC hdc, int ch, HBRUSH fg, HBRUSH bg)
{
  RECT rct;
  HBITMAP memBM = CreateCompatibleBitmap(hdc, scaling*9, 2*scaling*13);
  HGDIOBJ obj = SelectObject(memDC, memBM);

  rct.left   = 0;
  rct.right  = scaling * 9;
  rct.top    = 0;
  rct.bottom = 2 * scaling * 13;
  FillRect(memDC, &rct, bg);

  // NOTE: top row and two rightmost columns of all characters are blank
  for(int r=0; r<12; r++)
    for(int c=0; c<7; c++)
      {
        rct.left   = scaling * c;
        rct.right  = rct.left + scaling;
        rct.top    = 2*scaling * (r+1);
        rct.bottom = rct.top + 2*scaling;
        FillRect(memDC, &rct, (charset_7bit[ch][r] & (1<<(6-c))) ? fg : bg);
      }
  
  SelectObject(memDC, obj);
  return memBM;
}


static void create_char_bitmaps(HDC hdc)
{
  for(int ch=0; ch<128; ch++)
    {
      if( charsNormal[ch]!=NULL ) DeleteObject(charsNormal[ch]);
      charsNormal[ch]  = create_char_bitmap(hdc, ch, fgBrush, bgBrush);
      if( charsInverse[ch]!=NULL ) DeleteObject(charsInverse[ch]);
      charsInverse[ch] = create_char_bitmap(hdc, ch, bgBrush, fgBrush);
    }
}


static void draw_rect(HDC dc, int r, int c, int h, int w, HBRUSH color)
{
  RECT rct;
  rct.left   = border_left + scaling * c * 9;
  rct.right  = rct.left + scaling * w * 9;
  rct.top    = border_top + 2 * scaling * r * 13;
  rct.bottom = rct.top + 2*scaling * h * 13;
  FillRect(dc, &rct, color);
}


static void draw_char(HDC dc, int row, int col, byte ch)
{
  bool blank = false;
  bool inv = false;
  
  // DIP switch 5+6 (control character blanking)
  if( (dip & 0x30)==0x20 )
    blank = (ch&0x7f)<32;
  else if( (dip & 0x30)==0x00 )
    blank = true;

  // DIP switch 3+4 (cursor handling)
  if( ch & 0x80 )
    {
      if( dip & 0x04 )
        inv = !inv;
      else if( dip & 0x08 )
        inv = blinkOn;
    }

  // DIP switch 1 (whole screen inversion)
  if( dip & 1 ) inv = !inv;

  if( blank )
    draw_rect(dc, row, col, 1, 1, inv ? fgBrush: bgBrush);
  else
    {
      HGDIOBJ obj = SelectObject(memDC, inv ? charsInverse[ch&0x7f] : charsNormal[ch&0x7f]);
      BitBlt(dc, border_left + scaling * col * 9, border_top + 2 * scaling * row * 13, scaling*9, 2*scaling*13, memDC, 0, 0, SRCCOPY);
      SelectObject(memDC, obj);
    }
}


static void update_frame(HDC hdc)
{
  int firstDisplayed = (ctrl & 0xF0)/16;
  int firstLine      = ctrl & 0x0F;

  // whole screen blanked
  if( (dip & 3)==0 ) 
    {
      draw_rect(hdc, 0, 0, 16, 64, (dip&1) ? fgBrush : bgBrush);
      return;
    }

  // curtain blanking
  if( firstDisplayed>0 )
    draw_rect(hdc, 0, 0, firstDisplayed, 64, (dip&1) ? fgBrush : bgBrush);

  // reset VT blanking
  colVT=255; rowVT=255;

  int r, c;
  for(r=0; r<16 && rowVT==255; r++)
    {
      // compute row (with scrolling)
      int ra = ((r+(ctrl&15))*64) & 0x3ff;

      // reset CR blanking
      colCR[r] = 255;

      for(c=0; c<64; c++)
        {
          char ch = mem[ra+c];

          // the VT-CR logic must happen even within curtain-blanked screen,
          // so we can't just start the "r" loop above at firstDisplayed
          if( r>=firstDisplayed )
            draw_char(hdc, r, c, ch);
          
          // VT-CR blanking
          if( (dip & 0x30)!=0x30 )
            {
              if( (ch&0x7f)==11 )
                {
                  rowVT = r;
                  colVT = c++;
                  break;
                }
              else if( (ch&0x7f)==13 )
                {
                  colCR[r] = c++;
                  break;
                }
            }
        }
      
      if( c<64 )
        {
          // blank end of line
          draw_rect(hdc, r, c, 1, 64-c, (dip&1) ? fgBrush : bgBrush);
        }
    }

  if( r<16 )
    {
      // blank bottom of screen
      draw_rect(hdc, r, 0, 16-r, 64, (dip&1) ? fgBrush : bgBrush);
    }
}


static void update_byte(HDC dc, int a)
{
  int firstDisplayed = (ctrl & 0xF0)/16;
  int firstLine      = ctrl & 0x0F;

  // if whole screen is blanked then don't display
  if( (dip & 3)==0 ) return;

  byte ch = mem[a] & 0x7f;
  if( (ch==10 || ch==12) && (dip & 0x30)!=0x30 )
    {
      // if this is a VT or CR character then redraw whole screen
      update_frame(dc);
    }
  else
    {
      // compute row/col from memory address
      int row = (a & 0x03C0) >> 6;
      int col = a & 0x003F;

      // scrolling
      row -= firstLine;
      if( row<0 ) row += 16;

      // if within curtain blanking region then don't display
      if( row<firstDisplayed )
        return;

      // if we are changing a CR/VT character then redraw whole screen
      if( ((row==rowVT && col==colVT ) || col==colCR[row]) && (dip & 0x30)!=0x30 )
        { update_frame(dc); return; }

      // if within CR/VT blanking region then don't display
      if( (dip & 0x30)!=0x30 )
        if( row>rowVT || (row==rowVT && col>colVT) || col>colCR[row] )
          return;
      
      // draw character on screen
      draw_char(dc, row, col, mem[a]);
    }
}


static void update_frame(HWND hwnd)
{
  WaitForSingleObject(draw_mutex, INFINITE);
  HDC hdc = GetDC(hwnd);
  update_frame(hdc);
  ReleaseDC(hwnd, hdc);
  ReleaseMutex(draw_mutex);
}


void set_window_title(HWND hwnd)
{
  bool connected = (serial_conn!=INVALID_HANDLE_VALUE) || (server_socket!=INVALID_SOCKET);
  wchar_t buf[100];

  if( peer!=NULL )
    wsprintf(buf, L"VDM-1 Display (%s, %sconnected)",
             peer, connected ? L"" : L"not ");
  else if( g_com_port>0 )
    wsprintf(buf, L"VDM-1 Display (COM%i, %sconnected)",
             g_com_port, connected ? L"" : L"not ");
  else
    wsprintf(buf, L"VDM-1 Display");
  
  SetWindowText(hwnd, buf);
}


void receive(HWND hwnd, byte *data, int size)
{
  static int recv_status = 0, recv_bytes = 0, recv_ptr = 0;
  static byte buf[10];

  int i = 0;
  while( i<size )
    {
      if( recv_bytes>0 )
        {
          int n = recv_bytes > (size-i) ? (size-i) : recv_bytes;

          if( recv_status==VDM_FULLFRAME )
            memcpy(mem+recv_ptr, data+i, n);
          else
            memcpy(buf+recv_ptr, data+i, n);

          recv_bytes -= n;
          recv_ptr   += n;
          i          += n;

          if( recv_bytes == 0 )
            {
              switch( recv_status )
                {
                case VDM_FULLFRAME: 
                  update_frame(hwnd);
                  break;
                  
                case VDM_MEMBYTE:
                  {
                    int a = buf[0]*256+buf[1];
                    mem[a] = buf[2];

                    WaitForSingleObject(draw_mutex, INFINITE);
                    HDC hdc = GetDC(hwnd);
                    update_byte(hdc, a);
                    ReleaseDC(hwnd, hdc);
                    ReleaseMutex(draw_mutex);
                    break;
                  }
                  
                case VDM_CTRL:
                  ctrl = buf[0];
                  update_frame(hwnd);
                  set_window_title(hwnd);
                  break;

                case VDM_DIP:
                  dip = buf[0];
                  update_frame(hwnd);
                  set_window_title(hwnd);
                  break;
                }

              recv_status = 0;
            }
        }
      else
        {
          recv_status = data[i] & 0xf0;
          recv_bytes = 0;
          recv_ptr   = 0;

          switch( recv_status )
            {
            case VDM_FULLFRAME: 
              recv_bytes = 1024;
              break;
                  
            case VDM_MEMBYTE:
              recv_bytes = 2;
              buf[recv_ptr++] = data[i]&0x07;
              break;

            case VDM_CTRL:
            case VDM_DIP:
              recv_bytes = 1;
              break;

            default:
              recv_status = 0;
              break;
            }

          i++;
        }
    }
}


void send(HWND hwnd, byte *data, int size)
{
  DWORD n;

  if( serial_conn!=INVALID_HANDLE_VALUE )
    WriteFile(serial_conn, data, size, &n, NULL);
  if( server_socket!=INVALID_SOCKET )
    send(server_socket, (char *) data, size, 0);
}


struct send_text_info {
  byte *data;
  int   size;
  HWND  hwnd;
};


struct send_file_info {
  HANDLE f;
  HWND  hwnd;
};



DWORD WINAPI send_file_thread(void *data)
{
  DWORD n;
  struct send_file_info *info = (send_file_info *) data;

  byte buf[1024], cmd[2], prevC=0;
  cmd[0] = VDM_KEY;
  
  while( ReadFile(info->f, &buf, 1024, &n, NULL) && n>0 && goSend )
    for(DWORD i=0; i<n && goSend; i++)
      {
        cmd[1] = buf[i];
        send(info->hwnd, cmd, 2);
        if( delay_char>0 ) Sleep(delay_char);
        if( delay_line>0 && (buf[i]==13 || (buf[i]==10 && prevC!=13)) ) Sleep(delay_line);
        prevC = buf[i];
      }

  CloseHandle(info->f);
  free(info);
  return 0;
}


void send_file(HWND hwnd, LPWSTR fname)
{
  HANDLE f = CreateFile(fname, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if( f != INVALID_HANDLE_VALUE ) 
    {
      DWORD id;
      send_file_info *info = (send_file_info *) malloc(sizeof(send_file_info));
      info->f = f;
      info->hwnd = hwnd;
      
      goSend = true;
      HANDLE h = CreateThread(0, 0, send_file_thread, info, 0, &id);
      CloseHandle(h);
    }
}



DWORD WINAPI send_text_thread(void *data)
{
  struct send_text_info *info = (send_text_info *) data;

  byte buf[2];
  buf[0] = VDM_KEY;

  for(int i=0; i<info->size && goSend; i++)
    {
      buf[1] = info->data[i];
      send(info->hwnd, buf, 2);
      if( delay_char>0 ) Sleep(delay_char);
      if( delay_line>0 && buf[1]==13 ) Sleep(delay_line);
    }

  free(info->data);
  free(info);
  return 0;
}


void send_text(HWND hwnd, byte *data, int size)
{
  DWORD id;
  send_text_info *info = (send_text_info *) malloc(sizeof(send_text_info));
  info->data = (byte *) malloc(size);
  info->size = size;
  info->hwnd = hwnd;
  memcpy(info->data, data, size);

  goSend = true;
  HANDLE h = CreateThread(0, 0, send_text_thread, info, 0, &id);
  CloseHandle(h);
}



SOCKET connect_socket(HWND hwnd, const wchar_t *server)
{
  SOCKET ConnectSocket = INVALID_SOCKET;
  WSADATA wsaData;
  int iResult;
  ADDRINFOW *result = NULL, *ptr = NULL, hints;
    
  // Initialize Winsock
  iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
  if (iResult != 0) {
    return INVALID_SOCKET;
  }

  ZeroMemory( &hints, sizeof(hints) );
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  // Resolve the server address and port
  iResult = GetAddrInfo(server, L"8800", &hints, &result);
  if ( iResult != 0 ) {
    WSACleanup();
    return INVALID_SOCKET;
  }

  // Attempt to connect to an address until one succeeds
  for(ptr=result; ptr != NULL ;ptr=ptr->ai_next) 
    {
      // Create a SOCKET for connecting to server
      ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
      if (ConnectSocket == INVALID_SOCKET) {
        WSACleanup();
        return INVALID_SOCKET;
      }
        
      // Connect to server.
      iResult = connect( ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
      if (iResult == SOCKET_ERROR) 
        {
          closesocket(ConnectSocket);
          ConnectSocket = INVALID_SOCKET;
          continue;
        }
      break;
    }

  FreeAddrInfo(result);

  if (ConnectSocket == INVALID_SOCKET) 
    {
      WSACleanup();
      return INVALID_SOCKET;
    }
    
  return ConnectSocket;
}


void set_delay_menu(HMENU menuDelay, int menu_start, int menu_end, int setting)
{
  for(int i=0; i<menu_end-menu_start+1 && i<13; i++)
    if( delay_times[i]>=setting )
      {
        CheckMenuRadioItem(menuDelay, menu_start, menu_end, menu_start+i, MF_BYCOMMAND);
        break;
      }
}


bool write_setting_dword(PWSTR name, DWORD value)
{
  bool res = false;
  HKEY key;
  if( RegCreateKeyEx(HKEY_CURRENT_USER, REG_FOLDER, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &key, NULL) == ERROR_SUCCESS )
    {
      RegSetValueEx(key, name, 0, REG_DWORD, (const LPBYTE) &value, 4);
      RegCloseKey(key);
      res = true;
    }

  return res;
}


bool write_setting_string(PWSTR name, PWSTR value)
{
  bool res = false;
  HKEY key;
  if( RegCreateKeyEx(HKEY_CURRENT_USER, REG_FOLDER, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &key, NULL) == ERROR_SUCCESS )
    {
      RegSetValueEx(key, name, 0, REG_SZ, (const LPBYTE) value, (lstrlen(value)+1)*sizeof(WCHAR));
      RegCloseKey(key);
      res = true;
    }

  return res;
}


DWORD read_setting_dword(PWSTR name, DWORD def)
{
  bool ok = false;
  DWORD res = def;

  DWORD l = 4, tp;
  HKEY key;
  if( RegOpenKeyEx(HKEY_CURRENT_USER, REG_FOLDER, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS )
    {
      if( RegQueryValueEx(key, name, 0, &tp, (LPBYTE) &res, &l) == ERROR_SUCCESS && tp==REG_DWORD ) 
        ok = true;
      
      RegCloseKey(key);
    }
  
  return ok ? res : def;
}


PWSTR read_setting_string(PWSTR name, PWSTR def)
{
  PWSTR res = NULL;

  DWORD l, tp;
  HKEY key;
  if( RegOpenKeyEx(HKEY_CURRENT_USER, REG_FOLDER, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS )
    {
      if( RegQueryValueEx(key, name, 0, &tp, NULL, &l) == ERROR_SUCCESS && tp==REG_SZ )
        {
          res = (PWSTR) LocalAlloc(LPTR, (l+1) * sizeof(WCHAR));
          RegQueryValueEx(key, name, 0, &tp, (LPBYTE) res, &l);
        }
      
      RegCloseKey(key);
    }
  
  if( res==NULL ) res = StrDup(def);
  return res;
}


void set_baud_rate(HWND hwnd, int baud)
{
  int id;
  if( baud<=9600         ) id = ID_BAUD_9600;
  else if( baud<=38400   ) id = ID_BAUD_38400;
  else if( baud<=115200  ) id = ID_BAUD_115200;
  else if( baud<=250000  ) id = ID_BAUD_250000;
  else if( baud<=525000  ) id = ID_BAUD_525000;
  else if( baud<=750000  ) id = ID_BAUD_750000;
  else                     id = ID_BAUD_1050000;

  g_com_baud = baud;
  HMENU menuBaud = GetSubMenu(GetSubMenu(GetMenu(hwnd), 3), 1);
  CheckMenuRadioItem(menuBaud, ID_BAUD_9600, ID_BAUD_1050000, id, MF_BYCOMMAND);
  write_setting_dword(L"Baud", g_com_baud);
}


void set_com_port(HWND hwnd, int port)
{
  g_com_port = port;

  HMENU menuPort = GetSubMenu(GetSubMenu(GetMenu(hwnd), 3), 0);
  if( !CheckMenuRadioItem(menuPort, ID_PORT-1, ID_PORT+255, ID_PORT+g_com_port, MF_BYCOMMAND) )
    {
      // checking the item failed => no such port
      g_com_port = -1;
      CheckMenuRadioItem(menuPort, ID_PORT-1, ID_PORT+255, ID_PORT+g_com_port, MF_BYCOMMAND);
    }

  set_window_title(hwnd);
  if( g_com_port>0 ) write_setting_dword(L"Port", g_com_port);
}


void find_com_ports(HWND hwnd)
{
  static bool known_port[256], first_run = true;
  bool found_port[256];
  HKEY key;

  if( first_run ) memset(known_port, 0, sizeof(known_port));
  memset(found_port, 0, sizeof(found_port));

  if(RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_ENUMERATE_SUB_KEYS|KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
    {
      DWORD   dwIndex = 0, dwType, lpcName = 256, lpcValue = 256;
      wchar_t lpName[256], lpValue[256];

      while( RegEnumValue(key, dwIndex++, lpName, &lpcName, NULL, &dwType, (LPBYTE) lpValue, &lpcValue) == ERROR_SUCCESS )
        {
          lpValue[lpcValue]=0;
          if( wcsncmp(lpValue, L"COM", 3)==0 )
            {
              int i = _wtoi(lpValue+3);
              if( i<256 ) found_port[i] = true;
            }

          lpcName = 256;
          lpcValue = 256;
        }

      RegCloseKey(key);
    }

  int new_port = -1;
  HMENU menuPort = CreateMenu();
  AppendMenu(menuPort, MF_BYPOSITION | MF_STRING, ID_PORT_NONE, L"None");
  for(int i=0; i<256; i++)
    {
      if( found_port[i] )
        {
          // port was found in this check
          if( !known_port[i] )
            {
              // port did not exist before
              if( new_port==-1 )
                new_port = i;
              else if( new_port>0 )
                new_port = -2;
            }

          wchar_t comName[10];
          wsprintf(comName, L"COM%i", i);
          AppendMenu(menuPort, MF_BYPOSITION | MF_STRING, ID_PORT+i, comName);
        }

      known_port[i] = found_port[i];
    }
  
  HMENU menuSettings = GetSubMenu(GetMenu(hwnd), 3);
  ModifyMenu(menuSettings, 0, MF_BYPOSITION|MF_POPUP, (UINT_PTR) menuPort, L"&Port");
  if( !first_run && new_port>0 ) 
    set_com_port(hwnd, new_port);
  else
    CheckMenuRadioItem(menuPort, ID_PORT-1, ID_PORT+255, ID_PORT+g_com_port, MF_BYCOMMAND);

  first_run = false;
}


DWORD WINAPI serial_thread(void *data)
{
  const char *portName = (const char *) data;
  DWORD dwRead;
  HWND hwnd = (HWND) data;
  int current_port = -1, current_baud = -1;
 
  while( true )
    {
      if( (g_com_port!=current_port || g_com_baud!=current_baud) && serial_conn!=INVALID_HANDLE_VALUE )
        {
          // we are connected and the port has changed => disconnect
          CloseHandle(serial_conn);
          serial_conn=INVALID_HANDLE_VALUE;
          set_window_title(hwnd);
        }

      if( serial_conn==INVALID_HANDLE_VALUE )
        {
          find_com_ports(hwnd);

          if( g_com_port>0 )
            {
              wchar_t comName[100];
              wsprintf(comName, L"\\\\.\\COM%i", g_com_port);
              serial_conn = CreateFile(comName, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
              if( serial_conn!=INVALID_HANDLE_VALUE )
                {
                  DCB dcb;
                  FillMemory(&dcb, sizeof(dcb), 0);
                  if( GetCommState(serial_conn, &dcb) )
                    {
                      dcb.ByteSize = 8;
                      dcb.Parity   = NOPARITY;
                      dcb.StopBits = ONESTOPBIT;
                      dcb.BaudRate = g_com_baud;
                      SetCommState(serial_conn, &dcb);
                    }

                  COMMTIMEOUTS timeouts;
                  timeouts.ReadIntervalTimeout = 5; 
                  timeouts.ReadTotalTimeoutMultiplier = 0;
                  timeouts.ReadTotalTimeoutConstant = 5;
                  timeouts.WriteTotalTimeoutMultiplier = 0;
                  timeouts.WriteTotalTimeoutConstant = 0;
                  SetCommTimeouts(serial_conn, &timeouts);
                  set_window_title(hwnd);
                  current_port = g_com_port;
                  current_baud = g_com_baud;

                  Sleep(500);
                  byte b = VDM_CONNECT;
                  send(hwnd, &b, 1);
                }
              else
                Sleep(200);
            }
          else
            Sleep(500);
        }
      else
        {
          byte buf[100];
          if( ReadFile(serial_conn, buf, 100, &dwRead, NULL) )
            {
              if( dwRead>0 && hwnd!=NULL ) receive(hwnd, buf, dwRead);
            }
          else
            {
              CloseHandle(serial_conn);
              serial_conn=INVALID_HANDLE_VALUE;
              set_window_title(hwnd);
            }
        }
    }
}


void calc_pixel_scaling(HWND hwnd)
{
  RECT r;
  GetClientRect(hwnd, &r);
  int h = r.bottom-r.top, w = r.right-r.left;

  double scaleX = double(w)/HPIX;
  double scaleY = double(h)/VPIX;

  int newScaling = int(scaleX<scaleY ? scaleX : scaleY);
  if( newScaling<1 ) newScaling = 1;

  if( newScaling!=scaling )
    {
      scaling = newScaling;
      HDC hdc = GetDC(hwnd);
      create_char_bitmaps(hdc);
      ReleaseDC(hwnd, hdc);
    }

  border_left = (w-HPIX*scaling)/2;
  border_top  = (h-VPIX*scaling)/2;
}


void calc_window_size(long *w, long *h)
{
  RECT r;
  r.left = 0;
  r.right = *w;
  r.top = 0;
  r.bottom = *h;
  AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, true, 0);
  *w = r.right-r.left;
  *h = r.bottom-r.top;
}


void toggle_fullscreen(HWND hwnd)
{
  static WINDOWPLACEMENT s_wpPrev = { sizeof(s_wpPrev) };
  static HMENU s_menu = NULL;

  DWORD dwStyle = GetWindowLong(hwnd, GWL_STYLE);
  if( dwStyle & WS_OVERLAPPEDWINDOW )
    {
      MONITORINFO mi = { sizeof(mi) };

      s_menu = GetMenu(hwnd);
      SetMenu(hwnd, NULL);
      if( GetWindowPlacement(hwnd, &s_wpPrev) && GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi) )
        {
          SetWindowLong(hwnd, GWL_STYLE, dwStyle & ~WS_OVERLAPPEDWINDOW);
          SetWindowPos(hwnd, 
                       HWND_TOP,mi.rcMonitor.left, mi.rcMonitor.top,
                       mi.rcMonitor.right - mi.rcMonitor.left,
                       mi.rcMonitor.bottom - mi.rcMonitor.top,
                       SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
    } 
  else 
    {
      SetWindowLong(hwnd, GWL_STYLE,dwStyle | WS_OVERLAPPEDWINDOW);
      SetWindowPlacement(hwnd, &s_wpPrev);
      SetWindowPos(hwnd, NULL, 0, 0, 0, 0,SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
      SetMenu(hwnd, s_menu);
    }
}


LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
    {
    case WM_COMMAND: 
      {
        int id = LOWORD(wParam);

        // Test for the identifier of a command item. 
        switch( id )
          { 
          case ID_EXIT: 
            PostQuitMessage(0); 
            break;

          case ID_SEND:
            {
              OPENFILENAME ofn;
              wchar_t filename[MAX_PATH];
              ZeroMemory(&ofn, sizeof(ofn));
              ofn.lStructSize = sizeof (ofn);
              ofn.hwndOwner = hwnd;
              ofn.lpstrFile = filename;
              ofn.lpstrFile[0] = '\0';
              ofn.nMaxFile = MAX_PATH;
              ofn.lpstrFilter  = L"All\0*.*\0";
              ofn.nFilterIndex = 1;
              ofn.lpstrFileTitle = NULL;
              ofn.nMaxFileTitle = 0;
              ofn.lpstrInitialDir = read_setting_string(L"LastDir", L".");
              ofn.Flags = OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
              if( GetOpenFileName(&ofn) )
                {
                  send_file(hwnd, ofn.lpstrFile);
                  write_setting_string(L"LastDir", ofn.lpstrFile);
                }
              LocalFree((HLOCAL) ofn.lpstrInitialDir);
              break;
            }

          case ID_SEND_STOP:
            goSend = false;
            break;

          case ID_COPY:
            if( OpenClipboard(hwnd) )
              {
                EmptyClipboard(); 
                HGLOBAL hglbCopy = GlobalAlloc(GMEM_MOVEABLE, 16*66+1);
                if( hglbCopy ) 
                  {
                    LPSTR lpstrCopy = (LPSTR) GlobalLock(hglbCopy); 
                    int firstDisplayed = (ctrl & 0xF0)/16;
                    int firstLine      = ctrl & 0x0F;

                    for(int i=0; i<16; i++)
                      {
                        if( i >= firstDisplayed )
                          memcpy(lpstrCopy+i*66, mem+((i+firstLine)&0x0f)*64, 64);
                        else
                          memset(lpstrCopy+i*66, ' ', 64);
                        
                        lpstrCopy[i*66+64] = 13;
                        lpstrCopy[i*66+65] = 10;
                      }

                    lpstrCopy[16*66] = 0;
                    GlobalUnlock(hglbCopy); 
                    SetClipboardData(CF_TEXT, hglbCopy); 
                  }
                CloseClipboard();
              }
            break;

          case ID_PASTE:
            if( IsClipboardFormatAvailable(CF_TEXT) && OpenClipboard(hwnd) )
              {
                HGLOBAL hglb = GetClipboardData(CF_TEXT); 
                if(hglb != NULL)
                  { 
                    LPSTR lpstr = (LPSTR) GlobalLock(hglb);
                    if (lpstr != NULL) 
                      { 
                        send_text(hwnd, (byte *) lpstr, strlen(lpstr));
                        GlobalUnlock(hglb); 
                      }
                  }
                
                CloseClipboard();
              }
            break;

          case ID_FULLSCREEN: 
            toggle_fullscreen(hwnd); 
            break;

          case ID_BAUD_9600:    set_baud_rate(hwnd, 9600); break;
          case ID_BAUD_38400:   set_baud_rate(hwnd, 38400); break;
          case ID_BAUD_115200:  set_baud_rate(hwnd, 115200); break;
          case ID_BAUD_250000:  set_baud_rate(hwnd, 250000); break;
          case ID_BAUD_525000:  set_baud_rate(hwnd, 525000); break;
          case ID_BAUD_750000:  set_baud_rate(hwnd, 750000); break;
          case ID_BAUD_1050000: set_baud_rate(hwnd, 1050000); break;

          case ID_DELAY_CHAR_0: 
          case ID_DELAY_CHAR_1:
          case ID_DELAY_CHAR_2:
          case ID_DELAY_CHAR_5:
          case ID_DELAY_CHAR_10:
          case ID_DELAY_CHAR_20:
          case ID_DELAY_CHAR_30:
          case ID_DELAY_CHAR_40:
          case ID_DELAY_CHAR_50:
          case ID_DELAY_CHAR_75:
          case ID_DELAY_CHAR_100:
          case ID_DELAY_CHAR_200:
          case ID_DELAY_CHAR_500:
            {
              delay_char = delay_times[id-ID_DELAY_CHAR_0];
              HMENU menuDelay = GetSubMenu(GetSubMenu(GetMenu(hwnd), 3), 2);
              CheckMenuRadioItem(menuDelay, ID_DELAY_CHAR_0, ID_DELAY_CHAR_500, id, MF_BYCOMMAND);
              write_setting_dword(L"DelayChar", delay_char);
              break;
            }

          case ID_DELAY_LINE_0: 
          case ID_DELAY_LINE_1:
          case ID_DELAY_LINE_2:
          case ID_DELAY_LINE_5:
          case ID_DELAY_LINE_10:
          case ID_DELAY_LINE_20:
          case ID_DELAY_LINE_30:
          case ID_DELAY_LINE_40:
          case ID_DELAY_LINE_50:
          case ID_DELAY_LINE_75:
          case ID_DELAY_LINE_100:
          case ID_DELAY_LINE_200:
          case ID_DELAY_LINE_500:
            {
              delay_line = delay_times[id-ID_DELAY_LINE_0];
              HMENU menuDelay = GetSubMenu(GetSubMenu(GetMenu(hwnd), 3), 3);
              CheckMenuRadioItem(menuDelay, ID_DELAY_LINE_0, ID_DELAY_LINE_500, id, MF_BYCOMMAND);
              write_setting_dword(L"DelayLine", delay_line);
              break;
            }

          case ID_COLOR_BG:
          case ID_COLOR_FG:
            {
              CHOOSECOLOR cc;
              static COLORREF acrCustClr[16];
              ZeroMemory(&cc, sizeof(cc));
              cc.lStructSize = sizeof(cc);
              cc.hwndOwner = hwnd;
              cc.lpCustColors = (LPDWORD) acrCustClr;
              cc.rgbResult = id==ID_COLOR_FG ? fgColor : bgColor;
              cc.Flags = CC_FULLOPEN | CC_RGBINIT;
              
              if( ChooseColor(&cc)==TRUE )
                {
                  WaitForSingleObject(draw_mutex, INFINITE);
                  HDC hdc = GetDC(hwnd);

                  if( id==ID_COLOR_FG )
                    {
                      fgColor = cc.rgbResult;
                      DeleteObject(fgBrush);
                      fgBrush = CreateSolidBrush(fgColor);
                      write_setting_dword(L"ForegroundColor", fgColor);
                    }
                  else
                    {
                      RECT r;
                      bgColor = cc.rgbResult;
                      DeleteObject(bgBrush);
                      bgBrush = CreateSolidBrush(bgColor);
                      GetClientRect(hwnd, &r);
                      FillRect(hdc, &r, bgBrush);
                      write_setting_dword(L"BackgroundColor", bgColor);
                    }

                  create_char_bitmaps(hdc);
                  update_frame(hdc);
                  ReleaseDC(hwnd, hdc);
                  ReleaseMutex(draw_mutex);
                }
              break;
            }

          case ID_ABOUT:
            MessageBox(hwnd, 
                       L"Processor Technology VDM-1 Display application for\nArduino Altair 88000 simulator\n\n"
                       L"https://www.hackster.io/david-hansel/arduino-altair-8800-simulator-3594a6\n\n"
                       L"(C) 2018 David Hansel", 
                       L"About", MB_OK | MB_ICONINFORMATION);
            break;

          default:
            {
              if( id>=ID_PORT_NONE && id<ID_PORT+256 )
                set_com_port(hwnd, id-ID_PORT);
              break;
            }
          }
        break;
      }

    case WM_SIZE:
      {
        RECT r;
        HDC hdc;

        WaitForSingleObject(draw_mutex, INFINITE);
        hdc = GetDC(hwnd);
        GetClientRect(hwnd, &r);
        FillRect(hdc, &r, bgBrush);
        ReleaseDC(hwnd, hdc);
        calc_pixel_scaling(hwnd);
        ReleaseMutex(draw_mutex);

        update_frame(hwnd);
        break;
      }

    case WM_SYSCHAR:
      if( wParam==6 || (wParam==27 && (GetWindowLong(hwnd, GWL_STYLE)&WS_OVERLAPPEDWINDOW)==0) ) 
      return 1;
      break;
      
    case WM_CHAR: 
      {
        byte msg[2];
        msg[0] = VDM_KEY;
        msg[1] = wParam;
        send(hwnd, msg, 2);
        break;
      }
      
    case WM_LBUTTONDBLCLK:
      {
        toggle_fullscreen(hwnd);
        break;
      }

    case WM_PAINT:
      {
        WaitForSingleObject(draw_mutex, INFINITE);
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        ReleaseMutex(draw_mutex);
        break;
      }

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    case WM_GETMINMAXINFO:
      {
        LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
        lpMMI->ptMinTrackSize.x = 128;
        lpMMI->ptMinTrackSize.y = 128+20;
        calc_window_size(&lpMMI->ptMinTrackSize.x, &lpMMI->ptMinTrackSize.y);
        break;
      }

    case ID_SOCKET:
      {
        byte data[2500];
        int size = recv(server_socket, (char *) data, 2500, 0);
        if( size>0 ) 
          {
            static bool skip_greeting = true;
            if( skip_greeting )
              {
                // when connecting, PC host sends a greeting message saying
                // "[connected as nth client on port 8800]"
                // we need to ignore that
                int i;
                for(i=0; i<size && skip_greeting; i++) 
                  if( data[i]=='\n' )
                    skip_greeting = false; 
                
                if( i<size ) receive(hwnd, data+i, size-i);
              }
            else
              receive(hwnd, data, size);
          }
        else
          {
            server_socket = INVALID_SOCKET;
            set_window_title(hwnd);
          }
        break;
      }

    case WM_TIMER:
      blinkOn = !blinkOn;
      if( (dip & 0x0C)==0x08 ) update_frame(hwnd);
      break;
      
    default:
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    
    return 0;
}


int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
{
    // Register the window class.
    const wchar_t CLASS_NAME[]  = L"VDM1 Window Class";

    draw_mutex = CreateMutex(NULL, FALSE, NULL);
    
    colVT=255; rowVT=255;
    for(int i=0; i<16; i++) colCR[i]=255;
    for(int i=0; i<128; i++) { charsNormal[i] = charsInverse[i] = NULL; }

    WNDCLASS wc = { };

    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.style         = CS_DBLCLKS;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);

    RegisterClass(&wc);

    // Create the window.
    long w = border_left*2 + scaling * HPIX, h = border_top*2 + scaling * VPIX;
    calc_window_size(&w, &h);
    HWND hwnd = CreateWindowEx(0, CLASS_NAME, L"VDM-1 Display", WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, w, h,
                               NULL, NULL, hInstance, NULL);
    
    if( hwnd == NULL )
      return 0;

    HMENU menu = CreateMenu();
    HMENU menuFile = CreateMenu();
    AppendMenu(menuFile, MF_BYPOSITION | MF_STRING, ID_SEND, L"&Send File...");
    AppendMenu(menuFile, MF_BYPOSITION | MF_STRING, ID_SEND_STOP, L"S&top sending");
    AppendMenu(menuFile, MF_BYPOSITION | MF_STRING, ID_EXIT, L"E&xit");
    HMENU menuEdit = CreateMenu();
    AppendMenu(menuEdit, MF_BYPOSITION | MF_STRING, ID_COPY,  L"&Copy\tCtrl+Alt+C");
    AppendMenu(menuEdit, MF_BYPOSITION | MF_STRING, ID_PASTE, L"&Paste\tCtrl+Alt+V");
    HMENU menuView = CreateMenu();
    AppendMenu(menuView, MF_BYPOSITION | MF_STRING, ID_FULLSCREEN, L"&Full Screen\tCtrl+Alt+F");
    HMENU menuPort = CreateMenu();
    AppendMenu(menuPort, MF_BYPOSITION | MF_STRING, ID_PORT_NONE, L"None");
    HMENU menuBaud = CreateMenu();
    AppendMenu(menuBaud, MF_BYPOSITION | MF_STRING, ID_BAUD_9600, L"9600");
    AppendMenu(menuBaud, MF_BYPOSITION | MF_STRING, ID_BAUD_38400, L"38400");
    AppendMenu(menuBaud, MF_BYPOSITION | MF_STRING, ID_BAUD_115200, L"115200");
    AppendMenu(menuBaud, MF_BYPOSITION | MF_STRING, ID_BAUD_250000, L"250000");
    AppendMenu(menuBaud, MF_BYPOSITION | MF_STRING, ID_BAUD_525000, L"525000");
    AppendMenu(menuBaud, MF_BYPOSITION | MF_STRING, ID_BAUD_750000, L"750000");
    AppendMenu(menuBaud, MF_BYPOSITION | MF_STRING, ID_BAUD_1050000, L"1050000");
    HMENU menuDelayChar = CreateMenu();
    AppendMenu(menuDelayChar, MF_BYPOSITION | MF_STRING, ID_DELAY_CHAR_0, L"none");
    AppendMenu(menuDelayChar, MF_BYPOSITION | MF_STRING, ID_DELAY_CHAR_1, L"1 ms");
    AppendMenu(menuDelayChar, MF_BYPOSITION | MF_STRING, ID_DELAY_CHAR_2, L"2 ms");
    AppendMenu(menuDelayChar, MF_BYPOSITION | MF_STRING, ID_DELAY_CHAR_5, L"5 ms");
    AppendMenu(menuDelayChar, MF_BYPOSITION | MF_STRING, ID_DELAY_CHAR_10, L"10 ms");
    AppendMenu(menuDelayChar, MF_BYPOSITION | MF_STRING, ID_DELAY_CHAR_20, L"20 ms");
    AppendMenu(menuDelayChar, MF_BYPOSITION | MF_STRING, ID_DELAY_CHAR_30, L"30 ms");
    AppendMenu(menuDelayChar, MF_BYPOSITION | MF_STRING, ID_DELAY_CHAR_40, L"40 ms");
    AppendMenu(menuDelayChar, MF_BYPOSITION | MF_STRING, ID_DELAY_CHAR_50, L"50 ms");
    AppendMenu(menuDelayChar, MF_BYPOSITION | MF_STRING, ID_DELAY_CHAR_75, L"75 ms");
    AppendMenu(menuDelayChar, MF_BYPOSITION | MF_STRING, ID_DELAY_CHAR_100, L"100 ms");
    AppendMenu(menuDelayChar, MF_BYPOSITION | MF_STRING, ID_DELAY_CHAR_200, L"200 ms");
    AppendMenu(menuDelayChar, MF_BYPOSITION | MF_STRING, ID_DELAY_CHAR_500, L"500 ms");
    HMENU menuDelayLine = CreateMenu();
    AppendMenu(menuDelayLine, MF_BYPOSITION | MF_STRING, ID_DELAY_LINE_0, L"none");
    AppendMenu(menuDelayLine, MF_BYPOSITION | MF_STRING, ID_DELAY_LINE_1, L"1 ms");
    AppendMenu(menuDelayLine, MF_BYPOSITION | MF_STRING, ID_DELAY_LINE_2, L"2 ms");
    AppendMenu(menuDelayLine, MF_BYPOSITION | MF_STRING, ID_DELAY_LINE_5, L"5 ms");
    AppendMenu(menuDelayLine, MF_BYPOSITION | MF_STRING, ID_DELAY_LINE_10, L"10 ms");
    AppendMenu(menuDelayLine, MF_BYPOSITION | MF_STRING, ID_DELAY_LINE_20, L"20 ms");
    AppendMenu(menuDelayLine, MF_BYPOSITION | MF_STRING, ID_DELAY_LINE_30, L"30 ms");
    AppendMenu(menuDelayLine, MF_BYPOSITION | MF_STRING, ID_DELAY_LINE_40, L"40 ms");
    AppendMenu(menuDelayLine, MF_BYPOSITION | MF_STRING, ID_DELAY_LINE_50, L"50 ms");
    AppendMenu(menuDelayLine, MF_BYPOSITION | MF_STRING, ID_DELAY_LINE_75, L"75 ms");
    AppendMenu(menuDelayLine, MF_BYPOSITION | MF_STRING, ID_DELAY_LINE_100, L"100 ms");
    AppendMenu(menuDelayLine, MF_BYPOSITION | MF_STRING, ID_DELAY_LINE_200, L"200 ms");
    AppendMenu(menuDelayLine, MF_BYPOSITION | MF_STRING, ID_DELAY_LINE_500, L"500 ms");
    
    HMENU menuSettings = CreateMenu();
    AppendMenu(menuSettings, MF_POPUP, (UINT_PTR) menuPort, L"&Port");
    AppendMenu(menuSettings, MF_POPUP, (UINT_PTR) menuBaud, L"&Baud Rate");
    AppendMenu(menuSettings, MF_POPUP, (UINT_PTR) menuDelayChar, L"&Character Delay");
    AppendMenu(menuSettings, MF_POPUP, (UINT_PTR) menuDelayLine, L"&Line Delay");
    AppendMenu(menuSettings, MF_BYPOSITION | MF_STRING, ID_COLOR_FG, L"Foreground Color...");
    AppendMenu(menuSettings, MF_BYPOSITION | MF_STRING, ID_COLOR_BG, L"Background Color...");

    HMENU menuHelp = CreateMenu();
    AppendMenu(menuHelp, MF_BYPOSITION | MF_STRING, ID_ABOUT, L"&About");

    AppendMenu(menu, MF_POPUP, (UINT_PTR) menuFile, L"&File");
    AppendMenu(menu, MF_POPUP, (UINT_PTR) menuEdit, L"&Edit");
    AppendMenu(menu, MF_POPUP, (UINT_PTR) menuView, L"&View");
    AppendMenu(menu, MF_POPUP, (UINT_PTR) menuSettings, L"&Settings");
    AppendMenu(menu, MF_POPUP, (UINT_PTR) menuHelp, L"&Help");
    SetMenu(hwnd, menu);

    delay_char = read_setting_dword(L"DelayChar", delay_char);
    delay_line = read_setting_dword(L"DelayLine", delay_line);
    set_delay_menu(menuDelayChar, ID_DELAY_CHAR_0, ID_DELAY_CHAR_500, delay_char);
    set_delay_menu(menuDelayLine, ID_DELAY_LINE_0, ID_DELAY_LINE_500, delay_line);

    int p=-1, baud=1050000;
    find_com_ports(hwnd);     
    if( wcsncmp(pCmdLine, L"COM", 3)==0 && wcslen(pCmdLine)<7 )
      p = _wtoi(pCmdLine+3);
    else 
      p = read_setting_dword(L"Port", g_com_port);

    if( p>0 && p<256 ) set_com_port(hwnd, p);
    set_baud_rate(hwnd, read_setting_dword(L"Baud", g_com_baud));

    if( g_com_port>0 || wcslen(pCmdLine)==0 )
      {
        DWORD id; 
        HANDLE h = CreateThread(0, 0, serial_thread, hwnd, 0, &id);
        CloseHandle(h);
      }
    else
      {
        peer = pCmdLine;
        server_socket = connect_socket(hwnd, peer);
        if( server_socket==INVALID_SOCKET )
          return 0;
        else if( WSAAsyncSelect(server_socket, hwnd, ID_SOCKET, FD_READ)!=0 )
          return 0;

        byte b = VDM_CONNECT;
        send(hwnd, &b, 1);
        set_window_title(hwnd);
        RemoveMenu(menu, MF_BYPOSITION, 2);
      }

    // determine colors
    fgColor = read_setting_dword(L"ForegroundColor", RGB(0, 255, 0));
    bgColor = read_setting_dword(L"BackgroundColor", RGB(0, 0, 0));
    fgBrush = CreateSolidBrush(fgColor);
    bgBrush = CreateSolidBrush(bgColor);

    // create character bitmaps
    HDC hdc = GetDC(hwnd);
    memDC = CreateCompatibleDC(hdc);
    create_char_bitmaps(hdc);
    ReleaseDC(hwnd, hdc);

    set_window_title(hwnd);
    ShowWindow(hwnd, SW_SHOW);

    //for(int i=0; i<1024; i++) mem[i] = ~(i & 0xff);
    //mem[0x00] = mem[0x74] = mem[0xF2] = 32;
    //for(int i=0; i<16; i++) mem[i*64] = 65+i;
	
    for(int i=0; i<1024; i++) mem[i] = ' ';
	update_frame(hwnd);
    
    // start "blink" timer
    SetTimer(hwnd, -1, 500, NULL);

    // create accelerator table
    struct tagACCEL accel[5] = 
      { { FVIRTKEY|FCONTROL|FALT, 0x46, ID_FULLSCREEN },
        { FVIRTKEY|FCONTROL|FALT, 0x43, ID_COPY },
        { FVIRTKEY|FCONTROL     , 0x2D, ID_COPY },
        { FVIRTKEY|FCONTROL|FALT, 0x56, ID_PASTE },
        { FVIRTKEY|FSHIFT,        0x2D, ID_PASTE } };

    HACCEL hAccel = CreateAcceleratorTable(accel, sizeof(accel)/sizeof(struct tagACCEL));
    
    // Run the message loop.
    MSG msg = { };
    while( GetMessage(&msg, NULL, 0, 0) )
      {
        if( !TranslateAccelerator(hwnd, hAccel, &msg) )
          {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
          }
      }

    if( server_socket!=INVALID_SOCKET )
      {
        shutdown(server_socket, SD_SEND);
        closesocket(server_socket);
      }

    return 0;
}

#endif
