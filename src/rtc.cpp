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

    rtc.c: FatFS support function

*/

#include <inttypes.h>
#include <avr/pgmspace.h>

#include "config.h"
#include "rtc.h"
#ifdef NEED_RTCMUX
#  include "ds1307-3231.h"
#  include "pcf8583.h"
#  include "softrtc.h"
#endif

#ifdef HAVE_RTC     // hide file from Arduino if not enabled

rtcstate_t rtc_state;

/* Default date/time if the RTC isn't present or not set: 2020-01-01 00:00:00 3rd day of week (Wed)*/
const PROGMEM struct tm rtc_default_date = {
  0, 0, 0, 1, 1-1, 120, 3
};

/* Return current time in a FAT-compatible format */
uint32_t get_fattime(void) {
  struct tm time;

  rtc_get(&time);
  return ((uint32_t)time.tm_year-80) << 25 |
    ((uint32_t)time.tm_mon+1) << 21 |
    ((uint32_t)time.tm_mday)  << 16 |
    ((uint32_t)time.tm_hour)  << 11 |
    ((uint32_t)time.tm_min)   << 5  |
    ((uint32_t)time.tm_sec)   >> 1;
}

/* Convert a one-byte BCD value to a normal integer */
uint8_t bcd2int(uint8_t value) {
  return (value & 0x0f) + 10*(value >> 4);
}

/* Convert a uint8_t into a BCD value */
uint8_t int2bcd(uint8_t value) {
  return (value % 10) + 16*(value/10);
}

rtcstate_t rtc_get_state(void) {
  return rtc_state;
}

// if there is only 1 RTC included in the source file list, it has a weak
// aliases to rtc_init()/get()/set(), so none of these are needed.
#ifdef NEED_RTCMUX
/* RTC "multiplexer" to select the best available RTC at runtime */

typedef enum { RTC_NONE, RTC_SOFTWARE, RTC_PCF8583,
               RTC_LPC17XX, RTC_DSRTC } rtc_model_t;

static rtc_model_t current_rtc = RTC_NONE;

rtc_type_t rtc_get_type(void) {
  switch(current_rtc) {
    case RTC_NONE:
      return RTC_TYPE_NONE;
    case RTC_SOFTWARE:
      return RTC_TYPE_SW;
    default:
      return RTC_TYPE_HW;
  }
}

void rtc_init(void) {
  #ifdef CONFIG_RTC_DSRTC
  dsrtc_init();
  if (rtc_state != RTC_NOT_FOUND) {
    current_rtc = RTC_DSRTC;
    return;
  }
  #endif

  #ifdef CONFIG_RTC_PCF8583
  pcf8583_init();
  if (rtc_state != RTC_NOT_FOUND) {
    current_rtc = RTC_PCF8583;
    return;
  }
  #endif

  #ifdef CONFIG_RTC_SOFTWARE
  softrtc_init();
  current_rtc = RTC_SOFTWARE;
  /* This is the fallback RTC that will always work */
  return;
  #endif

  /* none of the enabled RTCs were found */
  rtc_state = RTC_NOT_FOUND;
}

void rtc_get(struct tm *time) {
  switch (current_rtc) {

  #ifdef CONFIG_RTC_DSRTC
  case RTC_DSRTC:
    dsrtc_get(time);
    break;
  #endif

  #ifdef CONFIG_RTC_PCF8583
  case RTC_PCF8583:
    pcf8583_get(time);
    break;
  #endif

  #ifdef CONFIG_RTC_SOFTWARE
  case RTC_SOFTWARE:
    softrtc_get(time);
    break;
  #endif

  case RTC_NONE:
  default:
    return;
  }
}

void rtc_set(struct tm *time) {
  switch (current_rtc) {

  #ifdef CONFIG_RTC_DSRTC
  case RTC_DSRTC:
    dsrtc_set(time);
    break;
  #endif

  #ifdef CONFIG_RTC_PCF8583
  case RTC_PCF8583:
    pcf8583_set(time);
    break;
  #endif

  #ifdef CONFIG_RTC_SOFTWARE
  case RTC_SOFTWARE:
    softrtc_set(time);
    break;
  #endif

  case RTC_NONE:
  default:
    return;
  }
}

  #endif
#endif
