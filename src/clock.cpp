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

    rtc.cpp: Clock device functions.
*/

// uncomment to use older RTC write functionality
//#define HEX_WRITE_OLD

#include <string.h>
#include <stdlib.h>
#include <avr/pgmspace.h>

#include "config.h"
#include "configure.h"
#include "debug.h"
#include "hexbus.h"
#include "hexops.h"
#include "registry.h"
#include "rtc.h"
#include "time.h"
#include "clock.h"

#ifdef INCLUDE_CLOCK

// Global defines
volatile uint8_t  rtc_open = 0;

/*
   Open access to RTC module.
   WORK IN MAJOR PROGRESS.
   These routines will currently flag an unused parameter 'pab' warning...
*/
static void hex_rtc_open( pab_t *pab ) {
  uint16_t len;
  uint8_t  att;
  char *buf;
  uint8_t blen;
  hexstatus_t rc = HEXSTAT_SUCCESS;

  debug_puts_P("Open RTC\n");

#ifdef USE_OPEN_HELPER
  rc = hex_open_helper(pab, HEXSTAT_TOO_LONG, &len, &att);
  if(rc != HEXSTAT_SUCCESS)
    return;
#else
  if(pab->datalen > BUFSIZE) {
    hex_eat_it( pab->datalen, HEXSTAT_TOO_LONG );
    return;
  }

  if ( hex_get_data( buffer, pab->datalen ) == HEXSTAT_SUCCESS ) {
    len = buffer[ 0 ] + ( buffer[ 1 ] << 8 );
    att = buffer[ 2 ];    // tells us open for read, write or both.
  } else {
    hex_release_bus();
    return;
  }
#endif

#ifdef USE_CMD_LUN
  //*******************************************************
  // special LUN = 255
  if(pab->lun == LUN_CMD) {
    blen = pab->datalen - 3;
    buf = (char *)(buffer + 3);
    trim(&buf, &blen);
    // we should check att, as it should be WRITE or UPDATE
    if(blen)
      rc = hex_exec_cmds(buf, blen);
    hex_finish_open(BUFSIZE, rc);
    return;
  }
#endif
  if ( rtc_open ) {
    rc = HEXSTAT_ALREADY_OPEN;
  } else if(!(att & OPENMODE_MASK)) {
    rc = HEXSTAT_ATTR_ERR; // append not allowed on RTC.  INPUT|OUTPUT|UPDATE is OK.
  }

  if ( rc == HEXSTAT_SUCCESS ) {
    len = len ? len : BUFSIZE;
    rtc_open = att;
  }
  hex_finish_open(len, rc);
}

/*
   Close access to RTC module. Shuts down Wire.
*/
static void hex_rtc_close(pab_t *pab __attribute__((unused))) {
  hexstatus_t rc = HEXSTAT_SUCCESS;

  debug_puts_P("Close RTC\n");

  if ( rtc_open ) {
    clock_reset();
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }
  hex_send_final_response(rc);
}

/*
   Return time in format YYYY,MM,DD,HH,MM,SS in 24h form.
   When RTC opened in INPUT or UPDATE mode.
*/
static void hex_rtc_read(pab_t *pab) {
  uint16_t len = 0;
  hexstatus_t rc = HEXSTAT_SUCCESS;
  uint8_t i;

  debug_puts_P("Read RTC\n");

  if ( rtc_open & OPENMODE_READ )
  {
    struct tm t;
    rtc_get(&t);
    buffer[0] = '2';
    buffer[1] = '0';
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
    debug_trace(buffer, 0, len);
  } else if ( rtc_open ) { // not open for INPUT?
    rc = HEXSTAT_ATTR_ERR;
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }
  if ( !hex_is_bav() ) {
    if ( rc == HEXSTAT_SUCCESS ) {
      len = (len > pab->buflen) ? pab->buflen : len;
      transmit_word( len );
      for ( i = 0; i < len; i++ ) {
        transmit_byte( buffer[ i ] );
      }
      transmit_byte( rc );
      hex_finish();
    } else {
      hex_send_final_response( rc );
    }
  } else
    hex_finish();
}


#ifdef HEX_WRITE_OLD
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
#endif


/*
   Set time when we receive time in format YY,MM,DD,HH,MM,SS
   When RTC opened in OUTPUT or UPDATE mode.
*/
static void hex_rtc_write( pab_t *pab ) {
#ifdef HEX_WRITE_OLD
  uint16_t len;
  char     *token;
  int16_t   t_array[6];
  uint8_t  i = 0;
#else
  char* buf;
  uint8_t len;
  uint32_t yr = 0;
  uint32_t mon = 0;
  uint32_t day = 0;
  uint32_t hour = 0;
  uint32_t min = 0;
  uint32_t sec = 0;
#endif
  hexstatus_t  rc = HEXSTAT_SUCCESS;

  debug_puts_P("Write RTC\n");

  len = pab->datalen;

#ifdef USE_CMD_LUN
  if(pab->lun == LUN_CMD) {
    // handle command channel
    hex_write_cmd(pab);
    return;
  }
#endif

  buf = (char *)buffer;
  if ( rtc_open & OPENMODE_WRITE ) {
    rc = (len < BUFSIZE ? HEXSTAT_SUCCESS : HEXSTAT_DATA_ERR );
    if ( rc == HEXSTAT_SUCCESS ) {
      rc = hex_get_data(buffer, len);
      if (rc == HEXSTAT_SUCCESS) {
#ifdef HEX_WRITE_OLD
        // process data in buffer and set clock.
        // incoming data should be formatted as YYYY,MM,DD,hh,mm,ss
        token = skip_blanks( (char *)buffer );
        len = strlen( token );
        do
        {
          if ( (!i && len <= 4) || len <= 2 ) {
            t_array[ i++ ] = atoi(token);
            token = skip_blanks( &token[ len + 1 ] );
            len = strlen( token );
          } else {
            len = 0; // out w bad data.
          }
        } while ( len );
        
        rc = HEXSTAT_DATA_INVALID; // assume data is bad.
        if ( i == 6 ) { // got sufficient data.
          if ( t_array[0] < 100 || t_array[0] > 1999 ) { // year between 00 and 255 or 2000+
            if ( t_array[1] > 0 && t_array[1] < 13 ) { // month between 01 and 12
              if ( t_array[2] > 0 && t_array[2] < 32 ) { // day between 1 and 31 (I know, some months are less; room for improvement here.)
                if ( t_array[3] < 24 ) { // hour between 0 and 23
                  if ( t_array[4] < 60 ) { // minutes between 00 and 59
                    if ( t_array[5] < 60 ) { // seconds between 00 and 59
                      rc = HEXSTAT_SUCCESS;
                      struct tm t;
                      if(t_array[0] < 100)
                        t.tm_year = t_array[ 0 ] + 100;
                      else
                        t.tm_year = t_array[ 0 ] - 1900;
                      t.tm_mon = t_array[ 1 ] - 1;
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
#else
        // process data in buffer and set clock.
        // incoming data should be formatted as YYYY,MM,DD,hh,mm,ss
        rc = HEXSTAT_DATA_INVALID; // assume data is bad.
        if(!parse_number(&buf, &len, 4, &yr) && (yr < 100 || yr > 1999))  // year between 00 and 255 or 1900+
          if(!parse_number(&buf, &len, 2, &mon) && mon > 0 && mon < 13)   // month between 01 and 12
            if(!parse_number(&buf, &len, 2, &day) && day > 0 && day < 32) // day between 1 and 31 (I know, some months are less; room for improvement here.)
              if(!parse_number(&buf, &len, 2, &hour) && hour < 24)        // hour between 0 and 23
                if(!parse_number(&buf, &len, 2, &min) && min < 60)        // minutes between 00 and 59
                  if(!parse_number(&buf, &len, 2, &sec) && sec < 60) {    // seconds between 00 and 59
                    rc = HEXSTAT_SUCCESS;
                    struct tm t;
                    if(yr < 100)
                      t.tm_year = yr + 100;
                    else
                      t.tm_year = yr - 1900;
                    t.tm_mon = mon - 1;
                    t.tm_mday = day;
                    t.tm_hour = hour;
                    t.tm_min = min;
                    t.tm_sec = sec;
                    rtc_set(&t);
                  }
      }
    }
#endif
  } else if ( rtc_open ) {
    rc = HEXSTAT_OUTPUT_MODE_ERR;
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }
  if ( rc == HEXSTAT_DATA_ERR ) {
    hex_eat_it( len, rc );
    return;
  }
  if ( !hex_is_bav() ) { // we can send response
    hex_send_final_response( rc );
  } else
    hex_finish();
}


static void hex_rtc_reset( pab_t *pab __attribute__((unused))) {
  clock_reset();
  // release the bus ignoring any further action on bus. no response sent.
  hex_finish();
  // wait here while bav is low
  while ( !hex_is_bav() ) {
    ;
  }
}


void clock_reset() {
  if ( rtc_open ) {
    rtc_open = 0;
  }
}


/*
   Command handling registry for device
*/

#ifdef USE_NEW_OPTABLE
static const cmd_op_t ops[] PROGMEM = {
                                        {HEXCMD_OPEN,            hex_rtc_open},
                                        {HEXCMD_CLOSE,           hex_rtc_close},
                                        {HEXCMD_READ,            hex_rtc_read},
                                        {HEXCMD_WRITE,           hex_rtc_write},
                                        {HEXCMD_RESET_BUS,       hex_rtc_reset},
                                        {(hexcmdtype_t)HEXCMD_INVALID_MARKER,  NULL}
                                      };
#else
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

void clock_register(void) {
#ifdef NEW_REGISTER
#ifdef USE_NEW_OPTABLE
  cfg_register(DEV_RTC_START, DEV_RTC_DEFAULT, DEV_RTC_END, ops);
#else
  //cfg1_register(DEV_RTC_START, DEV_RTC_DEFAULT, DEV_RTC_END, (const uint8_t**)&op_table, (const cmd_proc **)&fn_table);
  cfg_register(DEV_RTC_START, DEV_RTC_DEFAULT, DEV_RTC_END, op_table, fn_table);
#endif
#else
  uint8_t i = registry.num_devices;

  registry.num_devices++;
  registry.entry[ i ].dev_low = DEV_RTC_START;
  registry.entry[ i ].dev_cur = DEV_RTC_DEFAULT;
  registry.entry[ i ].dev_high = DEV_RTC_END; // support 230-239 as device codes
#ifdef USE_NEW_OPTABLE
  registry.entry[ i ].oplist = (cmd_op_t *)ops;
#else
  registry.entry[ i ].operation = (cmd_proc *)fn_table;
  registry.entry[ i ].command = (uint8_t *)op_table;
#endif
#endif
}


void clock_init() {
  struct tm t;

  rtc_init();

#ifdef INIT_COMBO
  clock_register();
#endif
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
}
#endif
