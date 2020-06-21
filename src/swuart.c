/*
 * swuart.c
 *
 *  Created on: Jun 3, 2020
 *      Author: brain
 */


#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include "swuart.h"

static volatile uint16_t tx_shift_reg[SWUART_PORTS];
static volatile uint16_t count[SWUART_PORTS];
static volatile uint16_t rate[SWUART_PORTS];
static volatile uint8_t running = 0;

SWUART_HANDLER {
  uint16_t local_tx_shift_reg;

  for(uint8_t i = 0; i < SWUART_PORTS ; i++) {
    if(tx_shift_reg[i]) {
      count[i]++;
      if(count[i] >= rate[i]) {
        count[i] = 0;
        local_tx_shift_reg = tx_shift_reg[i];
        //output LSB of the TX shift register at the TX pin
        swuart_set_tx_pin(i, local_tx_shift_reg & 1);
        //shift the TX shift register one bit to the right
        local_tx_shift_reg >>= 1;
        tx_shift_reg[i] = local_tx_shift_reg;
        //if the stop bit has been sent, the shift register will be 0
        if(!local_tx_shift_reg) {
          running--;
          if(!running) {
            swuart_disable_timer();
          }
        }
      }
    }
  }
}


void swuart_putc(uint8_t port, char character) {

  if(port < SWUART_PORTS) {
    while(tx_shift_reg[port]) { // previous not finished
      //return;
    }
    // stop bit | char | start bit (0)
    tx_shift_reg[port] = (1 << 9) | (character << 1); //stop bit at 10th position
    //start timer, if not already on.
    swuart_enable_timer();
    running++;
  }
}


void swuart_puts(uint8_t port, const char* string){
  while( *string ) {
    swuart_putc(port, *string++ );
    //wait until transmission is finished
    //while(tx_shift_reg[port]);
  }
}


void swuart_setrate(uint8_t port, uint16_t bpsrate) {
  if(port < SWUART_PORTS)
  count[port] = 0;
  rate[port] = bpsrate;
}


void swuart_init(void) {
  swuart_config();
#if SWUART_TEST
  sei();

  swuart_setrate(0, SB0300);
  swuart_setrate(1, SB9600);

  while(1) {
    swuart_puts(0, "Hello world!\n\r");
    swuart_puts(1, "Howdy WORLD!\n\r");
    _delay_ms(100);
  }
#endif
}
