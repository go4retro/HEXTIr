/*
 * debug.c
 *
 *  Created on: Jun 20, 2020
 *      Author: brain
 */

#if defined CONFIG_UART_DEBUG || defined CONFIG_UART_DEBUG_SW

#include <avr/pgmspace.h>

#include "config.h"
#include "swuart.h"
#include "uart.h"

#include "debug.h"

void debug_putc(uint8_t data) {
#ifdef CONFIG_UART_DEBUG_SW
  swuart_putc(CONFIG_UART_DEBUG_SW_PORT, data);
#endif
#ifdef CONFIG_UART_DEBUG
  uart_putc(data);
#endif
}


void debug_puthex(uint8_t hex) {
  uint8_t tmp = hex >> 4;

  debug_putc(tmp>9?tmp - 10 + 'a':tmp + '0');
  tmp = hex & 0x0f;
  debug_putc(tmp>9?tmp - 10 + 'a':tmp + '0');
}


void debug_putcrlf(void) {
  debug_putc(13);
  debug_putc(10);
}


void debug_puts(const char *text) {
  while( *text ) {
    debug_putc(*text++ );
  }
}


void debug_puts_P(const char *text) {
  uint8_t ch;

  while ((ch = pgm_read_byte(text++))) {
    debug_putc(ch);
  }
}


void debug_trace(void *ptr, uint16_t start, uint16_t len) {
  uint16_t i;
  uint8_t j;
  uint8_t ch;
  uint8_t *data = ptr;

  data+=start;
  for(i=0;i<len;i+=16) {

    debug_puthex(start >> 8);
    debug_puthex(start & 0xff);
    debug_putc('|');
    debug_putc(' ');
    for(j = 0;j < 16;j++) {
      if(i + j < len) {
        ch=*(data + j);
        debug_puthex(ch);
      } else {
        debug_putc(' ');
        debug_putc(' ');
      }
      debug_putc(' ');
    }
    debug_putc('|');
    for(j = 0;j < 16;j++) {
      if(i + j < len) {
        ch=*(data++);
        if(ch < 32 || ch > 0x7e)
          ch='.';
        debug_putc(ch);
      } else {
        debug_putc(' ');
      }
    }
    debug_putc('|');
    debug_putcrlf();
    start += 16;
  }
}

void debug_init(void) {
#ifdef CONFIG_UART_DEBUG_SW
  swuart_init();
  swuart_setrate(CONFIG_UART_DEBUG_SW_PORT, CALC_SWBPS(CONFIG_UART_DEBUG_RATE));
#elif CONFIG_UART_DEBUG
  uart_init();
  // rate set in init
#endif

}
#endif
