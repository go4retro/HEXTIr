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

    powermgmt.h: Power management defines
*/

#ifndef POWERMGMT_H
#define POWERMGMT_H
#ifdef __cplusplus
extern "C"{
#endif


#ifdef INCLUDE_POWERMGMT

void sleep_the_system( void );

#else
#define sleep_the_system()  do {} while (0)
#endif


#ifdef __cplusplus
} // extern "C"
#endif

#endif /* POWERMGMT_H */
