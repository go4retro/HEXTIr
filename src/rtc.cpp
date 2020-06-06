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

    rtc.cpp: DS3231 Clock device functions.
*/

#include "config.h"
#include "hexbus.h"
#include "hexops.h"

#include "rtc.h"

#ifdef INCLUDE_CLOCK

#include <DS3231.h>
#include <Wire.h>

// Global references
extern uint8_t buffer[BUFSIZE];
// Global defines
DS3231            clock_peripheral;       // Our CLOCK : access via the HexBus at device code 233 (E9h = ascii R+T+C)
volatile uint8_t  rtc_open = 0;
#endif


#ifdef INCLUDE_CLOCK
/*
   Open access to RTC module. Begin Wire, Begin DS3231  
   WORK IN MAJOR PROGRESS.
   These routines will currently flag an unused parameter 'pab' warning...
*/
static uint8_t hex_rtc_open(pab_t pab) {
  if ( !rtc_open ) {
    Wire.begin();
    clock_peripheral.setClockMode(false); // 24h
    rtc_open = 1;
  }
  return HEXERR_SUCCESS;
}

/*
   Close access to RTC module. Shut down Wire.
*/
static uint8_t hex_rtc_close(pab_t pab) {
  rtc_reset();
  return HEXERR_SUCCESS;
}

/*
   Return time in format YYMMDD_HHMMSS in 24h form.
   When RTC opened in INPUT or UPDATE mode.
*/
static uint8_t hex_rtc_read(pab_t pab) {
  if ( rtc_open )
  {

  }
  return HEXERR_SUCCESS;
}

/*
   Set time when we receive time in format YYMMDD_HHMMSS (13 bytes of data).
   When RTC opened in OUTPUT or UPDATE mode.
*/
static uint8_t hex_rtc_write(pab_t pab) {
  if ( rtc_open ) {

  }
  return HEXERR_SUCCESS;
}

static uint8_t hex_rtc_reset(pab_t pab) {

  rtc_reset();
  // release the bus ignoring any further action on bus. no response sent.
  hex_finish();
  // wait here while bav is low
  while ( !hex_is_bav() ) {
    ;
  }
  return HEXERR_SUCCESS;
}

#endif // include_clock


void rtc_init() {
  return;
}


void rtc_reset() {
#ifdef INCLUDE_CLOCK
  if ( rtc_open ) {
    Wire.end();
    rtc_open = 0;
  }
#endif
  return;
}

#ifdef INCLUDE_CLOCK
/*
 * Command handling registry for device
 */

static const cmd_proc fn_table[] PROGMEM = {
  hex_rtc_open,
  hex_rtc_close,
  hex_rtc_read,
  hex_rtc_write,
  hex_rtc_reset,
  NULL // end of table.
};

static const uint8_t op_table[] PROGMEM = {
  HEXCMD_OPEN,
  HEXCMD_CLOSE,
  HEXCMD_READ,
  HEXCMD_WRITE,
  HEXCMD_RESET_BUS,
  HEXCMD_INVALID_MARKER
};
#endif


void rtc_register(registry_t *registry) {
#ifdef INCLUDE_CLOCK
  uint8_t i = registry->num_devices;
  
  registry->num_devices++;
  registry->entry[ i ].device_code_start = RTC_DEV;
  registry->entry[ i ].device_code_end = MAX_RTC; // support 230-239 as device codes
  registry->entry[ i ].operation = (cmd_proc *)&fn_table;
  registry->entry[ i ].command = (uint8_t *)&op_table;
#endif
  return;
}
