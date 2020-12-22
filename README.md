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

###Native Implementation
If running on the native hardware config, run the following make command:

make CONFIG=config clean all fuses program

If running the Arduino hardware config, run the following make command:

make CONFIG=config-arduino clean all fuses program

You may need to adjust the avrdude settings in the respecive config files in the 
main directory.

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