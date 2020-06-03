/*
 * power.cpp
 *
 *  Created on: Jun 3, 2020
 *      Author: brain
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

#ifndef ARDUINO

ISR(POWER_MGMT_HANDLER) {
  sleep_disable();
  power_all_enable();
  pwr_irq_disable();
}

#else
void wakeUp(void)
{
  sleep_disable();
  power_all_enable();
  detachInterrupt(0);
}
#endif


// Power use reduction
void sleep_the_system( void )
{
#ifdef ARDUINO
  // attach interrupt for wakeup to D2
  attachInterrupt(0, wakeUp, LOW );
#else
  pwr_irq_enable();
#endif
  set_sleep_mode( SLEEP_MODE_STANDBY ); // cuts measured current use in about half or so...
  cli();
  sleep_enable();
  // The sleep_bod_disable operation may not be available on all targets!!!
  sleep_bod_disable();
  sei();
  sleep_cpu();
  // BAV low woke us up. Wait to see if we
  // get a HSK low, if so, drop our HSK and then proceed.
  // We do this here, because HSK must be held low after transmitter pulls it low
  // within a very short window of time (< 8us).
  hex_capture_hsk();
  return;
}
#endif


