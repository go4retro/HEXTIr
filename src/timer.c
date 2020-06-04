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

    timer.c: System timer (and LED enabler)

*/

#include "config.h"
#include "integer.h"
#include "led.h"

#include "timer.h"

volatile tick_t ticks;

#ifdef INCLUDE_POWERMGMT
extern volatile uint8_t led_pwr_enable;  // this volatile transitions to 0 before we sleep, and ffh when not sleeping.
#else
#define led_pwr_enable  0xff             // w no power management, just always do busy led when active.
#endif

/* The main timer interrupt */
SYSTEM_TICK_HANDLER {
  uint8_t state;

  ticks++;

  state = get_led_state();
  if (state & LED_ERROR) {
    if ((ticks & 15) == 0)
      toggle_led();
  } else {
    set_led((state & LED_BUSY) & led_pwr_enable );
  }
}


void timer_init(void) {
  timer_config();
  //set_error_led(TRUE);  //Just to test LED...
}
