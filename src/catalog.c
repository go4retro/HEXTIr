#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ff.h>
#include "config.h"
#include "hexbus.h"
#include "uart.h"
#include "catalog.h"

extern FATFS fs;  // from main.c

static const UCHAR   pgm_header[] = {0x80,0x03};
static const UCHAR   pgm_trailer[] = {0xff,0x7f,0x03,0x86,0x00,0x20, 0x00};

static const uint8_t pgm_header_len = 4;  // number of bytes in pgm_header + 2 bytes for file length
static const uint8_t pgm_trailer_len = 7; // number of bytes in the pgm_trailer
static const uint8_t pgm_record_len = 33; // length record (constant here), includes len. of line number
static const uint8_t pgm_line_len = 31;   // length of line, just the recordlen without len of line number (2 bytes)
static const uint8_t pgm_str_len = 27;    // length of string without terminating zero

void pgm_cat_open(uint16_t num_records) {
  uint16_t i = pgm_record_len * num_records + pgm_header_len;
  hex_putc(pgm_header[0],FALSE);
  hex_putc(pgm_header[1],FALSE);
  hex_putc(i & 255, FALSE);
  hex_putc(i >> 8, TRUE);
}

void pgm_cat_close(void) {
  for (uint8_t i = 0; i < sizeof(pgm_trailer); i++) {
    hex_putc(pgm_trailer[i], i + 1 == sizeof(pgm_trailer));
  }
}

void pgm_cat_record(uint16_t lineno, uint32_t fsize, const char* filename, char attrib) {
    uint8_t fsize_k = (uint8_t)(fsize / 1000);
    uint8_t rem = ((fsize % 1000) / 100);
    hex_puti(lineno, FALSE);                // line number
    hex_putc(pgm_line_len, FALSE);          // length of next "code" line (without len. of line number)
    hex_putc(0xca, FALSE);                  // 0xca : token for unquoted string, next data is string
    hex_putc(pgm_str_len, FALSE);           // length of the string without terminating zero
    hex_putc('!', FALSE);                   // separator char line number / string
    if(fsize_k > 9)
      hex_putc((fsize_k / 10) % 10 + '0', FALSE);
    else
      hex_putc(' ', FALSE);
    hex_putc(fsize_k % 10 + '0', FALSE);
    hex_putc('.', FALSE);
    hex_putc(rem + '0', FALSE);
    hex_putc(' ', FALSE);
    hex_putc('\"', FALSE);
    uint8_t j;
    for(j = 0; j < 18 && j < strlen(filename) ; j++) {
      hex_putc(filename[j], FALSE);
    }
    hex_putc('\"', FALSE);
    for( ; j < 18; j++) {
      hex_putc(' ', FALSE);
    }
    hex_putc(attrib, FALSE);
    hex_putc(0, TRUE); // null termination of str.
    uart_putcrlf();
}

uint16_t pgm_file_length(uint16_t dirnum) {
  uint16_t len = dirnum * pgm_record_len + pgm_header_len + pgm_trailer_len;
  return len;
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
    if (strcmp((const char*)fno.fname, ".") == 0 || strcmp((const char*)fno.fname, "..") == 0)
      continue; // skip
    count++;
  }
  // no closedir needed.
  return count;
}



