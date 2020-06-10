#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ff.h>

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

static UCHAR write_line(FIL *fp, UINT lineno, char* buf) {
  UINT written;
  UCHAR  linelen;
  UCHAR  recordlen;
  //UCHAR  remtoken = 0x82;
  UCHAR  strtoken = 0xca;
  UCHAR  zero = 0x00;
  UCHAR  slen;

  slen = strlen(buf);
  linelen = slen + sizeof(recordlen) + sizeof(strtoken)
    + sizeof(slen)+ sizeof(zero); 
  f_write(fp, &lineno, sizeof(lineno), &written);
  f_write(fp, &linelen, sizeof(linelen), &written);
  f_write(fp, &strtoken, sizeof(strtoken), &written);
  f_write(fp, &slen, sizeof(slen), &written);
  f_write(fp, buf, slen, &written);
  f_write(fp, &zero, sizeof(zero), &written);
  recordlen = linelen + sizeof(lineno);
  return recordlen;
}

static UINT pgmlen = 0;
static UCHAR linenumber = 0;
static FIL *fp = NULL;

FRESULT catalog_open(FATFS* fs,  const char* fname) {
  int res = FR_DENIED;
  if (fp == NULL) {
	fp = malloc(sizeof(FIL));
    linenumber = 0;
    pgmlen = sizeof(header) + sizeof(pgmlen);

    res = f_open(fs, fp, (UCHAR*)fname, FA_OPEN_ALWAYS | FA_WRITE);
    if (res == FR_OK) {
      write_header(fp);
      write_proglen(fp, pgmlen);
    }
  }
  return res;
}

void catalog_close(void) {
  if (fp != NULL) {
    update_proglen(fp, pgmlen);
    write_trailer(fp);
    f_close(fp);
    free(fp);
    fp = NULL;
  }
}


void catalog_write(char* entry) {
  UCHAR recordlen;
  if (fp != NULL) {
    linenumber = linenumber + 1;
    recordlen = write_line(fp,linenumber, entry);
    pgmlen = pgmlen + recordlen;
  }
}

/*
int main(int argc, char* argv[]) {
   catalog_open("/tmp/t.pgm");
   catalog_write(": 10 HALLO.PGM FIL");
   catalog_write(": 103 HAHA.TXT FIL");
   catalog_write(": 0 HUHU DIR");
   catalog_close();
}
*/

