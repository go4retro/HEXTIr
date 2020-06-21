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

    serial.cpp: simple SoftwareSerial device functions.
*/

#include <stdlib.h>
#include <string.h>
#include <avr/pgmspace.h>

#include "config.h"
#include "hexbus.h"
#include "hexops.h"
#include "timer.h"

#ifdef ARDUINO
// WORK IN PROGRESS - some of the routines will flag an unused parameter 'pab' warning.
#include <SoftwareSerial.h>
#include <Arduino.h>
#else
#include "uart.h"
#endif

#include "serial.h"

static const char baud_attrib[] PROGMEM = "B=";
// Global defines
volatile uint8_t  ser_open = 0;

#ifdef ARDUINO
SoftwareSerial    serial_peripheral( 8, 9 );  // either can support interrupts, and are otherwise available.
#endif

extern uint8_t buffer[BUFSIZE];

/*
   her_ser_open()
*/
static uint8_t hex_ser_open(pab_t pab) {
  char     *attrib;
  long     baud = 9600;
  uint16_t len;
  uint8_t  att;

  len = 0;
  memset( buffer, 0, sizeof(buffer) );
  if ( hex_get_data(buffer, pab.datalen) == HEXSTAT_SUCCESS )
  {
    len = buffer[ 0 ] + ( buffer[ 1 ] << 8 );
    att = buffer[ 2 ];  // tells us open for read, write or both.
  } else {
    hex_release_bus();
    return HEXERR_BAV; // BAV ERR.
  }
  // Now, we need to parse the input buffer and decide on parameters.
  // realistically, all we can actually support is B=xxx.  Some other
  // parameters we just pretty much ignore D=, P=, C=, N=, S=.
  // The E= echo parameter we can act on.
  // The R= N/C/L we act on (no nl, carriage return, or linefeed.  We also add B for both CR and LF
  // The T= transfer tpe is also acted on, R/C/W.
  // The O= data overrun reporting, Yes No is acted on.
  //--
  // the ATT ributes we support are: OUTPUT mode 10 (write only), INPUT mode 01 (read only), and UPDATE
  // mode, 11 (read/write).  Relative/Sequential is always 0.  Fixed/Variable is ignored.
  // Internal/Display is always 0, other bits not used.
  //
  // work in progress... not ready for prime time.
  if ( !hex_is_bav() ) {
    if ( !ser_open ) {
      if ( att != 0 ) {
        len = len ? len : sizeof( buffer );
        if ( att & OPENMODE_UPDATE ) {
          ser_open = att; // 00 attribute = illegal.
          if ( pab.datalen > 3 ) { // see if we have a B= in the buffer.
            attrib = strtok( (char *)&buffer[ 3 ], baud_attrib );
            if ( attrib != NULL ) {
              baud = atol( attrib + 2 );
            }
          }
#ifdef ARDUINO
          serial_peripheral.begin( baud );
#else
          uart_config(CALC_BPS(baud), UART_LENGTH_8, UART_PARITY_NONE, UART_STOP_1);
#endif
          if ( att & OPENMODE_READ ) {
#ifdef ARDUINO
            serial_peripheral.listen(); // apply listener if we are expecting input
#else
            // TODO figure out non Arduino alternative
#endif
          }
          transmit_word( 4 );
          transmit_word( len );
          transmit_word( 0 );
          transmit_byte( HEXSTAT_SUCCESS );
          hex_finish();
          return HEXERR_SUCCESS;
        } else {
          att = HEXSTAT_APPEND_MODE_ERR;
        }
      } else {
        att = HEXSTAT_ATTR_ERR;
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
   hex_ser_close()
*/
static uint8_t hex_ser_close(__attribute__((unused)) pab_t pab) {
  uint8_t rc = HEXSTAT_SUCCESS;

  if ( ser_open ) {
    ser_reset();
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }
  if (!hex_is_bav() ) { // we can send response
    hex_send_final_response( rc );
    return HEXERR_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}


static uint8_t hex_ser_read(pab_t pab) {
  uint16_t len = pab.buflen;
  uint16_t bcount = 0;
  int8_t  rc = HEXSTAT_SUCCESS;

  if ( ser_open ) {
    // protect access via ser_open since serial_peripheral is not present
    // if ser_open = 0.
#ifdef ARDUINO
    bcount = serial_peripheral.available();
#else
    bcount = (uart0_data_available() ? 1 : 0); // TODO  need to implement true count
#endif
    if ( bcount > pab.buflen ) {
      bcount = pab.buflen;
    }
  }

  if ( !hex_is_bav() ) {
    if ( ser_open & OPENMODE_READ ) {
      // send how much we are going to send
      rc = transmit_word( bcount );

      // while we have data remaining to send.
      while ( bcount && rc == HEXERR_SUCCESS ) {

        len = bcount;    // remaing amount to read from file
        // while it fit into buffer or not?  Only read as much
        // as we can hold in our buffer.
        len = ( len > sizeof( buffer ) ) ? sizeof( buffer ) : len;

        bcount -= len;
        while ( len-- && rc == HEXSTAT_SUCCESS ) {
#ifdef ARDUINO
          rc = transmit_byte( serial_peripheral.read() );
#else
          rc = transmit_byte( uart_getc() );
#endif
        }
      }
      if ( rc != HEXERR_BAV ) {
        transmit_byte( rc );
      }
      hex_finish();
    } else if ( ser_open ) {
      // open for OUTPUT only?
      hex_send_final_response( HEXSTAT_INPUT_MODE_ERR );
    } else {
      // not open at all?
      hex_send_final_response( HEXSTAT_NOT_OPEN );
    }
    return HEXERR_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}


static uint8_t hex_ser_write(pab_t pab) {
  uint16_t len;
  uint16_t i;
  uint16_t j;
  uint8_t  rc = HEXERR_SUCCESS;

  len = pab.datalen;
  if ( ser_open & OPENMODE_WRITE ) {
    while (len && rc == HEXERR_SUCCESS ) {
      i = (len >= sizeof(buffer) ? sizeof(buffer) : len);
      rc = hex_get_data(buffer, i);
      if (rc == HEXSTAT_SUCCESS) {
        j  = 0;
        while (j < len) {
#ifdef ARDUINO
          serial_peripheral.write( buffer[ j++ ] );
#else
          uart_putc(buffer[j++]);
#endif
        }
      }
      len -= i;
    }
  } else if ( ser_open ) {
    rc = HEXSTAT_OUTPUT_MODE_ERR;
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }

  if ( len ) {
    hex_eat_it( len, rc );
    return HEXERR_BAV;
  }

  if (!hex_is_bav() ) { // we can send response
    hex_send_final_response( rc );
    return HEXERR_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}


static uint8_t hex_ser_rtn_sta(pab_t pab) {
  // TBD
  return HEXERR_SUCCESS;
}


static uint8_t hex_ser_set_opts(pab_t pab) {
  // TBD
  return HEXERR_SUCCESS;
}


static uint8_t hex_ser_reset( __attribute__((unused)) pab_t pab) {

  ser_reset();
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
  hex_ser_open,
  hex_ser_close,
  hex_ser_read,
  hex_ser_write,
  hex_ser_rtn_sta,
  hex_ser_set_opts,
  hex_ser_reset,
  NULL // end of table.
};

static const uint8_t op_table[] PROGMEM = {
  HEXCMD_OPEN,
  HEXCMD_CLOSE,
  HEXCMD_READ,
  HEXCMD_WRITE,
  HEXCMD_RETURN_STATUS,
  HEXCMD_SET_OPTIONS,
  HEXCMD_RESET_BUS,
  HEXCMD_INVALID_MARKER
};


void ser_register(registry_t *registry) {
  uint8_t i = registry->num_devices;

  registry->num_devices++;
  registry->entry[ i ].device_code_start = SER_DEV;
  registry->entry[ i ].device_code_end = MAX_SER; // support 20, 21, 22, 23 as device codes
  registry->entry[ i ].operation = (cmd_proc *)&fn_table;
  registry->entry[ i ].command = (uint8_t *)&op_table;
  return;
}


void ser_reset(void) {
  if ( ser_open ) {
#ifdef ARDUINO
    serial_peripheral.end();
#endif
    ser_open = FALSE;
  }
  return;
}


void ser_init(void) {
  ser_open = FALSE;
}
