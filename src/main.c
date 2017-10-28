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

    main.c: Main application
*/

#include <inttypes.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "config.h"
#include "uart.h"

uint16_t length = 80;
uint8_t pgm[32];
uint8_t save[20] = { 0x80, 0x03, 0x0e, 0x00, 0x0a, 0x00, 0x07, 0x8a, 0xc9, 0x02, 0x68, 0x69, 0x00, 0xff, 0x7f, 0x03, 0x86, 0x00, 0x20, 0x00 };


typedef enum { ST_IDLE,
               ST_GETDEV,
               ST_GETCMD,
               ST_GETLUN,
               ST_GETRECNUM_L,
               ST_GETRECNUM_H,
               ST_GETBUFLEN_L,
               ST_GETBUFLEN_H,
               ST_GETDATLEN_L,
               ST_GETDATLEN_H,
               ST_GETDATA,
               ST_PROCESS
             } hexstate_t;

typedef enum { HEXCMD_OPEN = 0,
               HEXCMD_CLOSE,
               HEXCMD_DELETE,
               HEXCMD_READ,
               HEXCMD_WRITE,
            } hexcmdtype_t;

typedef struct _hexcmd{
  uint8_t dev;
  uint8_t cmd;
  uint8_t lun;
  uint16_t record;
  uint16_t buflen;
  uint16_t datalen;
} hexcmd_t;

void hex_control(uint8_t i) {
  if(i) {
    HEX_HSK_DDR |= HEX_HSK_PIN;
    //HEX_BAV_DDR |= HEX_BAV_PIN;
    HEX_DATA_DDR |= HEX_DATA_PIN;
  } else {
  }
}

void hex_init(void) {
  HEX_HSK_DDR &= ~HEX_HSK_PIN;
  HEX_BAV_DDR &= ~HEX_BAV_PIN;
  HEX_DATA_DDR &= ~HEX_DATA_PIN;
  HEX_HSK_OUT |= HEX_HSK_PIN;
  HEX_BAV_OUT |= HEX_BAV_PIN;
  HEX_DATA_OUT |= HEX_DATA_PIN;
}

uint8_t hex_is_bav(void) {
  return HEX_BAV_IN & HEX_BAV_PIN;
}

void hex_bav_hi(void) {
  if(!hex_is_bav()) {
    HEX_BAV_DDR &= ~HEX_BAV_PIN;
    HEX_BAV_OUT |= HEX_BAV_PIN;
  }
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
  HEX_HSK_OUT &= ~HEX_HSK_PIN;
  HEX_HSK_DDR |= HEX_HSK_PIN;
}

uint8_t hex_is_hsk(void) {
  return HEX_HSK_IN & HEX_HSK_PIN;

}

void hex_send_nybble(uint8_t data) {
  uint8_t i = HEX_DATA_PIN & data;

  hex_bav_lo();
  HEX_DATA_OUT = (HEX_DATA_OUT & ~HEX_DATA_PIN) | i;
  HEX_DATA_DDR = (HEX_DATA_OUT & ~HEX_DATA_PIN) | (~i & HEX_DATA_PIN);
  _delay_us(10);
  hex_hsk_lo();
  hex_bav_hi();
  _delay_us(9);
  hex_hsk_hi();
  while(!hex_is_hsk());
  //_delay_us(1);
  HEX_DATA_OUT |= HEX_DATA_PIN;
  HEX_DATA_DDR &= ~HEX_DATA_PIN;
  _delay_us(48);
}

void hex_putc(uint8_t data) {
  hex_send_nybble(data);
  hex_send_nybble(data >> 4);
  uart_puthex(data);
  uart_putc(' ');
}

void hex_puti(uint16_t data) {
  hex_putc(data);
  hex_putc(data >> 8);
}

uint8_t hex_read_nybble(uint8_t hold) {
  uint8_t data;

  //while(HEX_HSK_IN & HEX_HSK_PIN);  // wait until low happens.
  while(hex_is_hsk());  // wait until low happens.
  hex_hsk_lo();
  data = (HEX_DATA_IN & HEX_DATA_PIN);
  if(!hold) {
    hex_hsk_hi();
    while(!hex_is_hsk());  // wait until hi happens.
  }
  return data;
}



uint8_t hex_getc(uint8_t hold) {
  uint8_t data;
  data = hex_read_nybble(0) | (hex_read_nybble(hold) << 4);
  uart_puthex(data);
  uart_putc(' ');
  return data;

}

uint8_t hex_getdata(uint8_t buffer[256], uint16_t len) {
  uint16_t i = 0;
  uint8_t data;

  while(i < len) {
    if(hex_is_bav()){
      return 1;
    }
    data = hex_getc((i+1) == len);
    //data = hex_getc(0);
    buffer[i++] = data;
  }
  //if(len > 0) {
  //  uart_putc(13);
  //  uart_putc(10);
  //  uart_puthex(len >> 8);
  //  uart_puthex(len);
  //  uart_trace(buffer,0,len);
  //}
  return 0;
}

uint8_t hex_write(hexcmd_t cmd) {
  uint16_t i = 0;
  uint8_t data;
  uint8_t buffer[256];
  if(hex_getdata(buffer, cmd.datalen))
    return 1;
  hex_hsk_hi();  // normally we would hold here until writing was done.
  for(i = 0;i < cmd.datalen ; i++) {
    pgm[i] = buffer[i];
  }
  length = cmd.datalen;
  _delay_us(200);
  if(!hex_is_bav()) { // we can send response
    uart_putc(13);
    uart_putc(10);
    hex_puti(0);  // zero length data
    hex_putc(0);  // status code
    return 0;
  }
  return 1;

}

uint8_t hex_read(hexcmd_t cmd) {
  uint16_t i;

  _delay_ms(10000);
  hex_hsk_hi();
  hex_puti(sizeof(save));
  for(i = 0; i< sizeof(save); i++) {
    hex_putc(save[i]);
  }
  /*hex_puti(length);
  for(i = 0; i< length; i++) {
    hex_putc(pgm[i]);
  }*/
  hex_putc(0);    // status code
  return 0;
}

uint8_t hex_open(hexcmd_t cmd) {
  uint16_t i = 0;
  uint8_t data;
  uint8_t buffer[256];
  uint16_t len = 0;
  uint8_t att = 0;

  while(i < 3) {
    if(hex_is_bav()){
      return 1;
    }
    data = hex_getc(0);
    switch(i) {
      case 0:
        len = data;
        break;
      case 1:
        len |= data << 8;
        break;
      case 2:
        att = data;
        break;
    }
    i++;
  }
  if(hex_getdata(buffer,cmd.datalen-3))
    return 1;
  hex_hsk_hi(); // we could hold here, if we were opening a file.
  _delay_us(200);
  if(!hex_is_bav()) { // we can send response
    uart_putc(13);
    uart_putc(10);

    hex_puti(2);    // claims it is accepted buffer length, but looks to really be my return buffer length...
    switch(att) {
      case 64:
        hex_puti(length);
        break;
      default: // write
        hex_puti(len ? len : 128);  // this is evidently the value we should send back.  Not sure what one does if one needs to send two of these.
        break;
    }
    hex_putc(0);    // status code
    return 0;
  }
  return 1;
}

uint8_t hex_close(hexcmd_t cmd) {
  uart_putc(13);
  uart_putc(10);
  hex_puti(0);
  hex_putc(0);
  return 0;
}

//int main(void) __attribute__((OS_main));
int main(void) {
  hexstate_t state = ST_IDLE;
  uint8_t data;
  hexcmd_t cmd;

  hex_init();
  uart_init();
  sei();

  cmd.dev = 0;
  cmd.cmd = 0;
  cmd.lun = 0;
  cmd.record = 0;
  cmd.buflen = 0;
  cmd.datalen = 0;

  uart_putc('*');
  while(1) {
    while(HEX_BAV_IN & HEX_BAV_PIN) {
      state = ST_IDLE;  // wait until low happens.
    }
    data = hex_getc((cmd.cmd == 3) && (state == ST_GETDATLEN_H));  // if it's the last byte, hold the hsk low.
    switch(state) {
      case ST_IDLE:
        cmd.dev = data;
        state = ST_GETCMD;
        break;
      case ST_GETCMD:
        cmd.cmd = data;
        state = ST_GETLUN;
        break;
      case ST_GETLUN:
        cmd.lun = data;
        state = ST_GETRECNUM_L;
        break;
      case ST_GETRECNUM_L:
        cmd.record = data;
        state = ST_GETRECNUM_H;
        break;
      case ST_GETRECNUM_H:
        cmd.record |= data << 8;
        state = ST_GETBUFLEN_L;
        break;
      case ST_GETBUFLEN_L:
        cmd.buflen = data;
        state = ST_GETBUFLEN_H;
        break;
      case ST_GETBUFLEN_H:
        cmd.buflen |= data << 8;
        state = ST_GETDATLEN_L;
        break;
      case ST_GETDATLEN_L:
        cmd.datalen = data;
        state = ST_GETDATLEN_H;
        break;
      case ST_GETDATLEN_H:
        cmd.datalen |= data << 8;
        switch(cmd.cmd) {
          case HEXCMD_OPEN:
            hex_open(cmd);
            break;
          case HEXCMD_CLOSE:
            hex_close(cmd);
            break;
          case HEXCMD_DELETE:
            break;
          case HEXCMD_READ:
            hex_read(cmd);
            break;
          case HEXCMD_WRITE:
            hex_write(cmd);
            break;
        }
        uart_putc(13);
        uart_putc(10);
        state = ST_IDLE;
        break;
      default:
        break;
    }
  }
}

