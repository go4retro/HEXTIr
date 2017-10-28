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
uint8_t save[20] = { 0x80, 0x03, 0x0e, 0x00, 0x0a, 0x00, 0x07, 0x8a, 0xc9, 0x02, 0x68, 0x69, 0x00, 0xff, 0x7f, 0x03, 0x86, 0x00, 0x20, 0x00 };

uint8_t buffer[64];

typedef enum { HEXCMD_OPEN = 0,
               HEXCMD_CLOSE,
               HEXCMD_DELETE_OPEN,
               HEXCMD_READ,
               HEXCMD_WRITE,
               HEXCMD_RESTORE,
               HEXCMD_DELETE,
               HEXCMD_RETURN_STATUS,
               HEXCMD_SVC_REQ_ENABLE,
               HEXCMD_SVC_REQ_DISABLE,
               HEXCMD_SVC_REQ_POLL,
               HEXCMD_MASTER,
               HEXCMD_VERIFY,
               HEXCMD_FORMAT,
               HEXCMD_CATALOG,
               HEXCMD_SET_OPTIONS,
               HEXCMD_XMIT_BREAK,
               HEXCMD_WP_FILE,
               HEXCMD_READ_SECTORS,
               HEXCMD_WRITE_SECTORS,
               HEXCMD_RENAME_FILE,
               HEXCMD_READ_FD,
               HEXCMD_WRITE_FD,
               HEXCMD_READ_FILE_SECTORS,
               HEXCMD_WRITE_FILE_SECTORS,
               HEXCMD_LOAD,
               HEXCMD_SAVE,
               HEXCMD_INQ_SAVE,
               HEXCMD_HOME_COMP_STATUS,
               HEXCMD_HOME_COMP_VERIFY,
               HEXCMD_NULL = 0xfe,
               HEXCMD_RESET_BUS
            } hexcmdtype_t;

typedef enum {
              HEXSTAT_SUCCESS = 0,
              HEXSTAT_OPTION_ERR,
              HEXSTAT_ATTR_ERR,
              HEXSTAT_NOT_FOUND,
              HEXSTAT_NOT_OPEN,
              HEXSTAT_ALREADY_OPEN,
              HEXSTAT_DEVICE_ERR,
              HEXSTAT_EOF,
              HEXSTAT_TOO_LONG,
              HEXSTAT_WP_ERR,
              HEXSTAT_NOT_REQUEST,
              HEXSTAT_DIR_FULL,
              HEXSTAT_BUF_SIZE_ERR,
              HEXSTAT_UNSUPP_CMD,
              HEXSTAT_NOT_WRITE,
              HEXSTAT_NOT_READ,
              HEXSTAT_DATA_ERR,
              HEXSTAT_FILE_TYPE_ERR,
              HEXSTAT_FILE_PROT_ERR,
              HEXSTAT_APPEND_MODE_ERR,
              HEXSTAT_OUTPUT_MODE_ERR,
              HEXSTAT_INPUT_MODE_ERR,
              HEXSTAT_UPDATE_MODE_ERR,
              HEXSTAT_FILE_TYPE_INT_DISP_ERR,
              HEXSTAT_VERIFY_ERR,
              HEXSTAT_BATT_LOW,
              HEXSTAT_UNFORMATTED,
              HEXSTAT_BUS_ERR,
              HEXSTAT_DEL_PROTECT,
              HEXSTAT_CART_NOT_PRESENT,
              HEXSTAT_RESTORE_NOT_ALLOWED,
              HEXSTAT_FILE_NAME_INVALID,
              HEXSTAT_MEDIA_FULL,
              HEXSTAT_MAX_LUNS,
              HEXSTAT_DATA_INVALID,
              HEXSTAT_ILLEGAL_SLAVE = 0xfe,
              HEXSTAT_TIMEOUT
            } hexstatus_t;

           typedef struct _hexcmd{
  uint8_t dev;
  uint8_t cmd;
  uint8_t lun;
  uint16_t record;
  uint16_t buflen;
  uint16_t datalen;
} pab_t;

typedef struct {
  union {
    pab_t pab;
    uint8_t raw[9];
  };
} pab_raw_t;

void hex_init(void) {
  HEX_HSK_IN_DDR &= ~HEX_HSK_IN_PIN;  // bring HSK hi-Z
  HEX_HSK_IN_OUT |= HEX_HSK_IN_PIN;

  HEX_HSK_OUT_DDR |= HEX_HSK_OUT_PIN;
  HEX_HSK_OUT_OUT &= ~HEX_HSK_OUT_PIN;

  HEX_BAV_DDR &= ~HEX_BAV_PIN;
  HEX_BAV_OUT |= HEX_BAV_PIN;

  HEX_DATA_DDR &= ~HEX_DATA_PIN;
  HEX_DATA_OUT |= HEX_DATA_PIN;
}

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

#define OLD_HSK
//#define POS_HSK

void hex_hsk_hi(void) {
#ifdef OLD_HSK
  HEX_HSK_IN_DDR &= ~HEX_HSK_IN_PIN;
  HEX_HSK_IN_OUT |= HEX_HSK_IN_PIN;
#else
# ifdef POS_HSK
  HEX_HSK_OUT_OUT |= HEX_HSK_OUT_PIN;
# else
  // negated logic
  HEX_HSK_OUT_OUT &= ~HEX_HSK_OUT_PIN;
# endif
#endif
}

void hex_hsk_lo(void) {
#ifdef OLD_HSK
  HEX_HSK_IN_DDR |= HEX_HSK_IN_PIN;
  HEX_HSK_IN_OUT &= ~HEX_HSK_IN_PIN;
#else
# ifdef POS_HSK
  HEX_HSK_OUT_OUT &= ~HEX_HSK_OUT_PIN;
# else
  // negated logic
  HEX_HSK_OUT_OUT |= HEX_HSK_OUT_PIN;
# endif
#endif
}

uint8_t hex_is_hsk(void) {
  return HEX_HSK_IN_IN & HEX_HSK_IN_PIN;
}

void hex_release_bus_send(void) {
  _delay_us(1);
  hex_hsk_hi();
  while(!hex_is_hsk());
  HEX_DATA_OUT |= HEX_DATA_PIN;
  _delay_us(10);
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

  datal = hex_read_nybble(0);
  if(datal < 0)
    return datal;
  datah = hex_read_nybble(1);
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

int16_t hex_getdata(uint8_t buf[256], uint16_t len) {
  uint16_t i = 0;
  int16_t data;

  while(i < len) {
    if(hex_is_bav()){
      return 1;
    }
    data = hex_getc((i+1) == len);
    if(data < 0)
      return data;
    buf[i++] = data;
  }
  if(len > 0) {
    uart_putc(13);
    uart_putc(10);
    uart_trace(buf,0,len);
    uart_putc('<');
  }
  return 0;
}

uint8_t hex_write(pab_t pab) {
  uint16_t i = 0;
  if(hex_getdata(buffer, pab.datalen))
    return 1;
  _delay_ms(1000);
  hex_release_bus_recv();  // normally we would hold here until writing was done.
  for(i = 0;i < pab.datalen ; i++) {
  }
  length = pab.datalen;
  _delay_us(200);
  if(!hex_is_bav()) { // we can send response
    hex_puti(0, FALSE);  // zero length data
    hex_putc(HEXSTAT_SUCCESS, FALSE);  // status code
    return 0;
  }
  return 1;

}

uint8_t hex_read(pab_t pab) {
  uint8_t i;
  uint16_t len;

  uart_putc(13);
  uart_putc(10);
  uart_putc('<');
  hex_release_bus_recv();
  _delay_us(48);
  hex_puti(sizeof(save), TRUE);  // send full length of file
  uart_putc(13);
  uart_putc(10);
  uart_trace(save,0,sizeof(save));
  hex_release_bus_send();
  for(i = 0; i < sizeof(save); i++) {
    hex_putc(save[i], (i + 1) == sizeof(save));
  }
  hex_release_bus_send();
  hex_putc(HEXSTAT_SUCCESS, FALSE);    // status code
  return 0;
}

uint8_t hex_open(pab_t pab) {
  uint16_t i = 0;
  uint8_t data;
  uint16_t len = 0;
  uint8_t att = 0;
  uint8_t rc;

  hex_release_bus_recv();
  while(i < 3) {
    if(hex_is_bav()){
      return 1;
    }
    data = hex_getc(FALSE);
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
  if(hex_getdata(buffer,pab.datalen-3))
    return -1;
  buffer[pab.datalen-3] = 0;

  rc = HEXSTAT_SUCCESS;
  hex_release_bus_recv();
  _delay_us(200);  // wait a bit...
  if(!hex_is_bav()) { // we can send response

    hex_puti(2, FALSE);    // claims it is accepted buffer length, but looks to really be my return buffer length...
    switch(att) {
      case 64:
        hex_puti(sizeof(save), FALSE);
        break;
      default: // write
        hex_puti((len ? len : length), FALSE);  // this is evidently the value we should send back.  Not sure what one does if one needs to send two of these.
        break;
    }
    hex_putc(rc, FALSE);    // status code
    return 0;
  }
  return 1;
}

uint8_t hex_close(pab_t pab) {
  uint8_t rc;

  uart_putc(13);
  uart_putc(10);
  uart_putc('<');
  rc = HEXSTAT_SUCCESS;
  hex_release_bus_recv();
  hex_puti(0, FALSE);
  hex_putc(rc, FALSE);
  return 0;
}

int main(void) __attribute__((OS_main));
int main(void) {
  uint8_t i = 0;
  int16_t data;
  pab_raw_t pabdata;

  hex_init();
  uart_init();

  sei();

  uart_putcrlf();

  pabdata.pab.dev = 0;
  pabdata.pab.cmd = 0;
  pabdata.pab.lun = 0;
  pabdata.pab.record = 0;
  pabdata.pab.buflen = 0;
  pabdata.pab.datalen = 0;

  uart_putc('*');
  while(1) {
    while(hex_is_bav()) {
      i = 0;
    }
    uart_putc('>');
    while(!hex_is_bav()) {
      data = hex_getc(i == 8);  // if it's the last byte, hold the hsk low.
      if(-1 != data) {
        pabdata.raw[i++] = data;
        if(i == 9) {
          switch(pabdata.pab.cmd) {
            case HEXCMD_OPEN:
              hex_open(pabdata.pab);
              break;
            case HEXCMD_CLOSE:
              hex_close(pabdata.pab);
              break;
            case HEXCMD_DELETE_OPEN:
              break;
            case HEXCMD_READ:
              hex_read(pabdata.pab);
              break;
            case HEXCMD_WRITE:
              hex_write(pabdata.pab);
              break;
          }
          uart_putc(13);
          uart_putc(10);
          i = 0;  // not sure why this has to be here
        }
      }
    }

  }
}

