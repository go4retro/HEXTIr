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

    configure.h: Configuration endpoint defines and prototypes 
    by s.reid
*/

#ifndef CONFIGURE_H_
#define CONFIGURE_H_

#include "config.h"
#include "hexbus.h"
#include "hexops.h"
#include "ff.h"
#include "registry.h"

#ifndef NEW_DEV_CHK
extern uint8_t device_address[MAX_REGISTRY];
#endif

#define DEV_CFG_START   222
#define DEV_CFG_DEFAULT DEV_CFG_START
#define DEV_CFG_END     DEV_CFG_START

#ifdef USE_CFG_DEVICE
/* ----- Common definitions  ------ */
// BASE Device Numbers for peripheral groups (this is the low-end address for a particular group).
// TODO these 5 should move to a standard hexdev.h or something, since they are defaults, and they should be the same for all
// boards.
#define DRV_DEV       100    // Base disk device code we support
#define PRN_DEV        10    // Device code to support a printer on HW serial (rx/tx) @115200,N,8,1
#define SER_DEV        20    // Device code for RS-232 Serial peripheral using SW serial (def=300 baud)
#define RTC_DEV       230    // Device code to support DS3231 RTC on I2C/wire; A5/A4.
//
#define CFG_DEV       222    // Special device code to access configuration and setup port
#define NO_DEV          0
#define ALL_DEV    NO_DEV    // Device Code 0 addresses all devices
#define MAX_DEV       255    // our highest device code
//
#define MAX_DRV       117    // Device codes 100-109 were originally for hexbus 5.25" disk drives.
                             // Device codes 110-117 were later allocated for hexbu 3.5" disk drives.
#define MAX_PRN        19    // Printers are allowed from 10-19 for device codes.
#define MAX_SER        23    // Serial peripherals were allowed from 20-23 for device codes.
#define MAX_RTC       239    // New device: RTC peripheral: device code block from 230 to 239.
   
// Offsets into our device-code map for various peripheral functions.
#define DRIVE_GROUP       0
#define PRINTER_GROUP     1
#define SERIAL_GROUP      2
#define CLOCK_GROUP       3
#define CONFIG_GROUP      7
// Can have up to 'MAX_REGISTRY-1' of these (see registry.h)


// Configure initial default addressing here.
#define DEFAULT_DRIVE       (DRV_DEV)
#define SUPPORT_DRV         (1<<DRIVE_GROUP)

#define DEFAULT_CFGDEV      (CFG_DEV)
#define SUPPORT_CFG         (1<<CONFIG_GROUP)
// Other support devices included optionally in build.
#ifdef INCLUDE_PRINTER
 #define DEFAULT_PRINTER    (PRN_DEV+2)
 #define SUPPORT_PRN        (1<<PRINTER_GROUP)
#else
 #define DEFAULT_PRINTER    NO_DEV
 #define SUPPORT_PRN        0
#endif

#ifdef INCLUDE_CLOCK
 #define DEFAULT_CLOCK     (RTC_DEV)
 #define SUPPORT_RTC       (1<<CLOCK_GROUP)
#else
 #define DEFAULT_CLOCK     NO_DEV
 #define SUPPORT_RTC       0
#endif

#ifdef INCLUDE_SERIAL
 #define DEFAULT_SERIAL    (SER_DEV)
 #define SUPPORT_SER       (1<<SERIAL_GROUP)
#else
 #define DEFAULT_SERIAL    NO_DEV
 #define SUPPORT_SER       0
#endif

void cfg_reset(void);
void cfg_register1(void);
void cfg_init(void);

#endif
#ifdef USE_NEW_OPTABLE
void cfg_register(uint8_t low, uint8_t cur, uint8_t high, const cmd_op_t ops[]);
#else
void cfg_register(uint8_t low, uint8_t cur, uint8_t high, const uint8_t op_table[], const cmd_proc fn_table[]);
#endif
#endif /* CONFIGURE_H_ */
