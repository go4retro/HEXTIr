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

#include "config.h"
#include "hexbus.h"
#include "powermgmt.h"

#ifdef INCLUDE_POWERMGMT

POWER_MGMT_HANDLER {
  sleep_disable();
  //power_all_enable();
  pwr_irq_disable();
  power_spi_enable();
  power_timer0_enable();
  power_timer2_enable();
}

// Power use reduction
void pwr_sleep( sleep_mode_t mode ) {
  switch (mode) {
    case SLEEP_IDLE:
      power_spi_disable();
      power_timer0_disable();
      power_timer2_disable();
      set_sleep_mode( SLEEP_MODE_IDLE );
      break;
    case SLEEP_STANDBY:
      set_sleep_mode( SLEEP_MODE_STANDBY );
      break;
    case SLEEP_PWR_DOWN:
      set_sleep_mode( SLEEP_MODE_PWR_DOWN );
      break;
  }
  cli();
  pwr_irq_enable();

  sleep_enable();
  // The sleep_bod_disable operation may not be available on all targets!!!
  sleep_bod_disable();
  sei();
  led_sleep();            // make sure LED is not lit when we sleep.
  sleep_cpu();

  return;
}


void pwr_init(void) {
  power_adc_disable();    // TODO move to common or init, as only needed once.
  power_twi_disable();    // TODO move to common or init, as only needed once.
  power_timer1_disable(); // TODO move to common or init, as only needed once.
}

#endif
