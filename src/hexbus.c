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

    hexbus.c: Routines to support the Texas Instruments Hex Bus protocol
*/

#include <util/delay.h>
#include "config.h"
#include "hexbus.h"
#include "integer.h"
#include "uart.h"

uint8_t hex_is_bav(void) {
  return HEX_BAV_IN & HEX_BAV_PIN;
}

void hex_bav_hi(void) {
  //if(!hex_is_bav()) {
    HEX_BAV_DDR &= ~HEX_BAV_PIN;
    HEX_BAV_OUT |= HEX_BAV_PIN;
  //}
}

void hex_bav_lo(void) {
  HEX_BAV_OUT &= ~HEX_BAV_PIN;
  HEX_BAV_DDR |= HEX_BAV_PIN;
}

void hex_hsk_hi(void) {
  HEX_HSK_DDR &= ~HEX_HSK_PIN;
  HEX_HSK_OUT |= HEX_HSK_PIN;
}

void hex_hsk_lo(void) {
  HEX_HSK_DDR |= HEX_HSK_PIN;
  HEX_HSK_OUT &= ~HEX_HSK_PIN;
}

uint8_t hex_is_hsk(void) {
  return HEX_HSK_IN & HEX_HSK_PIN;
}

void hex_release_bus_send(void) {
  hex_hsk_hi();
  while(!hex_is_hsk());
  HEX_DATA_OUT |= HEX_DATA_PIN;
  HEX_DATA_DDR &= ~HEX_DATA_PIN;
  _delay_us(48); // won't need this if someone else did it.
}

void hex_release_bus_recv(void) {
  hex_hsk_hi();
  while(!hex_is_hsk());
}

void hex_send_nybble(uint8_t data, uint8_t hold) {
  uint8_t i = HEX_DATA_PIN & data;

  hex_bav_lo();
  HEX_DATA_OUT = (HEX_DATA_OUT & ~HEX_DATA_PIN) | i;
  HEX_DATA_DDR = (HEX_DATA_OUT & ~HEX_DATA_PIN) | (~i & HEX_DATA_PIN);
  _delay_us(10);
  hex_hsk_lo();
  hex_bav_hi();
  if(!hold) {
    hex_release_bus_send();
  }
}

void hex_putc(uint8_t data, uint8_t hold) {
  hex_send_nybble(data, FALSE);
  hex_send_nybble(data >> 4, TRUE);
  uart_puthex(data);
  uart_putc(' ');
  if(!hold) {
    hex_release_bus_send();
  }
}

void hex_puti(uint16_t data, uint8_t hold) {
  hex_putc(data, FALSE);
  hex_putc(data >> 8, hold);
}

int16_t hex_read_nybble(uint8_t hold) {
  uint8_t data;

  if(hex_is_bav())
    return HEXERR_BAV;
  // should time out in 20ms
  while(hex_is_hsk());  // wait until low happens.
  hex_hsk_lo();
  data = (HEX_DATA_IN & HEX_DATA_PIN);
  if(!hold) {
    hex_release_bus_recv();
  }
  return data;
}

int16_t hex_getc(uint8_t hold) {
  int16_t datal, datah;
  uint8_t data;

  datal = hex_read_nybble(FALSE);
  if(datal < 0)
    return datal;
  datah = hex_read_nybble(TRUE);
  if(datah < 0) {
    hex_release_bus_recv();
    return datah;
  }
  data = datal | (datah << 4);
  uart_puthex(data);
  uart_putc(' ');
  if(!hold)
    hex_release_bus_recv();
  return data;

}

void hex_init(void) {
  HEX_HSK_DDR &= ~HEX_HSK_PIN;  // bring HSK hi-Z
  HEX_HSK_OUT |= HEX_HSK_PIN;

  HEX_BAV_DDR &= ~HEX_BAV_PIN;
  HEX_BAV_OUT |= HEX_BAV_PIN;

  HEX_DATA_DDR &= ~HEX_DATA_PIN;
  HEX_DATA_OUT |= HEX_DATA_PIN;
}




