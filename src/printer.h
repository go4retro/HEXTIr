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

    printer.h: Printer device defines and prototypes
*/

#ifndef PRINTER_H
#define PRINTER_H

#include "config.h"
#include "hexops.h"
#include "registry.h"

void prn_reset(void);
void prn_register(registry_t *registry);
void prn_init(void);

#endif /* PRINTER_H */
