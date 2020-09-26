/*
    HEXTIr-SD - Texas Instruments HEX-BUS SD Mass Storage Device
    Copyright Jim Brain and RETRO Innovations, 2017

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License only.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    hexops.h: Foundational HexBus function defines and prototypes
*/

#ifndef HEXOPS_H
#define HEXOPS_H

#define LUN_CMD       255

// add 1 to buffer size to handle null termination if used as a string
extern uint8_t buffer[BUFSIZE + 1];


#define upper(x) ((x >= 'a') && (x <= 'z') ? x - ('a' - 'A') : x)
#define lower(x) ((x >= 'A') && (x <= 'Z') ? x + ('a' - 'A') : x)

typedef struct _action_t {
  uint8_t action;
  char text[8];
} action_t;



/*
    PAB (Peripheral Access Block) data structure
*/
typedef struct _pab_t {
  uint8_t dev;
  uint8_t cmd;
  uint8_t lun;
  uint16_t record;
  uint16_t buflen;
  uint16_t datalen;
} pab_t;

typedef struct _pab_raw_t {
  union {
    pab_t pab;
    uint8_t raw[9];
  };
} pab_raw_t;

#define FILEATTR_READ      1
#define FILEATTR_WRITE     2
#define FILEATTR_PROTECT   4
#define FILEATTR_DISPLAY   8
#define FILEATTR_CATALOG  16
#define FILEATTR_RELATIVE 32
#define FILEATTR_COMMAND  64

hexstatus_t hex_get_data(uint8_t buf[256], uint16_t len);
void hex_eat_it(uint16_t length, hexstatus_t rc);
void hex_unsupported(pab_t *pab);
void hex_null(pab_t *pab __attribute__((unused)));
//void trim(uint8_t **buf, uint8_t *blen);
#ifdef USE_OPEN_HELPER
hexstatus_t hex_open_helper(pab_t *pab, hexstatus_t err, uint16_t *len, uint8_t *att);
#endif


#endif  // hexops_h
