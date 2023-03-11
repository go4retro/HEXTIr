
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

    main.c: Main application
*/

#include <stddef.h>
#include <string.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#include "config.h"
#include "clock.h"
#include "debug.h"
#include "drive.h"
#include "eeprom.h"
#include "hexbus.h"
#include "hexops.h"
#include "led.h"
#include "printer.h"
#include "powermgmt.h"
#include "registry.h"
#include "rtc.h"
#include "serial.h"
#include "swuart.h"
#include "timer.h"
#include "uart.h"

// Our registry of installed devices, built during initialization.
registry_t  registry;

/*
   hex_reset_bus() -
   This command is normally used with device code zero, and must actually
   do something if files are open (or printer is open).
   Close any open files to ensure they are sync'd properly.  Close the printer if
   open, and ensure our file lun tables are reset.
   There is NO response to this command.
*/
static void hex_reset_bus(pab_t *pab __attribute__((unused))) {

  debug_puts_P("Reset Bus\r\n");

  // We ONLY do all devices if the command is directed to device 0.
  drv_reset();
  prn_reset();
  ser_reset();
  clk_reset();
  // release the bus ignoring any further action on bus. no response sent.
  hex_finish();
  // wait here while bav is low
  while ( !hex_is_bav() ) {
    ;
  }
}


static void execute_command(pab_t *pab) {
  cmd_proc  handler;
  cmd_op_t  *op;
  uint8_t   i = 0;
  uint8_t   j;
  uint8_t   cmd;

  while ( i < registry.num_devices ) {
    // does the incoming PAB have a device in this group in the registry?
    if ( registry.entry[ i ].dev_cur == pab->dev ) {
      // If so...
      // this entry will handle our device code.
      // Search for a matching command index now.
      op = registry.entry[ i ].oplist;
      j = 0;
      // Find a matching command code in this device's table, if present
      do {
        cmd = pgm_read_byte( &op[j].command );
        j++;
      } while ( ( cmd != HEXCMD_INVALID_MARKER ) && cmd != pab->cmd );
      // If we found the command, we have the index to the operations routine
      if ( cmd == pab->cmd ) {
        // found it!
        j--;  // here's the cmd index
        // fetch the handler for this command for this device group.
        handler = (cmd_proc)pgm_read_word( &op[j].operation );
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
}


//
// Default registry for global bus support (device 0).
//
static const cmd_op_t ops[] PROGMEM = {
                                       {HEXCMD_RESET_BUS, hex_reset_bus},
                                       {HEXCMD_NULL, hex_null},
                                       {HEXCMD_INVALID_MARKER, NULL}
                                      };


static inline void bus_init(void) {
  reg_add(DEV_ALL, DEV_ALL, DEV_ALL, ops);
}


/*
   setup() - In Arduino, this will be run once automatically.
   Building non-Arduino, we'll call it once at the beginning
   of the main() function.
*/

void setup(void) {
  board_init();
  pwr_init();
  debug_init();

  sei();

  ee_get_config();

  disk_init();
  hex_init();
  leds_init();
  timer_init();
  bus_init();
  clk_init();
  drv_init();
  ser_init();
  prn_init();

  wakeup_pin_init();
}


int main(void) __attribute__((OS_main));
//int  __attribute__ ((noreturn)) main(void);
int main(void) {

  // Variables used common to both Arduino and makefile builds
  uint8_t i = 0;
  uint8_t ignore_cmd = FALSE;
  pab_raw_t pabdata;
  hexerror_t res;
  rtc_type_t rtc_type;
#ifdef HAVE_HOTPLUG
  uint8_t disk_state_old = 0;
#endif

  setup();

  rtc_type = rtc_get_type();

  pabdata.pab.cmd = 0;
  pabdata.pab.lun = 0;
  pabdata.pab.record = 0;
  pabdata.pab.buflen = 0;
  pabdata.pab.datalen = 0;

  debug_puts_P("\r\n" TOSTRING(CONFIG_HARDWARE_NAME) " Version: " VERSION);
  debug_putcrlf();

  while (TRUE) {

    set_busy_led( FALSE );

    while (hex_is_bav()) {
      // sleep until BAV falls. If low, HSK will be low.(if power management enabled, if not this is nop)
      if(rtc_type != RTC_TYPE_SW) {  // can't sleep if RTC is SW
        if(ser_is_open() || prn_is_open()) { // snooze
          if(!uart_data_tosend() && !swuart_data_tosend()) {
            pwr_sleep(SLEEP_IDLE);
          }
        } else {
          pwr_sleep(SLEEP_STANDBY);
        }
      }
    }

#ifdef INCLUDE_POWERMGMT
    // BAV low woke us up. Wait to see if we
    // get a HSK low, if so, drop our HSK and then proceed.
    // We do this here, because HSK must be held low after transmitter pulls it low
    // within a very short window of time (< 8us).
    hex_capture_hsk();
#endif

    debug_putc('^');
    set_busy_led( TRUE ); // TODO why do we set busy led here?

    while (!hex_is_bav()) {

      while ( i < 9 ) {
        pabdata.raw[ i ] = i;
        res = hex_recv_byte( &pabdata.raw[ i ] );
#ifdef HAVE_HOTPLUG
        // Since we defer mounting the drive until a file is requested,
        // this state may exist for some time.
        if(disk_state_old != disk_state)
          /* This seems to be a nice point to handle card changes */
          switch(disk_state) {
          case DISK_CHANGED:
          case DISK_REMOVED:
            /* If the disk was changed the buffer contents are useless */
            // we need to clean out all disk buffers
            //free_multiple_buffers(FMB_ALL);
            //change_init();
            //fatops_init(0);
            drv_init();
            debug_putc('D');
            break;
          case DISK_ERROR:
          default:
            break;
          }
        disk_state_old = disk_state;
#endif


        if ( res == HEXERR_SUCCESS ) {
          i++;
        } else {
          ignore_cmd = TRUE;
          i = 9;
          hex_release_bus();
        }
      }

      if ( !ignore_cmd ) {
        ignore_cmd = TRUE;
        for (uint8_t j = 0; j < registry.num_devices; j++) {
          if(registry.entry[j].dev_cur == pabdata.pab.dev) {
            // we should cache the index...
            ignore_cmd = FALSE;
            break;
          }
        }
      }
      if ( !ignore_cmd ) {
        if (i == 9) {
          // exec command
          debug_putcrlf();
          if ( pabdata.pab.dev == 0 && pabdata.pab.cmd != HEXCMD_RESET_BUS ) {
            pabdata.pab.cmd = HEXCMD_NULL; // change out to NULL operation and let bus float.
          }
          execute_command( &(pabdata.pab) );
          ignore_cmd = TRUE;  // in case someone sends more data, ignore it.
        }
      } else {
        debug_putc('%');
        debug_puthex(pabdata.raw[0]);
        i = 0;
        hex_release_bus();
        while (!hex_is_bav() )  // wait for BAV back high, ignore any traffic
          ;
        ignore_cmd = FALSE;
      }
    }

    debug_putcrlf();
    i = 0;
    ignore_cmd = FALSE;
  }
}
