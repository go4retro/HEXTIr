/*
 * swuart.h
 *
 *  Created on: Jun 3, 2020
 *      Author: brain
 */

#ifndef SWUART_H
#define SWUART_H

#include <avr/io.h>

#define SWUART0_TX_OUT     PORTC
#define SWUART0_TX_PIN     _BV(PIN5)
#define SWUART0_TX_DDR     DDRC
#define SWUART1_TX_OUT     PORTC
#define SWUART1_TX_PIN     _BV(PIN4)
#define SWUART1_TX_DDR     DDRC
#define SWUART_MAX_BPS    115200
#define SWUART_HANDLER    ISR(TIMER2_COMPA_vect)
#define SWUART_PORTS      2

#define CALC_SWBPS(x)     (int)((double)SWUART_MAX_BPS/(x))

#define SB0300   CALC_SWBPS(300)
#define SB0600   CALC_SWBPS(600)
#define SB1200   CALC_SWBPS(1200)
#define SB2400   CALC_SWBPS(2400)
#define SB4800   CALC_SWBPS(4800)
#define SB9600   CALC_SWBPS(9600)
#define SB19200  CALC_SWBPS(19200)
#define SB38400  CALC_SWBPS(38400)
#define SB57600  CALC_SWBPS(57600)
#define SB76800  CALC_SWBPS(76800)
#define SB115200 CALC_SWBPS(115200)
#define SB230400 CALC_SWBPS(230400)

static inline void swuart_enable_timer(void) {
  TCCR2B = _BV(CS20);
}

static inline void swuart_disable_timer(void) {
  TCCR2B = 0;
  TCNT2 = 0;
}


static inline void swuart_config(void) {
  SWUART0_TX_DDR |= SWUART0_TX_PIN;
  SWUART0_TX_OUT |= SWUART0_TX_PIN;
  SWUART1_TX_DDR |= SWUART1_TX_PIN;
  SWUART1_TX_OUT |= SWUART1_TX_PIN;

  TCCR2A = _BV(WGM21);  // timer 2 to CTC
  TIMSK2 |= _BV(OCIE2A);
  OCR2A = (F_CPU / SWUART_MAX_BPS) - 1;
}


static inline void swuart_set_tx_pin(uint8_t port, uint8_t val) {
  if(val) {
    switch(port) {
    case 0:
      SWUART0_TX_OUT |= SWUART0_TX_PIN;
      break;
    case 1:
      SWUART1_TX_OUT |= SWUART1_TX_PIN;
      break;
    }
  } else {
    switch(port) {
    case 0:
      SWUART0_TX_OUT &= ~SWUART0_TX_PIN;
      break;
    case 1:
      SWUART1_TX_OUT &= ~SWUART1_TX_PIN;
      break;
    }
  }
}


void swuart_putc(uint8_t p, char character);
void swuart_puts(uint8_t p, const char* string);
void swuart_setrate(uint8_t port, uint16_t bpsrate);
void swuart_init(void);

#endif /* SRC_SWUART_H_ */
