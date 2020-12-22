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

    drive.cpp: Drive-based device functions.
*/

/*
 * Punch List
 *
 * TODO:  Add in support for Verify Read/Write operation 0x0c
 * TODO:  Read Catalog 0x0e
 * TODO:  Set Options 0x0f
 * TODO:  Protect/Unprotect File 0x11
 */

#include <string.h> // TODO can we remove these?
#include <stdlib.h>
#include <avr/pgmspace.h>

#include "config.h"
#include "catalog.h"
#include "debug.h"
#include "diskio.h"
#include "eeprom.h"
#include "ff.h"
#include "hexbus.h"
#include "hexops.h"
#include "led.h"
#include "registry.h"
#include "timer.h"
#include "drive.h"

#ifdef INCLUDE_DRIVE

FATFS fs;
uint8_t _cmd_lun = LUN_CMD;


// Global defines
uint8_t open_files = 0;
luntbl_t files[MAX_OPEN_FILES]; // file number to file mapping
uint8_t fs_initialized = FALSE;


static file_t* find_file_in_use(uint8_t *lun) {
  uint8_t i;
  for (i = 0; i < MAX_OPEN_FILES; i++ ) {
    if (files[i].used) {
      *lun = files[i].lun;
      return &(files[i].file);
    }
  }
  return NULL;
}


static file_t* find_lun(uint8_t lun) {
  uint8_t i;

  for (i = 0; i < MAX_OPEN_FILES; i++) {
    if (files[i].used && files[i].lun == lun) {
      return &(files[i].file);
    }
  }
  return NULL;
}


static file_t* reserve_lun(uint8_t lun) {
  uint8_t i;

  for (i = 0; i < MAX_OPEN_FILES; i++) {
    if (!files[i].used) {
      files[i].used = TRUE;
      files[i].lun = lun;
      files[i].file.pattern = (char*)NULL;
      files[i].file.attr = 0; // ensure clear attr before use
      open_files++;
      set_busy_led(TRUE);
      return &(files[i].file);
    }
  }
  return NULL;
}


static void free_lun(uint8_t lun) {
  uint8_t i;

  for (i = 0; i < MAX_OPEN_FILES; i++) {
    if (files[i].used && files[i].lun == lun) {
      files[i].used = FALSE;
      if (files[i].file.pattern != (char*) NULL)
        free(files[i].file.pattern);
      open_files--;
      set_busy_led(open_files);
      if ( !open_files ) {
      }
    }
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
 * Get the size of the next record in DISPLAY format.
 * Search for the trailing LF (\r\n) and ignore a CR (\r) before the LF.
 */
static uint16_t next_value_size_display(file_t* file) {
  BYTE res;
  UINT read;
  char token;
  uint8_t instr = FALSE;
  int val_len = 0;
  uint32_t val_ptr;

  val_ptr = file->fp.fptr; // save the current position for to restore
  res = f_read(&(file->fp), &token, 1, &read);
  while (res == FR_OK && read == 1) {
    if ((token == '\n') && !instr){ // stop on first trailing delimiter
      break;
    }
    if ((token != '\r') || instr) val_len++;
    if (token == '\"') instr = !instr;
    res = f_read(&(file->fp), &token, 1, &read);
  }
  res = f_lseek(&(file->fp), val_ptr); // re-position the file pointer
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

static hexstatus_t fresult2hexstatus(FRESULT fr) {
  hexstatus_t rc = HEXSTAT_SUCCESS;

  switch (fr) {
    case FR_OK:
      break;
    //case FR_EOF:
    //  rc = HEXSTAT_EOF;
    //  break;
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
    case FR_NO_FILE:
      rc = HEXSTAT_NOT_FOUND;
      break;
    case FR_INVALID_OBJECT:
    case FR_NOT_READY:
    default:
      rc = HEXSTAT_DEVICE_ERR;
      break;
  }
  return rc;
}


/*
   https://github.com/m5dk2n comments:

   What happens during the VERIFY is the following:
   The calculator sends an hex-bus open (R) command with the name of the
   stored program and receives its size in the response. Then the calculator
   sends the hex-bus verify command which contains the program it has in its
   memory. If the comparison is not successful either IO error 12 or 24 is
   sent according to the user manual. (12 = stored program larger then
   program in memory, 24 = programs differ). The calculator sends the hex-bus
   close command.
   The expectation was that IO error 12 was issued by the calculator in case
   the length of the stored program (returned in the open response) is greater
   then the length of the program in memory. But this is not the case. 
   
   The calculator always sends the complete program in memory, regardless of
   the size of the stored file. Therefore if pab->datalen is different from
   file size, we have an error 12.

   And although if a comparison error occured, one has read the data
   transmitted in the verify command until sending stops.
*/

static void drv_verify(pab_t *pab) {
  uint8_t  *data = &buffer[ BUFSIZE / 2 ]; // split our buffer in half
  // so we do not use all of our limited amount of RAM on buffers...
  UINT     read;
  uint16_t len;
  uint16_t i;
  file_t*  file;
  FRESULT  res = FR_OK;
  hexstatus_t rc = HEXSTAT_SUCCESS;

  debug_puts_P("Verify File\r\n");

  file = find_lun(pab->lun);
  len = pab->datalen;   // this is the size of the object to verify

  res = (file != NULL ? FR_OK : FR_NO_FILE);
  if (res == FR_OK) {
    if (len != file->fp.fsize) {
      rc = HEXSTAT_BUF_SIZE_ERR;
    }  

    while (( len > 0) && (rc == HEXSTAT_SUCCESS)) {

      // figure out how much will fit...
      i = ( len >= ( BUFSIZE / 2 ))  ? ( BUFSIZE / 2 ) : len;

      rc = hex_get_data(buffer, i);  // use front half of buffer for incoming data from the host.
      if (rc == HEXSTAT_SUCCESS) {
        res = f_read(&(file->fp), data, i, &read);
        rc = fresult2hexstatus(res);
        if ( memcmp(data, buffer, read) != 0 ) {
          rc = HEXSTAT_VERIFY_ERR;
        }
      }

      len -= read;
    }
  }
  // If we haven't read the entire incoming message yet, flush it.
  if ( len ) {
    if ( !fs_initialized ) {
      rc = HEXSTAT_DEVICE_ERR;
    }
    hex_eat_it( len, rc ); // reports status back.
    return;
  } else {
    hex_send_final_response( rc );
  }
}


typedef enum _diskcmd_t {
  DISK_CMD_NONE = 0,
  DISK_CMD_CHDIR,
  DISK_CMD_MKDIR,
  DISK_CMD_RMDIR,
  DISK_CMD_RENAME,
  DISK_CMD_COPY,
  DISK_CMD_PWD
} diskcmd_t;

static const action_t dcmds[] MEM_CLASS = {
                                  {DISK_CMD_CHDIR,    "cd"},
                                  {DISK_CMD_CHDIR,    "chdir"},
                                  {DISK_CMD_MKDIR,    "md"},
                                  {DISK_CMD_MKDIR,    "mkdir"},
                                  {DISK_CMD_RMDIR,    "del"},
                                  {DISK_CMD_RMDIR,    "delete"},
                                  {DISK_CMD_RMDIR,    "rmdir"},
                                  {DISK_CMD_RENAME,   "rn"},
                                  {DISK_CMD_RENAME,   "rename"},
                                  {DISK_CMD_RENAME,   "mv"},
                                  {DISK_CMD_RENAME,   "move"},
                                  {DISK_CMD_COPY,     "cp"},
                                  {DISK_CMD_COPY,     "copy"},
                                  {DISK_CMD_PWD,      "pwd"},
                                  {DISK_CMD_NONE,     ""}
                                };

static inline hexstatus_t drv_exec_cmd(char* buf, uint8_t len, uint8_t *dev) {
  hexstatus_t rc = HEXSTAT_SUCCESS;
  diskcmd_t cmd;
  FRESULT res = FR_OK;
  uint8_t i;
  uint8_t j;
  char* buf2;

  // path, trimmed whitespaces
  trim(&buf, &len);

  cmd = (diskcmd_t) parse_cmd(dcmds, &buf, &len);
  if(cmd != DISK_CMD_NONE) {
    //skip spaces
    trim (&buf, &len);
  }
  switch (cmd) {
  case DISK_CMD_CHDIR:
    res = f_chdir(&fs,(UCHAR*)buf);
    break;
  case DISK_CMD_MKDIR:
    res = f_mkdir(&fs,(UCHAR*)buf);
    break;
  case DISK_CMD_RMDIR:
  res = f_unlink(&fs,(UCHAR*)buf);
  break;
  case DISK_CMD_RENAME:
    i = 0;
    rc = HEXSTAT_OPTION_ERR; // no =
    while(i < len) {
      // handle rename
      if(buf[i] == '=') {
        buf2 = &buf[i + 1];
        j = len - i - 1;
        trim (&buf, &i);
        trim (&buf2, &j);
        rc = HEXSTAT_SUCCESS;
        res = f_rename(&fs, (UCHAR*)buf, (UCHAR*)buf2);
        break;
      } else {
        // no =
        i++;
      }
    }
    break;
  case DISK_CMD_COPY:
  case DISK_CMD_PWD:
    //populate error string with cwd.
  default:
    rc = hex_exec_cmd(buf, len, dev);
    break;
  }
  switch(res) {
  case FR_OK:
    break;
  default:
    // TODO should put better errors here
    rc = HEXSTAT_DEVICE_ERR;
    break;
  }
  return rc;
}


static inline hexstatus_t drv_exec_cmds(char* buf, uint8_t len, uint8_t *dev) {
  hexstatus_t rc = HEXSTAT_SUCCESS;
  char * buf2;
  uint8_t len2;

  debug_puts_P("Exec Drive Commands\r\n");

  buf2 = buf;
  len2 = len;
  do {
    buf = buf2;
    len = len2;
    split_cmd(&buf, &len, &buf2, &len2);
    rc = drv_exec_cmd(buf, len, dev);
  } while(rc == HEXSTAT_SUCCESS && len2);
  return rc;
}


static void drv_write_cmd(pab_t *pab, uint8_t *dev) {
  hexstatus_t rc = HEXSTAT_SUCCESS;

  debug_puts_P("Exec Disk Command\r\n");

  rc = hex_write_cmd_helper(pab->datalen);
  if(rc != HEXSTAT_SUCCESS) {
    return;
  }
  rc = drv_exec_cmds((char *)buffer, pab->datalen, dev);

  hex_send_final_response( rc );
}


/*
   drv_write() -
   writes data to the open file associated with the LUN number
   in the PAB.

*/
static void drv_write(pab_t *pab) {
  hexstatus_t rc = HEXSTAT_SUCCESS;
  uint16_t len;
  uint16_t i;
  uint16_t header = 0;
  uint8_t  first_buffer = 1;
  UINT written;
  file_t* file = NULL;
  FRESULT res = FR_OK;
  
  debug_puts_P("Write File\r\n");

  len = pab->datalen;
  if (pab->lun == _cmd_lun
      || pab->lun == LUN_CMD
      ) {
    // handle command channel
    drv_write_cmd(pab, &(_config.drv_dev));
    return;
  }

  file = find_lun(pab->lun);
  res = (file != NULL ? FR_OK : FR_NO_FILE);
  if (res == FR_OK && (file->attr & FILEATTR_RELATIVE)){
    // if we're not at the right record position, reposition
    if (file->fp.fsize < pab->buflen * pab->record) {
      // file is too small.  Enlarge.
      // TODO make this more efficient by handling in 256 byte chunks...
      res = f_lseek( &(file->fp), file->fp.fsize );
      buffer[0] = 0;
      for (i = file->fp.fsize; i < pab->buflen * pab->record; i++)
        res = f_write(&(file->fp), buffer, 1, &written);
    }  else // file is big enough, just find the right spot.
      res = f_lseek(&(file->fp), pab->buflen * pab->record);
  }
  // OK, read the data we need to send to the file
  while (len && rc == HEXSTAT_SUCCESS && res == FR_OK ) {
    i = (len >= BUFSIZE ? BUFSIZE : len);
    rc = hex_get_data(buffer, i);
    if ((pab->lun == 0) && (first_buffer == 1)) {
      header = (buffer[0] | ( buffer[1] << 8 )) & 0xffdf;
      if ((header == 0x380) || (header  == 0x10f))
        header = 1;
      else
        header = 0;
      first_buffer = 0;
    }
    if (file != NULL && res == FR_OK && rc == HEXSTAT_SUCCESS) {

      res = f_write(&(file->fp), buffer, i, &written);
      if ( written != i ) {
        res = FR_DENIED;
      }
    }
    len -= i;
  }

  if ( len ) {
    if ( !fs_initialized ) {
      rc = HEXSTAT_DEVICE_ERR;
    }
    // TODO what error should we return to caller?
    hex_eat_it( len, rc );
    return;
  }

  // if in DISPLAY mode
  if (file != NULL && (file->attr & FILEATTR_DISPLAY) && (header == 0)) {
	// add CRLF to data (for DISPLAY mode)
    buffer[0] = 13;
    buffer[1] = 10;
    len = 2;
    res = f_write(&(file->fp), buffer, 2, &written);
    if (!res) {
      debug_trace(buffer, 0, written);
    }
    if (written != 2) {
      rc = HEXSTAT_BUF_SIZE_ERR;  // generic error.
    }
  }
  if (file != NULL && (file->attr & FILEATTR_RELATIVE)) {
    buffer[0] = 0;
    for (i = pab->datalen + 1 + len; i <= pab->buflen; i++)
      res = f_write(&(file->fp), buffer, 1, &written);
  }
  if (rc == HEXSTAT_SUCCESS) {
    rc = fresult2hexstatus(res);
  }

  hex_send_final_response( rc );
}

/*
   drv_read() -
   read data from currently open file associated with the LUN
   in the PAB.
   structured data files:
     in INTERNAL mode the amount of bytes to be send is determined
     by the first byte to be read next and the corresponding amount
     of bytes is send
     
     in DISPLAY mode the amount of bytes to be send is determined
     by the first occurrence of (CR)LF outside a double-quoted string.
     After sending, the (CR)LF is skipped to position the file at the
     beginning of the next record or EOF
  raw data files
    OLD/RUN reads a program file, the amount of bytes to be send is
    determined by the file length. These commands use LUN 0 as special LUN
    Other raw data files (e.g. RAM/ROM images) must use LUN 254 as a
    special LUN. The amount of bytes to be send is determined by the
    file length    
*/

static void drv_read(pab_t *pab) {
  hexstatus_t rc;
  uint8_t i;
  uint16_t len;
  uint16_t fsize;
  char token;
  UINT read;
  FRESULT res = FR_OK;
  file_t* file;

  debug_puts_P("Read File\r\n");

  if(pab->lun == LUN_CMD || pab->lun == _cmd_lun) {
    hex_read_status();
    return;
  }

  file = find_lun(pab->lun);
  if (file != NULL && (file->attr & FILEATTR_CATALOG)) {
    if (pab->lun == 0 ) {
      debug_putc('P');
      hex_read_catalog(file);
      return;
    }
    else {
      debug_putc('T');
      hex_read_catalog_txt(file);
      return;
    }
  }
  if ((file != NULL) && (file->attr & FILEATTR_RELATIVE))
    f_lseek(&(file->fp), pab->buflen * pab->record);
  if (file != NULL) {
    if ( (uint16_t)file->fp.fptr < file->fp.fsize ) {
      fsize = file->fp.fsize - (uint16_t)file->fp.fptr; // amount of data in file that can be sent.
      if (fsize != 0 && pab->lun != 0 && pab->lun != LUN_RAW) { // for 'normal' files (lun > 0 && lun < 254) send data value by value
        // amount of data for next value to be sent
        if (file->attr & FILEATTR_RELATIVE) 
          if (file->attr & FILEATTR_DISPLAY) 
            fsize = next_value_size(file);
          else
            fsize = pab->buflen;
        else  
          fsize = next_value_size(file); // TODO maybe rename fsize to something like send_size
      }
      else if (pab->lun == 0)
        fsize = pab->buflen;
      if (res == FR_OK) {
        if ( fsize == 0 ) {
          rc = HEXSTAT_EOF;
        } else {
          // size of buffer provided by host (amount to send)
          if ( fsize > pab->buflen ) {
            fsize = pab->buflen;
          }
        }
      }
      if ((uint16_t)file->fp.fptr + fsize >= file->fp.fsize)
        fsize = file->fp.fsize - (uint16_t)file->fp.fptr;
      // send how much we are going to send
      rc = (hex_send_word( fsize ) == HEXERR_SUCCESS ? HEXSTAT_SUCCESS : HEXSTAT_DATA_ERR);
      len = 1; // send data immediately byte by byte to avoid time-out errors
      // while we have data remaining to send.
      while ( fsize && rc == HEXSTAT_SUCCESS && res == FR_OK) {
        /*
        len = fsize;    // remaining amount to read from file
        // will it fit into buffer or not?  Only read as much
        // as we can hold in our buffer.
        len = ( len > BUFSIZE ) ? BUFSIZE : len;
        */
        res = f_read(&(file->fp), buffer, len, &read);
        if (!res) {
          debug_trace(buffer, 0, read);
        }
        if (FR_OK == res) {
          for (i = 0; i < read; i++) {
            rc = (hex_send_byte(buffer[i]) == HEXERR_SUCCESS ? HEXSTAT_SUCCESS : HEXSTAT_DATA_ERR);
          }
        }
        fsize -= read;
      }

      if(rc == HEXSTAT_SUCCESS)
        rc = fresult2hexstatus(res);
      if(rc == HEXSTAT_SUCCESS) {
        if (file->attr & FILEATTR_DISPLAY) {
          res = f_read(&(file->fp), &token, 1, &read); // skip (CR)LF
          if (token == '\r')
            res = f_read(&(file->fp), &token, 1, &read);
          if (file->attr & FILEATTR_RELATIVE)
            f_lseek(&(file->fp), pab->buflen * (pab->record + 1));
        }
      }
    }
    else {
      hex_send_word(0);
      rc = HEXSTAT_EOF;
    }  
  } else {
    hex_send_word(0);      // null file
    rc = HEXSTAT_NOT_FOUND;
    if ( !fs_initialized ) {
      rc = HEXSTAT_DEVICE_ERR;
    }
  }
  hex_send_byte( rc ); // status byte transmit
  hex_finish();
}


/*
   drv_start - open filesystem.
   make- ignore/ empty function.
*/
/*
   If we are attempting to use the SD card, we
   initialize it NOW.  If it fails (no card present)
   or other reasons, the various SD file usage commands
   will be failed with a device-error, simply by
   testing the sd_initialized flag as needed.
*/

static void drv_start(void) {

  if (!fs_initialized) {
    if (f_mount(1, &fs) == FR_OK) {
      fs_initialized = TRUE;
    }
  }
}


/*
   drv_open() -
   open a file for read or write on the SD card.
*/
static void drv_open(pab_t *pab) {
  uint16_t len = 0;
  uint8_t att = 0;
  hexstatus_t rc;
  BYTE    mode = 0;
  uint16_t fsize = 0;
  file_t* file = NULL;
  FRESULT res = FR_OK;
  char *path;
  uint8_t pathlen;

  drv_start();

  debug_puts_P("Open File\r\n");

  rc = hex_open_helper(pab, (pab->lun == LUN_CMD ? HEXSTAT_TOO_LONG : HEXSTAT_FILE_NAME_INVALID), &len, &att);
  if(rc != HEXSTAT_SUCCESS)
    return;

  pathlen = pab->datalen - 3;
  path = (char *)(buffer + 3);
  // file path, trimmed whitespaces
  trim(&path, &pathlen);

  //*******************************************************
  // special LUN = 255
  if(
      // special file name "" -> command_mode
     (path[0] == 0)
     || (pab->lun == LUN_CMD)
    ) {
      _cmd_lun = pab->lun;
    // we should check att, as it should be WRITE or UPDATE
    if(path[0] != 0)
      rc = drv_exec_cmds(path, pathlen, &(_config.drv_dev));
    hex_finish_open(BUFSIZE, rc);
    return;
  }

  debug_puts_P("Filename: ");
  debug_puts(path);
  debug_putcrlf();

  //*****************************************************
  // special file name "$" -> catalog
  if (path[0]=='$') {
    file = reserve_lun(pab->lun);
    hex_open_catalog(file, pab->lun, att, (char*)path);  // check file!= null in there
    return;
  }
  //*******************************************************
  // map attributes to FatFS file access mode
  switch (att & OPENMODE_MASK) {
    case OPENMODE_APPEND:  // append mode
      mode = FA_WRITE | FA_OPEN_ALWAYS;
      break;
    case OPENMODE_WRITE: // write, truncate if present. Maybe...
      mode = FA_WRITE | FA_CREATE_ALWAYS;
      break;
    case OPENMODE_UPDATE:
      mode = FA_WRITE | FA_READ | FA_OPEN_ALWAYS;
      break;
    default: //OPENMODE_READ
      mode = FA_READ;
      break;
  }

  if ( !path[ 0 ] ) {
    rc = HEXSTAT_OPTION_ERR; // no name?
  } else {
    if ( fs_initialized ) {
      file = reserve_lun(pab->lun);
      file->pattern = strdup(path);
    }
    if (file != NULL) {
      res = f_open(&fs, &(file->fp), (UCHAR *)path, mode);
      // TODO we can remove if we add FA_OPEN_APPEND to FatFS
      if(res == FR_OK && (att & OPENMODE_MASK) == OPENMODE_APPEND ) {
        // TODO we can remove if we add FA_OPEN_APPEND to FatFS
        res = f_lseek( &(file->fp), file->fp.fsize ); // position for append.
      }

      rc = fresult2hexstatus(res);
      if(rc == HEXSTAT_SUCCESS) {
        fsize = file->fp.fsize;
      } else {
        free_lun(pab->lun); // free up buffer
      }
    } else { // too many open files, or file system maybe not initialized?
      rc = HEXSTAT_MAX_LUNS;
      if ( !fs_initialized ) {
        rc = HEXSTAT_DEVICE_ERR;
      }
    }
  }
  if (att & OPENMODE_RELATIVE) file->attr |= FILEATTR_RELATIVE;

  if ( rc == HEXSTAT_SUCCESS ) {
    switch (att & OPENMODE_MASK) {

      // when opening to write, or read/write
      default:
        if (!(att & OPENMODE_INTERNAL) /*&& pab->lun != 0*/) {
          file->attr |= FILEATTR_DISPLAY;
        }
        // if we don't know how big its going to be... we may need multiple writes.
        if ( len == 0 ) {
          fsize = BUFSIZE;
        } else {
          // otherwise, we know. and do NOT allow fileattr display under any circumstance.
          fsize = len;
          //file->attr &= ~FILEATTR_DISPLAY;
        }
        break;

      // when opening to read-only
      case OPENMODE_READ:
        if (!(att & OPENMODE_INTERNAL)) {
          file->attr |= FILEATTR_DISPLAY;
        }
        // open read-only w LUN=0: just return size of file we're reading; always. this is for verify, etc.
        if (pab->lun != 0 ) {
          if (len) {
            fsize = len; // non zero length requested, use it.
          } else {
            fsize = BUFSIZE;  // on zero length request, return buffer size we use.
          }
        }
        else if (len)
          fsize = len; // accept the requested buffer size for verify
        // for len=0 return fsize.
        break;
    }
  }

  hex_finish_open(fsize, rc);
}


/*
   drv_close() -
   close the file associated with the LUN in the PAB.
   If the file is open, it is closed and data is sync'd.
   If the file is not open, appropriate status is returned

*/
static void drv_close(pab_t *pab) {
  hexstatus_t rc;
  file_t* file = NULL;
  FRESULT res = FR_OK;

  if (
      pab->lun == _cmd_lun
      || pab->lun == LUN_CMD
     ) {
    // handle command channel read
    hex_close_cmd();
    _cmd_lun = LUN_CMD;
    return;
  }

  debug_puts_P("Close File\r\n");

  file = find_lun(pab->lun);
  if (file != NULL) {
    if(!(file->attr & FILEATTR_CATALOG)) {
      res = f_close(&(file->fp));
    }
    free_lun(pab->lun);
    rc = fresult2hexstatus(res);
  } else {
    rc = HEXSTAT_NOT_OPEN; // File not open.
    if ( !fs_initialized ) {
      rc = HEXSTAT_DEVICE_ERR;
    }
  }

  hex_send_final_response( rc );
}

/*
   drv_restore() -
   reset file to beginning
   valid for update, input mode open files.
*/
static void drv_restore( pab_t *pab ) {
  hexstatus_t  rc = HEXSTAT_SUCCESS;
  file_t*  file = NULL;

  debug_puts_P("Drive Restore\r\n");

  drv_start();

  if ( open_files ) {
    file = find_lun(pab->lun);
    if ( file == NULL ) {
      rc = HEXSTAT_DEVICE_ERR;
    }
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }

  if ( rc == HEXSTAT_SUCCESS ) {
    // If we are restore on an open directory...rewind to start
    if ( file->attr & FILEATTR_CATALOG ) {
      //file->fp.rewindDirectory();
      rc = HEXSTAT_UNSUPP_CMD;
    } else {
      // if we are a normal file, rewind to starting position.
      f_lseek(&(file->fp),  0 ); // restore back to start of file.
    }
  }
  hex_send_final_response( rc );
}

/*
   drv_delete() -
   delete a file from the SD card.
*/
static void drv_delete(pab_t *pab) {
  hexstatus_t rc = HEXSTAT_SUCCESS;
  FRESULT fr;
  uint8_t len;
  char *buf;

  debug_puts_P("Delete File\r\n");

  drv_start();

  if(pab->datalen > BUFSIZE) { // command too long...
    hex_eat_it( pab->datalen, HEXSTAT_FILE_NAME_INVALID );
    return;
  }
  len = pab->datalen;
  rc = hex_get_data(buffer, len);

  // simplistic removal. doesn't check for much besides
  // existance at this point.  We should be able to know if
  // the file is open or not, and test for that; also should
  // test if it is really a file, or if it is a directory.
  // But for now; this'll do.

  if ( rc == HEXSTAT_SUCCESS ) {
    buf = (char *)buffer;
    trim(&buf, &len);
      // remove file
    fr = f_unlink(&fs, (UCHAR *)buf);
    rc = fresult2hexstatus(fr);
  }
  hex_send_final_response( rc );
}

/*
   drv_delete_open() -
   delete a open file from the SD card.
*/
static void drv_delete_open(pab_t *pab) {
  hexstatus_t rc = HEXSTAT_SUCCESS;
  FRESULT res;
  file_t*  file = NULL;

  debug_puts_P("Delete Open File\r\n");

  drv_start();

  file = find_lun(pab->lun);
  if (file != NULL){
    res = f_close(&(file->fp));
    rc = fresult2hexstatus(res);
    if(rc == HEXSTAT_SUCCESS) {
      res = f_unlink(&fs, (UCHAR *)file->pattern);
      rc = fresult2hexstatus(res);
    }
    free_lun(pab->lun);
  } else
    rc = HEXSTAT_NOT_OPEN;
  hex_send_final_response( rc );
}

/*
    drv_status() -
    initial simplistic implementation
*/
static void drv_status( pab_t *pab ) {
  uint8_t st = FILE_REQ_STATUS_NONE;
  file_t* file = NULL;

  debug_puts_P("Drive Status\r\n");
  if ( pab->lun == 0 ) {
    st = open_files ? FILE_DEV_IS_OPEN : FILE_REQ_STATUS_NONE;
    st |= FILE_IO_MODE_READWRITE;  // if SD is write-protected, then FILE_IO_MODE_READONLY should be here.
  } else {
    file = find_lun(pab->lun);
    if ( file == NULL ) {
      st = FILE_IO_MODE_READWRITE | FILE_DEV_TYPE_INTERNAL | FILE_SUPPORTS_RELATIVE;
    } else {
      // TODO: we need to cache the file's "open" mode (read-only, write-only, read/write/append and report that properly here.
      //       ... relative or sequential access as well)
      st = FILE_DEV_IS_OPEN | FILE_SUPPORTS_RELATIVE | FILE_IO_MODE_READWRITE;
      if ( !( file->attr & FILEATTR_CATALOG )) {
        if ( file->fp.fptr == file->fp.fsize ) {
          st |= FILE_EOF_REACHED;
        }
      }
      else { // FILEATTR_CATALOG
        if (file->dirnum == 0) {
          st |= FILE_EOF_REACHED;
        }
      }
    }
  }
  if ( pab->buflen >= 1 ) {
    if ( !hex_is_bav() ) {
      hex_send_word( 1 );
      hex_send_byte( st );
      hex_send_byte( HEXSTAT_SUCCESS );
      hex_finish();
    }
  } else {
    hex_send_final_response( HEXSTAT_BUF_SIZE_ERR );
  }
}


static void drv_reset_dev( __attribute__((unused)) pab_t *pab) {

  drv_reset();
  // release the bus ignoring any further action on bus. no response sent.
  hex_finish();
  // wait here while bav is low
  while ( !hex_is_bav() ) {
    ;
  }
}


/*
   Command handling registry for device
*/
static const cmd_op_t ops[] PROGMEM = {
                                        {HEXCMD_OPEN, drv_open},
                                        {HEXCMD_CLOSE, drv_close},
                                        {HEXCMD_DELETE_OPEN, drv_delete_open},
                                        {HEXCMD_READ, drv_read},
                                        {HEXCMD_WRITE, drv_write},
                                        {HEXCMD_RESTORE, drv_restore},
                                        {HEXCMD_RETURN_STATUS, drv_status},
                                        {HEXCMD_DELETE, drv_delete},
                                        {HEXCMD_VERIFY, drv_verify},
                                        {HEXCMD_RESET_BUS, drv_reset_dev},
                                        {(hexcmdtype_t)HEXCMD_INVALID_MARKER, NULL}
                                      }; // end of table.


void drv_register(void) {
  uint8_t drv_dev = DEV_DRV_DEFAULT;

  if(_config.valid) {
    drv_dev = _config.drv_dev;
  }
  reg_add(DEV_DRV_START, drv_dev, DEV_DRV_END, ops);
  disk_init();
}


void drv_reset( void )
{
  file_t* file = NULL;
  uint8_t lun;

  debug_puts_P("Reset\r\n");

  drv_start();

  if ( open_files ) {
    // find file(s) that are open, get file pointer and lun number
    while ( (file = find_file_in_use(&lun) ) != NULL ) {
      // if we found a file open, silently close it, and free its lun.
      if ( fs_initialized ) {
        f_close(&(file->fp));  // close and sync file.
      }
      free_lun(lun);
      // continue until we find no additional files open.
    }
  }
}


void drv_init(void) {
  uint8_t i;

  // close all open files
  for (i = 0; i < MAX_OPEN_FILES; i++) {
    files[i].used = FALSE;
  }
  open_files = 0;
  fs_initialized = FALSE;
  drv_register();
  disk_init();

  //strcpy((char *)buffer,"set device=101");
  //drv_exec_cmds((char *)buffer, strlen((char *)buffer));


}
#endif
