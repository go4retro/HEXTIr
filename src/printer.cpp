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

    printer.cpp: Printer-based (over Serial) device functions.
*/

#include <string.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#include "config.h"
#include "debug.h"
#include "hexbus.h"
#include "hexops.h"
#include "registry.h"
#include "swuart.h"
#include "timer.h"
#include "printer.h"

#ifdef INCLUDE_PRINTER

// Global defines
volatile uint8_t  prn_open = 0;


/*
   hex_prn_open() -
   "opens" the Serial.object for use as a printer at device code 12 (default PC-324 printer).
*/
static hexstatus_t hex_prn_open(pab_t *pab) {
  uint16_t len = 0;
  hexstatus_t  rc = HEXSTAT_SUCCESS;
  uint8_t  att = 0;

  debug_puts_P(PSTR("Open Printer\n"));


#ifdef USE_OPEN_HELPER
  rc = hex_open_helper(pab, HEXSTAT_TOO_LONG, &len, &att);
#else
  if(pab->datalen > BUFSIZE) {
    hex_eat_it( pab->datalen, HEXSTAT_TOO_LONG );
    return HEXSTAT_TOO_LONG;
  }

  if ( hex_get_data( buffer, pab->datalen ) == HEXSTAT_SUCCESS ) {
    len = buffer[ 0 ] + ( buffer[ 1 ] << 8 );
    att = buffer[ 2 ];
  } else {
    hex_release_bus();
    return HEXSTAT_BUS_ERR;
  }
#endif
  if(rc != HEXSTAT_SUCCESS)
    return rc;

  if ( !prn_open ) {
    if ( att != OPENMODE_WRITE ) {
      rc = HEXSTAT_OPTION_ERR;
    }
  } else {
    rc = HEXSTAT_ALREADY_OPEN;
  }
  if (!hex_is_bav()) { // we can send response
    if ( rc == HEXSTAT_SUCCESS )
    {
//#ifndef ARDUINO
      swuart_setrate(0, SB115200);
//#endif
      prn_open = 1;  // our printer is NOW officially open.
      len = len ? len : BUFSIZE;
      hex_send_size_response( len );
    }
    else
    {
      hex_send_final_response( rc );
    }
    return HEXSTAT_SUCCESS;
  }
  hex_finish();
  return HEXSTAT_BUS_ERR;
}


/*
   hex_prn_close() -
   closes printer at device 12 for use from host.
*/
static hexstatus_t hex_prn_close(__attribute__((unused)) pab_t *pab) {
  hexstatus_t rc = HEXSTAT_SUCCESS;

  if ( !prn_open ) {
    rc = HEXSTAT_NOT_OPEN;
  }
  prn_open = 0;      // mark printer closed regardless.
  if ( !hex_is_bav() ) {
    // send 0000 response with appropriate status code.
    hex_send_final_response( rc );
  }
  return HEXSTAT_SUCCESS;
}


/*
    hex_prn_write() -
    write data to serial port when printer is open.
*/
static hexstatus_t hex_prn_write(pab_t *pab) {
  uint16_t len;
  uint16_t i;
  uint8_t  written = 0;
  hexstatus_t  rc = HEXSTAT_SUCCESS;

  debug_puts_P(PSTR("Write Printer\n"));

  len = pab->datalen;

  while ( len && rc == HEXSTAT_SUCCESS ) {
    i = (len >= BUFSIZE ? BUFSIZE : len);
    rc = hex_get_data(buffer, i);
    /*
        printer open? print a buffer of data.  We hold off
        on continuation until we've actually got the data
        flushed from the serial buffers before we continue.
        Digital logic analyzer shows some "glitchy" behavior
        on our HSK signal that may be due (somehow) to serial
        port use.  Rather than take the chance, ensure that
        serial data is flushed before proceeding to make sure
        that HexBus operations are not compromised.
    */
    if ( rc == HEXSTAT_SUCCESS && prn_open ) {
//#ifdef ARDUINO
//      Serial.write(buffer, i);
//      delayMicroseconds( i*72 );
//      /* use 72 us per character sent delay.
//         digital logic analyzer confirms that @ 115200 baud, the data is
//         flushed over the wire BEFORE we continue HexBus operations.
//      */
//#else
      for(uint8_t j = 0; j < i; j++) {
        swuart_putc(0, buffer[j]);
      }
      swuart_flush();
//#endif
      written = 1; // indicate we actually wrote some data
    }
    len -= i;
  }

  /* if we've written data and our printer is open, finish the line out with
      a CR/LF.
  */
  if ( written && prn_open ) {
//#ifdef ARDUINO
//    buffer[0] = 13;
//    buffer[1] = 10;
//    Serial.write(buffer, 2);
//    delayMicroseconds(176);
//#else
    swuart_putcrlf(0);
    swuart_flush();
//#endif
  }
  /*
     if printer is NOT open, report such status back
  */
  if ( !prn_open ) {
    rc = HEXSTAT_NOT_OPEN;   // if printer is NOT open, report such status
  }

  /*
     send response and finish operation
  */
  if (!hex_is_bav() ) {
    hex_send_final_response( rc );
  } else {
    hex_finish();
  }
  return HEXSTAT_SUCCESS;
}


static hexstatus_t hex_prn_reset( __attribute__((unused)) pab_t *pab) {
  
  prn_reset();
  // release the bus ignoring any further action on bus. no response sent.
  hex_finish();
  // wait here while bav is low
  while ( !hex_is_bav() ) {
    ;
  }
  return HEXSTAT_SUCCESS;
}


/*
 * Command handling registry for device
 */
#ifdef USE_NEW_OPTABLE
static const cmd_op_t ops[] PROGMEM = {
                                        {HEXCMD_OPEN,            hex_prn_open},
                                        {HEXCMD_CLOSE,           hex_prn_close},
                                        {HEXCMD_WRITE,           hex_prn_write},
                                        {HEXCMD_RESET_BUS,       hex_prn_reset},
                                        {HEXCMD_INVALID_MARKER,  NULL}
                                      };
#else
static const cmd_proc fn_table[] PROGMEM = {
  hex_prn_open,
  hex_prn_close,
  hex_prn_write,
  hex_prn_reset,
  NULL // end of table.
};

static const uint8_t op_table[] PROGMEM = {
  HEXCMD_OPEN,
  HEXCMD_CLOSE,
  HEXCMD_WRITE,
  HEXCMD_RESET_BUS,
  HEXCMD_INVALID_MARKER
};
#endif

void prn_reset( void ) {
  prn_open = 0; // make sure our printer is closed.
}

void prn_init( void ) {

  prn_open = 0;

#ifdef USE_NEW_OPTABLE
  cfg_register(DEV_PRN_START, DEV_PRN_DEFAULT, DEV_PRN_END, (const cmd_op_t**)&ops);
#else
  cfg_register(DEV_PRN_START, DEV_PRN_DEFAULT, DEV_PRN_END, (const uint8_t**)&op_table, (const cmd_proc **)&fn_table);
#endif
//#ifndef ARDUINO
  // TODO not sure where BPS rate is set on Arduino...
//  swuart_setrate(0, SB9600);
//#endif
}
#endif
