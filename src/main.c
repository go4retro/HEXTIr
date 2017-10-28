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

#include <avr/io.h>
#include <inttypes.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "config.h"
#include "diskio.h"
#include "hexbus.h"
#include "ff.h"
#include "timer.h"
#include "uart.h"

FATFS fs;
uint8_t buffer[64];

typedef struct _luntbl {
  uint8_t used;
  uint8_t lun;
  FIL fp;
} luntbl;

luntbl files[MAX_OPEN_FILES]; //file number to file mapping


typedef struct _pab_t {
  uint8_t dev;
  uint8_t cmd;
  uint8_t lun;
  uint16_t record;
  uint16_t buflen;
  uint16_t datalen;
} pab_t;

typedef struct _pab_raw_t {
  union {
    pab_t pab;
    uint8_t raw[9];
  };
} pab_raw_t;

FIL* find_lun(uint8_t lun) {
  uint8_t i;

  for(i=0;i < MAX_OPEN_FILES; i++) {
    if(files[i].used && files[i].lun == lun) {
      return &(files[i].fp);
    }
  }
  return NULL;
}

FIL* reserve_lun(uint8_t lun) {
  uint8_t i;

  for(i = 0; i < MAX_OPEN_FILES; i++) {
    if(!files[i].used) {
      files[i].used = TRUE;
      return &(files[i].fp);
    }
  }
  return NULL;
}

void free_lun(uint8_t lun) {
  uint8_t i;

  for(i=0;i < MAX_OPEN_FILES; i++) {
    if(files[i].used && files[i].lun == lun) {
      files[i].used = FALSE;
    }
  }
}

void init(void) {
  uint8_t i;

  for(i=0;i < MAX_OPEN_FILES; i++) {
    files[i].used = FALSE;
  }
}

int16_t hex_getdata(uint8_t buf[256], uint16_t len) {
  uint16_t i = 0;
  int16_t data;

  while(i < len) {
    if(hex_is_bav()){
      return HEXERR_BAV;
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

int8_t hex_write(pab_t pab) {
  uint8_t rc = HEXSTAT_SUCCESS;
  uint16_t len;
  uint8_t i;
  UINT written;
  FIL* fp;
  BYTE res = FR_OK;

  uart_putc(13);
  uart_putc(10);
  uart_putc('>');
  fp = find_lun(pab.lun);
  //hex_release_bus_recv();
  len = pab.datalen;
  while(len) {
    i = (len >= sizeof(buffer) ? sizeof(buffer) : len);
    hex_release_bus_recv();
    if(hex_getdata(buffer, i))
      rc = HEXERR_BAV;
    if(fp != NULL && res == FR_OK && rc == HEXSTAT_SUCCESS) {
      res = f_write(fp, buffer, i, &written);
      uart_putc(13);
      uart_putc(10);
      uart_trace(buffer,0,written);
    }
    if(written != i) {
      rc = HEXSTAT_TOO_LONG;  // generic error.
    }
    len -= i;
  }
  if(rc == HEXSTAT_SUCCESS) {
    switch(res) {
      case FR_OK:
        rc = HEXSTAT_SUCCESS;
        break;
      default:
        rc = HEXSTAT_DEVICE_ERR;
        break;
    }
  }
  uart_putc('>');
  hex_release_bus_recv();
  _delay_us(200);
  if(!hex_is_bav()) { // we can send response
    hex_puti(0, FALSE);  // zero length data
    hex_putc(rc, FALSE);    // status code
    return HEXERR_SUCCESS;
  } else {
    return HEXERR_BAV;
  }

}

uint8_t hex_read(pab_t pab) {
  uint8_t rc;
  uint8_t i;
  uint16_t len = 0;
  UINT read;
  BYTE res = FR_OK;
  FIL* fp;

  uart_putc(13);
  uart_putc(10);
  uart_putc('<');
  fp = find_lun(pab.lun);
  hex_release_bus_recv();
  _delay_us(200);
  if(fp != NULL) {
    hex_puti(fp->fsize, TRUE);  // send full length of file
    while(len < fp->fsize) {
      res = f_read(fp, buffer, sizeof(buffer), &read);
      if(!res) {
        uart_putc(13);
        uart_putc(10);
        uart_trace(buffer,0,read);
      }
      hex_release_bus_send();
      if(FR_OK == res) {
        for(i = 0; i < read; i++) {
          hex_putc(buffer[i], (i + 1) == read);
        }
        len+=read;
      }  // TODO if reading fails, we need to error out.
    }
    switch(res) {
      case FR_OK:
        rc = HEXSTAT_SUCCESS;
        break;
      default:
        rc = HEXSTAT_DEVICE_ERR;
        break;
    }
    uart_putc('>');
    hex_release_bus_send();
  } else {
    hex_puti(0, FALSE);  // null file
    rc = HEXSTAT_NOT_OPEN;
  }
  hex_putc(rc, FALSE);    // status code
  return 0;
}

uint8_t hex_open(pab_t pab) {
  uint16_t i = 0;
  uint8_t data;
  uint16_t len = 0;
  uint8_t att = 0;
  uint8_t rc;
  BYTE mode = 0;
  uint16_t fsize = 0;
  FIL* fp;
  BYTE res;

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
    return HEXERR_BAV;
  buffer[pab.datalen-3] = 0;

  switch (att & (0x80 | 0x40)) {
    case 0x00:  // append mode
      mode = FA_WRITE;
      break;
    case 0x80: // write, truncate if present. Maybe...
      mode = FA_WRITE | FA_CREATE_ALWAYS;
      break;
    case 0xC0:
      mode = FA_WRITE | FA_READ;
      break;
    default:
      mode = FA_READ;
      break;
  }
  fp = reserve_lun(pab.lun);
  if(fp != NULL) {
    uart_putc('^');
    res = f_open(&fs,fp,(UCHAR*)buffer,mode);
    switch(res) {
      case FR_OK:
        rc = HEXSTAT_SUCCESS;
        fsize = fp->fsize;
        break;
      case FR_IS_READONLY:
        rc = HEXSTAT_NOT_WRITE;
        break;
      case FR_INVALID_NAME:
        rc = HEXSTAT_FILE_NAME_INVALID;
        break;
      case FR_RW_ERROR:
        rc = HEXSTAT_DEVICE_ERR;
        break;
      case FR_EXIST:
      case FR_DENIED:
      case FR_IS_DIRECTORY:
      case FR_NO_PATH:
      default:
        rc = HEXSTAT_NOT_FOUND;
        break;
    }
  } else { // too many open files.
    rc = HEXSTAT_MAX_LUNS;
  }
  hex_release_bus_recv();
  _delay_us(200);  // wait a bit...
  if(!hex_is_bav()) { // we can send response
    hex_puti(2, FALSE);    // claims it is accepted buffer length, but looks to really be my return buffer length...
    switch(att) {
      case 64: // read
        hex_puti(fsize, FALSE);
        break;
      default: // write
        hex_puti((len ? len : sizeof(buffer)), FALSE);  // this is evidently the value we should send back.  Not sure what one does if one needs to send two of these.
        break;
    }
    hex_putc(rc, FALSE);    // status code
    return HEXERR_SUCCESS;
  }
  return HEXERR_BAV;
}

uint8_t hex_close(pab_t pab) {
  uint8_t rc;
  FIL* fp;
  BYTE res;

  uart_putc(13);
  uart_putc(10);
  uart_putc('<');
  fp = find_lun(pab.lun);
  if(fp != NULL) {
    res = f_close(fp);
    free_lun(pab.lun);
    switch(res) {
      case FR_OK:
        rc = HEXSTAT_SUCCESS;
        break;
      case FR_INVALID_OBJECT:
      case FR_NOT_READY:
      default:
        rc = HEXSTAT_DEVICE_ERR;
        break;
    }
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }
  hex_release_bus_recv();
  _delay_us(200);  // wait a bit...
  if(!hex_is_bav()) { // we can send response
    hex_puti(0, FALSE);
    hex_putc(rc, FALSE);
    return HEXERR_SUCCESS;
  }
  return HEXERR_BAV;
}

uint8_t hex_reset_bus(pab_t pab) {
  uart_putc(13);
  uart_putc(10);
  uart_putc('R');
  hex_release_bus_recv();
  return HEXSTAT_SUCCESS;
}

int main(void) __attribute__((OS_main));
int main(void) {
  uint8_t i = 0;
  int16_t data;
  uint8_t ignore_cmd = FALSE;
  pab_raw_t pabdata;
  BYTE res;

  hex_init();
  uart_init();
  disk_init();
  timer_init();
  init();

  sei();

  res=f_mount(1, &fs);
  uart_puthex(res);
  uart_putcrlf();
  /*
  //printf("f_mount result=%d\n",res);
  //res = f_open(&fs,&fp,(UCHAR*)"TESTFILE.BIN",FA_WRITE|FA_CREATE_ALWAYS);
  //printf("f_open result=%d\n",res);
  //do {
  //  buf[i] = i;
  //  i++;
  //} while(i);
  //res = f_write(&fp, buf, 256, &len);
  //printf("f_write result=%d\n",res);
  //printf("f_write len=%d\n",len);
  //res = f_close(&fp);
  //printf("f_close result=%d\n",res);

  res = f_open(&fs,&fp,(UCHAR*)"games",FA_READ);
  uart_puthex(res);
  uart_putcrlf();
  //printf("f_open result=%d\n",res);
  res = f_read(&fp, buffer, sizeof(buffer), &len);
  //printf("f_read result=%d\n",res);
  //printf("f_read len=%d\n",len);

  uart_trace(buffer,0,len);
  res = f_close(&fp);
  //printf("f_close result=%d\n",res);*/

  pabdata.pab.dev = 0;
  pabdata.pab.cmd = 0;
  pabdata.pab.lun = 0;
  pabdata.pab.record = 0;
  pabdata.pab.buflen = 0;
  pabdata.pab.datalen = 0;

  uart_putc('*');
  while(TRUE) {
    while(hex_is_bav()) {
    }
    uart_putc('>');
    while(!hex_is_bav()) {
      while(!ignore_cmd) {
        data = hex_getc(i == 8);  // if it's the last byte, hold the hsk low.
        if(-1 < data) {
          pabdata.raw[i++] = data;
          if(i == 1 && pabdata.pab.dev != DEFAULT_DEVICE_ID) {
            ignore_cmd = TRUE;
          } else if(i == 9) { // exec command
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
              case HEXCMD_RESET_BUS:
                hex_reset_bus(pabdata.pab);
                break;
              default:
                uart_putc(13);
                uart_putc(10);
                uart_putc('E');
                hex_release_bus_recv();
                break;
            }
            ignore_cmd = TRUE;  // in case someone sends more data, ignore it.
          }
        } else {
          uart_putc('%');
          uart_puthex(data);
          i = 0;
        }
      }
    }
    uart_putc(13);
    uart_putc(10);
    i = 0;
    ignore_cmd = FALSE;
  }
}

