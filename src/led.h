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

    led.h: Definitions for the LEDs

*/

#ifndef LED_H
#define LED_H
#ifdef __cplusplus
extern "C"{
#endif

#include "config.h"

/* LED-to-bit mapping */
// TODO make enum.
#define LED_ERROR      1
#define LED_BUSY       2

//extern volatile uint8_t led_state;

void set_error_led(uint8_t state);
void set_busy_led(uint8_t state);
uint8_t get_led_state(void);

#ifdef __cplusplus
} // extern "C"
#endif
#endif
