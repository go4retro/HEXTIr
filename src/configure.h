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
#include "hexops.h"
#include "ff.h"
#include "registry.h"

extern uint8_t device_address[MAX_REGISTRY];

void cfg_start(void);
void cfg_reset(void);
void cfg_register(registry_t *registry);
void cfg_init(void);

#endif /* CONFIGURE_H_ */
