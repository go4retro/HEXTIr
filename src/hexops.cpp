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

    hexops.cpp: Foundational Hex Bus functions
*/


#include <stddef.h>
#include "config.h"
#include "debug.h"
#include "hexbus.h"
#include "hexops.h"
#include "timer.h"
#include "uart.h"

#include "hexops.h"

uint8_t buffer[BUFSIZE];

uint8_t hex_get_data(uint8_t *buf, uint16_t len) {
  uint8_t rc = HEXERR_SUCCESS;
  uint16_t i = 0;

  while (i < len) {
    buf[ i ] = 1;
    if((rc = receive_byte( &buf[ i++ ] )) != HEXERR_SUCCESS) {
      return HEXERR_BAV;
    }
  }
  if (len > 0) {
    debug_putcrlf();
    debug_trace(buf, 0, len);
  }
  return HEXERR_SUCCESS;
}


void hex_eat_it(uint16_t length, uint8_t status )
{
  uint16_t i = 0;
  uint8_t  data;

  while ( i < length ) {
    data = 1;
    if ( receive_byte( &data ) != HEXERR_SUCCESS ) {
      hex_release_bus();
      return;
    }
    i++;
  }
  // safe to turn around now.  As long as BAV is still
  // low, then go ahead and send a response.
  if (!hex_is_bav()) { // we can send response
    hex_send_final_response( status );
  }
  return;
}


/*
 * hex_unsupported() should be used for any command on any device
 * where we provide no support for that command.
 */
uint8_t hex_unsupported(pab_t pab) {
  hex_eat_it(pab.datalen, HEXSTAT_UNSUPP_CMD );
  return HEXERR_BAV;
}


uint8_t hex_null( __attribute__((unused)) pab_t pab ) {
  hex_release_bus();
  while (!hex_is_bav() )  // wait for BAV back high, ignore any traffic
    ;
  return HEXERR_SUCCESS;
}
