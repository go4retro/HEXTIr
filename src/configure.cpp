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


#include <stddef.h>
#include <avr/pgmspace.h>
#include "config.h"
#include "hexbus.h"
#include "hexops.h"
#include "timer.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif
// Global references
extern uint8_t buffer[BUFSIZE];
//
// Eventually, this is configuration info that will be in EEPROM, in some form, I think...
// so the #ifdef's would disappear... the system will have a means of informing which group(s)
// of peripherals are supported. (probably something like a 8 bit mask because I do not see
// the system supporting more than maybe 6 unique functions at this time.  Each bit flag would
// indicate the group being supported; then device address for each group can be defaulted during
// the build process, and configured via special commands at an address that is always supported.
//  DRIVE GROUP 0 is always supported.
//  other groups may be optionally included in the build.
uint8_t device_address[ MAX_REGISTRY ] = {
  DEFAULT_DRIVE,   // periph 0
  DEFAULT_PRINTER, // periph 1
  DEFAULT_SERIAL,  // periph 2
  DEFAULT_CLOCK,   // periph 3
  NO_DEV,          // periph 4
  NO_DEV,          // periph 5
  NO_DEV,          // periph 6
  DEFAULT_CFGDEV,  // periph 7
};

static const uint8_t config_option[ MAX_REGISTRY - 1 ] PROGMEM = {
  'D', 'P', 'S', 'C', ' ', ' ', ' '
};

// Bitmask of supported groups : 1 = drive, etc.
static const uint8_t supported_groups PROGMEM = {
  SUPPORT_DRV
  | SUPPORT_PRN
  | SUPPORT_SER
  | SUPPORT_RTC
  //  | SUPPORT_CFG  // This group is NOT indicated in the supported configurable devices
  // additional group functions may be added later for periph 4, 5, and 6.  Periph 7 is reserved for cfg.
};

static const uint8_t low_device_address[ MAX_REGISTRY - 1 ] PROGMEM = {
  DRV_DEV,
  PRN_DEV,
  SER_DEV,
  RTC_DEV,
  0,
  0,
  0
};

static const uint8_t high_device_address[ MAX_REGISTRY - 1 ] PROGMEM = {
  MAX_DRV,
  MAX_PRN,
  MAX_SER,
  MAX_RTC,
  0,
  0,
  0
};


/*
    Make our supported group mask available to callers.
    This data (our supported groups) is built during compile time
    and stored in flash.  does not need to be in eeprom though.
*/
static inline uint8_t our_support_mask(void) {
  return (uint8_t)pgm_read_byte( &supported_groups );
}


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
      transmit_byte( our_support_mask() );
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
  char    *p = NULL;
  char    *s;
  uint16_t len = 0;
  uint8_t  addresses[MAX_REGISTRY - 1]; // we can parse up to 7 new addresses.
  uint8_t  ch;
  uint8_t  i;
  uint8_t  rc = HEXSTAT_SUCCESS;
  uint8_t  addr;
  uint8_t  change_mask = 0;

  len = 0;
  i = 0;
  while ( i < MAX_REGISTRY - 1 ) {
    addresses[ i++ ] = 0;
  }

  if ( hex_get_data(buffer,pab.datalen) == HEXSTAT_SUCCESS ) {
    len = pab.datalen;
    /* our "data" to parse begins at buffer[0] and is consists of 'len' bytes. */
    p = (char *)&buffer[0];
    s = p;
  } else {
    hex_release_bus();
    return HEXERR_BAV; // BAV ERR.
  }

  while ( (( (unsigned int)p - (unsigned int)s) < len ) && ( rc == HEXSTAT_SUCCESS )) {
    ch = *p++;
    if ( *p++ == '=' ) {
      ch &= ~0x20; // map to uppercase if lower.
      // Now, is this a valid option?
      i = 0;
      do
      {
        if ( ch == (uint8_t)pgm_read_byte( &config_option[ i ] )) {
          break;
        }
        i++;
      } while ( i < MAX_REGISTRY - 1 );

      if ( i >= MAX_REGISTRY - 1 ) {
        rc = HEXSTAT_OPTION_ERR; // bad or invalid option
      } else {
        // i is the index to our "group" now.
        // p should be pointing to digits that represent the new address.
        addr = 0;
        // parse digits to get new address; then check to see if the address is valid for
        // the selected device.  If so, mark the change mask and store the address in our
        // change array.
        // we do not change any addresses until the entire message is parsed.

        while ( isdigit( *p ) ) {
          addr = ( addr * 10 ) + ((*p++) - '0');
        }
        
        if ( *p == ',' ) {
          p++;
        } else if ( (( (unsigned int)p - (unsigned int)s) < len ) ) {
          rc = HEXSTAT_OPTION_ERR;
        }
        addresses[ i ] = addr; // new address
        change_mask |= (1 << i); // and our adress "group" number.
      }
    } else {
      rc = HEXSTAT_OPTION_ERR;
    }
    // Done parsing incoming options.  If no error, we can now see
    // if the addresses we were given are any good.
    if ( rc == HEXSTAT_SUCCESS ) {
      if ( change_mask != 0 ) {
        rc = our_support_mask() & change_mask; // check to ensure that we are not trying to set address on unsupported periperhals
        if ( rc == change_mask ) {
          rc = HEXSTAT_SUCCESS;
          // continue
          i = 0;
          do
          {
            if ( (1 << i) & change_mask ) {
              if ( addresses[ i ] >= (uint8_t)pgm_read_byte( &low_device_address[ i ] ) &&
                   addresses[ i ] <= (uint8_t)pgm_read_byte( &high_device_address[ i ] ) )
              {
                device_address[ i ] = addresses[ i ]; // this will be a structure that will be updated to EEPROM at some point.
              } else {
                rc = HEXSTAT_OPTION_ERR;
              }
            }
            i++;
          } while ( i < MAX_REGISTRY - 1 && rc == HEXSTAT_SUCCESS );
        } else {
          rc = HEXSTAT_DEVICE_ERR;
        }
      } else {
        rc = HEXSTAT_DATA_INVALID;
      }
    }
  }
  if ( !hex_is_bav() ) {
    hex_send_final_response( rc );
    return HEXERR_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}



/*
   hex_cfg_get() -
   This routine reads the current address configuation
   and sends a configuartion string to the host in the
   form of 'D=xxx,P=xxx,S=xxx,R=xxx' for the supported
   device(s) in the system, where the xxx is the current
   address assigned to that device.  The receiving buffer
   must be large enough to receive the entire message
   or a buffer size error will be returned.
*/
static uint8_t hex_cfg_get(pab_t pab) {
  uint8_t mask = our_support_mask();
  uint8_t i = 0;
  uint8_t index = 0;
  uint8_t dev;
  uint8_t val;

  if ( !hex_is_bav() ) {
    memset((char*)buffer,0,sizeof(buffer));
    for (i = 0; i < MAX_REGISTRY - 1; i++ ) {
      if (( (1 << i) & mask ) != 0 ) {
        if ( index ) {
          buffer[index++] = ',';
        }
        buffer[ index++ ] = pgm_read_byte( &config_option[ i ] );
        buffer[ index++ ] = '=';
        dev = device_address[ i ];
        itoa( dev, (char *)&buffer[ index ], 10 );
        index = strlen((char*)buffer );
      }
    }
    if ( index <= pab.buflen ) {
      transmit_word( index ); // number of bytes in buffer to send
      for (i = 0; i < index; i++ ) {
        transmit_byte(buffer[i]);
      }
      transmit_byte( HEXSTAT_SUCCESS );
    } else {
      hex_send_final_response( HEXSTAT_BUF_SIZE_ERR );
      return HEXERR_SUCCESS;
    }
  }
  hex_finish(); // bav error, OR normal exit after sending data.
  return HEXERR_BAV;
}


static uint8_t hex_cfg_getmask74( __attribute__((unused)) pab_t pab ) {
  uint8_t rc = our_support_mask();
  hex_send_final_response( rc );  // we return the mask as our status for TI-74
  return HEXERR_SUCCESS;
}


/*
 * common support code for incrementing device codes for various supported
 * peripherals, using only the simplest form of the CALL IO() routine.
 *   
 *   CALL IO( device, command, status ) - available on both CC-40 and TI-74.
 *   The more complex form sending PAB$ is only available on CC-40.  TI-74
 *   has a much simpler use form and cannot do the more advanced PAB structures.
 *   
 */
static uint8_t hex_cfg_inc_common( uint8_t index ) {
  uint8_t rc = 0;
  if ( our_support_mask() & (1 << index) ) {
    rc = device_address[ index ];
    rc = ( rc >= pgm_read_byte( &high_device_address[ index ] ) ) ? pgm_read_byte( &low_device_address[ index ] ) : rc + 1;
    device_address[ index ] = rc;
  }
  hex_send_final_response( rc );  // RC=0: nothing done.  RC !=0 : status returned is the "newly" updated device address.
  return HEXERR_SUCCESS;
}


static uint8_t hex_cfg_incdrv74( __attribute__((unused)) pab_t pab ) {
  return hex_cfg_inc_common( DRIVE_GROUP );
}


static uint8_t hex_cfg_incprn74( __attribute__((unused)) pab_t pab ) {
  return hex_cfg_inc_common( PRINTER_GROUP );
}


static uint8_t hex_cfg_incser74( __attribute__((unused)) pab_t pab ) {
  return hex_cfg_inc_common( SERIAL_GROUP );
}


static uint8_t hex_cfg_incrtc74( __attribute__((unused)) pab_t pab ) {
  return hex_cfg_inc_common( CLOCK_GROUP );
}


static uint8_t hex_cfg_write_eeprom( __attribute__((unused)) pab_t pab ) {
  // TODO: Write the 'device_address' data block to EEPROM.
  hex_send_final_response( HEXSTAT_SUCCESS );
  return HEXSTAT_SUCCESS;
}


/*
   hex_cfg_reset() -
   handle the reset commad if directed to us.
*/
static uint8_t hex_cfg_reset( pab_t pab) {
  return hex_null(pab);
}


/*
   Command handling registry for device
*/
// Peripheral (3rd party) specific command codes.
// These are custom commands associated with the
// configuration address of this device.

#define HEXCMD_GETMASK     202  // read support mask
#define HEXCMD_READCFG     203  // read current setup configuration
#define HEXCMD_SETCFG      204  // set new setup configuration
#define HEXCMD_GETMASK74   205  // read support mask from TI-74 CALL IO
#define HEXCMD_INCDRV74    206  // increment drive address (TI-74)
#define HEXCMD_INCPRN74    207
#define HEXCMD_INCSER74    208
#define HEXCMD_INCRTC74    209
#define HEXCMD_WRITE_EE    210  // update current address settings to EEPROM


static const cmd_proc fn_table[] PROGMEM = {
  hex_cfg_getmask,
  hex_cfg_get,
  hex_cfg_set,
  hex_cfg_getmask74,
  hex_cfg_incdrv74,
  hex_cfg_incprn74,
  hex_cfg_incser74,
  hex_cfg_incrtc74,
  hex_cfg_write_eeprom,
  hex_cfg_reset,
  NULL // end of table.
};


static const uint8_t op_table[] PROGMEM = {
  HEXCMD_GETMASK,
  HEXCMD_READCFG,
  HEXCMD_SETCFG,
  HEXCMD_GETMASK74,
  HEXCMD_INCDRV74,
  HEXCMD_INCPRN74,
  HEXCMD_INCSER74,
  HEXCMD_INCRTC74,
  HEXCMD_WRITE_EE, // write current RAM-based configuration to EEPROM.
  HEXCMD_RESET_BUS,
  HEXCMD_INVALID_MARKER
};


/*
   Caveat for using CONFIGURATION device:
   When we register this device, there is only one address
   that can be used with this function.  So; only run
   configuration when the peripheral is the ONLY thing connected
   to the bus.
*/
void cfg_register(registry_t *registry) {
  uint8_t i = registry->num_devices;

  registry->num_devices++;
  registry->entry[ i ].device_code_start = CFG_DEV;
  registry->entry[ i ].device_code_end = CFG_DEV;
  registry->entry[ i ].operation = (cmd_proc *)&fn_table;
  registry->entry[ i ].command = (uint8_t *)&op_table;
  return;
}


/*
    we may need these when we switch to eeprom...
*/
void cfg_reset( void )
{
  return;
}


void cfg_init( void )
{

  // TODO: read the EEPROM configuration, if it exists, into the 'device_address' block
  // loading our current default device addresses for supported devices in the build.
  // Load only the addresses for the devices supported by the build configuration.
  // call 'our_support_mask()' to get this information.
  return;
}
