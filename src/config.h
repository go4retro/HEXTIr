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

    config.h: User-configurable options to simplify hardware changes and/or
              reduce the code/ram requirements of the code.
*/

#ifndef CONFIG_H
#define CONFIG_H

#include <avr/io.h>

#define BUFSIZE       64

#if defined ARDUINO_AVR_UNO || defined ARDUINO_AVR_PRO || defined ARDUINO_AVR_NANO
 #define CONFIG_HARDWARE_VARIANT   3 // Hardware variant 3 is Arduino, with BAV on D2 for wakeup from standby mode.
 // Variant 3 has been tested on Pro Mini, Uno, and Nano as functional.  Select target platform in the IDE.
#endif

#ifndef ARDUINO
 #include "autoconf.h"
 #define MAX_OPEN_FILES 8

 #ifdef CONFIG_UART_DEBUG
  #define UART0_ENABLE
  #define UART0_BAUDRATE CONFIG_UART_DEBUG_RATE
 #endif

 #ifdef CONFIG_UART_DEBUG_SW
  #ifndef CONFIG_UART_DEBUG_SW_PORT
   #define CONFIG_UART_DEBUG_SW_PORT 1
  #endif
 #endif

 #ifndef UART0_ENABLE
  #define UART0_ENABLE
  #define DYNAMIC_UART

  #ifdef CONFIG_UART_BUF_SHIFT
   #define UART0_TX_BUFFER_SHIFT CONFIG_UART_BUF_SHIFT
  #endif

  #ifdef CONFIG_UART_BAUDRATE
   #define UART0_BAUDRATE CONFIG_UART_BAUDRATE
  #endif
 #endif


#else
 #define MAX_OPEN_FILES 3      // SD 1.0 and later let us have more than one open file, each additional file uses 30 bytes RAM
 // 3 files lets us use E/A easily.  source, object, listing.

 #define INCLUDE_CLOCK
 #define INCLUDE_POWERMGMT  // Power Management may not be fully available on all platforms
#endif

#define INCLUDE_PRINTER
#define INCLUDE_SERIAL

#include "configure.h"
/* ----- Common definitions for all AVR hardware variants ------ */


#if CONFIG_HARDWARE_VARIANT == 1

/* ---------- Hardware configuration: HEXTIr v1 ---------- */
#  define HEX_HSK_DDR         DDRC
#  define HEX_HSK_OUT         PORTC
#  define HEX_HSK_IN          PINC
#  define HEX_HSK_PIN         _BV(PIN4)

#  define HEX_BAV_DDR         DDRC
#  define HEX_BAV_OUT         PORTC
#  define HEX_BAV_IN          PINC
#  define HEX_BAV_PIN         _BV(PIN5)

#  define HEX_DATA_DDR        DDRC
#  define HEX_DATA_OUT        PORTC
#  define HEX_DATA_IN         PINC
#  define HEX_DATA_PIN        (_BV(PIN0) | _BV(PIN1) | _BV(PIN2) | _BV(PIN3))

#  define LED_BUSY_DDR        DDRD
#  define LED_BUSY_OUT        PORTD
#  define LED_BUSY_PIN        _BV(PIN2)

#  define HAVE_SD
#  define SD_CHANGE_HANDLER     ISR(PCINT0_vect)
#  define SD_SUPPLY_VOLTAGE     (1L<<21)

/* 250kHz slow, 2MHz fast */
#  define SPI_DIVISOR_SLOW 64
#  define SPI_DIVISOR_FAST 8

static inline void sdcard_interface_init(void) {
  DDRB  &= ~_BV(PB0);  // wp
  PORTB |=  _BV(PB0);
  DDRB  &= ~_BV(PB1);  // detect
  PORTB |=  _BV(PB1);
  PCICR |= _BV(PCIE0);
  //EICRB |=  _BV(ISC60);
  PCMSK0 |= _BV(PCINT0);
  //EIMSK |=  _BV(INT6);
}

static inline uint8_t sdcard_detect(void) {
  return !(PINB & _BV(PIN1));
}

static inline uint8_t sdcard_wp(void) {
  return PINB & _BV(PIN0);
}

static inline void wakeup_pin_init(void) {
  ;
}

static inline void board_init(void) {
  // turn on power LED
  DDRD  |= _BV(PIN3);
  PORTD |= _BV(PIN3);
}

#elif CONFIG_HARDWARE_VARIANT == 2

/* ---------- Hardware configuration: HEXTIr Arduino ---------- */
#  define HEX_HSK_DDR         DDRD
#  define HEX_HSK_OUT         PORTD
#  define HEX_HSK_IN          PIND
#  define HEX_HSK_PIN         _BV(PIN3)

#  define HEX_BAV_DDR         DDRD
#  define HEX_BAV_OUT         PORTD
#  define HEX_BAV_IN          PIND
#  define HEX_BAV_PIN         _BV(PIN2)

#  define HEX_DATA_DDR        DDRC
#  define HEX_DATA_OUT        PORTC
#  define HEX_DATA_IN         PINC
#  define HEX_DATA_PIN        (_BV(PIN0) | _BV(PIN1) | _BV(PIN2) | _BV(PIN3))

#  define LED_BUSY_DDR        DDRD
#  define LED_BUSY_OUT        PORTD
#  define LED_BUSY_PIN        _BV(PIN7)

#  define HAVE_SD
#  define SD_CHANGE_HANDLER     ISR(PCINT0_vect)
#  define SD_SUPPLY_VOLTAGE     (1L<<21)

/* 250kHz slow, 2MHz fast */
#  define SPI_DIVISOR_SLOW 64
#  define SPI_DIVISOR_FAST 8

static inline void sdcard_interface_init(void) {
  DDRB  &= ~_BV(PB0);  // wp
  PORTB |=  _BV(PB0);
  DDRB  &= ~_BV(PB1);  // detect
  PORTB |=  _BV(PB1);
  PCICR |= _BV(PCIE0);
  //EICRB |=  _BV(ISC60);
  PCMSK0 |= _BV(PCINT0);
  //EIMSK |=  _BV(INT6);
}

static inline uint8_t sdcard_detect(void) {
  return !(PINB & _BV(PIN1));
}

static inline uint8_t sdcard_wp(void) {
  return PINB & _BV(PIN0);
}


static inline void board_init(void) {
}


static inline void wakeup_pin_init(void) {
  ;
}


#  define INCLUDE_POWERMGMT  // Power Management may not be fully available on all platforms
#  define POWER_MGMT_HANDLER  INT0_vect

static inline void pwr_irq_enable(void) {
  EICRA |= _BV(ISC01);  // trigger power enable on falling IRQ.
  EIMSK |= _BV(INT0);   // turn on IRQ
}

static inline void pwr_irq_disable(void) {
  EIMSK &= ~_BV(INT0);   // turn off IRQ
}

#elif CONFIG_HARDWARE_VARIANT == 3
/* ---------- Hardware configuration: Arduino with low power sleep---------- */
#  define HEX_HSK_DDR         DDRD
#  define HEX_HSK_OUT         PORTD
#  define HEX_HSK_IN          PIND
#  define HEX_HSK_PIN         _BV(PIN3)

#  define HEX_BAV_DDR         DDRD
#  define HEX_BAV_OUT         PORTD
#  define HEX_BAV_IN          PIND
#  define HEX_BAV_PIN         _BV(PIN2)

#  define HEX_DATA_DDR        DDRC
#  define HEX_DATA_OUT        PORTC
#  define HEX_DATA_IN         PINC
#  define HEX_DATA_PIN        (_BV(PIN0) | _BV(PIN1) | _BV(PIN2) | _BV(PIN3))

#  define LED_BUSY_DDR        DDRD
#  define LED_BUSY_OUT        PORTD
#  define LED_BUSY_PIN        _BV(PIN7)


// PB.0/.1 which are SDcard detect and WP for non-Arduino build are
// repurposed in the Arduino build to be a software serial port using
// the SoftwareSerial library.


static inline void board_init(void) {
}


static inline void wakeup_pin_init(void) {
  DDRD &= ~_BV(PIN2);
}

#define POWER_MGMT_HANDLER  INT0_vect

static inline void pwr_irq_enable(void) {
  EICRA |= _BV(ISC01);  // trigger power enable on falling IRQ.
  EIMSK |= _BV(INT0);   // turn on IRQ
}

static inline void pwr_irq_disable(void) {
  EIMSK &= ~_BV(INT0);   // turn off IRQ
}


#elif CONFIG_HARDWARE_VARIANT == 4

/* ---------- Hardware configuration: Old HEXTIr Arduino ---------- */
#  define HEX_HSK_DDR         DDRD
#  define HEX_HSK_OUT         PORTD
#  define HEX_HSK_IN          PIND
#  define HEX_HSK_PIN         _BV(PIN3)

#  define HEX_BAV_DDR         DDRD
#  define HEX_BAV_OUT         PORTD
#  define HEX_BAV_IN          PIND
#  define HEX_BAV_PIN         _BV(PIN7)

#  define HEX_DATA_DDR        DDRC
#  define HEX_DATA_OUT        PORTC
#  define HEX_DATA_IN         PINC
#  define HEX_DATA_PIN        (_BV(PIN0) | _BV(PIN1) | _BV(PIN2) | _BV(PIN3))

#  define LED_BUSY_DDR        DDRD
#  define LED_BUSY_OUT        PORTD
#  define LED_BUSY_PIN        _BV(PIN2)


#  define HAVE_SD
#  define SD_CHANGE_HANDLER     ISR(PCINT0_vect)
#  define SD_SUPPLY_VOLTAGE     (1L<<21)

/* 250kHz slow, 2MHz fast */
#  define SPI_DIVISOR_SLOW 64
#  define SPI_DIVISOR_FAST 8

static inline void sdcard_interface_init(void) {
  DDRB  &= ~_BV(PB0);  // wp
  PORTB |=  _BV(PB0);
  DDRB  &= ~_BV(PB1);  // detect
  PORTB |=  _BV(PB1);
  PCICR |= _BV(PCIE0);
  //EICRB |=  _BV(ISC60);
  PCMSK0 |= _BV(PCINT0);
  //EIMSK |=  _BV(INT6);
}

static inline uint8_t sdcard_detect(void) {
  return !(PINB & _BV(PIN1));
}

static inline uint8_t sdcard_wp(void) {
  return PINB & _BV(PIN0);
}


static inline void board_init(void) {
}

static inline void wakeup_pin_init(void) {
  ;
}

#else
#  error "CONFIG_HARDWARE_VARIANT is unset or set to an unknown value."
#endif


/* ---------------- End of user-configurable options ---------------- */

#ifndef SYSTEM_TICK_HANDLER

static inline void timer_config(void) {
  /* Set up a 100Hz interrupt using timer 0 */
  TCCR0A = _BV(WGM01);
  TCCR0B = _BV(CS02) | _BV(CS00);
  OCR0A  = F_CPU / 1024 / 100 - 1;
  TCNT0  = 0;
  TIMSK0 |= _BV(OCIE0A);
}

#define SYSTEM_TICK_HANDLER ISR(TIMER0_COMPA_vect)

#endif

static inline void leds_init(void) {
  LED_BUSY_DDR |= LED_BUSY_PIN;
}

static inline __attribute__((always_inline)) void set_led(uint8_t state) {
  if (state)
    LED_BUSY_OUT |= LED_BUSY_PIN;
  else
    LED_BUSY_OUT &= ~LED_BUSY_PIN;
}

static inline void toggle_led(void) {
  LED_BUSY_OUT ^= LED_BUSY_PIN;
}

static inline void leds_sleep(void) {
  LED_BUSY_OUT &= ~LED_BUSY_PIN;
  LED_BUSY_DDR |= LED_BUSY_PIN;
}


#ifndef ARDUINO

/* An interrupt for detecting card changes implies hotplugging capability */
#if defined(SD_CHANGE_HANDLER) || defined (CF_CHANGE_HANDLER)
#  define HAVE_HOTPLUG
#endif

/* ----- Translate CONFIG_ADD symbols to HAVE symbols ----- */
/* By using two symbols for this purpose it's easier to determine if */
/* support was enabled by default or added in the config file.       */
#if defined(CONFIG_ADD_SD) && !defined(HAVE_SD)
#  define HAVE_SD
#endif

/* Hardcoded maximum - reducing this won't save any ram */
#define MAX_DRIVES 8

/* SD access LED dummy */
#ifndef HAVE_SD_LED
# define set_sd_led(x) do {} while (0)
#endif

#endif // build-using-arduino

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#endif /*CONFIG_H*/
