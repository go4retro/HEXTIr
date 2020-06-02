/*
 * drive.h
 *
 *  Created on: May 31, 2020
 *      Author: brain
 */

#ifndef DRIVE_H
#define DRIVE_H

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

    drive.c: Drive-based HexBus functions.
*/
#include "config.h"
#include "hexops.h"
#include "ff.h"
#include "registry.h"

#ifndef ARDUINO

#include "diskio.h"

typedef struct _file_t {
  FIL fp;
  uint8_t attr;
} file_t;

#else

  #include <SPI.h>
  #include <SD.h>
  
typedef struct _file_t {
  File fp;
  uint8_t attr;
} file_t;

#endif // arduino

typedef struct _luntbl_t {
  uint8_t used;
  uint8_t lun;
  file_t  file;
} luntbl_t;


void drv_start(void);
void drv_reset(void);
void drv_register(void);
void drv_init(void);

#endif /* DRIVE_H */
