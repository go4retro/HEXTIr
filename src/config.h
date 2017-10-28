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
#include "autoconf.h"

/* ----- Common definitions for all AVR hardware variants ------ */

#ifdef CONFIG_UART_DEBUG
#define UART0_ENABLE
#endif

#ifdef CONFIG_UART_BUF_SHIFT
#define UART0_TX_BUFFER_SHIFT CONFIG_UART_BUF_SHIFT
#endif

#ifdef CONFIG_UART_BAUDRATE
#define UART0_BAUDRATE CONFIG_UART_BAUDRATE
#endif

#ifndef TRUE
#define FALSE                 0
#define TRUE                  (!FALSE)
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

#else
#  error "CONFIG_HARDWARE_VARIANT is unset or set to an unknown value."
#endif

/* ---------------- End of user-configurable options ---------------- */

#endif /*CONFIG_H*/
