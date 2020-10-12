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

    catalog.h: Catalog defines and prototypes
*/

#ifndef CATALOG_H
#define CATALOG_H

#include "drive.h" // for file_t

void hex_read_catalog(file_t* file);
void hex_read_catalog_txt(file_t* file);
void hex_open_catalog(file_t *file, uint8_t lun, uint8_t att, char* path);

#endif /* SRC_CATALOG_H */
