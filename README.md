# HEXTIr
TI HEX-BUS SD Drive Interface
Copyright (C) 2017-20  Jim Brain

##Description
Coupled with an Atmel ATMEGA328 microcontroller (either as a standalone PCB or an 
Arduino UNO, Nano, etc. development board), this firmware implements the functionality
of the various HEXBUS peripherals:

* Secure Digital (SD) based random access disk drive at device # 100
* RS232 port at device # 20
* RS232-based printer port at device #10

##Implementation
###Arduino Implementation
Configure the Arduino IDE for the specific board in use.  Once configured, load the 
src.ino file in the src directory, compile and download the object code to the Arduino
system.

Because the GCC C++ compiler and Arduino settings compile the code slightly differently,
the resulting code size is larger and thus not all features can be enabled.  By default, 
only the driver function is enabled in the Arduino build.  You can optionally enable some
of the other peripherals by uncommenting lines in config.h:

```
#elif CONFIG_HARDWARE_VARIANT == 4
/* ---------- Hardware configuration: Old HEXTIr Arduino ---------- */

#define INCLUDE_DRIVE
//#define INCLUDE_CLOCK
//#define INCLUDE_SERIAL
//#define INCLUDE_PRINTER
```

###Native Implementation
If compiling directly from the command line, run the following make command:

####For users of the PCB design in the PCB directory:
make CONFIG=config clean all fuses program

####For folks using an Arduino UNO with a SD Card shield, run the following make command:

make CONFIG=config-arduino clean all fuses program

You may need to adjust the avrdude settings in the respecive config files in the 
main directory.

Native and Arduino builds differ only in the pin mappings.  All features are enabled
in both builds.  If space becomes an issue, you can comment out specific peripherals in config.h:

```
#define INCLUDE_DRIVE
#define INCLUDE_CLOCK
#define INCLUDE_SERIAL
#define INCLUDE_PRINTER
```

##PCB Design Copyright

This project's PCB files are free designs; you can redistribute them 
and/or modify them under the terms of the Creative Commons
Attribution-ShareAlike 4.0 International License.

You should have received a copy of the license along with this
work. If not, see <http://creativecommons.org/licenses/by-sa/4.0/>.

These files are distributed in the hope that they will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
license for more details.

##Firmware Copyright

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