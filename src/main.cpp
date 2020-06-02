
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

    main.c: Main application /or  hexbus.ino - used for building with arduino (same file).
*/

#include <stddef.h>
#include <string.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <util/delay.h>
#include "config.h"
#include "drive.h"
#include "hexbus.h"
#include "hexops.h"
#include "led.h"
#include "printer.h"
#include "rtc.h"
#include "serial.h"
#include "timer.h"
#include "registry.h"

#ifdef ARDUINO

#include <Arduino.h>

#else

#include "diskio.h"

FATFS fs;

#endif

#include "uart.h"

extern uint8_t buffer[BUFSIZE];


// Our registry of installed devices, built during initialization.
REGISTRY  registry;

// Eventually, this is configuration info that will be in EEPROM, in some form, I think...
// so the #ifdef's would disappear... the system will have a means of informing which group(s)
// of peripherals are supported. (probably something like a 8 bit mask because I do not see
// the system supporting more than maybe 6 unique functions at this time.  Each bit flag would
// indicate the group being supported; then device address for each group can be defaulted during
// the build process, and configured via special commands at an address that is always supported.
//  DRIVE GROUP 0 is always supported.
//  other groups may be optionally included in the build.

static uint8_t device_address[ MAX_REGISTRY ] = {
  DEFAULT_DRIVE,   // periph 0
  DEFAULT_PRINTER, // periph 1
  DEFAULT_SERIAL,  // periph 2
  DEFAULT_CLOCK,   // periph 3
  NO_DEV,          // periph 4
  NO_DEV           // periph 5
};


// Bitmask of supported groups : 1 = drive, etc.
static const uint8_t supported_groups PROGMEM = {
      SUPPORT_DRV
    | SUPPORT_PRN
    | SUPPORT_SER
    | SUPPORT_RTC
//additional group functions may be added later for periph 4 and 5.
};


// TODO should be okaced in powermgmt.c/.h/etc.
#ifdef INCLUDE_POWERMGMT

static void wakeUp(void)
{
  sleep_disable();
  power_all_enable();
  detachInterrupt(0);
}


// Power use reduction
static void sleep_the_system( void )
{
  // attach interrupt for wakeup to D2
  attachInterrupt(0, wakeUp, LOW );
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


/*
 * hex_unsupported() should be used for any command on any device
 * where we provide no support for that command.
 */
static uint8_t hex_unsupported(pab_t pab) {
  hex_eat_it(pab.datalen, HEXSTAT_UNSUPP_CMD );
  return HEXERR_BAV;
}

static uint8_t hex_null( __attribute__((unused)) pab_t pab ) {
  hex_release_bus();
  while (!hex_is_bav() )  // wait for BAV back high, ignore any traffic
    ;
  return HEXERR_SUCCESS;
}

/*
   hex_reset_bus() -
   This command is normally used with device code zero, and must actually
   do something if files are open (or printer is open).
   Close any open files to ensure they are sync'd properly.  Close the printer if
   open, and ensure our file lun tables are reset.
   There is NO response to this command.
*/
static uint8_t hex_reset_bus(pab_t pab) {

  uart_putc(13);
  uart_putc(10);
  uart_putc('R');

  // We ONLY do all devices if the command is directed to device 0.
  if ( pab.dev == 0 ) {
    drv_reset();
    prn_reset();
    ser_reset();
    rtc_reset();
  }
  // release the bus ignoring any further action on bus. no response sent.
  hex_finish();
  // wait here while bav is low
  while ( !hex_is_bav() ) {
    ;
  }
  return HEXERR_SUCCESS;
}



static void execute_command(pab_t pab)
{
  cmd_proc  handler;
  uint8_t   *op;
  uint8_t   i = 0;
  uint8_t   j;
  uint8_t   cmd;

  // Parse the registry.  If incoming PAB has device code 0, start
  // at index 0 in the registry as those are the handlers for that
  // device code.

  // If the incoming device code is NOT 0, then start at index 1
  // and proceed forward.  
  // If no handler is found, IGNORE the command, unless the 
  // device code within the PAB IS found to be in our registry.  
  // If it is found, then we'll use the "unsupported" command 
  // default handler.
  if ( pab.dev != 0 ) {
    i++;
  }
  
  while ( i < registry.num_devices ) {
    // does the incoming PAB have a device in this group in the registry?
    if ( ( registry.entry[ i ].device_code_start <= pab.dev ) && 
         ( registry.entry[ i ].device_code_end >= pab.dev ) )
    {
      // If so...
      // this entry will handle our device code. 
      // Search for a matching command index now.
      op = registry.entry[ i ].command;
      j = 0;
      // Find a matching command code in this device's table, if present
      do 
      {
        cmd = pgm_read_byte( &op[j] );
        j++;
      } while ( ( cmd != HEXCMD_INVALID_MARKER ) && cmd != pab.cmd );
      // If we found the command, we have the index to the operations routine
      if ( cmd == pab.cmd ) {
        // found it!        
        j--;  // here's the cmd index
        // fetch the handler for this command for this device group.
        handler = (cmd_proc)pgm_read_word( &registry.entry[ i ].operation[ j ] );        
        (handler)( pab );
        // and exit the command processor
        return;
      }
      // If we have a supported device but not a supported command...
      hex_unsupported(pab);
      // we report and unsupported command and return
      return;
    }
    // otherwise; let's move to possibly the next device group in the registry
    i++;
  }
  // If the device is not supported at all, then treat as a null. 
  // Release the HSK line and simply wait for BAV to go high indicating end of message.
  // This is the best we can do, as someone else may be acting on this message.
  hex_null(pab);
  return;
}

//
// Default registry for global bus support (device 0).
//
static const cmd_proc fn_table[] PROGMEM = {
  hex_reset_bus,
  hex_null,
  NULL
};

static const uint8_t op_table[] PROGMEM = {
  HEXCMD_RESET_BUS,
  HEXCMD_NULL,
  HEXCMD_INVALID_MARKER // mark end of this table.
};


void setup_registry(void)
{
  registry.num_devices = 1;
  registry.entry[ 0 ].device_code_start = 0;
  registry.entry[ 0 ].device_code_end = 255;
  registry.entry[ 0 ].operation = (cmd_proc *)&fn_table;
  registry.entry[ 0 ].command = (uint8_t *)&op_table;

  // Register any configured peripherals.  if the peripheral type is not included, the call is to an empty routine.
  drv_register();
  prn_register();
  ser_register();
  rtc_register();
  return;
}


#ifndef ARDUINO
// Non-Arduino makefile entry point
int main(void) __attribute__((OS_main));
int main(void) { 
#else
// Arduino entry for running system
void loop(void) { // Arduino main loop routine.

#endif   // arduino

  // Variables used common to both Arduino and makefile builds
  uint8_t i = 0;
  uint8_t ignore_cmd = FALSE;
  pab_raw_t pabdata;
  BYTE res;

#ifndef ARDUINO

 // setup stuff for main
  board_init();
  hex_init();
  uart_init();
  disk_init();
  leds_init();
  timer_init();
  device_hw_address_init();
  drv_init();
  ser_init();
  rtc_init();
  prn_init();
  
  sei();

  res = f_mount(1, &fs);
  
#endif

  setup_registry();

  pabdata.pab.cmd = 0;
  pabdata.pab.lun = 0;
  pabdata.pab.record = 0;
  pabdata.pab.buflen = 0;
  pabdata.pab.datalen = 0;

  uart_puts_P(PSTR("Device ID: 0x"));
  uart_puthex(device_hw_address());
  uart_putcrlf();

  while (TRUE) {

#ifdef ARDUINO
    set_busy_led( FALSE );
    timer_check(1);
#endif

    while (hex_is_bav()) {

#ifdef ARDUINO
      timer_check(0);
#endif

#ifdef INCLUDE_POWERMGMT
      sleep_the_system();  // sleep until BAV falls. If low, HSK will be low.
#endif

    }

    uart_putc('^');
    
#ifdef ARDUINO
    set_busy_led( TRUE );
    timer_check(1);
#endif

    while (!hex_is_bav()) {

      while ( i < 9 ) {
        pabdata.raw[ i ] = i;
        res = receive_byte( &pabdata.raw[ i ] );

        if ( res == HEXERR_SUCCESS ) {
          i++;
        } else {
          ignore_cmd = TRUE;
          i = 9;
          hex_release_bus();
        }
      }

      if ( !ignore_cmd ) {
        if ( !( ( pabdata.pab.dev == 0 ) ||
                ( pabdata.pab.dev == device_address[ DRIVE_GROUP ] )
#ifdef INCLUDE_PRINTER
                ||
                (( pabdata.pab.dev == device_address[ PRINTER_GROUP ] ) )
#endif
#ifdef INCLUDE_CLOCK
                ||
                (( pabdata.pab.dev == device_address[ CLOCK_GROUP ] ) )
#endif
#ifdef INCLUDE_SERIAL
                ||
                (( pabdata.pab.dev == device_address[ SERIAL_GROUP ] ) )
#endif 
              )
           )
        {
          ignore_cmd = TRUE;
        }
      }

      if ( !ignore_cmd ) {
        if (i == 9) {
          // exec command
          uart_putc(13);
          uart_putc(10);
#ifdef ARDUINO
          timer_check(1);
          /*
             If we are attempting to use the SD card, we
             initialize it NOW.  If it fails (no card present)
             or other reasons, the various SD file usage commands
             will be failed with a device-error, simply by
             testing the sd_initialized flag as needed.
          */
          if ( pabdata.pab.dev == device_address[ DRIVE_GROUP ] ) {
            drv_start();
          }
#endif

          if ( pabdata.pab.dev == 0 && pabdata.pab.cmd != HEXCMD_RESET_BUS ) {
            pabdata.pab.cmd = HEXCMD_NULL; // change out to NULL operation and let bus float.
          }
          execute_command( pabdata.pab );
          ignore_cmd = TRUE;  // in case someone sends more data, ignore it.
        }
      } else {
        uart_putc('%');
        uart_puthex(pabdata.raw[0]);
        i = 0;
        hex_release_bus();
        while (!hex_is_bav() )  // wait for BAV back high, ignore any traffic
          ;
        ignore_cmd = FALSE;
      }
    }

    uart_putc(13);
    uart_putc(10);
    i = 0;
    ignore_cmd = FALSE;
  }
}
