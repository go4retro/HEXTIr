#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ff.h>
#include "catalog.h"

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

static FRESULT update_proglen(FIL *fp, UINT pgmlen) {
  UINT written;
  FRESULT res;
  // write correct length
  DWORD fpos;
  fpos = fp->fptr; // store file pointer position
  f_lseek(fp, sizeof(header));
  res = f_write(fp, &pgmlen, sizeof(pgmlen), &written);
  f_lseek(fp, fpos);
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

/**
 * Open the catalog file ("$") and add the header.
 */
FRESULT Catalog_open(Catalog* self, FATFS* fs,  const char* fname) {
  int res = FR_DENIED;
  if (self->fp == NULL) {
	self->fp = malloc(sizeof(FIL));
    self->linenumber = 0;
    self->pgmlen = sizeof(header) + sizeof(self->pgmlen);

    res = f_open(fs, self->fp, (UCHAR*)fname, FA_OPEN_ALWAYS | FA_WRITE);
    if (res == FR_OK) {
      write_header(self->fp);
      write_proglen(self->fp, self->pgmlen);
    }
    else {
      free(self->fp);
    }
  }
  return res;
}

/**
 * Close the catalog file ("$") and reset the Catalog structure.
 */
void Catalog_close(Catalog* self) {
  if (self->fp != NULL) {
    update_proglen(self->fp, self->pgmlen);
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



