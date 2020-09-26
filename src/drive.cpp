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
#include <string.h>
#include <stdlib.h>
#include <avr/pgmspace.h>

#include "config.h"
#include "catalog.h"
#include "debug.h"
#include "diskio.h"
#include "ff.h"
#include "hexbus.h"
#include "hexops.h"
#include "led.h"
#include "registry.h"
#include "timer.h"
#include "drive.h"

#define FR_EOF     255    // We need an EOF error for hexbus_read.

FATFS fs;

// Global defines
uint8_t open_files = 0;
luntbl_t files[MAX_OPEN_FILES]; // file number to file mapping
uint8_t fs_initialized = FALSE;


/**
 * Remove all leading and trailing whitespaces
 */
static void trim(uint8_t **buf, uint8_t *blen) {
  uint8_t i;

  // Trim leading space
  while(**buf == ' ') {
    (*buf)++;
    (*blen)--;
  }

  if(!(*blen)) {  // All spaces?
    (*buf)[0] = '\0';
    return;
  }

  // Trim trailing space
  i = *blen - 1;
  while(i) {
    if((*buf)[i] != ' ')
      break;
    i--;
    (*blen)--;
  }

  // Write new null terminator character
  (*buf)[*blen] = '\0';
  return;
}

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
 * Search for the trailing LF (\n) and ignore a CR (\r) before the LF.
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
   then the length of the program in memory. But this is not the case. Instead
   the calculator starts to send (step 2) its memory content up to the length
   it got in the open response no matter if this exceeds the actual length of
   the program stored in memory! By staring at the debug output I found out
   that the bytes 2 and 3 (start counting at 0) is the actual length of the
   program. So one can compare the sizes and, in case they differ, return IO
   error 12. And although if a comparison error occured, one has read the data
   transmitted in the verify command until sending stops.
*/

static hexstatus_t hex_drv_verify(pab_t pab) {
  uint16_t len_prog_mem = 0;
  uint16_t len_prog_stored = 0;
  uint8_t  *data = &buffer[ BUFSIZE / 2 ]; // split our buffer in half
  // so we do not use all of our limited amount of RAM on buffers...
  UINT     read;
  uint16_t len;
  uint16_t i;
  file_t*  file;
  BYTE     res = FR_OK;
  hexstatus_t rc = HEXSTAT_SUCCESS;
  uint8_t  first_buffer = 1;

  debug_puts_P(PSTR("Verify File\n"));

  file = find_lun(pab.lun);
  len = pab.datalen;   // this is the size of the object to verify

  res = (file != NULL ? FR_OK : FR_NO_FILE);

  while (len && res == FR_OK) {

    // figure out how much will fit...
    i = ( len >= ( BUFSIZE / 2 ))  ? ( BUFSIZE / 2 ) : len;

    if ( hex_get_data(buffer, i) ) { // use front half of buffer for incoming data from the host.
      hex_release_bus();
      return HEXSTAT_BUS_ERR;
    }

    if (res == FR_OK) {
      // length of program in memory
      if (first_buffer) {
        len_prog_mem = buffer[2] | ( buffer[3] << 8 );
      }

      res = f_read(&(file->fp), data, i, &read);

      if (res == FR_OK) {
        debug_trace(buffer, 0, read);
      }
      // TODO should convert res to rc value
      // length of program on storage device
      if (first_buffer) {
        len_prog_stored = data[2] | ( data[3] << 8);
      }

      if (len_prog_stored != len_prog_mem) {
        // program on disk not same length as one in memory
        rc = HEXSTAT_BUF_SIZE_ERR;
      }
      else {
        if ( memcmp(data, buffer, read) != 0 ) {
          rc = HEXSTAT_VERIFY_ERR;
        }
      }
    }

    first_buffer = 0;
    len -= read;
  }
  // If we haven't read the entire incoming message yet, flush it.
  if ( len ) {
    if ( !fs_initialized ) {
      rc = HEXSTAT_DEVICE_ERR;
    }
    hex_eat_it( len, rc ); // reports status back.
    return HEXSTAT_BUS_ERR;
  } else {

    if (!hex_is_bav()) { // we can send response
      hex_send_final_response( rc );
    } else {
      hex_finish();
    }
  }
  return HEXSTAT_SUCCESS;
}

static hexstatus_t hex_drv_write_cmd(pab_t pab) {
  hexstatus_t rc = HEXSTAT_SUCCESS;
  uint16_t len;
  BYTE res = FR_OK;
  uint8_t *s;
  uint8_t *comma;
  uint8_t *newname;
  uint8_t *name;
  uint8_t namelen;
  uint8_t cmd;

  len = pab.datalen;
  if (len < BUFSIZE){
    rc = hex_get_data(buffer, len);
    if (rc == HEXSTAT_SUCCESS) {
      name = &(buffer[0]);
      namelen = len;
      // path, trimmed whitespaces
      trim(&name, &namelen);
      // ToDo: We need a simple parser. That requires more knowledge of C/C++ then I have
      // and some string operations. This is a very simple implementation:
      do {
        comma = (uint8_t *)strchr((char *)name, ',');
        if (comma != NULL) comma[0] = 0; // terminating next command
        namelen = strlen((char *)name);
        trim(&name, &namelen);
        if (namelen > 3 && (name[1]=='d' || name[1]=='D') && name[2]==' ')  {
          cmd = name[0];
          name = &(name[3]);
          namelen = strlen((char *)name);
          trim(&name, &namelen);
          if (cmd == 'c' || cmd == 'C')
            res = f_chdir(&fs,(UCHAR*)name);
          else if (cmd == 'm' || cmd == 'M')
            res = f_mkdir(&fs,(UCHAR*)name);
          else
            res = FR_RW_ERROR;
          }
        else if (namelen > 3 && (name[1]=='n' || name[1]=='N') && name[2]==' ')  {
          cmd = name[0];
          name = &(name[3]);
          if (cmd == 'r' || cmd == 'R'){
            s = (uint8_t *)strchr((char *)name, '=');
            if (s != NULL){
              s[0] = 0; // terminating 0 for name
              newname = &(s[1]);
              namelen = strlen((char *)name);
              trim(&name, &namelen);
              namelen = strlen((char *)newname);
              trim(&newname, &namelen);
              res = f_rename(&fs,(UCHAR*)name,(UCHAR*)newname);
            }
            else
              res = FR_RW_ERROR;
          }
          else
            res = FR_RW_ERROR;
          }
        else
          res = FR_RW_ERROR;
        if (comma != NULL) name = &(comma[1]);
      } while (res == FR_OK && comma != NULL);
      }
    else
      res = FR_RW_ERROR;
  }
  else {
    res = FR_INVALID_OBJECT;
    // TODO what error should this be?
    hex_eat_it( len, HEXSTAT_BUS_ERR ); // reports status back.
    return HEXSTAT_BUS_ERR;
  }
  if (rc == HEXSTAT_SUCCESS) {
    switch (res) {
      case FR_OK:
        rc = HEXSTAT_SUCCESS;
        break;
      default:
        rc = HEXSTAT_DEVICE_ERR;
        break;
    }
  }

  if (!hex_is_bav() ) { // we can send response
    hex_send_final_response( rc );
  } else {
    hex_finish();
  }
  return HEXSTAT_SUCCESS;
}


/*
   hex_drv_write() -
   writes data to the open file associated with the LUN number
   in the PAB.

   TODO: for files opened in RELATIVE (not VARIABLE) mode, the
   file should be in READ/WRITE mode.  RELATIVE mode is considered
   to be like a 'database' file with FIXED length records of 'x'
   bytes per record.  The record number field of the PAB informs
   the file-write operation of which record is to be written.  If
   the file needs to be increased in size to reach that record,
   then zero-filled records should be written to reach the desired
   position (i.e. you can get the current size of the file, divide
   by the record size to determine the current number of records.
   if the record being written exists beyond the end of the file,
   write empty records until the desired size is reached, then
   write the new record to the file.  If the record number in the
   PAB points to a location within the file, seek to that offset
   from the beginning of the file and overwrite the data that
   currently exists in that location.  This all assumes that the
   underlying file system supports this behavior and feature.

   If it does not, then the hex_open operation, when it detects
   a RELATIVE file open, should always report an error of
   HEXSTAT_FILE_TYPE_ERR. (And, currently, the Arduino SD file
   library does NOT handle opening files for both read AND write
   of a selective nature such as required for this feature; from
   what I can tell of the implementation.

*/
static hexstatus_t hex_drv_write(pab_t pab) {
  hexstatus_t rc = HEXSTAT_SUCCESS;
  uint16_t len;
  uint16_t i;
  UINT written;
  file_t* file = NULL;
  BYTE res = FR_OK;
  
  debug_puts_P(PSTR("Write File\n"));

  len = pab.datalen;
  file = find_lun(pab.lun);
  if (file != NULL && (file->attr & FILEATTR_COMMAND)) {
    // handle command channel
    return hex_drv_write_cmd(pab);
  }
  res = (file != NULL ? FR_OK : FR_NO_FILE);
  if (res == FR_OK && (file->attr & FILEATTR_RELATIVE)){
    // if we're not at the right record position, reposition
    if (file->fp.fsize < pab.buflen * pab.record) {
      // file is too small.  Enlarge.
      // TODO make this more efficient by handling in 256 byte chunks...
      res = f_lseek( &(file->fp), file->fp.fsize );
      buffer[0] = 0;
      for (i = file->fp.fsize; i < pab.buflen * pab.record; i++)
        res = f_write(&(file->fp), buffer, 1, &written);
    }  else // file is big enough, just find the right spot.
      res = f_lseek(&(file->fp), pab.buflen * pab.record);
  }
  // OK, read the data we need to send to the file
  while (len && rc == HEXSTAT_SUCCESS && res == FR_OK ) {
    i = (len >= BUFSIZE ? BUFSIZE : len);
    rc = hex_get_data(buffer, i);
    if (file != NULL && res == FR_OK && rc == HEXSTAT_SUCCESS) {

      res = f_write(&(file->fp), buffer, i, &written);
      if ( written != i ) {
        res = FR_DENIED;
      }
    }
    len -= i;
  }
  if (file != NULL && (file->attr & FILEATTR_RELATIVE)){
    buffer[0] = 0;
    for (i=pab.datalen + 1; i <= pab.buflen; i++)
      res = f_write(&(file->fp), buffer, 1, &written);
  }
  if ( len ) {
    if ( !fs_initialized ) {
      rc = HEXSTAT_DEVICE_ERR;
    }
    // TODO what error should we return to caller?
    hex_eat_it( len, rc );
    return HEXSTAT_BUS_ERR;
  }

  // if in DISPLAY mode
  if (file != NULL && (file->attr & FILEATTR_DISPLAY)) {
	// add CRLF to data (for DISPLAY mode)
    buffer[0] = 13;
    buffer[1] = 10;

    res = f_write(&(file->fp), buffer, 2, &written);
    if (!res) {
      debug_trace(buffer, 0, written);
    }
    if (written != 2) {
      rc = HEXSTAT_BUF_SIZE_ERR;  // generic error.
    }
  }
  if (rc == HEXSTAT_SUCCESS) {
    switch (res) {
      case FR_OK:
        rc = HEXSTAT_SUCCESS;
        break;
      default:
        rc = HEXSTAT_DEVICE_ERR;
        break;
    }
  }

  if (!hex_is_bav() ) { // we can send response
    hex_send_final_response( rc );
  } else {
    hex_finish();
  }
  return HEXSTAT_SUCCESS;
}

static hexstatus_t hex_drv_read_cmd(pab_t pab __attribute__((unused))) {
  hexstatus_t rc;
  // This really should always succeed
  transmit_word(0);      // null file
  rc = HEXSTAT_NOT_FOUND;
  if ( !fs_initialized ) {
    rc = HEXSTAT_DEVICE_ERR;
  }
  transmit_byte( rc ); // status byte transmit
  hex_finish();
  return HEXSTAT_SUCCESS;
}


/*
   hex_drv_read() -
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
    Other raw data files (e.g. RAM/ROM images) must use LUN 255 as a
    special LUN. The amount of bytes to be send is determined by the
    file length    
*/
static hexstatus_t hex_drv_read(pab_t pab) {
  hexstatus_t rc;
  uint8_t i;
  uint16_t len;
  uint16_t fsize;
  char token;
  UINT read;
  BYTE res = FR_OK;
  file_t* file;

  debug_puts_P(PSTR("Read File\n"));

  file = find_lun(pab.lun);
  if (file != NULL && (file->attr & FILEATTR_COMMAND)) {
    // handle command channel read
    return hex_drv_read_cmd(pab);
  }

  if (file != NULL && (file->attr & FILEATTR_CATALOG)) {
    if (pab.lun == 0 ) {
      debug_putc('P');
        return hex_read_catalog(file);
    }
    else {
      debug_putc('T');
        return hex_read_catalog_txt(file);
    }
  }
  if ((file != NULL) && (file->attr & FILEATTR_RELATIVE))
    f_lseek(&(file->fp), pab.buflen * pab.record);
  if (file != NULL) {
    fsize = file->fp.fsize - (uint16_t)file->fp.fptr; // amount of data in file that can be sent.
    if (fsize != 0 && pab.lun != 0 && pab.lun != 255) { // for 'normal' files (lun > 0 && lun < 255) send data value by value
      // amount of data for next value to be sent
      if (file->attr & FILEATTR_RELATIVE) 
        fsize = pab.buflen;
      else  
        fsize = next_value_size(file); // TODO maybe rename fsize to something like send_size
    }

    if (res == FR_OK) {
      if ( fsize == 0 ) {
        res = FR_EOF;
      } else {
        // size of buffer provided by host (amount to send)
        if ( fsize > pab.buflen ) {
          fsize = pab.buflen;
        }
      }
    }
    // send how much we are going to send
    rc = (transmit_word( fsize ) == HEXERR_SUCCESS ? HEXSTAT_SUCCESS : HEXSTAT_DATA_ERR);

    // while we have data remaining to send.
    while ( fsize && rc == HEXSTAT_SUCCESS ) {

      len = fsize;    // remaining amount to read from file
      // while it fit into buffer or not?  Only read as much
      // as we can hold in our buffer.
      len = ( len > BUFSIZE ) ? BUFSIZE : len;

      if ( !(file->attr & FILEATTR_CATALOG )) {
        // memset((char *)buffer, 0, BUFSIZE);  // TODO Do we need this? No!
        res = f_read(&(file->fp), buffer, len, &read);
        if (!res) {
          debug_trace(buffer, 0, read);
        }
      } else {
        // catalog entry, if that's what we're reading, is already in buffer.
        read = fsize; // 0 if no entry, else size of entry in buffer.
      }

      if (FR_OK == res) {
        for (i = 0; i < read; i++) {
          rc = (transmit_byte(buffer[i]) == HEXERR_SUCCESS ? HEXSTAT_SUCCESS : HEXSTAT_DATA_ERR);
        }
      }
      else
      {
        // TODO set res or rc, or both here?
        res = FR_RW_ERROR;
      }

      fsize -= read;
    }

    switch (res) {
      case FR_OK:
        if (file->attr & FILEATTR_DISPLAY) {
          res = f_read(&(file->fp), &token, 1, &read); // skip (CR)LF
          if (token == '\r')
            res = f_read(&(file->fp), &token, 1, &read);
        } 
        rc = HEXSTAT_SUCCESS;
        break;
      case FR_EOF:
        rc = HEXSTAT_EOF;
        break;
      default:
        rc = HEXSTAT_DEVICE_ERR;
        break;
    }

  } else {
    transmit_word(0);      // null file
    rc = HEXSTAT_NOT_FOUND;
    if ( !fs_initialized ) {
      rc = HEXSTAT_DEVICE_ERR;
    }
  }
  transmit_byte( rc ); // status byte transmit
  hex_finish();
  return HEXSTAT_SUCCESS;
}


/*
   drv_start - open filesystem.
   make- ignore/ empty function.
*/
void drv_start(void) {

  if (!fs_initialized) {
    if (f_mount(1, &fs) == FR_OK) {
      fs_initialized = TRUE;
    }
  }
}


/*
   hex_drv_open() -
   open a file for read or write on the SD card.
*/
static hexstatus_t hex_drv_open(pab_t pab) {
  uint16_t len = 0;
  uint8_t att = 0;
  hexstatus_t rc;
  BYTE    mode = 0;
  uint16_t fsize = 0;
  file_t* file = NULL;
  BYTE res = FR_OK;
  uint8_t *path;
  uint8_t pathlen;

  debug_puts_P(PSTR("Open File\n"));

  /*
     If we are attempting to use the SD card, we
     initialize it NOW.  If it fails (no card present)
     or other reasons, the various SD file usage commands
     will be failed with a device-error, simply by
     testing the sd_initialized flag as needed.
  */
  drv_start();

  // we need one more byte for the null terminator, so check.
  if(pab.datalen > BUFSIZE - 1) { // name too long
    hex_eat_it( pab.datalen, HEXSTAT_FILE_NAME_INVALID );
    return HEXSTAT_BUS_ERR;
  }

  if ( hex_get_data(buffer, pab.datalen) == HEXSTAT_SUCCESS ) {
    len = buffer[ 0 ] + ( buffer[ 1 ] << 8 );
    att = buffer[ 2 ];
  } else {
    hex_release_bus();
    return HEXSTAT_BUS_ERR; // BAV ERR.
  }
  
  path = &(buffer[3]);
  pathlen = pab.datalen - 3;
  // file path, trimmed whitespaces
  trim(&path, &pathlen);

  debug_puts_P(PSTR("Filename: "));
  debug_puts((char *)path);
  debug_putcrlf();

  //*****************************************************
  // special file name "$" -> catalog
  if (path[0]=='$') {
    file = reserve_lun(pab.lun);
    return hex_open_catalog(file, pab.lun, att, (char*)path);  // check file!= null in there
  }
  //*******************************************************
  // special file name "" -> command_mode
  if (path[0] == 0)  {
    file = reserve_lun(pab.lun);
    if (file != NULL){
      file->attr |= FILEATTR_COMMAND;
      rc = HEXSTAT_SUCCESS;
    }
    else
      rc = HEXSTAT_MAX_LUNS;      
    if(!hex_is_bav()) { // we can send response
      if(rc == HEXSTAT_SUCCESS) {
        transmit_word(2); 
        transmit_word(255);
        transmit_byte(HEXSTAT_SUCCESS);    // status code
        hex_finish();
        return HEXSTAT_SUCCESS;
      } else
        hex_send_final_response( rc );
    }
    hex_finish();
    debug_putc('E');
    return HEXSTAT_BUS_ERR;
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
      file = reserve_lun(pab.lun);
    }
    if (file != NULL) {
      res = f_open(&fs, &(file->fp), (UCHAR *)path, mode);
      // TODO we can remove if we add FA_OPEN_APPEND to FatFS
      if(res == FR_OK && (att & OPENMODE_MASK) == OPENMODE_APPEND ) {
        // TODO we can remove if we add FA_OPEN_APPEND to FatFS
        res = f_lseek( &(file->fp), file->fp.fsize ); // position for append.
      }

      switch (res) {
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
      if (rc) {
        free_lun(pab.lun); // free up buffer
      }
    } else { // too many open files, or file system maybe not initialized?
      rc = HEXSTAT_MAX_LUNS;
      if ( !fs_initialized ) {
        rc = HEXSTAT_DEVICE_ERR;
      }
    }
  }
  if (att & OPENMODE_RELATIVE) file->attr |= FILEATTR_RELATIVE;
  if (!hex_is_bav()) { // we can send response
    if ( rc == HEXSTAT_SUCCESS ) {
      switch (att & OPENMODE_MASK) {

        // when opening to write, or read/write
        default:
          if (!(att & OPENMODE_INTERNAL)) {
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
          if (pab.lun != 0 ) {
            if (len) {
              fsize = len; // non zero length requested, use it.
            } else {
              fsize = BUFSIZE;  // on zero length request, return buffer size we use.
            }
          }
          // for len=0 OR lun=0, return fsize.
          break;
      }

      if ( rc == HEXSTAT_SUCCESS ) {
        transmit_word( 4 );
        transmit_word( fsize );
        transmit_word( 0 );      // position
        transmit_byte( HEXSTAT_SUCCESS );
        hex_finish();
      } else {
        hex_send_final_response( rc );
      }
    } else {
      hex_send_final_response( rc );
    }
    return HEXSTAT_SUCCESS;
  }
  hex_finish();
  return HEXSTAT_BUS_ERR;
}


/*
   hex_drv_close() -
   close the file associated with the LUN in the PAB.
   If the file is open, it is closed and data is sync'd.
   If the file is not open, appropriate status is returned

*/
static hexstatus_t hex_drv_close(pab_t pab) {
  hexstatus_t rc;
  file_t* file = NULL;
  BYTE res = 0;

  debug_puts_P(PSTR("Close File\n"));

  file = find_lun(pab.lun);
  if (file != NULL) {
    // Don't need to close the command channel.
    if(!(file->attr & FILEATTR_CATALOG) && !(file->attr & FILEATTR_COMMAND)) {
      res = f_close(&(file->fp));
    }
    free_lun(pab.lun);
    switch (res) {
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
    rc = HEXSTAT_NOT_OPEN; // File not open.
    if ( !fs_initialized ) {
      rc = HEXSTAT_DEVICE_ERR;
    }
  }

  if ( !hex_is_bav() ) {
    hex_send_final_response( rc );
    return HEXSTAT_SUCCESS;
  }
  hex_finish();
  return HEXSTAT_BUS_ERR;
}

/*
   hex_drv_restore() -
   reset file to beginning
   valid for update, input mode open files.
*/
static hexstatus_t hex_drv_restore( pab_t pab ) {
  hexstatus_t  rc = HEXSTAT_SUCCESS;
  file_t*  file = NULL;
  //BYTE     res = 0;

  debug_puts_P(PSTR("Drive Restore\n"));
  if ( open_files ) {
    file = find_lun(pab.lun);
    if ( file == NULL ) {
      rc = HEXSTAT_DEVICE_ERR;
    }
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }

  if (!hex_is_bav() ) {
    if ( rc == HEXSTAT_SUCCESS ) {
/*#ifdef ARDUINO
      // If we are restore on an open directory...rewind to start
      if ( file->attr & FILEATTR_CATALOG ) {
        file->fp.rewindDirectory();
      } else {
        // if we are a normal file, rewind to starting position.
        file->fp.seek( 0 ); // restore back to start of file.
      }
#else*/  // TODO need to implement this
      rc = HEXSTAT_UNSUPP_CMD;
    }
    hex_send_final_response( rc );
  }
  hex_finish();
  return HEXSTAT_BUS_ERR;
}

/*
   hex_drv_delete() -
   delete a file from the SD card.
*/
static hexstatus_t hex_drv_delete(pab_t pab) {
  hexstatus_t rc = HEXSTAT_SUCCESS;
  FRESULT fr;

  debug_puts_P(PSTR("Delete File\n"));

  memset(buffer, 0, BUFSIZE);

  if ( hex_get_data(buffer, pab.datalen) == HEXSTAT_SUCCESS ) {
  } else {
    hex_release_bus();
    return HEXSTAT_BUS_ERR;
  }
  // simplistic removal. doesn't check for much besides
  // existance at this point.  We should be able to know if
  // the file is open or not, and test for that; also should
  // test if it is really a file, or if it is a directory.
  // But for now; this'll do.

  if ( rc == HEXSTAT_SUCCESS ) {
    // If we did not fill buffer, we have a null at end due to memset before retrieval.
    if ( pab.datalen < BUFSIZE - 1 ) {
      // remove file
      fr = f_unlink(&fs, buffer);
      switch (fr) {
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
    } else {
      rc = HEXSTAT_FILE_NAME_INVALID;
    }
  }
  if (!hex_is_bav()) { // we can send response
    hex_send_final_response( rc );
    return HEXSTAT_SUCCESS;
  }
  hex_finish();
  return HEXSTAT_BUS_ERR;
}

/*
    hex_drv_status() -
    initial simplistic implementation
*/
static hexstatus_t hex_drv_status( pab_t pab ) {
  uint8_t st = FILE_REQ_STATUS_NONE;
  file_t* file = NULL;

  debug_puts_P(PSTR("Drive Status\n"));
  if ( pab.lun == 0 ) {
    st = open_files ? FILE_DEV_IS_OPEN : FILE_REQ_STATUS_NONE;
    st |= FILE_IO_MODE_READWRITE;  // if SD is write-protected, then FILE_IO_MODE_READONLY should be here.
  } else {
    file = find_lun(pab.lun);
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
  if ( !hex_is_bav() ) {
    if ( pab.buflen >= 1 )
    {
      transmit_word( 1 );
      transmit_byte( st );
      transmit_byte( HEXSTAT_SUCCESS );
      hex_finish();
      return HEXSTAT_SUCCESS;
    } else {
      hex_send_final_response( HEXSTAT_BUF_SIZE_ERR );
      return HEXSTAT_SUCCESS;
    }
  }
  hex_finish();
  return HEXSTAT_BUS_ERR;
}


static hexstatus_t hex_drv_reset( __attribute__((unused)) pab_t pab) {

  drv_reset();
  // release the bus ignoring any further action on bus. no response sent.
  hex_finish();
  // wait here while bav is low
  while ( !hex_is_bav() ) {
    ;
  }
  return HEXSTAT_SUCCESS;
}


/*
   Command handling registry for device
*/
#ifdef USE_NEW_OPTABLE
static const cmd_op_t ops[] PROGMEM = {
                                        {HEXCMD_OPEN, hex_drv_open},
                                        {HEXCMD_CLOSE, hex_drv_close},
                                        {HEXCMD_READ, hex_drv_read},
                                        {HEXCMD_WRITE, hex_drv_write},
                                        {HEXCMD_RESTORE, hex_drv_restore},
                                        {HEXCMD_RETURN_STATUS, hex_drv_status},
                                        {HEXCMD_DELETE, hex_drv_delete},
                                        {HEXCMD_VERIFY, hex_drv_verify},
                                        {HEXCMD_RESET_BUS, hex_drv_reset},
                                        {(hexcmdtype_t)HEXCMD_INVALID_MARKER, NULL}
                                      }; // end of table.
#else
static const cmd_proc fn_table[] PROGMEM = {
  hex_drv_open,
  hex_drv_close,
  hex_drv_read,
  hex_drv_write,
  hex_drv_restore,
  hex_drv_status,
  hex_drv_delete,
  hex_drv_verify,
  hex_drv_reset,
  NULL // end of table.
};


static const uint8_t op_table[] PROGMEM = {
  HEXCMD_OPEN,
  HEXCMD_CLOSE,
  HEXCMD_READ,
  HEXCMD_WRITE,
  HEXCMD_RESTORE,
  HEXCMD_RETURN_STATUS,
  HEXCMD_DELETE,
  HEXCMD_VERIFY,
  HEXCMD_RESET_BUS,
  HEXCMD_INVALID_MARKER
};
#endif

void drv_register(void) {
#ifdef NEW_REGISTER
#ifdef USE_NEW_OPTABLE
  cfg_register(DEV_DRV_START, DEV_DRV_DEFAULT, DEV_DRV_END, ops);
#else
  cfg_register(DEV_DRV_START, DEV_DRV_DEFAULT, DEV_DRV_END, op_table, fn_table);
#endif
  disk_init();
#else
  uint8_t i = registry.num_devices;

  registry.num_devices++;
  registry.entry[ i ].dev_low = DEV_DRV_START;
  registry.entry[ i ].dev_cur = DEV_DRV_DEFAULT;
  registry.entry[ i ].dev_high = DEV_DRV_END; // support 100-109 for disks
#ifdef USE_NEW_OPTABLE
#else
  registry.entry[ i ].operation = (cmd_proc *)&fn_table;
  registry.entry[ i ].command = (uint8_t *)&op_table;
#endif
#endif
}


void drv_reset( void )
{
  file_t* file = NULL;
  uint8_t lun;

  debug_puts_P(PSTR("Reset\n"));

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
  return;
}


void drv_init(void) {
  uint8_t i;

  // close all open files
  for (i = 0; i < MAX_OPEN_FILES; i++) {
    files[i].used = FALSE;
  }
  open_files = 0;
  fs_initialized = FALSE;
#ifdef INIT_COMBO
  drv_register();
#endif
  disk_init();
}
