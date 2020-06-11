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
#include "configure.h"
#include "hexbus.h"
#include "led.h"
#include "timer.h"
#include "ff.h"
#include "drive.h"
#include "eeprom.h"
#include "serial.h"
#include "rtc.h"
#include "printer.h"
#include "configure.h"
#include "registry.h"

extern config_t * config;

#ifdef INCLUDE_CLOCK

#include <DS3231.h>
#include <SD.h>
#include <Wire.h>

extern DS3231 clock_peripheral;       // Our CLOCK : access via the HexBus at device code 230-239

void dateTime(uint16_t* theDate, uint16_t* theTime)
{
  unsigned int year;
  byte month,day,hour,minute,second;
  bool cen,pm;

  year = clock_peripheral.getYear() + 2000;
  month = clock_peripheral.getMonth( cen );
  day = clock_peripheral.getDate();
  hour = clock_peripheral.getHour(cen,pm);
  minute = clock_peripheral.getMinute();
  second = clock_peripheral.getSecond();
  
  *theDate = FAT_DATE(year, month, day);
  *theTime = FAT_TIME(hour, minute, second);

  return;
}

#endif

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
  drv_init();
  ser_init();
  rtc_init();
  prn_init();
  cfg_init();

  config = ee_get_config();

  sei();

#ifdef INCLUDE_PRINTER
  Serial.begin(115200);
  // Ensure serial initialized before proceeding.
  while (!Serial) {
    ;
  }
#endif

  wakeup_pin_init();
  
}
