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

    serial.h: Serial device defines and prototypes
*/

#ifndef SERIAL_H
#define SERIAL_H

#include "config.h"
#include "hexops.h"
#include "registry.h"

#define DEV_SER_START   20            // Device code for RS-232 Serial peripheral using serial (def=300 baud)
#define DEV_SER_DEFAULT DEV_SER_START
#define DEV_SER_END     23            // Serial peripherals were allowed from 20-23 for device codes.

#ifdef INCLUDE_SERIAL
void ser_reset(void);
void ser_register(void);
void ser_init(void);
#else
#define ser_reset()     do {} while(0)
#define ser_register()  do {} while(0)
#define ser_init()      do {} while(0)
#endif
#endif /* SERIAL_H */
