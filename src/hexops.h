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

#define FILEATTR_READ     1
#define FILEATTR_WRITE    2
#define FILEATTR_PROTECT  4
#define FILEATTR_DISPLAY  8
#define FILEATTR_CATALOG  16
#define FILEATTR_RELATIVE 32

uint8_t hex_get_data(uint8_t buf[256], uint16_t len);
void hex_eat_it(uint16_t length, uint8_t status );
uint8_t hex_unsupported(pab_t pab);
uint8_t hex_null( __attribute__((unused)) pab_t pab );

#endif  // hexops_h
