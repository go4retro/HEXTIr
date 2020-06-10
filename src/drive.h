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
#include "hexops.h"
#include "ff.h"
#include "registry.h"


#define F_ISDIRECTORY        1


#ifndef ARDUINO

#include "diskio.h"

typedef struct _file_t {
  FIL fp;
  uint8_t attr;
} file_t;

#else

  #include <SPI.h>
  #include <SD.h>

// FILE_WRITE is used for open-for-append (creating file if it doesn't exist).
// We'll define our own FILE_WRITE_NEW when the mode specified for open is just write, not append.
#define FILE_WRITE_NEW    (O_READ | O_WRITE | O_CREAT)


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
void drv_register(registry_t *registry);
void drv_init(void);

#endif /* DRIVE_H */
