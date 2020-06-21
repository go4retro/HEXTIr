#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ff.h>
#include <math.h>
#include "config.h"
#include "hexbus.h"
#include "uart.h"
#include "catalog.h"

// ------------------------- OLD/PGM catalog -------------------------

//extern FATFS fs;  // from main.c
//static const PROGMEM
UCHAR   pgm_header[] = {0x80,0x03};
//static const PROGMEM
UCHAR   pgm_trailer[] = {0xff,0x7f,0x03,0x86,0x00,0x20, 0x00};

static const uint8_t pgm_header_len = 4;  // number of bytes in pgm_header + 2 bytes for file length
static const uint8_t pgm_trailer_len = 7; // number of bytes in the pgm_trailer
static const uint8_t pgm_record_len = 33; // length record (constant here), includes len. of line number
static const uint8_t pgm_line_len = 31;   // length of line, just the recordlen without len of line number (2 bytes)
static const uint8_t pgm_str_len = 27;    // length of string without terminating zero

// called once at the beginning
void cat_open_pgm(uint16_t num_entries) {
  uint16_t i = pgm_record_len * num_entries + pgm_header_len;
  hex_putc(pgm_header[0],FALSE);
  hex_putc(pgm_header[1],FALSE);
  hex_putc(i & 255, FALSE);
  hex_putc(i >> 8, TRUE);
}

// called onece at the end
void cat_close_pgm(void) {
  for (uint8_t i = 0; i < sizeof(pgm_trailer); i++) {
    hex_putc(pgm_trailer[i], i + 1 == sizeof(pgm_trailer));
  }
}

// called multiple times, once for each catalog entry
void cat_write_record_pgm(uint16_t lineno, uint32_t fsize, const char* filename, char attrib) {

	uint8_t i;
    char buf[5]; // needed for size_kb
    char* size_kb = cat_bytes_to_kb(fsize, buf, sizeof(buf));

    hex_puti(lineno, FALSE);                // line number, 2 bytes
    hex_putc(pgm_line_len, FALSE);          // length of next "code" line (without len. of line number), 1 byte
    hex_putc(0xca, FALSE);                  // 0xca : token for unquoted string, next data is string, 1 byte
    hex_putc(pgm_str_len, FALSE);           // length of the string without terminating zero, 1 byte
    hex_putc('!', FALSE);                   // separator char line number / string 1 byte
    for (i = 0; i < 4; i++) {               // file size in kilo bytes, 4 byte
      hex_putc(size_kb[i], FALSE);          //
    }                                       //
    hex_putc(' ', FALSE);                   // blank, 1 byte
    hex_putc('\"', FALSE);                  // quote, 1 byte
    for(i = 0; i < 18 && i < strlen(filename) ; i++) {  // file name padded with trailing blanks, 18 bytes
      hex_putc(filename[i], FALSE);
    }
    hex_putc('\"', FALSE);                  // quote, 1 byte
    for( ; i < 18; i++) {
      hex_putc(' ', FALSE);
    }
    hex_putc(attrib, FALSE);                 // file attribute, 1 byte
    hex_putc(0, TRUE);                       // null termination of string, 1 byte
                                             // in total 33 bytes
}

uint16_t cat_file_length_pgm(uint16_t num_entries) {
  uint16_t len = num_entries * pgm_record_len + pgm_header_len + pgm_trailer_len;
  return len;
}

// ------------------------- OPEN/INPUT catalog -------------------------

uint16_t cat_max_file_length_txt(void) {
  // 4 bytes for file size in kB plus
  // 1 byte for "," separator plus
  // _MAX_LFN_LENGTH bytes max. for file name plus
  // 1 byte for "," separator plus
  // 1 byte for file attribute (F,D,V,..)
  uint16_t len = 4 + 1 + _MAX_LFN_LENGTH + 1 + 1;
  return len;
}

// Output looks like : 10.2,HELLO.PGM,F
void cat_write_txt(uint16_t* dirnum, uint32_t fsize, const char* filename, char attrib) {
	uint8_t i;
    char buf[5];
    char* size_kb = cat_bytes_to_kb(fsize, buf, sizeof(buf));

	int len = strlen(size_kb) + 1 + strlen(filename) + 1 + 1; // length of data transmitted
	hex_puti(len, FALSE);                                     // length
	for(i = 0; i < strlen(size_kb)  ; i++) {                  // file size in kilo bytes, 4 byte
	  hex_putc(size_kb[i], FALSE);                            //
	}                                                         //
	hex_putc(',', FALSE);                                     // "," separator, 1 byte
	for(i = 0; i < strlen(filename); i++) {                   // file name , max. _MAX_LFN_LENGTH bytes
	  hex_putc(filename[i], FALSE);                           //
	}                                                         //
	hex_putc(',', FALSE);                                     // "," separator, 1 byte
	hex_putc(attrib, TRUE);                                   // file attribute, 1 byte

	*dirnum = *dirnum - 1; // decrement dirnum, used here as entries left to detect EOF for catalog when dirnum = 0
}

// ----------------------------- common -----------------------------------
// Get number of directory (=catalog) entries.
uint16_t cat_get_num_entries(FATFS* fsp, const char* directory) {
  FRESULT res;
  DIR dir;
  FILINFO fno;
# ifdef _MAX_LFN_LENGTH
UCHAR lfn[_MAX_LFN_LENGTH+1];
fno.lfn = lfn;
#endif
  uint16_t count = 0;
  res = f_opendir(fsp, &dir, (UCHAR*)directory); // open the directory
  while(res == FR_OK) {
    res = f_readdir(&dir, &fno);                   // read a directory item
    if (res != FR_OK || fno.fname[0] == 0)
      break;  // break on error or end of dir
    char* filename = (char*)(fno.lfn[0] != 0 ? fno.lfn : fno.fname );
    if (cat_skip_file(filename))
    	continue; // skip certain files like "." and ".."
    count++;
  }
  // no closedir needed.
  return count;
}

// Return true if catalog entry shall be skipped.
BOOL cat_skip_file(const char* filename) {
	BOOL skip = FALSE;
	if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
		skip = TRUE;
	}
	return skip;
}

// Convert bytes to kBytes and format to ##.# . In case size >= 100kB returns 99.9.
char* cat_bytes_to_kb(uint32_t bytes, char* buf, uint8_t len) {
  int kb = bytes / 1024;
  int rb = (int)round(((bytes % 1024)/1024.0)*10);
  if (kb > 99) { // return 99.9 for files >= 100 kB
    kb = 99;
    rb = 9;
  }
  snprintf(buf, len, "%2d.%d", kb, rb);
  return buf;
}


