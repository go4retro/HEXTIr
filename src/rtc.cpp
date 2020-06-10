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
DS3231            clock_peripheral;       // Our CLOCK : access via the HexBus at device code 230-239
volatile uint8_t  rtc_open = 0;

/*
   Open access to RTC module. Begin Wire, Begin DS3231
   WORK IN MAJOR PROGRESS.
   These routines will currently flag an unused parameter 'pab' warning...
*/
static uint8_t hex_rtc_open( pab_t pab ) {
  uint16_t len;
  uint8_t  att;

  len = 0;
  if ( hex_get_data(buffer, pab.datalen) == HEXSTAT_SUCCESS )
  {
    len = buffer[ 0 ] + ( buffer[ 1 ] << 8 );
    att = buffer[ 2 ];    // tells us open for read, write or both.
  } else {
    hex_release_bus();
    return HEXERR_BAV; // BAV ERR.
  }

  if ( !hex_is_bav() ) {
    if ( !rtc_open ) {
      if ( att & OPENMODE_READ | OPENMODE_WRITE ) {
        len = len ? len : sizeof(buffer);
        clock_peripheral.setClockMode(false);
        rtc_open = att;
        transmit_word( 4 );
        transmit_word( len );
        transmit_word( 0 );
        transmit_byte( HEXSTAT_SUCCESS );
        hex_finish();
        return HEXERR_SUCCESS;
      } else {
        att = HEXSTAT_ATTR_ERR; // append not allowed on RTC.  INPUT|OUTPUT|UPDATE is OK.
      }
    } else {
      att = HEXSTAT_ALREADY_OPEN;
    }
    hex_send_final_response( att );
    return HEXERR_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}

/*
   Close access to RTC module. Shuts down Wire.
*/
static uint8_t hex_rtc_close(pab_t pab) {
  uint8_t rc = HEXSTAT_SUCCESS;
  if ( rtc_open ) {
    rtc_reset();
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }
  hex_send_final_response(rc);
  return HEXERR_SUCCESS;
}

/*
   Return time in format YY,MM,DD,HH,MM,SS in 24h form.
   When RTC opened in INPUT or UPDATE mode.
*/
static uint8_t hex_rtc_read(pab_t pab) {
  uint16_t len = 0;
  uint8_t rc = HEXSTAT_SUCCESS;
  uint8_t i;
  RTClib RTC;
  char buf[8];

  memset( (char *)buffer, 0, sizeof(buffer) );
  if ( rtc_open & OPENMODE_READ )
  {
    DateTime now = RTC.now();
    buf[0] = 0;

    itoa( now.year(), buf, 10 );
    strcpy((char *)buffer, buf );
    strcat((char *)buffer, "," );
    buf[0] = 0;

    itoa( now.month(), buf, 10 );
    strcpy((char *)buffer, buf );
    strcat((char *)buffer, "," );
    buf[0] = 0;

    itoa( now.day(), buf, 10 );
    strcpy((char *)buffer, buf );
    strcat((char *)buffer, "," );
    buf[0] = 0;

    itoa( now.hour(), buf, 10 );
    strcpy((char *)buffer, buf );
    strcat((char *)buffer, "," );
    buf[0] = 0;
    itoa( now.minute(), buf, 10 );
    strcpy((char *)buffer, buf );
    strcat((char *)buffer, "," );
    buf[0] = 0;
    itoa( now.second(), buf, 10 );
    strcpy((char *)buffer, buf );
    len = strlen( (char *)buffer );
    
  } else if ( rtc_open ) { // not open for INPUT?
    rc = HEXSTAT_ATTR_ERR;
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }
  if ( !hex_is_bav() ) {
    if ( rc == HEXSTAT_SUCCESS ) {
      len = (len > pab.buflen) ? pab.buflen : len;
      transmit_word( len );
      for ( i = 0; i < len; i++ ) {
        transmit_byte( buffer[ i ] );
      }
      transmit_byte( rc );
    } else {
      hex_send_final_response( rc );
    }
    return HEXSTAT_SUCCESS;
  }
  hex_finish();
  return HEXERR_SUCCESS;
}

/*
   Set time when we receive time in format YY,MM,DD,HH,MM,SS
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
#ifdef ARDUINO
  Wire.begin();  // bring up the I2C interface
#endif
  return;
}


void rtc_reset() {
#ifdef INCLUDE_CLOCK
  if ( rtc_open ) {
    rtc_open = 0;
  }
#endif
  return;
}

#ifdef INCLUDE_CLOCK
/*
   Command handling registry for device
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
