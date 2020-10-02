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

    rtc.h: Definitions for RTC support

    There is no rtc.c, the functions defined here are implemented by a
    device-specific .c file, e.g. pcf8583.c.
*/

#ifndef RTC_H
#define RTC_H
#ifdef HAVE_RTC

#include "time.h"

typedef enum {
  RTC_NOT_FOUND,  /* No RTC present                    */
  RTC_INVALID,    /* RTC present, but contents invalid */
  RTC_OK          /* RTC present and working           */
} rtcstate_t;

extern const /* PROGMEM */ struct tm rtc_default_date;
extern rtcstate_t rtc_state;

/* detect and initialize RTC */
void rtc_init(void);

rtcstate_t rtc_get_state(void);

/* Return current time in struct tm */
void rtc_get(struct tm *time);

/* Set time from struct tm */
void rtc_set(struct tm *time);

uint8_t bcd2int(uint8_t value);
uint8_t int2bcd(uint8_t value);

#else

#define rtc_init()      do {} while(0)
#define rtc_get_state() do {} while(0)
#define rtc_get(x)      do {} while(0)
#define rtc_set(x)      do {} while(0)
#define bcd2int(x)      do {} while(0)
#define int2bcd(x)      do {} while(0)

#endif
#endif
