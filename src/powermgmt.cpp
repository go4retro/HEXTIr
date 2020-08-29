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

    powermgmt.cpp: Power management routines
*/

#include <avr/interrupt.h>
#include <avr/power.h>
#include <avr/sleep.h>
#ifdef ARDUINO
#include <Arduino.h>
#endif

#include "config.h"
#include "hexbus.h"

#include "powermgmt.h"

#ifdef INCLUDE_POWERMGMT

volatile uint8_t  led_pwr_enable = 0;

ISR(POWER_MGMT_HANDLER) {
  sleep_disable();
  power_all_enable();
  pwr_irq_disable();
  led_pwr_enable = 0xff;
}

// Power use reduction
void sleep_the_system( void )
{
  pwr_irq_enable();
  set_sleep_mode( SLEEP_MODE_STANDBY ); // cuts measured current use in about half or so...
  cli();
  led_pwr_enable = 0;
  sleep_enable();
  // The sleep_bod_disable operation may not be available on all targets!!!
  sleep_bod_disable();
  sei();
  leds_sleep(); // make sure LED is not lit when we sleep.
  sleep_cpu();

  // BAV low woke us up. Wait to see if we
  // get a HSK low, if so, drop our HSK and then proceed.
  // We do this here, because HSK must be held low after transmitter pulls it low
  // within a very short window of time (< 8us).
  hex_capture_hsk();
  return;
}

#endif
