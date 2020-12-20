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

    registry.cpp: General config registry functions.
*/


#include "config.h"
#include "debug.h"
#include "hexops.h"
#include "registry.h"

void reg_add(uint8_t low, uint8_t cur, uint8_t high, const cmd_op_t ops[]) {
  uint8_t i;

  for (i = 0; i < registry.num_devices; i++) {
    if(registry.entry[i].dev_low == low && registry.entry[i].dev_high == high)
      return; // don't re-register.
  }
  i = registry.num_devices;
  registry.num_devices++;
  registry.entry[ i ].dev_low = low;
  registry.entry[ i ].dev_cur = cur;
  registry.entry[ i ].dev_high = high;
  registry.entry[ i ].oplist = (cmd_op_t *)ops;
}


