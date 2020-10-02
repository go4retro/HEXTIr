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

    drive.h: Drive device defines and prototypes
*/

#ifndef DRIVE_H
#define DRIVE_H

#include "config.h"
#include "diskio.h"
#include "hexops.h"
#include "ff.h"
#include "registry.h"

#define DEV_DRV_START   100           // Base disk device code we support
#define DEV_DRV_DEFAULT DEV_DRV_START
#define DEV_DRV_END     117           // Device codes 100-109 were originally for hexbus 5.25" disk drives.
                                      // Device codes 110-117 were later allocated for hexbu 3.5" disk drives.

typedef struct _file_t {
  FIL fp;
  DIR dir;
  uint8_t attr;
  uint16_t dirnum;
  char* pattern;
} file_t;


typedef struct _luntbl_t {
  uint8_t used;
  uint8_t lun;
  file_t  file;
} luntbl_t;


#ifdef INCLUDE_DRIVE
void drv_reset(void);
void drv_register(void);
void drv_init(void);
#else
#define drv_reset()     do {} while(0)
#define drv_register()  do {} while(0)
#define drv_init()      do {} while(0)
#endif

#endif /* DRIVE_H */
