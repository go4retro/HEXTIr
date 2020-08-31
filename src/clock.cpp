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

#include <string.h>
#include <stdlib.h>
#include <avr/pgmspace.h>
#include "config.h"
#include "debug.h"
#include "hexbus.h"
#include "hexops.h"
#include "rtc.h"
#include "time.h"

#include "clock.h"

#ifdef INCLUDE_CLOCK

// Global references
extern uint8_t buffer[BUFSIZE];
// Global defines
volatile uint8_t  rtc_open = 0;

/*
   Open access to RTC module.
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
      if ( att & OPENMODE_MASK ) {
        len = len ? len : sizeof(buffer);
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
    clock_reset();
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }
  hex_send_final_response(rc);
  return HEXERR_SUCCESS;
}

/*
   Return time in format YYYY,MM,DD,HH,MM,SS in 24h form.
   When RTC opened in INPUT or UPDATE mode.
*/
static uint8_t hex_rtc_read(pab_t pab) {
  uint16_t len = 0;
  uint8_t rc = HEXSTAT_SUCCESS;
  uint8_t i;
  //uint16_t y;
  //char buf[8];

  memset( (char *)buffer, 0, sizeof(buffer) );
  if ( rtc_open & OPENMODE_READ )
  {
    struct tm t;
    rtc_get(&t);
#if 1
    buffer[0] = (t.tm_year > 99 ? '2' : '1');
    buffer[1] = (t.tm_year > 199 ? '1' : (t.tm_year > 99 ? '0' : '9'));
    i = t.tm_year % 100;
    buffer[2] = '0' + i / 10;
    buffer[3] = '0' + i % 10;
    buffer[4] = ',';
    i = t.tm_mon + 1;
    buffer[5] = '0' + i / 10;
    buffer[6] = '0' + i % 10;
    buffer[7] = ',';
    buffer[8] = '0' + t.tm_mday / 10;
    buffer[9] = '0' + t.tm_mday % 10;
    buffer[10] = ',';
    buffer[11] = '0' + t.tm_hour / 10;
    buffer[12] = '0' + t.tm_hour % 10;
    buffer[13] = ',';
    buffer[14] = '0' + t.tm_min / 10;
    buffer[15] = '0' + t.tm_min % 10;
    buffer[16] = ',';
    buffer[17] = '0' + t.tm_sec / 10;
    buffer[18] = '0' + t.tm_sec % 10;
    buffer[19] = 0;
    len = 19;
#else
    buf[0] = 0;
    y = t.tm_year + 1900;
    itoa( y, buf, 10 );
    strcpy((char *)buffer, buf );
    strcat((char *)buffer, "," );
    buf[0] = 0;
    i =t.tm_mon;
    itoa( i, buf, 10 );
    strcat((char *)buffer, buf );
    strcat((char *)buffer, "," );
    buf[0] = 0;
    i = t.tm_mday;
    itoa( i, buf, 10 );
    strcat((char *)buffer, buf );
    strcat((char *)buffer, "," );
    buf[0] = 0;
    i = t.tm_hour;
    itoa( i, buf, 10 );
    strcat((char *)buffer, buf );
    strcat((char *)buffer, "," );
    buf[0] = 0;
    i = t.tm_min;
    itoa( i, buf, 10 );
    strcat((char *)buffer, buf );
    strcat((char *)buffer, "," );
    buf[0] = 0;
    i = t.tm_sec;
    itoa( i, buf, 10 );
    strcat((char *)buffer, buf );
    len = strlen( (char *)buffer );
#endif
    debug_putcrlf();
    debug_trace(buffer, 0, len);
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
      hex_finish();
    } else {
      hex_send_final_response( rc );
    }
    return HEXSTAT_SUCCESS;
  }
  hex_finish();
  return HEXERR_SUCCESS;
}


/*
 * bracket space delimited input and return pointer to the start of a non-blank string.
 */
static char *skip_blanks( char *inbuf ) {
  char *ebuf;
  while ( *inbuf == ' ' ) {
    *inbuf = 0;
    inbuf++;
  }
  ebuf = inbuf;
  while ( *ebuf != ' ' && *ebuf != 0 ) {
    ebuf++;
  }
  *ebuf = 0;
  return inbuf;
}


/*
   Set time when we receive time in format YY,MM,DD,HH,MM,SS
   When RTC opened in OUTPUT or UPDATE mode.
*/
static uint8_t hex_rtc_write( pab_t pab ) {
  uint16_t len;
  char     *token;
  uint8_t  i = 0;
  uint8_t  rc = HEXSTAT_SUCCESS;
  int8_t   t_array[6];

  len = pab.datalen;
  if ( rtc_open & OPENMODE_WRITE ) {
    rc = (len >= sizeof(buffer) ) ? HEXSTAT_DATA_ERR : HEXSTAT_SUCCESS;
    if ( rc == HEXSTAT_SUCCESS ) {
      rc = hex_get_data(buffer, pab.datalen);
      if (rc == HEXSTAT_SUCCESS) {
        // process data in buffer and set clock.
        // incoming data should be formatted as YYYY,MM,DD,hh,mm,ss
        token = skip_blanks( (char *)buffer );
        len = strlen( token );
        do
        {
          if ( len <= 2 ) {
            t_array[ i++ ] = atoi(token);
            token = skip_blanks( &token[ len + 1 ] );
            len = strlen( token );
          } else {
            len = 0; // out w bad data.
          }
        } while ( len );
        
        rc = HEXSTAT_DATA_INVALID; // assume data is bad.
        if ( i == 6 ) { // got sufficient data.
          if ( t_array[0] < 100 ) { // year between 00 and 99
            if ( t_array[1] > 0 && t_array[1] < 13 ) { // month between 01 and 12
              if ( t_array[2] > 0 && t_array[2] < 32 ) { // day between 1 and 31 (I know, some months are less; room for improvement here.)
                if ( t_array[3] < 24 ) { // hour between 0 and 23
                  if ( t_array[4] < 60 ) { // minutes between 00 and 59
                    if ( t_array[5] < 60 ) { // seconds between 00 and 59
                      rc = HEXSTAT_SUCCESS;
                      struct tm t;
                      t.tm_year = t_array[ 0 ];
                      t.tm_mon = t_array[ 1 ];
                      t.tm_mday = t_array[ 2 ];
                      t.tm_hour = t_array[ 3 ];
                      t.tm_min = t_array[ 4 ];
                      t.tm_sec = t_array[ 5 ];
                      rtc_set(&t);
                    }
                  }
                }
              }
            }
          }
        }
      }
      len = 0;
    }
  } else if ( rtc_open ) {
    rc = HEXSTAT_OUTPUT_MODE_ERR;
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }

  if ( len ) {
    hex_eat_it( len, rc );
    return HEXERR_BAV;
  }

  if ( !hex_is_bav() ) { // we can send response
    hex_send_final_response( rc );
    return HEXERR_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}


static uint8_t hex_rtc_reset( pab_t pab ) {
  clock_reset();
  // release the bus ignoring any further action on bus. no response sent.
  hex_finish();
  // wait here while bav is low
  while ( !hex_is_bav() ) {
    ;
  }
  return HEXERR_SUCCESS;
}


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

void clock_reset() {
  if ( rtc_open ) {
    rtc_open = 0;
  }
  return;
}

void clock_register(registry_t *registry) {
  uint8_t i = registry->num_devices;

  registry->num_devices++;
  registry->entry[ i ].device_code_start = RTC_DEV;
  registry->entry[ i ].device_code_end = MAX_RTC; // support 230-239 as device codes
  registry->entry[ i ].operation = (cmd_proc *)&fn_table;
  registry->entry[ i ].command = (uint8_t *)&op_table;
  return;
}


void clock_init() {
  struct tm t;

  rtc_init();

  // if RTC has been stopped, store default date in it.
  if(rtc_get_state() == RTC_INVALID) {
    t.tm_year = __YEAR__ - 1900;
    t.tm_mon = __MONTH__;
    t.tm_mday = __DAY__;
    t.tm_hour = __HOUR__;
    t.tm_min = __MIN__;
    t.tm_sec = __SEC__;
    rtc_set(&t);
  }
  return;
}
#endif
