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

    led.c: handling

*/

#include "config.h"
#include "led.h"

volatile uint8_t led_state;

inline __attribute__((always_inline)) void set_error_led(uint8_t state) {
  if (state) {
    led_state |= LED_ERROR;
  } else {
    led_state &= ~LED_ERROR;
  }
}

inline __attribute__((always_inline)) void set_busy_led(uint8_t state) {
  if (state) {
    led_state |= LED_BUSY;
  } else {
    led_state &= ~LED_BUSY;
  }
}

inline __attribute__((always_inline)) uint8_t get_led_state(void) {
  return led_state;
}

