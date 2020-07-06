#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ff.h>
#include <math.h>
#include "config.h"
#include "hexbus.h"
#include "uart.h"
#include "catalog.h"

// ------------------------ local functions --------------------------
static const FILE_SIZE_WIDTH = 5; // width of file size format

static int wild_cmp(const char *pattern, const char *string);
static uint32_t number_of_digits(uint32_t num);
static char* left_pad_with_blanks(char *buf, uint8_t width);
static char* format_file_size(uint32_t bytes, char* buf, uint8_t width);

// ------------------------- OLD/PGM catalog -------------------------

//extern FATFS fs;  // from main.c
//static const PROGMEM
UCHAR   pgm_header[] = {0x80,0x03};
//static const PROGMEM
UCHAR   pgm_trailer[] = {0xff,0x7f,0x03,0x86,0x00,0x20, 0x00};

// constants
static const uint8_t PGM_HEADER_LEN = 4;  // number of bytes in pgm_header + 2 bytes for file length
static const uint8_t PGM_TRAILER_LEN = 7; // number of bytes in the pgm_trailer
static const uint8_t PGM_RECORD_LEN = 33; // length record (constant here), includes len. of line number
static const uint8_t PGM_LINE_LEN = 31;   // length of line, just the recordlen without len of line number (2 bytes)
static const uint8_t PGM_STR_LEN = 27;    // length of string without terminating zero

// called once at the beginning
void cat_open_pgm(uint16_t num_entries) {
  uint16_t i = PGM_RECORD_LEN * num_entries + PGM_HEADER_LEN;
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
	uint8_t width=FILE_SIZE_WIDTH;
    char buf[width+1]; // needed for size_kb
    char* file_size = format_file_size(fsize, buf, width);

    hex_puti(lineno, FALSE);                // line number, 2 bytes
    hex_putc(PGM_LINE_LEN, FALSE);          // length of next "code" line (without len. of line number), 1 byte
    hex_putc(0xca, FALSE);                  // 0xca : token for unquoted string, next data is string, 1 byte
    hex_putc(PGM_STR_LEN, FALSE);           // length of the string without terminating zero, 1 byte
    hex_putc('!', FALSE);                   // separator char line number / string 1 byte
    for (i = 0; i < width; i++) {               // file size in bytes or kiB, 5 bytes
      hex_putc(file_size[i], FALSE);          //
    }                                       //
    hex_putc(' ', FALSE);                   // blank, 1 byte
    hex_putc('\"', FALSE);                  // quote, 1 byte
    for(i = 0; i < 17 && i < strlen(filename) ; i++) {  // file name padded with trailing blanks, 17 bytes
      hex_putc(filename[i], FALSE);
    }
    hex_putc('\"', FALSE);                  // quote, 1 byte
    for( ; i < 17; i++) {
      hex_putc(' ', FALSE);
    }
    hex_putc(attrib, FALSE);                 // file attribute, 1 byte
    hex_putc(0, TRUE);                       // null termination of string, 1 byte
                                             // in total 33 bytes
}

uint16_t cat_file_length_pgm(uint16_t num_entries) {
  uint16_t len = num_entries * PGM_RECORD_LEN + PGM_HEADER_LEN + PGM_TRAILER_LEN;
  return len;
}

// ------------------------- OPEN/INPUT catalog -------------------------

uint16_t cat_max_file_length_txt(void) {
  // 5 bytes for file size in bytes or kiB plus
  // 1 byte for "," separator plus
  // _MAX_LFN_LENGTH bytes max. for file name plus
  // 1 byte for "," separator plus
  // 1 byte for file attribute (F,D,V,..)
  uint16_t len = FILE_SIZE_WIDTH + 1 + _MAX_LFN_LENGTH + 1 + 1;
  return len;
}

// Output looks like : 10.2,HELLO.PGM,F
void cat_write_txt(uint16_t* dirnum, uint32_t fsize, const char* filename, char attrib) {
	uint8_t i;
	uint8_t width = FILE_SIZE_WIDTH;
    char buf[width+1];
    char* file_size = format_file_size(fsize, buf, width);

	int len = strlen(file_size) + 1 + strlen(filename) + 1 + 1; // length of data transmitted
	hex_puti(len, FALSE);                                     // length
	for(i = 0; i < strlen(file_size)  ; i++) {                // file size in kilo bytes, 5 byte
	  hex_putc(file_size[i], FALSE);                          //
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
uint16_t cat_get_num_entries(FATFS* fsp, const char* directory, const char* pattern) {
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
    if (cat_skip_file(filename, pattern))
    	continue; // skip certain files like "." and ".."
    count++;
  }
  // no closedir needed.
  return count;
}

// ----------------------------- local -----------------------------------
// Return true if catalog entry shall be skipped.
BOOL cat_skip_file(const char* filename, const char* pattern) {
	BOOL skip = FALSE;
	if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
		skip = TRUE;
	}
	else if (pattern != (char*)NULL) {
		skip = (wild_cmp(pattern, filename) == 0 ? TRUE : FALSE); // skip, if pattern does not match
	}
	return skip;
}

// Return true if string matches the pattern.
static int wild_cmp(const char *pattern, const char *string)
{
  if(*pattern=='\0' && *string=='\0') // Check if string is at end or not
	  return 1;

  if((*pattern=='?' && *string!='\0')|| *pattern==*string) //Check for single character missing or match
    return wild_cmp(pattern+1,string+1);
		
  if(*pattern=='*')  // Check for multiple character missing
    return wild_cmp((char*)(pattern+1),string) || (*string!='\0' && wild_cmp(pattern, string+1));


  return 0;
}

// Return the number of digits of a decimal number.
static uint32_t number_of_digits(uint32_t num) {
  return  (num == 0) ? 1  : ((uint32_t)log10(num) + 1);
}

static char* left_pad_with_blanks(char *buf, uint8_t width) {
  int shift = width - strlen(buf);
  if (shift>0) {
    memmove(&buf[shift], buf, strlen(buf)+1);
    memset(buf,' ', shift);
  }
  return buf;
}

/**
 * Format the file size in bytes to an output of certain width.
 * Returns a char* pointer to the formatted output.
 * The working buffer buf must be at least of size width+1.
 * If the file size number of digits is smaller or equal the given
 * width, the output is in bytes, else the output is in kiB with an
 * accuracy of 0.1 kiB. If the file size it too large to fit into the
 * given width, a number of '?' is returned.
 * Example : width=5
              12345
 * 100    -> "  100"    bytes
 * 99999  -> "99999"    bytes
 * 100000 -> " 97.6"    kiB
 * 1023999-> "999.9"    kiB
 * 1024000-> "?????"    with too small
 */
static char* format_file_size(uint32_t bytes, char* buf, uint8_t width) {
  if (number_of_digits(bytes) <= width)  {
    itoa(bytes,buf,10);
    left_pad_with_blanks(buf, width);
  }
  else {
    int kb = bytes / 1024;
    if ( number_of_digits(kb)+2 <= width) {
      int rb = (bytes % 1024)/(1024 / 10.0);
      itoa(kb,buf,10);
      int l=strlen(buf);
      buf[l]='.';
      itoa(rb,&buf[l+1],10);
      left_pad_with_blanks(buf, width);
    }
    else {
      memset(buf, '?', width);
      buf[width]=0;
    }
  }
  return buf;
}







