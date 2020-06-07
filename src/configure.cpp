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
 * Configuration port open status, 0 = closed.
 */
static volatile uint8_t cfg_open = 0;


/*
    Make our supported group mask available to callers.
    This data (our supported groups) is built during compile time
    and stored in flash.  does not need to be in eeprom though.
*/
static inline uint8_t our_support_mask(void) {
  return (uint8_t)pgm_read_byte( &supported_groups );
}




/*
   hex_cfg_open() -

   Open the configuration device to allow reconfiguration
   of the supported peripheral's addresses.

   If mask is retrieved via command, the bit mask values 
   tell which peripheral(s) are supported by the system.
   
   REC 0 = drive
   REC 1 = printer
   REC 2 = serial
   REC 3 = clock
   REC 4 = unused/reserved
   REC 5 = unused/reserved
   REC 6 = unused/reserved

   OPEN #n,"222",RELATIVE[,INPUT | OUTPUT | UPDATE]
   INPUT #1,REC n,A$
   PRINT #1,REC n,A$
   CALL IO(222,238,STATUS) ! 238 = command to update EEPROM with current data
   CALL IO(222,202,STATUS) ! 202 = command to retrieve support mask (tells us which devices are supported) STATUS=mask
   CLOSE #1
*/
static uint8_t hex_cfg_open( pab_t pab ) {
  uint16_t len = 0;
  uint8_t  att = 0;
  uint8_t  rc;

  if ( hex_get_data(buffer, pab.datalen) == HEXSTAT_SUCCESS ) {
    len = buffer[ 0 ] + ( buffer[ 1 ] << 8 );
    att = buffer[ 2 ];
  } else {
    hex_release_bus();
    return HEXERR_BAV; // BAV ERR.
  }

  if ( cfg_open ) {
    rc = HEXSTAT_ALREADY_OPEN;
  } else {
    if ( att & OPENMODE_RELATIVE ) { // we have to be opened in RELATIVE mode.
      if ( (att & OPENMODE_READ | OPENMODE_WRITE) != 0 ) { // append NOT allowed.
        if ( !( att & (OPENMODE_INTERNAL | OPENMODE_FIXED) ) ) { // internal and fixed are illegal.
          cfg_open = att; // we're open for use.
          if ( !len ) {
            len = sizeof(buffer);
          }
          rc = HEXSTAT_SUCCESS;
        } else {
          rc = HEXSTAT_FILE_TYPE_INT_DISP_ERR;
        }
      } else {
        rc = HEXSTAT_APPEND_MODE_ERR;
      }
    } else {
      rc = HEXSTAT_FILE_TYPE_ERR;
    }
  }
  if ( !hex_is_bav() ) {
    if ( rc == HEXSTAT_SUCCESS ) {
      transmit_word( 4 );
      transmit_word( len );
      transmit_word( 0 );      // position at record 0
      transmit_byte( HEXERR_SUCCESS );
      hex_finish();
    } else {
      hex_send_final_response( rc );
    }
    return HEXSTAT_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}


/*
 * hex_cfg_close() - 
 * Close an open configuration port
 */
static uint8_t hex_cfg_close( pab_t pab ) {
  uint8_t rc = HEXSTAT_SUCCESS;

  if ( cfg_open ) {
    cfg_open = 0;
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }

  if ( !hex_is_bav() ) {
    hex_send_final_response( rc );
    return HEXSTAT_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}


/*
 * hex_cfg_read() - 
 * Reads the specified RECORD from configuration as
 * given by the pab.record field.  Response is in form
 * of a string such as 'D=100' or 'P=12'.
 * 
 */
static uint8_t hex_cfg_read( pab_t pab ) {
  uint16_t len = 0;
  uint8_t mask = our_support_mask();
  uint8_t  rc = HEXSTAT_SUCCESS;
  uint8_t  i = 0;
  uint8_t  dev;

  if ( !hex_is_bav() ) {
    if ( ( cfg_open & (OPENMODE_READ | OPENMODE_RELATIVE) ) == (OPENMODE_READ | OPENMODE_RELATIVE) ) {
      if ( pab.record > (MAX_REGISTRY - 1) ) {
        rc = HEXSTAT_EOF;
      } else {
        memset((char*)buffer, 0, sizeof(buffer));
        if (( (1 << pab.record ) & mask ) != 0 ) {
          buffer[ len++ ] = pgm_read_byte( &config_option[ pab.record ] );
          buffer[ len++ ] = '=';
          dev = device_address[ pab.record ];
          itoa( dev, (char *)&buffer[ len ], 10 ) ;
          len = strlen((char *)buffer);
        }
        rc = HEXSTAT_SUCCESS;
      }
    } else {
      if ( cfg_open & OPENMODE_READ  ) {
        rc = HEXSTAT_INPUT_MODE_ERR;
      } else if ( cfg_open ) {
        rc = HEXSTAT_NOT_READ;
      } else {
        rc = HEXSTAT_NOT_OPEN;
      }
    }
    if ( len && rc == HEXSTAT_SUCCESS ) {
      transmit_word( len );
      for ( i = 0; i < len; i++ ) {
        transmit_byte( buffer[ i ] );
      }
      transmit_byte( rc );
      hex_finish();
      return HEXSTAT_SUCCESS;
    } else {
      hex_send_final_response( rc );
      return HEXSTAT_SUCCESS;
    }
  }
  hex_finish();
  return HEXERR_BAV;
}


/*
 * hex_cfg_restore() -
 * minimal "restore" support.
 */
static uint8_t hex_cfg_restore( pab_t pab ) {
  uint8_t rc;

  if ( !hex_is_bav() ) {
    if ( cfg_open ) {
      rc = HEXSTAT_SUCCESS;
    } else {
      rc = HEXSTAT_NOT_OPEN;
    }
    hex_send_final_response( rc );
    return HEXSTAT_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}


/*
 * hex_cfg_write() -
 * write and update address in use for specified peripheral by
 * record number provided in pab.record and the data buffer, 
 * which must contain an appropriate option string for the
 * given device, matching format of the read operation.
 * 
 * D=100, P=12, etc.
 * 
 * Record number in pab specifies which peripheral to affect.
 */
static uint8_t hex_cfg_write( pab_t pab ) {
  char    *p = NULL;
  char    *s;
  uint8_t  ch;
  uint8_t  i;
  uint8_t  rc = HEXSTAT_SUCCESS;
  uint8_t  addr = 0;
  uint8_t  change_mask = 0;


  if ( hex_get_data(buffer, pab.datalen) == HEXSTAT_SUCCESS ) {
    /* our "data" to parse begins at buffer[0] and is consists of 'len' bytes. */
    p = (char *)&buffer[0];
    s = p;
  } else {
    hex_release_bus();
    return HEXERR_BAV; // BAV ERR.
  }

  if ( (cfg_open & (OPENMODE_WRITE | OPENMODE_RELATIVE) ) == (OPENMODE_WRITE | OPENMODE_RELATIVE) ) {
    ch = *p++;
    if ( *p++ == '=' ) {
      ch &= ~0x20; // map to uppercase if lower.
      // Now, is this a valid option given the record we are writing?
      if ( ch != (uint8_t)pgm_read_byte( &config_option[ pab.record ] )) {
        rc = HEXSTAT_OPTION_ERR; // bad or invalid option for this record
      } else {
        // parse digits to get new address; then check to see if the address is valid for
        // the selected device.  If so, mark the change mask and store the address in our
        // change array.
        // we do not change any addresses until the entire message is parsed.
        // skip any leading spaces if present
        while ( *p == ' ' ) {
          p++;
        }
        while ( isdigit( *p ) ) {
          addr = ( addr * 10 ) + ((*p++) - '0');
        }
        // and skip any trailing spaces if present
        while ( *p == ' ' ) {
          p++;
        }
        // At this point, we should be at end of input data...
        if ( (( (unsigned int)p - (unsigned int)s) < pab.datalen ) ) {
          rc = HEXSTAT_OPTION_ERR;
        }
        change_mask = (1 << pab.record); // and our address "group" number.
      }
    } else {
      rc = HEXSTAT_OPTION_ERR;
    }

    // Done parsing incoming options.  If no error, we can now see
    // if the addresses we were given are any good.
    if ( rc == HEXSTAT_SUCCESS ) {
      if ( change_mask != 0 ) {
        rc = HEXSTAT_OPTION_ERR;
        ch = our_support_mask() & change_mask; // check to ensure that we are not trying to set address on unsupported periperhals
        if ( ch == change_mask ) {
          if ( (1 << pab.record) & change_mask ) {
            if ( addr >= (uint8_t)pgm_read_byte( &low_device_address[ pab.record ] ) &&
                 addr <= (uint8_t)pgm_read_byte( &high_device_address[ pab.record ] ) )
            {
              device_address[ pab.record ] = addr; // this will be a structure that will be updated to EEPROM at some point.
              rc = HEXSTAT_SUCCESS;
            }
          }
        }
      } else {
        rc = HEXSTAT_DATA_INVALID;
      }
    }
  } else {
    if ( cfg_open & OPENMODE_WRITE ) {
      rc = HEXSTAT_OUTPUT_MODE_ERR;
    } else if ( cfg_open ) {
      rc = HEXSTAT_NOT_WRITE;
    } else {
      rc = HEXSTAT_NOT_OPEN;
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
   hex_cfg_getmask() -
   returns the current configuration mask indicating which peripheral
   groups are currently supported.
*/
static uint8_t hex_cfg_getmask(pab_t pab) {
  uint8_t mask;

  if ( !hex_is_bav() ) {
    // we should only be asked to send one byte; if zero bytes, then send mask as return status
    // if > 1 byte, send buffer size error response.
    mask = our_support_mask();
    if ( pab.buflen == 1 ) {
      transmit_word( 1 );
      transmit_byte( mask );
      transmit_byte( HEXSTAT_SUCCESS );
      hex_finish();
    } else {
      // regardless of buffer size sent, respond with the mask value as status.
      hex_send_final_response( mask );
    }
    return HEXERR_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}


/*
 * hex_cfg_write_eeprom() -
 * update the content of EEPROM with the current device address block
 * of supported peripherals.
 */
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
  cfg_open = 0;
  return hex_null(pab);
}


/*
   Command handling registry for device
*/
// Peripheral (3rd party) specific command codes.
// These are custom commands associated with the
// configuration address of this device.

#define HEXCMD_GETMASK     237  // read support mask = EDh
#define HEXCMD_WRITE_EE    238  // update current address settings to EEPROM 238d = EEh


static const cmd_proc fn_table[] PROGMEM = {
  hex_cfg_open,
  hex_cfg_close,
  hex_cfg_read,
  hex_cfg_write,
  hex_cfg_restore,
  //
  hex_cfg_getmask,
  hex_cfg_write_eeprom,
  //
  hex_cfg_reset,
  NULL // end of table.
};


static const uint8_t op_table[] PROGMEM = {
  HEXCMD_OPEN,
  HEXCMD_CLOSE,
  HEXCMD_READ,
  HEXCMD_WRITE,
  HEXCMD_RESTORE,
  //
  HEXCMD_GETMASK,
  HEXCMD_WRITE_EE, // write current RAM-based configuration to EEPROM.
  //
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
  cfg_open = 0;
  return;
}


void cfg_init( void )
{
  cfg_open = 0;

  // TODO: read the EEPROM configuration, if it exists, into the 'device_address' block
  // loading our current default device addresses for supported devices in the build.
  // Load only the addresses for the devices supported by the build configuration.
  // call 'our_support_mask()' to get this information.
  
  return;
}
