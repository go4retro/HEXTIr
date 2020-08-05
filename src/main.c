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
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "config.h"
#include "diskio.h"
#include "ff.h"
#include "hexbus.h"
#include "led.h"
#include "timer.h"
#include "uart.h"
#include "catalog.h"

#define FR_EOF     255    // We need an EOF error for hexbus_read.

FATFS fs;
uint8_t buffer[40];

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

#define FILEATTR_READ     1
#define FILEATTR_WRITE    2
#define FILEATTR_PROTECT  4
#define FILEATTR_DISPLAY  8
#define FILEATTR_CATALOG  0x80

typedef struct _file_t {
  FIL fp;
  DIR dir;
  uint8_t attr;
  uint16_t dirnum;
  char* pattern;
} file_t;

typedef struct _luntbl_t {
  uint8_t used;
  uint8_t lun;
  file_t file;
} luntbl_t;

uint8_t open_files = 0;

luntbl_t files[MAX_OPEN_FILES]; //file number to file mapping

static file_t* find_lun(uint8_t lun) {
  uint8_t i;

  for(i=0;i < MAX_OPEN_FILES; i++) {
    if(files[i].used && files[i].lun == lun) {
      return &(files[i].file);
    }
  }
  uart_putc('n');
  return NULL;
}

static file_t* reserve_lun(uint8_t lun) {
  uint8_t i;

  for(i = 0; i < MAX_OPEN_FILES; i++) {
    if(!files[i].used) {
      files[i].used = TRUE;
      files[i].lun = lun;
      files[i].file.pattern = (char*)NULL;
      files[i].file.attr = 0;
      open_files++;
      set_busy_led(TRUE);
      return &(files[i].file);
    }
  }
  return NULL;
}

static void free_lun(uint8_t lun) {
  uint8_t i;

  for(i=0;i < MAX_OPEN_FILES; i++) {
    if(files[i].used && files[i].lun == lun) {
      files[i].used = FALSE;
      if (files[i].file.pattern != (char*) NULL)
    	  free(files[i].file.pattern);
      open_files--;
      set_busy_led(open_files);
    }
  }
}

static void init(void) {
  uint8_t i;

  for(i=0;i < MAX_OPEN_FILES; i++) {
    files[i].used = FALSE;
  }
}

/**
 * Get the size of the next value stored in INTERNAL format.
 */
static uint16_t next_value_size_internal(file_t* file) {
  // if data stored in INTERNAL format send next value
  uint16_t read;    // how many bytes are read
  uint8_t val_len;  // length of next value
  uint32_t val_ptr; // start position in file
  uint8_t res;      // read result

  val_ptr= file->fp.fptr;
  res = f_read(&(file->fp), &val_len, 1, &read);
  f_lseek(&(file->fp), val_ptr);
  return (res == FR_OK ? val_len + 1 : 0);
}



/**
 * Get the size of the next value stored in DISPLAY format.
 * Values stored in DISPLAY format are found to be separated by one or more spaces (0x20), that is
 * spaces are the delimiters between two values. The end of a value is reached, when a space char
 * has been found. But a value can start with one or more spaces. These heading spaces are counted
 * and contribute for the value size; they are not treated as delimiters.
 * When the first non-space char is detected the data part of the value starts. The first space
 * found after the data part is the delimiter. Spaces can too be part of the data part of a string
 * value. For not to be treated as delimiters the string value has to be put in quotes to form a 'block'.
 */
static uint16_t next_value_size_display(file_t* file) {
  BYTE res;
  UINT read;
  char token;
  char delimit[] = " ";    // delimiter chars to separate values when in DISPLAY mode ( a space char)
  char openblock[] = "\""; // characters that start a block
  char closeblock[] = "\"";// characters that terminate a block
  char *block = NULL;
  int iBlock = 0;
  int iBlockIndex = 0;
  int val_len = 0;
  int iData = 0;
  uint32_t val_ptr;

  val_ptr = file->fp.fptr; // save the current position for to restore
  res = f_read(&(file->fp), &token, 1, &read);
  while (res == FR_OK && read == 1) {
	val_len++;
	if (!iData && strchr(delimit, token) != NULL) { // eat up heading spaces
	  res = f_read(&(file->fp), &token, 1, &read);
	  continue;
	}
	else {
	  iData = 1; // data start found ( first non-delimiter char)
	}
	if (iBlock) { // if token is in block
	  if (closeblock[iBlockIndex] == token) { // block ends
		iBlock = 0;
	  }
	  res = f_read(&(file->fp), &token, 1, &read);
	  continue;
	}
	if ((block = strchr(openblock, token)) != NULL) { // block starts
	  iBlock = 1;
	  iBlockIndex = block - openblock;
	  res = f_read(&(file->fp), &token, 1, &read);
	  continue;
	}
	if (strchr(delimit, token) != NULL) { // stop on first trailing delimiter
	  break;
	}
	res = f_read(&(file->fp), &token, 1, &read);
  }
  f_lseek(&(file->fp), val_ptr); // re-position the file pointer
  return (res == FR_OK ? val_len : 0);
}

/**
 * Get the size of the next stored value.
 */
static uint16_t next_value_size(file_t* file) {
	if (file->attr & FILEATTR_DISPLAY) {
	  return next_value_size_display(file);
	} else {
	  return next_value_size_internal(file);
	}
}



static uint8_t hex_read_catalog(pab_t pab) {
  uint8_t rc;
  BYTE res = FR_OK;
  file_t* file;
  FILINFO fno;
  # ifdef _MAX_LFN_LENGTH
  UCHAR lfn[_MAX_LFN_LENGTH+1];
  fno.lfn = lfn;
  #endif

  uart_putc('r');
  file = find_lun(pab.lun);
  hex_release_bus_recv();
  _delay_us(200);
  if(file != NULL) {
    hex_puti(cat_file_length_pgm(file->dirnum), FALSE);  // send full length of file
    cat_open_pgm(file->dirnum);
    uint16_t i = 1;
    while(i <= file->dirnum && res == FR_OK) {
      memset(lfn,0,sizeof(lfn));
      res = f_readdir(&file->dir, &fno);                   // read a directory item
      if (res != FR_OK || fno.fname[0] == 0)
        break;  // break on error or end of dir

      char* filename = (char*)(fno.lfn[0] != 0 ? fno.lfn : fno.fname );

      if (cat_skip_file(filename, file->pattern))
    	continue; // skip certain files like "." and ".."

      char attrib = ((fno.fattrib & AM_DIR) ? 'D' : ((fno.fattrib & AM_VOL) ? 'V' : 'F'));

      uart_trace(filename, 0, strlen(filename));
      uart_putcrlf();

      hex_release_bus_send();

      cat_write_record_pgm(i++,fno.fsize, filename,attrib);

      uart_putcrlf();
    }
    hex_release_bus_send();
    cat_close_pgm();
    uart_putc('>');
    hex_release_bus_send();
    rc = HEXSTAT_SUCCESS;
  } else {
    hex_puti(0, FALSE);  // null file
    rc = HEXSTAT_NOT_OPEN;
  }
  hex_putc(rc, FALSE);    // status code
  return 0;
}

static uint8_t hex_read_catalog_txt(pab_t pab) {
  uint8_t rc;
  BYTE res = FR_OK;
  file_t* file;
  FILINFO fno;
  # ifdef _MAX_LFN_LENGTH
  UCHAR lfn[_MAX_LFN_LENGTH+1];
  fno.lfn = lfn;
  #endif

  uart_putc('r');
  file = find_lun(pab.lun);
  hex_release_bus_recv();
  _delay_us(200);
  if(file != NULL) {
	// the loop is to be able to skip files that shall not be listed in the catalog
	// else we only go through the loop once
    do {
	  memset(lfn,0,sizeof(lfn));
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
	  uart_trace(filename, 0, strlen(filename));
	  uart_putcrlf();

	  char attrib = ((fno.fattrib & AM_DIR) ? 'D' : ((fno.fattrib & AM_VOL) ? 'V' : 'F'));

	  hex_release_bus_send();

	  // write the calatog entry for OPEN/INPUT
	  cat_write_txt(&(file->dirnum), fno.fsize, filename, attrib);

	  break; // success, leave the do .. while loop
    } while(1);

    hex_release_bus_send(); // in the right place ?

    switch(res) {
      case FR_OK:
        rc = HEXSTAT_SUCCESS;
        break;
      case FR_NO_FILE:
      	hex_puti(0, FALSE); // zero length data
    	rc = HEXSTAT_EOF;
    	break;
      default:
        hex_puti(0, FALSE); // zero length data
        rc = HEXSTAT_DEVICE_ERR;
        break;
    }
  } else {
    hex_puti(0, FALSE);  // null file
    rc = HEXSTAT_NOT_OPEN;
  }
  hex_putc(rc, FALSE);    // status code
  return 0;
}




static int16_t hex_getdata(uint8_t buf[256], uint16_t len) {
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
  }
  return HEXERR_SUCCESS;
}

static int8_t hex_return_status(pab_t pab) {
	uint8_t rc = HEXSTAT_SUCCESS;
	file_t* file;
	BYTE res = FR_OK;
	BYTE status = 0x0;

	uart_putc('>');

	file = find_lun(pab.lun);
	if(file != NULL && (file->attr & FILEATTR_CATALOG)) {
		// EOF on catalog for OPEN/INPUT only
		// each INPUT on the catalog decrements lun->dirnum
		if (pab.lun != 0 ) {
		  if (file->dirnum == 0) {
			 status = 0x80; // EOF on catalog for OPEN/INPUT only
		  }
		}
	}
	else {
		res = (file != NULL ? FR_OK : FR_NO_FILE);
		if (file->fp.fptr == file->fp.fsize) {
			status = 0x80; // EOF on file
		}
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
		hex_puti(1, FALSE);
		hex_putc(status, FALSE);
//		hex_puti(0, FALSE);  // zero length data
		hex_putc(rc, FALSE);    // status code
		return HEXERR_SUCCESS;
	} else {
		return HEXERR_BAV;
	}
}



/*
 * https://github.com/m5dk2n comments:
 *
 * What happens during the VERIFY is the following:

   The calculator sends an hex-bus open (R) command with the name of the
   stored program and receives its size in the response. Then the calculator
   sends the hex-bus verify command which contains the program it has in its
   memory. If the comparison is not successful either IO error 12 or 24 is
   sent according to the user manual. (12 = stored program larger then
   program in memory, 24 = programs differ). The calculator sends the hex-bus
   close command.

   The expectation was that IO error 12 was issued by the calculator in case
   the length of the stored program (returned in the open response) is greater
   then the length of the program in memory. But this is not the case. Instead
   the calculator starts to send (step 2) its memory content up to the length
   it got in the open response no matter if this exceeds the actual length of
   the program stored in memory! By staring at the debug output I found out
   that the bytes 2 and 3 (start counting at 0) is the actual length of the
   program. So one can compare the sizes and, in case they differ, return IO
   error 12. And although if a comparison error occured, one has read the data
   transmitted in the verify command until sending stops.
 */

static int8_t hex_verify(pab_t pab) {
	uint8_t rc = HEXSTAT_SUCCESS;
	uint16_t len;
	uint8_t i;
	UINT read;
	file_t* file;
	BYTE res = FR_OK;
	uint8_t data[sizeof(buffer)];
	uint16_t len_prog_mem = 0;
	uint16_t len_prog_stored = 0;

	uart_putc('>');
	file = find_lun(pab.lun);
	len = pab.datalen;

	res = (file != NULL ? FR_OK : FR_NO_FILE);
	int first_buffer = 1;
	while(len) {
		i = (len >= sizeof(buffer) ? sizeof(buffer) : len);
		hex_release_bus_recv();
		if(hex_getdata(buffer, i)) {
			rc = HEXERR_BAV;
		}

		if(res == FR_OK && rc == HEXSTAT_SUCCESS) {

			// length of program in memory
			if (first_buffer == 1) {
				len_prog_mem = buffer[2];
				len_prog_mem |= buffer[3] << 8;
			}

			res = f_read(&(file->fp), data, i, &read);
			if(res == FR_OK) {
				// trace
				uart_putc(13);
				uart_putc(10);
				uart_trace(data,0,read);

				// length of program on storage device
				if (first_buffer == 1) {
					len_prog_stored = data[2];
					len_prog_stored |= data[3] << 8;
				}

				if (len_prog_stored > len_prog_mem) {
					// program on disk larger then in memory
					rc = HEXSTAT_BUF_SIZE_ERR;
				}
				else {
					for (int j=0; j<read; j++) {
						if (data[j] != buffer[j]) {
							// program data on storage device differs from data in memory
							rc= HEXSTAT_VERIFY_ERR;
							break; // the for loop
						}
					}
				}
			}
		}
		if (first_buffer ==1) {
			first_buffer = 0;
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

static uint8_t hex_delete(pab_t pab) {

	uint8_t rc = HEXSTAT_SUCCESS;;
	FRESULT fr;

	uart_putc('>');
	hex_release_bus_recv();

	// the file path
	if(hex_getdata(buffer,pab.datalen))
		return HEXERR_BAV;
	buffer[pab.datalen] = 0; // terminate the string

	// remove file
	fr = f_unlink(&fs, buffer);

	// map return code
	if(rc == HEXSTAT_SUCCESS) {
		switch(fr) {
		case FR_OK:
			rc = HEXSTAT_SUCCESS;
			break;
		case FR_NO_FILE:
			rc = HEXSTAT_NOT_FOUND;
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
	return HEXERR_BAV;
}



static int8_t hex_write(pab_t pab) {
  uint8_t rc = HEXSTAT_SUCCESS;
  uint16_t len;
  uint8_t i;
  UINT written;
  file_t* file;
  BYTE res = FR_OK;

  uart_putc('>');
  file = find_lun(pab.lun);
  len = pab.datalen;
  while(len) {
    i = (len >= sizeof(buffer) ? sizeof(buffer) : len);
    hex_release_bus_recv();
    if(hex_getdata(buffer, i))
      rc = HEXERR_BAV;
    if(file != NULL && res == FR_OK && rc == HEXSTAT_SUCCESS) {
      res = f_write(&(file->fp), buffer, i, &written);
    }
    len -= i;
  }
  if(file != NULL && (file->attr & FILEATTR_DISPLAY)) { // if w data in DISPLAY mode
	if (pab.lun == 0) { // LUN == 0
	  // add CRLF to data, for LIST command
      buffer[0] = 13;
      buffer[1] = 10;
      res = f_write(&(file->fp), buffer, 2, &written);
      if(written != 2)
        rc = HEXSTAT_BUF_SIZE_ERR;  // generic error.
    }
    else { // LUN != 0
	  // add BLANK to data (as delimiter, just to be sure there is one), for PRINT command
	  buffer[0] = 32;
	  res = f_write(&(file->fp), buffer, 1, &written);
	  if(written != 1)
	    rc = HEXSTAT_BUF_SIZE_ERR;  // generic error.
    }
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

static uint8_t hex_read(pab_t pab) {
  uint8_t rc;
  uint8_t i;
  uint16_t len = 0;
  UINT read;
  BYTE res = FR_OK;
  file_t* file;

  uart_putc('R');
  file = find_lun(pab.lun);
  if(file != NULL && (file->attr & FILEATTR_CATALOG)) {
    if (pab.lun == 0 ) {
      uart_putc('P');
        return hex_read_catalog(pab);
    }
    else {
      uart_putc('T');
        return hex_read_catalog_txt(pab);
    }
  }
  if(file != NULL) {
    hex_release_bus_recv();
    _delay_us(200);
    if(file != NULL) {
      uint16_t fsize = file->fp.fsize - (uint16_t)file->fp.fptr; // amount of data in file that can be sent.
      if (fsize != 0 && pab.lun != 0) { // for 'normal' files (lun != 0) send data value by value
        // amount of data for next value to be sent
        fsize = next_value_size(file); // TODO maybe rename fsize to something like send_size
      }
      if (res == FR_OK) {
        if ( fsize == 0 ) {
          res = FR_EOF;
        } else {
          // size of buffer provided by host (amount to send)
          //len = pab.buflen;

          if ( fsize > pab.buflen ) {
            fsize = pab.buflen;
          }
        }
      }
      hex_puti(fsize, TRUE);  // send full length of file
      while(len < fsize) {
    	UINT num_read = ( fsize > sizeof( buffer ) ) ? sizeof( buffer ) : fsize;
        res = f_read(&(file->fp), buffer, num_read, &read);
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
        case FR_EOF:
          rc = HEXSTAT_EOF;
          break;
        default:
          rc = HEXSTAT_DEVICE_ERR;
          break;
      }
      uart_putc('>');
      hex_release_bus_send();
    } else {
      hex_puti(0, FALSE);  // null lun
      rc = HEXSTAT_NOT_OPEN;
    }
  } else {
    hex_puti(0, FALSE);  // null file
    rc = HEXSTAT_NOT_OPEN;
  }
  hex_putc(rc, FALSE);    // status code
  return 0;
}

static uint8_t hex_open_catalog(pab_t pab, uint8_t att) {
  uint8_t rc = HEXERR_SUCCESS;
  uint16_t fsize = 0;
  file_t *file;
  BYTE res = FR_OK;

  uart_putc('o');
  if (!(att & OPENMODE_READ)) {
	// send back error on anything not OPENMODE_READ
	res =  FR_IS_READONLY;
  }
  else {
	file = reserve_lun(pab.lun);

	if(file != NULL) {
	  file->attr |= FILEATTR_CATALOG;
	  // remove the leading $
	  char* string = (char*)buffer;
	  if (strlen(string)<2 || string[1]!='/') { // if just $ or $ABC..
		string[0] = '/'; // $ -> /, $ABC/ -> /ABC/
	  }
	  else {
		string = (char*)&buffer[1]; // $/ABC/... -> /ABC/...
	  }
	  // separate into directory path and pattern
	  char* dirpath = string;
	  char* pattern = (char*)NULL;
	  char* s =  strrchr(string, '/');
	  if (strlen(s) > 1) {       // there is a pattern
	    pattern = strdup(s+1);   // copy pattern to store it, will be freed in free_lun
 	    *(s+1) = '\0';           // set new terminating zero for dirpath
 	    uart_trace(pattern,0,strlen(pattern));
	  }
	  // if not the root slash, remove slash from dirpath
	  if (strlen(dirpath)>1 && dirpath[strlen(dirpath)-1] == '/')
		dirpath[strlen(dirpath)-1] = '\0';
	  // get the number of catalog entries from dirpath that match the pattern
	  file->dirnum = cat_get_num_entries(&fs, dirpath, pattern);
	  if (pattern != (char*)NULL)
	    file->pattern = pattern; // store pattern, will be freed in free_lun
	  // the file size is either the length of the PGM file for OLD/PGM or the max. length of the txt file for OPEN/INPUT.
	  fsize = (pab.lun == 0 ? cat_file_length_pgm(file->dirnum)  : cat_max_file_length_txt());
	  res = f_opendir(&fs, &(file->dir), (UCHAR*)dirpath); // open the director
	}
	else {
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
  hex_release_bus_recv();
  _delay_us(200);  // wait a bit...
  if(!hex_is_bav()) { // we can send response
    hex_puti(2, FALSE);    // claims it is accepted buffer length, but looks to really be my return buffer length...
    hex_puti(fsize, FALSE);
    hex_putc(rc, FALSE);    // status code
    uart_putcrlf();
    return HEXERR_SUCCESS;
  }
  uart_putc('E');
  return HEXERR_BAV;
}

static uint8_t hex_open(pab_t pab) {
  uint16_t i = 0;
  uint8_t data;
  uint16_t len = 0;
  uint8_t att = 0;
  uint8_t rc;
  BYTE mode = 0;
  uint16_t fsize = 0;
  file_t* file;
  BYTE res;


  uart_putc('O');
  hex_release_bus_recv();
  while(i < 3) {
    if(hex_is_bav()){
      return HEXERR_BAV;
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
  if(hex_getdata(buffer,pab.datalen - 3))
    return HEXERR_BAV;
  buffer[pab.datalen - 3] = 0; // terminate the string

  //*****************************************************
  // special file name "$" -> catalog
  char* path=((char)buffer[0]=='$' ? "$" : (char*)buffer);

  if ((char)buffer[0]=='$') {
	  return hex_open_catalog(pab, att);
  }
  //*******************************************************

  switch (att & (0x80 | 0x40)) {
    case 0x00:  // append mode
      mode = FA_WRITE;
      break;
    case OPENMODE_WRITE: // write, truncate if present. Maybe...
      mode = FA_WRITE | FA_CREATE_ALWAYS;
      break;
    case OPENMODE_WRITE | OPENMODE_READ:
      mode = FA_WRITE | FA_READ | FA_CREATE_ALWAYS;
      break;
    default: //OPENMODE_READ
      mode = FA_READ;
      break;
  }




  file = reserve_lun(pab.lun);
  if(file != NULL) {

    res = f_open(&fs,&(file->fp),(UCHAR *)path,mode);
    switch(res) {
      case FR_OK:
        rc = HEXSTAT_SUCCESS;
        fsize = file->fp.fsize;
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
    if(rc) {
      free_lun(pab.lun); // free up buffer
    }
  } else { // too many open files.
    rc = HEXSTAT_MAX_LUNS;
  }
  hex_release_bus_recv();
  _delay_us(200);  // wait a bit...
  if(!hex_is_bav()) { // we can send response
    hex_puti(2, FALSE);    // claims it is accepted buffer length, but looks to really be my return buffer length...
    switch(att & (OPENMODE_WRITE | OPENMODE_READ)) {
      case OPENMODE_READ: // read
    	if (!(att & OPENMODE_INTERNAL)) {
       	  file->attr |= FILEATTR_DISPLAY;
    	}
        hex_puti(fsize, FALSE);
        break;
      default: //  when opening to write, or read/write
    	if (!(att & OPENMODE_INTERNAL)) {
    	  file->attr |= FILEATTR_DISPLAY;
    	}
    	 if ( len == 0 ) {
    	   len = sizeof(buffer);
    	 } else {
    	    // otherwise, we know. and do NOT allow fileattr display under any circumstance.
    	    file->attr &= ~FILEATTR_DISPLAY;
    	}
        hex_puti(len, FALSE);  // this is evidently the value we should send back.  Not sure what one does if one needs to send two of these.
        break;
    }
    hex_putc(rc, FALSE);    // status code
    return HEXERR_SUCCESS;
  }
  return HEXERR_BAV;
}

static uint8_t hex_close(pab_t pab) {
  uint8_t rc;
  file_t* file;
  BYTE res;

  uart_putc('<');
  file = find_lun(pab.lun);
  if(file != NULL) {
    if(file->attr & FILEATTR_CATALOG) {
      free_lun(pab.lun);
      rc = HEXSTAT_SUCCESS;
    } else {
      res = f_close(&(file->fp));
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

static uint8_t hex_format(pab_t pab) {

  uart_putc('>');
  hex_release_bus_recv();
  // get options, if any
  if(hex_getdata(buffer, pab.datalen))
    return HEXERR_BAV;
  hex_release_bus_recv();
  _delay_us(200);  // wait a bit...
  if(!hex_is_bav()) { // we can send response
    //if(pab.datalen) {
      // technically, a format with options should return size of disk, I assume in sectors
      //hex_puti(2, FALSE);
      //hex_puti(65535, FALSE);
    //} else {
    hex_puti(0, FALSE);
    //}
    //hex_putc(HEXSTAT_SUCCESS, FALSE);
    hex_putc(HEXSTAT_UNSUPP_CMD, FALSE);
    return HEXERR_SUCCESS;
  }
  return HEXERR_BAV;

}

static uint8_t hex_reset_bus(pab_t pab) {
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

  board_init();
  hex_init();
  uart_init();
  disk_init();
  timer_init();
  leds_init();
  device_hw_address_init();
  init();

  sei();

  res=f_mount(1, &fs);
  uart_puts_P(PSTR("Mount SD RC: "));
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

  uart_puts_P(PSTR("Device ID: 0x"));
  uart_puthex(device_hw_address());
  uart_putcrlf();
  while(TRUE) {
    while(hex_is_bav()) {
    }
    uart_putc('^');
    while(!hex_is_bav()) {
      while(!ignore_cmd) {
        data = hex_getc(i == 8);  // if it's the last byte, hold the hsk low.
        if(-1 < data) {
          pabdata.raw[i++] = data;
          if(i == 1 && pabdata.pab.dev != device_hw_address()) {
            ignore_cmd = TRUE;
          } else if(i == 9) { // exec command
            uart_putc(13);
            uart_putc(10);
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
              case HEXCMD_VERIFY:
            	hex_verify(pabdata.pab);
            	break;
              case HEXCMD_DELETE:
            	  hex_delete(pabdata.pab);
            	  break;
              case HEXCMD_RETURN_STATUS:
            	  hex_return_status(pabdata.pab);
            	  break;
              case HEXCMD_FORMAT:
                hex_format(pabdata.pab);
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

