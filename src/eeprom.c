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

    eeprom.c: Configuration storage
*/

#include <stddef.h>
#include <string.h>
#include <avr/eeprom.h>
#include "config.h"
#include "debug.h"
#include "eeprom.h"

static EEMEM config_t _eeconfig;

config_t _config;

/**
 * ee_get_config - reads configuration from EEPROM
 *
 * This function reads the stored configuration values from the EEPROM.
 * If the stored checksum doesn't match the calculated one defaults
 * will be returned
 */
void ee_get_config(void) {
  uint_fast16_t i, size;
  uint8_t checksum;

  /* Set default values */

  /* done setting defaults */

  size = eeprom_read_word(&_eeconfig.structsize);

  /* Calculate checksum of EEPROM contents */
  checksum = 0;
  for (i = 2; i < size; i++)
    checksum += eeprom_read_byte((uint8_t *)(&_eeconfig) + i);

  /* Abort if the checksum doesn't match */
  if (checksum != eeprom_read_byte(&_eeconfig.checksum)) {
    eeprom_safety();
    _config.valid = FALSE;
  } else {
    /* Read data from EEPROM */
    eeprom_read_block(&_config, &_eeconfig, size);
    _config.valid = TRUE;
  }
  /* Prevent problems due to accidental writes */
  eeprom_safety();
}

/**
 * ee_set_config - stores configuration data to EEPROM
 *
 * This function stores the current configuration values to the EEPROM.
 */
void ee_set_config(void) {
  uint_fast16_t i;
  uint8_t checksum;

  /* Calculate checksum over EEPROM contents */
  checksum = 0;
  for (i = 2; i < sizeof(config_t); i++)
    checksum += *((uint8_t *) &_config + i);
  /* Write configuration to EEPROM */
  _config.structsize = sizeof(config_t);
  _config.checksum = checksum;
  eeprom_write_block(&_config, &_eeconfig, _config.structsize);
  //debug_trace(&_config,0,sizeof(config_t));

  /* Prevent problems due to accidental writes */
  eeprom_safety();
}

