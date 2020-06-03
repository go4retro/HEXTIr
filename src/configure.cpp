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

    configure.cpp: Configuration peripheral device functions.
    by s.reid
*/
#include "config.h"
#include "hexbus.h"
#include "hexops.h"
#include "timer.h"

#include "configure.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif
// Global references
extern uint8_t buffer[BUFSIZE];
//
extern uint8_t what_is_the_support_mask(void);


/*
   hex_cfg_getmask() -
   returns the current configuration mask indicating which peripheral
   groups are currently supported.
*/
static uint8_t hex_cfg_getmask(pab_t pab) {
  if ( !hex_is_bav() ) {
    // we should only be asked to send one byte.
    if ( pab.buflen == 1 ) {

      transmit_word( 1 );
      transmit_byte( what_is_the_support_mask() );
      transmit_byte( HEXSTAT_SUCCESS );
      hex_finish();
    } else {
      hex_send_final_response( HEXSTAT_BUF_SIZE_ERR );
    }
    return HEXERR_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}


/*
   hex_cfg_set() -
   receives a set of options consisting of a string buffer with option
   codes and new device numbers.  Parse and check for correct range of
   valid device numbers for a given group. Settings are separated by
   commas...
   "D=100,P=12,S=20,C=233" : sets DISK DRIVE to device 100, Printer
   to device 12, serial port to device 20, and clock to device 233.

   This assumes the support mask has bit 0 set to 1 (disk group),
   bit 1 set to 1, printer group, bit 2 set to 1, serial group, and
   bit 3 set to 1, clock group.  bit 7 will be the configuration device
*/
static uint8_t hex_cfg_set(pab_t pab) {
  // need to fetch our options.  then, parse, and set the new device
  // addresses, unless ANY of them fall outside the valid range for
  // the group being configured.
  if ( !hex_is_bav() ) {
    hex_send_final_response( HEXSTAT_UNSUPP_CMD );
    return HEXERR_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}


static uint8_t hex_cfg_get(pab_t pab) {
  if ( !hex_is_bav() ) {
    hex_send_final_response( HEXSTAT_UNSUPP_CMD );
    return HEXERR_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}


static uint8_t hex_cfg_reset( __attribute__((unused)) pab_t pab) {

  hex_finish();
  // wait here while bav is low
  while ( !hex_is_bav() ) {
    ;
  }
  return HEXERR_SUCCESS;
}


/*
   Command handling registry for device
*/

// Peripheral (3rd party) specific command codes.
#define HEXCMD_GETMASK     202
#define HEXCMD_READCFG     203
#define HEXCMD_SETCFG      204

static const cmd_proc fn_table[] PROGMEM = {
  hex_cfg_getmask,
  hex_cfg_get,
  hex_cfg_set,
  hex_cfg_reset,
  NULL // end of table.
};


static const uint8_t op_table[] PROGMEM = {
  HEXCMD_GETMASK,
  HEXCMD_READCFG,
  HEXCMD_SETCFG,
  HEXCMD_RESET_BUS,
  HEXCMD_INVALID_MARKER
};


void cfg_register(registry_t *registry) {
  uint8_t i = registry->num_devices;

  registry->num_devices++;
  registry->entry[ i ].device_code_start = CFG_DEV;
  registry->entry[ i ].device_code_end = CFG_DEV;
  registry->entry[ i ].operation = (cmd_proc *)&fn_table;
  registry->entry[ i ].command = (uint8_t *)&op_table;
  return;
}

void cfg_reset( void )
{
  return;
}

void cfg_init( void )
{
  return;
}
