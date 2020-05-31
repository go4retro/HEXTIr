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

#include "led.h"
#include "timer.h"

volatile tick_t ticks;


/* The main timer interrupt */
#ifndef ARDUINO
ISR(TIMER1_COMPA_vect) {

  ticks++;

  if (led_state & LED_ERROR) {
    if ((ticks & 15) == 0)
      toggle_led();
  } else {
    set_led(led_state & LED_BUSY);
  }
}


void timer_init(void) {
  /* Count F_CPU/8 in timer 0 */
  TCCR0B = _BV(CS01);

  /* Set up a 100Hz interrupt using timer 1 */
  OCR1A  = F_CPU / 64 / 100 - 1;
  TCNT1  = 0;
  TCCR1A = 0;
  TCCR1B = _BV(WGM12) | _BV(CS10) | _BV(CS11);
  TIMSK1 |= _BV(OCIE1A);
}

#else

unsigned long t_time;

void timer_init(void) {
  t_time = millis() + 10;
  ticks = 0;
}

void timer_check(uint8_t flag) {
  unsigned long t = millis();
  if ( t > t_time || flag ) {
    t_time = t;
    ticks++;
    if (led_state & LED_ERROR) {
      if ((ticks & 15) == 0) {
        toggle_led();  // blink LED as error, 1 flash every 150 ms or so.
      }
    } else {
      set_led(led_state & LED_BUSY);
    }
  }
}

#endif
