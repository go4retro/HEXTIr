/*
 * hexops.c
 *
 *  Created on: May 31, 2020
 *      Author: brain
 */

#include <stddef.h>
#include "config.h"
#include "hexbus.h"
#include "hexops.h"
#include "timer.h"
#include "uart.h"

uint8_t buffer[BUFSIZE];

uint8_t hex_getdata(uint8_t buf[256], uint16_t len) {
  uint8_t rc = HEXERR_SUCCESS;
  uint16_t i = 0;

  while (i < len && rc == HEXERR_SUCCESS ) {
    buf[ i ] = 1;
    rc = receive_byte( &buf[ i++ ] );
#ifdef ARDUINO
    timer_check(0);
#endif
  }

  if (len > 0) {
    uart_putc(13);
    uart_putc(10);
    uart_trace(buf, 0, len);
  }

  return HEXERR_SUCCESS;
}


uint8_t hex_receive_options( pab_t pab ) {
  uint16_t i = 0;
  uint8_t  data;

  while (i < pab.datalen) {
    data = 1;   // tells receive byte to release HSK from previous receipt
    if ( receive_byte( &data ) == HEXERR_SUCCESS ) {
      buffer[ i ] = data;
      i++;
    } else {
      return HEXERR_BAV;
    }
  }
  buffer[ i ] = 0;
  return HEXSTAT_SUCCESS;
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
