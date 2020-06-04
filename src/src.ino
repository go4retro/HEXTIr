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

    src.ino - Stub to allow Arduino project to compile.
*/

#include "config.h"
#include "hexbus.h"
#include "led.h"
#include "timer.h"
#include "ff.h"
#include "drive.h"
#include "eeprom.h"
#include "serial.h"
#include "rtc.h"
#include "printer.h"
#include "registry.h"

extern config_t * config;

/*
   setup() - In Arduino, this will be run once automatically.
   Building non-Arduino, we'll call it once at the beginning
   of the main() function.
*/
void setup(void) {
  board_init();
  hex_init();
  leds_init();
  timer_init();
  device_hw_address_init();
  drv_init();
  ser_init();
  rtc_init();
  prn_init();
  config = ee_get_config();

  sei();

#ifdef INCLUDE_PRINTER
  Serial.begin(115200);
  // Ensure serial initialized before proceeding.
  while (!Serial) {
    ;
  }
#endif

#ifdef INCLUDE_POWERMGMT
  pinMode(WAKEUP_PIN, INPUT);
#endif

}
