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

#if defined ARDUINO_AVR_UNO || defined ARDUINO_AVR_PRO
 #define CONFIG_HARDWARE_VARIANT   3 // Hardware variant 3 is Arduino, with BAV on D2 for wakeup from standby mode.
 // Variant 3 has been tested on Pro Mini, Uno, and Nano as functional.  Select target platform in the IDE.
#elif defined ARDUINO_AVR_MEGA2560
 #define CONFIG_HARDWARE_VARIANT   3 // this is for testing the build
 // Variant 3 has been tested on Pro Mini, Uno, and Nano as functional.  Select target platform in the IDE.
#endif

#ifndef ARDUINO

 #include "autoconf.h"
 #define MAX_OPEN_FILES 8
 
#else
 #define MAX_OPEN_FILES 4      // SD 1.0 and later let us have more than one open file.
 #define INCLUDE_PRINTER
 #define INCLUDE_CLOCK
 #define INCLUDE_SERIAL
 #define INCLUDE_POWERMGMT
#endif

/* ----- Common definitions  ------ */
// TODO these 5 should move to a standard hexdev.h or something, since they are defaults, and they should be the same for all
// boards.
#define PRINTER_DEV     12    // Device code to support a printer on HW serial (rx/tx) @115200,N,8,1
#define DISK_DEV       100    // Base disk device code we support
#define ANY_DEV        255    // used by this implementation to allow the same handler to be used by any/all devices, from the handler table.
#define RTC_DEV        233    // Device code to support DS3231 RTC on I2C/wire; A5/A4.
#define RS232_DEV       20    // Device code for RS-232 Serial peripheral using SW serial (def=300 baud)

#define MAX_DISKS        7    // Add to DISK_DEV to obtain highest address.  1 less than true number of max disks.

/* ----- Common definitions for all AVR hardware variants ------ */

/* Interrupt handler for system tick */
#define SYSTEM_TICK_HANDLER ISR(TIMER1_COMPA_vect)

#ifdef CONFIG_UART_DEBUG
#define UART0_ENABLE
#endif

#ifdef CONFIG_UART_BUF_SHIFT
#define UART0_TX_BUFFER_SHIFT CONFIG_UART_BUF_SHIFT
#endif

#ifdef CONFIG_UART_BAUDRATE
#define UART0_BAUDRATE CONFIG_UART_BAUDRATE
#endif

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

/* This allows the user to set the drive address to be 100-107 or 108-117) */
static inline uint8_t device_hw_address(void) {
  return 100 + !((PIND & (_BV(PIN4) | _BV(PIN5) | _BV(PIN6))) >> 4);
}

static inline void device_hw_address_init(void) {
  DDRD  &= ~(_BV(PIN4) | _BV(PIN5) | _BV(PIN6) | _BV(PIN7));
  PORTD |=  (_BV(PIN4) | _BV(PIN5) | _BV(PIN6) | _BV(PIN7));
}

static inline void leds_init(void) {
  DDRD |= _BV(PIN2);
}

static inline __attribute__((always_inline)) void set_led(uint8_t state) {
  if (state)
    PORTD |= _BV(PIN2);
  else
    PORTD &= ~_BV(PIN2);
}

static inline void toggle_led(void) {
  PORTD ^= _BV(PIN2);
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
#  define HEX_BAV_PIN         _BV(PIN7)

#  define HEX_DATA_DDR        DDRC
#  define HEX_DATA_OUT        PORTC
#  define HEX_DATA_IN         PINC
#  define HEX_DATA_PIN        (_BV(PIN0) | _BV(PIN1) | _BV(PIN2) | _BV(PIN3))

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

/* This allows the user to set the drive address to be 100-107 */
static inline uint8_t device_hw_address(void) {
  return 100 + !((PIND & (_BV(PIN4) | _BV(PIN5) | _BV(PIN6))) >> 4);
}

static inline void device_hw_address_init(void) {
  DDRD  &= ~(_BV(PIN4) | _BV(PIN5) | _BV(PIN6));
  PORTD |=  (_BV(PIN4) | _BV(PIN5) | _BV(PIN6));
}

static inline void leds_init(void) {
  DDRD |= _BV(PIN2);
}

static inline __attribute__((always_inline)) void set_led(uint8_t state) {
  if (state)
    PORTD |= _BV(PIN2);
  else
    PORTD &= ~_BV(PIN2);
}

static inline void toggle_led(void) {
  PORTD ^= _BV(PIN2);
}

static inline void board_init(void) {
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

#  define WAKEUP_PIN          2 // BAV on D2

// PB.0/.1 which are SDcard detect and WP for non-Arduino build are
// repurposed in the Arduino build to be a software serial port using
// the SoftwareSerial library.

/* This allows the user to set the drive address to be 100-107 or 108-117) */
static inline uint8_t device_hw_address(void) {
  return 100 + !((PIND & (_BV(PIN4) | _BV(PIN5) | _BV(PIN6))) >> 4);
}

static inline void device_hw_address_init(void) {
  DDRD  &= ~(_BV(PIN4) | _BV(PIN5) | _BV(PIN6));
  PORTD |=  (_BV(PIN4) | _BV(PIN5) | _BV(PIN6));
}

static inline void leds_init(void) {
  DDRD |= _BV(PIN7);
}

static inline __attribute__((always_inline)) void set_led(uint8_t state) {
  if (state)
    PORTD |= _BV(PIN7);
  else
    PORTD &= ~_BV(PIN7);
}

static inline void toggle_led(void) {
  PORTD ^= _BV(PIN7);
}

static inline void board_init(void) {
}

#else
#  error "CONFIG_HARDWARE_VARIANT is unset or set to an unknown value."
#endif


/* ---------------- End of user-configurable options ---------------- */
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

#endif /*CONFIG_H*/
