#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ff.h>
#include "config.h"
#include "hexbus.h"
#include "uart.h"
#include "catalog.h"

extern FATFS fs;  // from main.c

UCHAR  header[]= {0x80,0x03};
UCHAR  trailer[] = {0xff,0x7f,0x03,0x86,0x00,0x20, 0x00};

static FRESULT write_header(FIL *fp) {
  UINT written;
  FRESULT res;
  res = f_write(fp, &header, sizeof(header), &written);
  return res;
}

static FRESULT write_trailer(FIL *fp) {
  UINT written;
  FRESULT res;
  res = f_write(fp, &trailer, sizeof(trailer), &written);
  return res;
}

static FRESULT write_proglen(FIL *fp, UINT pgmlen) {
  UINT written;
  FRESULT res;
  res = f_write(fp, &pgmlen, sizeof(pgmlen), &written);
  return res;
}

static UCHAR write_line(FIL *fp, UINT lineno, const char* cstr) {
  UINT written;
  UCHAR  linelen;
  UCHAR  recordlen;
  //UCHAR  remtoken = 0x82;
  UCHAR  strtoken = 0xca;
  UCHAR  zero = 0x00;
  UCHAR  slen;

  slen = strlen(cstr);
  linelen = slen + sizeof(recordlen) + sizeof(strtoken)
    + sizeof(slen)+ sizeof(zero); 
  f_write(fp, &lineno, sizeof(lineno), &written);
  f_write(fp, &linelen, sizeof(linelen), &written);
  f_write(fp, &strtoken, sizeof(strtoken), &written);
  f_write(fp, &slen, sizeof(slen), &written);
  f_write(fp, cstr, slen, &written);
  f_write(fp, &zero, sizeof(zero), &written);
  recordlen = linelen + sizeof(lineno);
  uart_puthex(recordlen >> 8);
  uart_puthex(recordlen & 255);
  uart_putcrlf();
  return recordlen;
}

/**
 * Initialize the catalog structure.
 */
void Catalog_init(Catalog* self){
  self->pgmlen = 0;
  self->linenumber = 0;
  self->fp = NULL;
}

void cat_open(uint16_t num) {
  uint16_t i = 33 * num + 4;

  hex_putc(header[0],FALSE);
  hex_putc(header[1],FALSE);
  hex_putc(i & 255, FALSE);
  hex_putc(i >> 8, TRUE);
}
/**
 * Open the catalog file ("$") and add the header.
 */
FRESULT Catalog_open(Catalog* self, FATFS* fs,  const char* fname, uint16_t num) {
  int res = FR_DENIED;
  if (self->fp == NULL) {
	self->fp = malloc(sizeof(FIL));
    self->linenumber = 0;
    self->pgmlen = sizeof(header) + sizeof(self->pgmlen);
    uint16_t i = sizeof(header) + 33*num + 2;
    uart_puthex(i>>8);
    uart_puthex(i);
    uart_putcrlf();


    res = f_open(fs, self->fp, (UCHAR*)fname, FA_CREATE_ALWAYS | FA_WRITE);
    if (res == FR_OK) {
      write_header(self->fp);
      write_proglen(self->fp, i);
    }
    else {
      free(self->fp);
    }
  }
  return res;
}

void cat_close(void) {
  for (uint8_t i = 0; i < sizeof(trailer); i++) {
    hex_putc(trailer[i], i + 1 == sizeof(trailer));
  }
}

/**
 * Close the catalog file ("$") and reset the Catalog structure.
 */
void Catalog_close(Catalog* self) {
  if (self->fp != NULL) {
    //update_proglen(self->fp, self->pgmlen);
    write_trailer(self->fp);
    f_close(self->fp);
    free(self->fp);
    self->pgmlen = 0;
    self->linenumber = 0;
    self->fp = NULL;
  }
}

/**
 * Write a record to the catalog.
 */
void Catalog_write(Catalog* self, const char* cstr) {
  UCHAR recordlen;
  if (self->fp != NULL) {
    self->linenumber = self->linenumber + 1;
    recordlen = write_line(self->fp, self->linenumber, cstr);
    self->pgmlen = self->pgmlen + recordlen;
  }
}

uint16_t cat_get_length(const char* directory) {
  FRESULT res;
  DIR dir;
  FILINFO fno;
# ifdef _MAX_LFN_LENGTH
UCHAR lfn[_MAX_LFN_LENGTH+1];
fno.lfn = lfn;
#endif
  uint16_t count = 0;

  res = f_opendir(&fs, &dir, (UCHAR*)directory); // open the directory
  while(res == FR_OK) {
    res = f_readdir(&dir, &fno);                   // read a directory item
    if (res != FR_OK || fno.fname[0] == 0)
      break;  // break on error or end of dir
    if (strcmp((const char*)fno.fname, "$") == 0)  // TODO remove this
      continue; // skip the catalog file
    if (strcmp((const char*)fno.fname, ".") == 0 || strcmp((const char*)fno.fname, "..") == 0)
      continue; // skip
    if (fno.fsize < 0)
      continue; // skip
    count++;
  }
  // no closedir needed.
  return count;
}



