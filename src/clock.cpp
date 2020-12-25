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

    clock.cpp: Clock device functions.
*/

// uncomment to use older RTC write functionality
//#define HEX_WRITE_OLD

#include <string.h>
#include <stdlib.h>
#include <avr/pgmspace.h>

#include "config.h"
#include "debug.h"
#include "eeprom.h"
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
static void clk_open( pab_t *pab ) {
  uint16_t len;
  uint8_t  att;
  char *buf;
  uint8_t blen;
  hexstatus_t rc = HEXSTAT_SUCCESS;

  debug_puts_P("Open RTC\r\n");

  rc = hex_open_helper(pab, HEXSTAT_TOO_LONG, &len, &att);
  if(rc != HEXSTAT_SUCCESS)
    return;

  //*******************************************************
  // special LUN = 255
  if(pab->lun == LUN_CMD) {
    blen = pab->datalen - 3;
    buf = (char *)(buffer + 3);
    trim(&buf, &blen);
    // we should check att, as it should be WRITE or UPDATE
    if(blen)
      rc = hex_exec_cmds(buf, blen, &(_config.clk_dev));
    hex_finish_open(BUFSIZE, rc);
    return;
  }

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
static void clk_close(pab_t *pab __attribute__((unused))) {
  hexstatus_t rc = HEXSTAT_SUCCESS;

  debug_puts_P("Close RTC\r\n");

  if (pab->lun == LUN_CMD) {
    // handle command channel close
    hex_close_cmd();
    return;
  }

  if ( rtc_open ) {
    clk_reset();
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }
  hex_send_final_response(rc);
}

/*
   Return time in format YYYY,MM,DD,HH,MM,SS in 24h form.
   When RTC opened in INPUT or UPDATE mode.
*/
static void clk_read(pab_t *pab) {
  uint16_t len = 0;
  hexstatus_t rc = HEXSTAT_SUCCESS;
  uint8_t i;

  debug_puts_P("Read RTC\r\n");

  if(pab->lun == LUN_CMD) {
    hex_read_status();
    return;
  }

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
  if ( rc == HEXSTAT_SUCCESS ) {
    if ( !hex_is_bav() ) {
      len = (len > pab->buflen) ? pab->buflen : len;
      hex_send_word( len );
      for ( i = 0; i < len; i++ ) {
        hex_send_byte( buffer[ i ] );
      }
      hex_send_byte( rc );
      hex_finish();
    }
  } else {
    hex_send_final_response( rc );
  }
}


/*
   Set time when we receive time in format YY,MM,DD,HH,MM,SS
   When RTC opened in OUTPUT or UPDATE mode.
*/
static void clk_write( pab_t *pab ) {
  char* buf;
  uint8_t len;
  uint32_t yr = 0;
  uint32_t mon = 0;
  uint32_t day = 0;
  uint32_t hour = 0;
  uint32_t min = 0;
  uint32_t sec = 0;

  hexstatus_t  rc = HEXSTAT_SUCCESS;

  debug_puts_P("Write RTC\r\n");

  len = pab->datalen;

  if(pab->lun == LUN_CMD) {
    // handle command channel
    hex_write_cmd(pab, &(_config.clk_dev));
    return;
  }

  buf = (char *)buffer;
  if ( rtc_open & OPENMODE_WRITE ) {
    rc = (len < BUFSIZE ? HEXSTAT_SUCCESS : HEXSTAT_DATA_ERR );
    if ( rc == HEXSTAT_SUCCESS ) {
      rc = hex_get_data(buffer, len);
      if (rc == HEXSTAT_SUCCESS) {
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
  } else if ( rtc_open ) {
    rc = HEXSTAT_OUTPUT_MODE_ERR;
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }
  if ( rc == HEXSTAT_DATA_ERR ) {
    hex_eat_it( len, rc );
    return;
  }
  hex_send_final_response( rc );
}


static void clk_reset_dev( pab_t *pab __attribute__((unused))) {
  clk_reset();
  // release the bus ignoring any further action on bus. no response sent.
  hex_finish();
  // wait here while bav is low
  while ( !hex_is_bav() ) {
    ;
  }
}


void clk_reset() {
  if ( rtc_open ) {
    rtc_open = 0;
  }
}


/*
   Command handling registry for device
*/

static const cmd_op_t ops[] PROGMEM = {
                                        {HEXCMD_OPEN,            clk_open},
                                        {HEXCMD_CLOSE,           clk_close},
                                        {HEXCMD_READ,            clk_read},
                                        {HEXCMD_WRITE,           clk_write},
                                        {HEXCMD_RESET_BUS,       clk_reset_dev},
                                        {(hexcmdtype_t)HEXCMD_INVALID_MARKER,  NULL}
                                      };

static uint8_t is_cfg_valid(void) {
  return (_config.valid && _config.clk_dev >= DEV_RTC_START && _config.clk_dev <= DEV_RTC_END);
}


void clk_register(void) {
  uint8_t clk_dev = DEV_RTC_DEFAULT;

  if(is_cfg_valid()) {
    clk_dev = _config.clk_dev;
  }
  reg_add(DEV_RTC_START, clk_dev, DEV_RTC_END, ops);
}


void clk_init() {
  struct tm t;

  rtc_init();

  clk_register();
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
