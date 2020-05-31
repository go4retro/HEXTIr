/*
    HEXTIr-SD - Texas Instruments HEX-BUS SD Mass Storage Device
    Copyright Jim Brain and RETRO Innovations, 2017

    This code is a modification of the file from the following project:

    sd2iec - SD/MMC to Commodore serial bus interface/controller
    Copyright (C) 2007-2017  Ingo Korb <ingo@akana.de>

    Inspired by MMC2IEC by Lars Pontoppidan et al.

    FAT filesystem access based on code from ChaN and Jim Brain, see ff.c|h.

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

    spi.h: Definitions for the low-level SPI routines - AVR version

*/
#ifndef SPI_H
#define SPI_H
#ifndef ARDUINO

#include "config.h"

/* SPI and I2C */
#if defined __AVR_ATmega32__    \
 || defined __AVR_ATmega644__   \
 || defined __AVR_ATmega644P__  \
 || defined __AVR_ATmega1284P__

#  define SPI_PORT   PORTB
#  define SPI_DDR    DDRB
#  define SPI_SS     _BV(PB4)
#  define SPI_MOSI   _BV(PB5)
#  define SPI_MISO   _BV(PB6)
#  define SPI_SCK    _BV(PB7)
#  define HWI2C_PORT PORTC
#  define HWI2C_SDA  _BV(PC1)
#  define HWI2C_SCL  _BV(PC0)

#elif defined __AVR_ATmega128__ || defined __AVR_ATmega1281__ || defined __AVR_ATmega2561__

#  define SPI_PORT  PORTB
#  define SPI_DDR   DDRB
#  define SPI_SS    _BV(PB0)
#  define SPI_SCK   _BV(PB1)
#  define SPI_MOSI  _BV(PB2)
#  define SPI_MISO  _BV(PB3)
#  define HWI2C_PORT PORTD
#  define HWI2C_SDA  _BV(PD1)
#  define HWI2C_SCL  _BV(PD0)

#elif defined __AVR_ATmega48__ || defined __AVR_ATmega88__ || defined __AVR_ATmega168__ || defined __AVR_ATmega328__ || defined __AVR_ATmega328P__

#  define SPI_PORT   PORTB
#  define SPI_DDR    DDRB
#  define SPI_SS     _BV(PB2)
#  define SPI_SCK    _BV(PB5)
#  define SPI_MOSI   _BV(PB3)
#  define SPI_MISO   _BV(PB4)
#  define HWI2C_PORT PORTC
#  define HWI2C_SDA  _BV(PC4)
#  define HWI2C_SCL  _BV(PC5)

#else
#  error Unknown chip! (SPI/TWI)
#endif

#define SPI_MASK (SPI_SS|SPI_MOSI|SPI_MISO|SPI_SCK)

/* Low speed 400kHz for init, fast speed <=20MHz (MMC limit) */
typedef enum { SPI_SPEED_FAST, SPI_SPEED_SLOW } spi_speed_t;

/* Available SPI devices - special case to select all SD cards during init */
/* Note: SD cards must be 1 and 2 */
/* AVR note: The code assumes that spi_device_t can be used as bit field of selected cards */
typedef enum { SPIDEV_NONE     = 0,
               SPIDEV_CARD0    = 1,
               SPIDEV_CARD1    = 2,
               SPIDEV_ALLCARDS = 3 } spi_device_t;

/* Initialize SPI interface */
void spi_init(spi_speed_t speed);

/* SD SS pin default implementation */
static inline __attribute__((always_inline)) void sdcard_set_ss(uint8_t state) {
  if (state)
    SPI_PORT |= SPI_SS;
  else
    SPI_PORT &= ~SPI_SS;
}

/* select device */
static inline void spi_select_device(spi_device_t dev) {
  if (dev & 1)
    sdcard_set_ss(0);
  else
    sdcard_set_ss(1);
#ifdef CONFIG_TWINSD
  if (dev & 2)
    sdcard2_set_ss(0);
  else
    sdcard2_set_ss(1);
#endif
}

/* Transmit a single byte */
void spi_tx_byte(uint8_t data);

/* Exchange a data block - internal API only! */
void spi_exchange_block(void *data, unsigned int length, uint8_t write);

/* Receive a data block */
static inline void spi_tx_block(const void *data, unsigned int length) {
  spi_exchange_block((void *)data, length, 0);
}

/* Receive a single byte */
uint8_t spi_rx_byte(void);

/* Receive a data block */
static inline void spi_rx_block(void *data, unsigned int length) {
  spi_exchange_block(data, length, 1);
}

/* Switch speed of SPI interface */
void spi_set_speed(spi_speed_t speed);

#endif
#endif
