/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2011  Ingo Korb <ingo@akana.de>

   Inspiration and low-level SD/MMC access based on code from MMC2IEC
     by Lars Pontoppidan et al., see sdcard.c|h and config.h.

   FAT filesystem access based on code from ChaN and Jim Brain, see ff.c|h.

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


   eeprom.h: Persistent configuration storage

*/

#ifndef EEPROM_H
#define EEPROM_H
#ifdef __cplusplus
extern "C"{
#endif

/**
 * struct config_s - in-eeprom data structure
 * @dummy      : EEPROM position 0 is unused
 * @checksum   : Checksum over the EEPROM contents
 * @structsize : size of the eeprom structure
 * @glob_flags : global flags
 * @drv_addr   : drive address
 * @drv_flags  : drive config flags
 * @prn_addr   : printer address
 * @prn_flags  : printer config flags
 * @ser_addr   : serial address
 * @ser_flags  : serial config flags
 * @rtc_addr   : RTC address
 * @rtc_flags  : RTC config flags
 *
 * This is the data structure for the contents of the EEPROM.
 *
 * Do not remove any fields!
 * Only add fields at the end!
 */
typedef struct config_s {
  uint8_t  dummy;
  uint8_t  checksum;
  uint16_t structsize;
  uint8_t  glob_flags;
  uint8_t  drv_addr;
  uint8_t  drv_flags;
  uint8_t  prn_addr;
  uint8_t  prn_flags;
  uint8_t  ser_addr;
  uint8_t  ser_flags;
  uint8_t  rtc_addr;
  uint8_t  rtc_flags;
} config_t;


/* Set EEPROM address pointer to the dummy entry */
static inline void eeprom_safety(void) {
  EEAR = 0;
}

config_t* ee_get_config(void);
void ee_set_config(void);

#ifdef __cplusplus
} // extern "C"
#endif
#endif
