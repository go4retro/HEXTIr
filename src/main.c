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

typedef struct _file_t {
  FIL fp;
  uint8_t attr;
} file_t;

typedef enum _luntype_t {
  LUN_FILE,
  LUN_DIR
} luntype_t;

typedef struct _luntbl_t {
  uint8_t used;
  uint8_t lun;
  file_t file;
  DIR dir;
  luntype_t type;
  uint16_t dirnum;
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

static luntbl_t* find_lun2(uint8_t lun) {
  uint8_t i;

  for(i=0;i < MAX_OPEN_FILES; i++) {
    if(files[i].used && files[i].lun == lun) {
      return &(files[i]);
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
      files[i].type = LUN_FILE;
      open_files++;
      set_busy_led(TRUE);
      return &(files[i].file);
    }
  }
  return NULL;
}

static luntbl_t* reserve_lun2(uint8_t lun) {
  uint8_t i;

  for(i = 0; i < MAX_OPEN_FILES; i++) {
    if(!files[i].used) {
      files[i].used = TRUE;
      files[i].lun = lun;
      files[i].type = LUN_DIR;
      open_files++;
      set_busy_led(TRUE);
      return &(files[i]);
    }
  }
  return NULL;
}

static void free_lun(uint8_t lun) {
  uint8_t i;

  for(i=0;i < MAX_OPEN_FILES; i++) {
    if(files[i].used && files[i].lun == lun) {
      files[i].used = FALSE;
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


static uint8_t hex_read_catalog(pab_t pab) {
  uint8_t fsize_k;
  uint8_t rem;
  uint8_t rc;
  BYTE res = FR_OK;
  luntbl_t* lun;
  FILINFO fno;
  # ifdef _MAX_LFN_LENGTH
  UCHAR lfn[_MAX_LFN_LENGTH+1];
  fno.lfn = lfn;
  #endif

  uart_putc('r');
  lun = find_lun2(pab.lun);
  hex_release_bus_recv();
  _delay_us(200);
  if(lun != NULL) {
    hex_puti(pgm_file_length(lun->dirnum), FALSE);  // send full length of file
    pgm_cat_open(lun->dirnum);
    uint16_t i = 1;
    while(i <= lun->dirnum && res == FR_OK) {
      memset(lfn,0,sizeof(lfn));
      res = f_readdir(&lun->dir, &fno);                   // read a directory item
      if (res != FR_OK || fno.fname[0] == 0)
        break;  // break on error or end of dir

      char* filename = (char*)(fno.lfn[0] != 0 ? fno.lfn : fno.fname );

      if (cat_skip_file(filename))
    	continue; // skip certain files like "." and ".."

      char attrib = ((fno.fattrib & AM_DIR) ? 'D' : ((fno.fattrib & AM_VOL) ? 'V' : 'F'));

      uart_trace(filename, 0, strlen(filename));
      uart_putcrlf();

      hex_release_bus_send();

      pgm_cat_record(i++,fno.fsize, filename,attrib);

    }
    hex_release_bus_send();
    pgm_cat_close();
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
  uint8_t fsize_k;
  uint8_t rem;
  uint8_t rc;
  BYTE res = FR_OK;
  luntbl_t* lun;
  FILINFO fno;
  # ifdef _MAX_LFN_LENGTH
  UCHAR lfn[_MAX_LFN_LENGTH+1];
  fno.lfn = lfn;
  #endif

  uart_putc('r');
  lun = find_lun2(pab.lun);
  hex_release_bus_recv();
  _delay_us(200);
  if(lun != NULL) {
    //hex_puti(lun->dirnum * 36, FALSE);  // send full length of file
    hex_puti(36, FALSE);
    uint16_t i = 1;
    while(i <= lun->dirnum && res == FR_OK) {
      memset(lfn,0,sizeof(lfn));
      res = f_readdir(&lun->dir, &fno);                   // read a directory item
      if (res != FR_OK || fno.fname[0] == 0)
        break;  // break on error or end of dir
      if (strcmp((const char*)fno.fname, ".") == 0 || strcmp((const char*)fno.fname, "..") == 0)
        continue; // skip

      fsize_k = (uint8_t)(fno.fsize / 1000);
      rem = ((fno.fsize % 1000) / 100);

      char* filename = (char*)(fno.lfn[0] != 0 ? fno.lfn : fno.fname );
      char* attrib = ((fno.fattrib & AM_DIR) ? "D" : ((fno.fattrib & AM_VOL) ? "V" : "F"));

      uart_trace(filename, 0, strlen(filename));
      uart_putcrlf();

      hex_release_bus_send();
      hex_putc('\"', FALSE);
      char lno[5];
      memset(lno,0,sizeof(lno));
      snprintf(lno,4,"%d  ",i++);
      for (int j=0; j<strlen(lno) && j<3; j++) {
    	  hex_putc(lno[j],FALSE);
      }
      hex_putc(' ', FALSE);
      hex_putc('!', FALSE);
      if(fsize_k > 9) {
        hex_putc((fsize_k / 10) % 10 + '0', FALSE);
      }
      else {
        hex_putc(' ', FALSE);
      }
      hex_putc(fsize_k % 10 + '0', FALSE);
      hex_putc('.', FALSE);
      hex_putc(rem + '0', FALSE);
      hex_putc(' ', FALSE);
      hex_putc('\"', FALSE);
      hex_putc('\"', FALSE);
      uint8_t j;
      for(j = 0; j < 18 && j < strlen(filename) ; j++) {
        hex_putc(filename[j], FALSE);
      }
      hex_putc('\"', FALSE);
      hex_putc('\"', FALSE);
      for( ; j < 18; j++) {
        hex_putc(' ', FALSE);
      }
      hex_putc(attrib[0], FALSE);
      hex_putc('\"', FALSE);
      hex_putc(',', TRUE);
      //hex_putc(0, TRUE); // null termination
      lun->dirnum = lun->dirnum - 1;
      uart_putcrlf();
      break;
    }
    hex_release_bus_send();
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
	luntbl_t* lun;

	uart_putc('>');
	file = find_lun(pab.lun);

	lun = find_lun2(pab.lun);
	if(lun != NULL && lun->type == LUN_DIR) {
		if (pab.lun != 0 ) {
		  if (lun->dirnum == 0) {
			 status = 0x80; // EOF
		  }
		}
	}
	else {
		file = &(lun->file);
		res = (file != NULL ? FR_OK : FR_NO_FILE);
		if (file->fp.fptr == file->fp.fsize) {
			status = 0x80; // EOF
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
  if(file != NULL && (file->attr & FILEATTR_DISPLAY)) { // add CRLF to data
    buffer[0] = 13;
    buffer[1] = 10;
    res = f_write(&(file->fp), buffer, 2, &written);
    if(written != 2)
      rc = HEXSTAT_BUF_SIZE_ERR;  // generic error.
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
  luntbl_t* lun;
  file_t* file;

  uart_putc('R');
  lun = find_lun2(pab.lun);
  if(lun != NULL && lun->type == LUN_DIR) {
	if (pab.lun == 0 ) {
	  uart_putc('P');
      return hex_read_catalog(pab);
	}
	else {
	  uart_putc('T');
      return hex_read_catalog_txt(pab);
	}
  }
  if(lun != NULL) {
    file = &(lun->file);
    hex_release_bus_recv();
    _delay_us(200);
    if(file != NULL) {
      hex_puti(file->fp.fsize, TRUE);  // send full length of file
      while(len < file->fp.fsize) {
        res = f_read(&(file->fp), buffer, sizeof(buffer), &read);
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

static uint8_t hex_open_catalog(pab_t pab) {
  uint8_t rc = HEXERR_SUCCESS;
  uint16_t fsize = 0;
  luntbl_t *lun;
  BYTE res;

  uart_putc('o');
  // TODO send back error on anything not OPENMODE_READ;
  lun = reserve_lun2(pab.lun);
  if(lun == NULL) {
    // too many open files.
    rc = HEXSTAT_MAX_LUNS;
  }
  char* dirpath = (strlen((char*)buffer) > 1 ? (char*)&(buffer[1]) : "/");
  lun->dirnum = cat_get_length(dirpath);
  fsize = (pab.lun == 0 ? pgm_file_length(lun->dirnum)  : lun->dirnum * 36);
  res = f_opendir(&fs, &(lun->dir), (UCHAR*)dirpath); // open the director

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
	  char* dirpath = (strlen((char*)buffer)>1 ? (char*)&(buffer[1]) : "/");
	  return hex_open_catalog(pab);
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
        hex_puti(fsize, FALSE);
        break;
      default: //
        if(!len) // len = 0 means list "<device>.<filename>" or open #<> for writing, thus we should add CRLF to each line.
          file->attr |= FILEATTR_DISPLAY;
        hex_puti((len ? len : sizeof(buffer)), FALSE);  // this is evidently the value we should send back.  Not sure what one does if one needs to send two of these.
        break;
    }
    hex_putc(rc, FALSE);    // status code
    return HEXERR_SUCCESS;
  }
  return HEXERR_BAV;
}

static uint8_t hex_close(pab_t pab) {
  uint8_t rc;
  luntbl_t *lun;
  file_t* file;
  BYTE res;

  uart_putc('<');
  lun = find_lun2(pab.lun);
  if(lun != NULL) {
    if(lun->type == LUN_DIR) {
      free_lun(pab.lun);
      rc = HEXSTAT_SUCCESS;
    } else {
      file = &(lun->file);
      if(file != NULL) {
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
      } else {
        rc = HEXSTAT_NOT_OPEN;
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

