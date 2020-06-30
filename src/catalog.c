#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#include "config.h"

#include "debug.h"
#include "drive.h"
#include "ff.h"
#include "hexbus.h"

#include "catalog.h"

// Global references
extern uint8_t buffer[BUFSIZE];
extern FATFS fs;


// ------------------------- OLD/PGM catalog -------------------------

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
  transmit_byte(pgm_header[0]);
  transmit_byte(pgm_header[1]);
  transmit_byte(i & 255);
  transmit_byte(i >> 8);
}

// called onece at the end
void cat_close_pgm(void) {
  for (uint8_t i = 0; i < sizeof(pgm_trailer); i++) {
    transmit_byte(pgm_trailer[i]);
  }
}

// called multiple times, once for each catalog entry
void cat_write_record_pgm(uint16_t lineno, uint32_t fsize, const char* filename, char attrib) {

	uint8_t i;
    char buf[5]; // needed for size_kb
    char* size_kb = cat_bytes_to_kb(fsize, buf, sizeof(buf));

    transmit_word(lineno);                // line number, 2 bytes
    transmit_byte(pgm_line_len);          // length of next "code" line (without len. of line number), 1 byte
    transmit_byte(0xca);                  // 0xca : token for unquoted string, next data is string, 1 byte
    transmit_byte(pgm_str_len);           // length of the string without terminating zero, 1 byte
    transmit_byte('!');                   // separator char line number / string 1 byte
    for (i = 0; i < 4; i++) {               // file size in kilo bytes, 4 byte
      transmit_byte(size_kb[i]);          //
    }                                       //
    transmit_byte(' ');                   // blank, 1 byte
    transmit_byte('\"');                  // quote, 1 byte
    for(i = 0; i < 18 && i < strlen(filename) ; i++) {  // file name padded with trailing blanks, 18 bytes
      transmit_byte(filename[i]);
    }
    transmit_byte('\"');                  // quote, 1 byte
    for( ; i < 18; i++) {
      transmit_byte(' ');
    }
    transmit_byte(attrib);                 // file attribute, 1 byte
    transmit_byte(0);                       // null termination of string, 1 byte
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
	transmit_word(len);                                       // length
	for(i = 0; i < strlen(size_kb)  ; i++) {                  // file size in kilo bytes, 4 byte
	  transmit_byte(size_kb[i]);                              //
	}                                                         //
	transmit_byte(',');                                       // "," separator, 1 byte
	for(i = 0; i < strlen(filename); i++) {                   // file name , max. _MAX_LFN_LENGTH bytes
	  transmit_byte(filename[i]);                             //
	}                                                         //
	transmit_byte(',');                                       // "," separator, 1 byte
	transmit_byte(attrib);                                    // file attribute, 1 byte

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

int wild_cmp(const char *pattern, const char *string)
{
  if(*pattern=='\0' && *string=='\0') // Check if string is at end or not
	  return 1;

  if((*pattern=='?' && *string!='\0')|| *pattern==*string) //Check for single character missing or match
    return wild_cmp(pattern+1,string+1);
		
  if(*pattern=='*')  // Check for multiple character missing
    return wild_cmp((char*)(pattern+1),string) || (*string!='\0' && wild_cmp(pattern, string+1));


  return 0;
}

// Convert bytes to kBytes and format to ##.# . In case size >= 100kB returns 99.9.
// TODO might make sense to send values < 1024 as actual bytes, since they will take the same number of chars...
char* cat_bytes_to_kb(uint32_t bytes, char* buf, uint8_t len) {
  int kb = bytes / 1024;
  //int rb = (int)round(((bytes % 1024)/1024.0)*10);
  int rb = (bytes % 1024)/(1024 / 10);
  //if (rb == 10) { // bugfix
  //  kb = kb + 1;
  //  rb = 0;
  //}
  if (kb > 99) { // return 99.9 for files >= 100 kB
    kb = 99;
    rb = 9;
  }
  // next 11 lines replace snprintf
  if (kb<10) {
	  if (len>0) buf[0] =' ';
	  if (len>1)  buf[1]=kb + '0';
  }
  else {
	  if (len>0) buf[0]=kb/10 + '0';
	  if (len>1) buf[1]=kb%10 + '0';
  }
  if (len>2) buf[2]='.';
  if (len>3) buf[3]=rb + '0';
  if (len>4) buf[4]=0;
  //snprintf(buf, len, "%2d.%d", kb, rb); // costs 2k extra
  return buf;
}

uint8_t hex_read_catalog(file_t *file) {
  uint8_t rc;
  BYTE res = FR_OK;
  FILINFO fno;
  # ifdef _MAX_LFN_LENGTH
  UCHAR lfn[_MAX_LFN_LENGTH+1];
  fno.lfn = lfn;
  #endif

  debug_puts_P(PSTR("\n\rRead PGM Catalog\n\r"));
  if(file != NULL) { // TODO remove this, as we've already checked for !NULL in drv_read
    transmit_word(cat_file_length_pgm(file->dirnum));  // send full length of file
    cat_open_pgm(file->dirnum);
    uint16_t i = 1;
    while(i <= file->dirnum && res == FR_OK) {
      memset(lfn, 0, sizeof(lfn));
      res = f_readdir(&file->dir, &fno);                   // read a directory item
      if (res != FR_OK || fno.fname[0] == 0)
        break;  // break on error or end of dir

      char* filename = (char*)(fno.lfn[0] != 0 ? fno.lfn : fno.fname );

      if (cat_skip_file(filename, file->pattern))
        continue; // skip certain files like "." and ".."

      char attrib = ((fno.fattrib & AM_DIR) ? 'D' : ((fno.fattrib & AM_VOL) ? 'V' : 'F'));

      debug_trace(filename, 0, strlen(filename));

      cat_write_record_pgm(i++, fno.fsize, filename, attrib);
    }
    cat_close_pgm();
    //debug_putc('>');
    rc = HEXSTAT_SUCCESS;
  } else {
    transmit_word(0);  // null file
    rc = HEXSTAT_NOT_OPEN;
  }
  transmit_byte( rc ); // status byte transmit
  hex_finish();
  return HEXERR_SUCCESS;
}

uint8_t hex_read_catalog_txt(file_t * file) {
  uint8_t rc;
  BYTE res = FR_OK;
  FILINFO fno;
  # ifdef _MAX_LFN_LENGTH
  UCHAR lfn[_MAX_LFN_LENGTH+1];
  fno.lfn = lfn;
  #endif

  debug_puts_P(PSTR("\n\rRead TXT Catalog\n\r"));
  if(file != NULL) {  // TODO remove this, as we already checked in drv_read
  // the loop is to be able to skip files that shall not be listed in the catalog
  // else we only go through the loop once
    do {
      memset(lfn, 0, sizeof(lfn));
      res = f_readdir(&file->dir, &fno); // read a directory item
      if (res != FR_OK) {
        break; // break on error, leave do .. while loop
      }
      if (fno.fname[0] == 0) {
          res = FR_NO_FILE; // OK  to set this ?
        break;  // break on end of dir, leave do .. while loop
      }

      char* filename = (char*)(fno.lfn[0] != 0 ? fno.lfn : fno.fname );
      if (cat_skip_file(filename, file->pattern))
        continue; // skip certain files like "." and "..", next do .. while
      debug_trace(filename, 0, strlen(filename));

      char attrib = ((fno.fattrib & AM_DIR) ? 'D' : ((fno.fattrib & AM_VOL) ? 'V' : 'F'));

      // write the calatog entry for OPEN/INPUT
      cat_write_txt(&(file->dirnum), fno.fsize, filename, attrib);

      // TODO why is the break below needed?
      break; // success, leave the do .. while loop
    } while(1);

    switch(res) {
      case FR_OK:
        rc = HEXSTAT_SUCCESS;
        break;
      case FR_NO_FILE:
        transmit_word(0); // zero length data
        rc = HEXSTAT_EOF;
        break;
      default:
        transmit_word(0); // zero length data
        rc = HEXSTAT_DEVICE_ERR;
        break;
    }
  } else {
    transmit_word(0);  // null file
    rc = HEXSTAT_NOT_OPEN;
  }
  transmit_byte(rc);    // status code
  hex_finish();
  return HEXERR_SUCCESS;
}

uint8_t hex_open_catalog(file_t *file, uint8_t lun, uint8_t att) {
  uint8_t rc = HEXERR_SUCCESS;
  uint16_t fsize = 0;
  BYTE res = FR_OK;

  debug_puts_P(PSTR("\n\rOpen Catalog\n\r"));
  if (!(att & OPENMODE_READ)) {
  // send back error on anything not OPENMODE_READ
    res =  FR_IS_READONLY;
  } else {
    if(file != NULL) {
      file->attr |= FILEATTR_CATALOG;
      // remove the leading $
      char* string = (char*)buffer;
      if (strlen(string) < 2 || string[1] != '/') { // if just $ or $ABC..
        string[0] = '/'; // $ -> /, $ABC/ -> /ABC/
      } else {
        string = (char*)&buffer[1]; // $/ABC/... -> /ABC/...
      }
      // separate into directory path and pattern
      char* dirpath = string;
      char* pattern = (char*)NULL;
      char* s =  strrchr(string, '/');
      if (strlen(s) > 1) {       // there is a pattern
        pattern = strdup(s+1);   // copy pattern to store it, will be freed in free_lun
        *(s+1) = '\0';           // set new terminating zero for dirpath
        debug_trace(pattern,0,strlen(pattern));
      }
      // if not the root slash, remove slash from dirpath
      if (strlen(dirpath)>1 && dirpath[strlen(dirpath)-1] == '/')
        dirpath[strlen(dirpath)-1] = '\0';
      // get the number of catalog entries from dirpath that match the pattern
      file->dirnum = cat_get_num_entries(&fs, dirpath, pattern);
      if (pattern != (char*)NULL)
        file->pattern = pattern; // store pattern, will be freed in free_lun
      // the file size is either the length of the PGM file for OLD/PGM or the max. length of the txt file for OPEN/INPUT.
      fsize = (lun == 0 ? cat_file_length_pgm(file->dirnum)  : cat_max_file_length_txt());
      res = f_opendir(&fs, &(file->dir), (UCHAR*)dirpath); // open the director
    } else {
      // too many open files.
      rc = HEXSTAT_MAX_LUNS;
    }
  }
  if (rc == HEXSTAT_SUCCESS) {
    switch(res) {
      case FR_OK:
        rc = HEXSTAT_SUCCESS;
        break;
      case FR_IS_READONLY:
        rc = HEXSTAT_OUTPUT_MODE_ERR; // is this the right return value ?
        break;
      default:
        rc = HEXSTAT_NOT_FOUND;
      break;
    }
  }
  if(!hex_is_bav()) { // we can send response
    if(rc == HEXSTAT_SUCCESS) {
      transmit_word(4);    // claims it is accepted buffer length, but looks to really be my return buffer length...
      transmit_word(fsize);
      transmit_word(0);
      transmit_byte(HEXSTAT_SUCCESS);    // status code
      hex_finish();
      return HEXERR_SUCCESS;
    } else {
      hex_send_final_response( rc );
    }
  }
  hex_finish();
  debug_putc('E');
  return HEXERR_BAV;
}




