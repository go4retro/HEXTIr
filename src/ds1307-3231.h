/*
    HEXTIr-SD - Texas Instruments HEX-BUS SD Mass Storage Device
    Copyright Jim Brain and RETRO Innovations, 2017

    This code is a modification of the file from the following project:

    sd2iec - SD/MMC to Commodore serial bus interface/controller
    Copyright (C) 2007-2017  Ingo Korb <ingo@akana.de>

    Inspired by MMC2IEC by Lars Pontoppidan et al.

    FAT filesystem access based on code from ChaN and Jim Brain, see ff.c|h.

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

    ds1307-3231.h: RTC support for DS1307/DS3231 chips
*/

#ifndef DS1307_3231_H
#define DS1307_3231_H
#ifdef __cplusplus
extern "C"{
#endif

#include "rtc.h"

#ifdef CONFIG_RTC_DSRTC

void dsrtc_get(struct tm *time);
void dsrtc_set(struct tm *time);
rtc_type_t dsrtc_get_type(void);
void dsrtc_init(void);

#else

#  define dsrtc_get()             do {} while(0)
#  define dsrtc_set()             do {} while(0)
#  define dsrtc_get_type(void)    0
#  define dsrtc_init()            do {} while(0)

#endif

#ifdef __cplusplus
} // extern "C"
#endif
#endif
