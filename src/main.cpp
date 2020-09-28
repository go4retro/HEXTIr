
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
#include "configure.h"
#include "debug.h"
#include "drive.h"
#include "eeprom.h"
#include "hexbus.h"
#include "hexops.h"
#include "led.h"
#include "printer.h"
#include "powermgmt.h"
#include "registry.h"
#include "serial.h"
#include "swuart.h"
#include "timer.h"
#include "uart.h"

config_t *config;

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
static void hex_reset_bus(pab_t *pab) {

  debug_puts_P("Reset Bus\n");

  // We ONLY do all devices if the command is directed to device 0.
  if ( pab->dev == 0 ) {
    drv_reset();
    prn_reset();
    ser_reset();
    clock_reset();
#ifdef USE_CFG_DEVICE
    cfg_reset();
#endif
  }
  // release the bus ignoring any further action on bus. no response sent.
  hex_finish();
  // wait here while bav is low
  while ( !hex_is_bav() ) {
    ;
  }
}


static void execute_command(pab_t *pab) {
  cmd_proc  handler;
#ifdef USE_NEW_OPTABLE
  cmd_op_t  *op;
#else
  uint8_t   *op;
#endif
  uint8_t   i = 0;
  uint8_t   j;
  uint8_t   cmd;

#ifndef NEW_REGISTER
  // Parse the registry.  If incoming PAB has device code 0, start
  // at index 0 in the registry as those are the handlers for that
  // device code.

  // If the incoming device code is NOT 0, then start at index 1
  // and proceed forward.
  // If no handler is found, IGNORE the command, unless the
  // device code within the PAB IS found to be in our registry.
  // If it is found, then we'll use the "unsupported" command
  // default handler.
  if ( pab->dev != 0 ) {
    i++;
  }
#endif

  while ( i < registry.num_devices ) {
    // does the incoming PAB have a device in this group in the registry?
#ifdef USE_NEW_OPTABLE
    if ( registry.entry[ i ].dev_cur == pab->dev ) {
#else
    if ( ( registry.entry[ i ].dev_low <= pab->dev ) &&
         ( registry.entry[ i ].dev_high >= pab->dev ) )
    {
#endif
      // If so...
      // this entry will handle our device code.
      // Search for a matching command index now.
#ifdef USE_NEW_OPTABLE
      op = registry.entry[ i ].oplist;
#else
      op = registry.entry[ i ].command;
#endif
      j = 0;
      // Find a matching command code in this device's table, if present
      do {
#ifdef USE_NEW_OPTABLE
        cmd = pgm_read_byte( &op[j].command );
#else
        cmd = pgm_read_byte( &op[j] );
#endif
        j++;
      } while ( ( cmd != HEXCMD_INVALID_MARKER ) && cmd != pab->cmd );
      // If we found the command, we have the index to the operations routine
      if ( cmd == pab->cmd ) {
        // found it!
        j--;  // here's the cmd index
        // fetch the handler for this command for this device group.
#ifdef USE_NEW_OPTABLE
        handler = (cmd_proc)pgm_read_word( &op[j].operation );
#else
        handler = (cmd_proc)pgm_read_word( &registry.entry[ i ].operation[ j ] );
#endif
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
#ifdef USE_NEW_OPTABLE
static const cmd_op_t ops[] PROGMEM = {
                                       {HEXCMD_RESET_BUS, hex_reset_bus},
                                       {HEXCMD_NULL, hex_null},
                                       {HEXCMD_INVALID_MARKER, NULL}
                                      };
#else
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
#endif


static inline void reg_init(void) {
#ifdef NEW_REGISTER
#ifdef USE_NEW_OPTABLE
  cfg_register(DEV_ALL, DEV_ALL, DEV_MAX, ops);
#else
  cfg_register(DEV_ALL, DEV_ALL, DEV_MAX, op_table, fn_table);
#endif
#else
  registry.num_devices = 1;
  registry.entry[ 0 ].dev_low = DEV_ALL;
  registry.entry[ 0 ].dev_cur = DEV_ALL;
  registry.entry[ 0 ].dev_high = DEV_MAX;
#ifdef USE_NEW_OPTABLE
  registry.entry[ 0 ].oplist = (cmd_op_t *)&ops;
#else
  registry.entry[ 0 ].operation = (cmd_proc *)&fn_table;
  registry.entry[ 0 ].command = (uint8_t *)&op_table;
#endif
#endif
#ifndef INIT_COMBO
#ifdef USE_CFG_DEVICE
  cfg_register1();
#endif
  drv_register();
  prn_register();
  ser_register();
  clock_register();
#endif
}


/*
   setup() - In Arduino, this will be run once automatically.
   Building non-Arduino, we'll call it once at the beginning
   of the main() function.
*/

void setup(void) {
  board_init();
  debug_init();
#  if defined INCLUDE_PRINTER || defined INCLUDE_SERIAL
  uart_init();
  swuart_init();
#  endif

  sei();

  disk_init();
  hex_init();
  leds_init();
  timer_init();
#ifdef INIT_COMBO
  reg_init();
#endif
  clock_init();
  drv_init();
  ser_init();
  prn_init();
#ifdef USE_CFG_DEVICE
  cfg_init(); // fetch our current settings from EEPROM if any (otherwise, the default RAM contents on reset apply)
#endif
  config = ee_get_config();

  wakeup_pin_init();
}


int main(void) __attribute__((OS_main));
int  __attribute__ ((noreturn)) main(void);
int main(void) {

  // Variables used common to both Arduino and makefile builds
  uint8_t i = 0;
  uint8_t ignore_cmd = FALSE;
  pab_raw_t pabdata;
  BYTE res;
#ifdef HAVE_HOTPLUG
  uint8_t disk_state_old = 0;
#endif

  setup();
#ifndef INIT_COMBO
  reg_init(); // this must be done first.
#endif

  pabdata.pab.cmd = 0;
  pabdata.pab.lun = 0;
  pabdata.pab.record = 0;
  pabdata.pab.buflen = 0;
  pabdata.pab.datalen = 0;

  debug_puts_P("\n" TOSTRING(CONFIG_HARDWARE_NAME) " Version: " VERSION);
  debug_putcrlf();

  while (TRUE) {

    set_busy_led( FALSE );

    while (hex_is_bav()) {
      sleep_the_system();  // sleep until BAV falls. If low, HSK will be low.(if power management enabled, if not this is nop)
    }

    debug_putc('^');
    set_busy_led( TRUE ); // TODO why do we set busy led here?

    while (!hex_is_bav()) {

      while ( i < 9 ) {
        pabdata.raw[ i ] = i;
        res = receive_byte( &pabdata.raw[ i ] );
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

#ifdef NEW_DEV_CHK
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
#else
      if ( !ignore_cmd ) {
       if ( !( ( pabdata.pab.dev == 0 ) ||
                ( pabdata.pab.dev DEV_DRV_START )
                || ( pabdata.pab.dev == DEV_CFG_START )
#ifdef INCLUDE_PRINTER
                ||
                (( pabdata.pab.dev == DEV_PRN_DEFAULT ) )
#endif
#ifdef INCLUDE_CLOCK
                ||
                (( pabdata.pab.dev == DEV_RTC_START ) )
#endif
#ifdef INCLUDE_SERIAL
                ||
                (( pabdata.pab.dev == DEV_SER_START ) )
#endif
              )
           )
        {
          ignore_cmd = TRUE;
        }
      }
#endif
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
