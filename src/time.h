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

    time.h: Time structure definition
*/

#ifndef TIME_H
#define TIME_H

typedef uint32_t softtime_t;
struct tm {
  uint8_t tm_sec;  // 0..59
  uint8_t tm_min;  // 0..59
  uint8_t tm_hour; // 0..23
  uint8_t tm_mday; // 1..[28..31]
  uint8_t tm_mon;  // 0..11
  uint8_t tm_year; // since 1900, i.e. 2000 is 100
  uint8_t tm_wday; // 0 to 6, sunday is 0
  // A Unix struct tm has a few more fields we don't need in this application
};

#define __YEAR__  (\
                ((__DATE__)[7] - '0') * 1000 + \
                ((__DATE__)[8] - '0') * 100  + \
                ((__DATE__)[9] - '0') * 10   + \
                ((__DATE__)[10] - '0') * 1      \
              )
#define __MONTH__ (\
    __DATE__[2] == 'n' ? (__DATE__[1] == 'a' ? 0 : 5) \
    : __DATE__[2] == 'b' ? 1 \
    : __DATE__[2] == 'r' ? (__DATE__[0] == 'M' ? 2 : 3) \
    : __DATE__[2] == 'y' ? 4 \
    : __DATE__[2] == 'l' ? 6 \
    : __DATE__[2] == 'g' ? 7 \
    : __DATE__[2] == 'p' ? 8 \
    : __DATE__[2] == 't' ? 9 \
    : __DATE__[2] == 'v' ? 10 \
    : 11)

#define __DAY__  ((__DATE__[4]- '0') * 10 + (__DATE__[5]- '0'))

#define __HOUR__ (\
                 ((__TIME__)[0] - '0') * 10 + \
                 ((__TIME__)[1] - '0') * 1    \
                 )

#define __MIN__ (\
                ((__TIME__)[3] - '0') * 10 + \
                ((__TIME__)[4] - '0') * 1    \
                )

#define __SEC__ (\
                ((__TIME__)[6] - '0') * 10 + \
                ((__TIME__)[7] - '0') * 1    \
                )

#endif	/* TIME_H */
