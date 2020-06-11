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
#include "config.h"
#include "ff.h"
#include "hexbus.h"
#include "hexops.h"
#include "led.h"
#include "timer.h"
#include "uart.h"

#include "drive.h"

#define FR_EOF     255    // We need an EOF error for hexbus_read.

#ifdef ARDUINO

#include <SPI.h>
#include <SD.h>

const uint8_t  chipSelect = 10;

#ifdef INCLUDE_CLOCK
void dateTime(uint16_t* date, uint16_t* time);
#endif

#else
FATFS fs;
#include "diskio.h"
#endif

// Global references
extern uint8_t buffer[BUFSIZE];

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
      open_files--;
      set_busy_led(open_files);
      if ( !open_files ) {
#ifdef ARDUINO
#ifdef INCLUDE_CLOCK
        SdFile::dateTimeCallbackCancel();
#endif
        SD.end();
        fs_initialized = FALSE;
#endif
      }
    }
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

static uint8_t hex_drv_verify(pab_t pab) {
  uint16_t len_prog_mem = 0;
  uint16_t len_prog_stored = 0;
  uint8_t  *data = &buffer[ sizeof(buffer) / 2 ]; // split our buffer in half
  // so we do not use all of our limited amount of RAM on buffers...
  UINT     read;
  uint16_t len;
  uint16_t i;
  file_t*  file;
  BYTE     res = FR_OK;
  uint8_t  first_buffer = 1;

  uart_putc('>');

  file = find_lun(pab.lun);
  len = pab.datalen;   // this is the size of the object to verify

  res = (file != NULL ? FR_OK : FR_NO_FILE);

  while (len && res == FR_OK) {

    // figure out how much will fit...
    i = ( len >= ( sizeof(buffer) / 2 ))  ? ( sizeof(buffer) / 2 ) : len;

    if ( hex_get_data(buffer, i) ) { // use front half of buffer for incoming data from the host.
      hex_release_bus();
      return HEXERR_BAV;
    }

    if (res == FR_OK) {
      // length of program in memory
      if (first_buffer) {
        len_prog_mem = buffer[2] | ( buffer[3] << 8 );
      }

#ifdef ARDUINO
      // grab same amount of data from file that we have received so far on the bus
      read = file->fp.read( (char *)data, i );
      if ( read ) {
        res = FR_OK;
      } else {
        res = FR_RW_ERROR;
      }

#else

      res = f_read(&(file->fp), data, i, &read);

      if (res == FR_OK) {
        uart_putcrlf();
        uart_trace(buffer, 0, read);
      }

#endif

      // length of program on storage device
      if (first_buffer) {
        len_prog_stored = data[2] | ( data[3] << 8);
      }

      if (len_prog_stored != len_prog_mem) {
        // program on disk not same length as one in memory
        res = HEXSTAT_BUF_SIZE_ERR;
      }
      else {
        if ( memcmp(data, buffer, read) != 0 ) {
          res = HEXSTAT_VERIFY_ERR;
        }
      }
    }

    first_buffer = 0;
    len -= read;
  }
  // If we haven't read the entire incoming message yet, flush it.
  if ( len ) {
    if ( !fs_initialized ) {
      res = HEXSTAT_DEVICE_ERR;
    }
    hex_eat_it( len, res ); // reports status back.
    return HEXERR_BAV;
  } else {

    uart_putc('>');

    if (!hex_is_bav()) { // we can send response
      hex_send_final_response( res );
    } else {
      hex_finish();
    }
  }
  return HEXERR_SUCCESS;
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
static uint8_t hex_drv_write(pab_t pab) {
  uint8_t rc = HEXERR_SUCCESS;
  uint16_t len;
  uint16_t i;
  UINT written;
  file_t* file = NULL;
  BYTE res = FR_OK;

  uart_putc('>');

  file = find_lun(pab.lun);
  len = pab.datalen;
  res = (file != NULL ? FR_OK : FR_NO_FILE);

  while (len && rc == HEXERR_SUCCESS && res == FR_OK ) {
    i = (len >= sizeof(buffer) ? sizeof(buffer) : len);
    rc = hex_get_data(buffer, i);

    if (file != NULL && res == FR_OK && rc == HEXSTAT_SUCCESS) {

#ifdef ARDUINO
      written = (file->fp).write( buffer, i );
#else
      res = f_write(&(file->fp), buffer, i, &written);
#endif
      if ( written != i ) {
        res = FR_DENIED;
      }
    }
    len -= i;
  }

  if ( len ) {
    if ( !fs_initialized ) {
      res = HEXSTAT_DEVICE_ERR;
    }
    hex_eat_it( len, res );
    return HEXERR_BAV;
  }

  if (file != NULL && (file->attr & FILEATTR_DISPLAY)) { // add CRLF to data
    buffer[0] = 13;
    buffer[1] = 10;
#ifdef ARDUINO
    written = (file->fp).write( buffer, 2 );
#else
    res = f_write(&(file->fp), buffer, 2, &written);
#endif
    if (written != 2) {
      rc = HEXSTAT_BUF_SIZE_ERR;  // generic error.
    }
  }

  if (rc == HEXERR_SUCCESS) {
    switch (res) {
      case FR_OK:
        rc = HEXSTAT_SUCCESS;
        break;
      default:
        rc = HEXSTAT_DEVICE_ERR;
        break;
    }
  }

  uart_putc('>');

  if (!hex_is_bav() ) { // we can send response
    hex_send_final_response( rc );
  } else {
    hex_finish();
  }
  return HEXERR_SUCCESS;
}


/*
   hex_drv_read() -
   read data from currently open file associated with the LUN
   in the PAB.
*/
static uint8_t hex_drv_read(pab_t pab) {
  uint8_t rc;
  uint8_t i;
  uint16_t len = 0;
  uint16_t fsize;
  UINT read;
  BYTE res = FR_OK;
  file_t* file;

  uart_putc('<');

  file = find_lun(pab.lun);

  if (file != NULL) {
    if ( !(file->attr & FILEATTR_CATALOG ) ) {
#ifdef ARDUINO
      // amount remaining to read from file
      fsize = (uint16_t)file->fp.size() - (uint16_t)file->fp.position(); // amount of data in file that can be sent.
#else
      fsize = file->fp.fsize;
#endif
      if ( fsize == 0 ) {
        res = FR_EOF;
      } else {
        // size of buffer provided by host (amount to send)
        len = pab.buflen;

        if ( fsize > pab.buflen ) {
          fsize = pab.buflen;
        }
      }
    } else {

      // Read next catalog entry.

#ifdef ARDUINO
      {
        File entry = file->fp.openNextFile();
        uint32_t osize = 0;
        uint8_t flag = 0;

        fsize = 0;
        if ( !entry ) {
          res = FR_EOF;
        } else {
          memset(buffer, 0, sizeof(buffer));
          strcpy((char *)&buffer[0], entry.name() );
          if ( entry.isDirectory() ) {
            flag = F_ISDIRECTORY;
          } else {
            osize = entry.size();
          }
          entry.close();
          // Do we have at least 12 bytes left in buffer?
          if ( sizeof(buffer) - fsize > 12 ) {
            strcat((char *)buffer, ",");
            fsize = strlen((char *)buffer);
            ltoa( osize, (char *)&buffer[fsize], 10);
            strcat((char *)buffer, ",");
            fsize = strlen((char *)buffer );
            itoa( flag, (char *)&buffer[fsize], 10);
            fsize = strlen((char *)buffer );
          }
        }
      }
#else
      // TODO: read next available directory entry using FatFS
      res = FR_EOF;
      fsize = 0;
#endif
    }

    // send how much we are going to send
    rc = transmit_word( fsize );

    // while we have data remaining to send.
    while ( fsize && rc == HEXERR_SUCCESS ) {

      len = fsize;    // remaining amount to read from file
      // while it fit into buffer or not?  Only read as much
      // as we can hold in our buffer.
      len = ( len > sizeof( buffer ) ) ? sizeof( buffer ) : len;

      if ( !(file->attr & FILEATTR_CATALOG )) {
#ifdef ARDUINO
        memset((char *)buffer, 0, sizeof( buffer ));
        read = file->fp.read( (char *)buffer, len );
        if ( read ) {
          res = FR_OK;
        } else {
          res = FR_RW_ERROR;
        }
#else

        res = f_read(&(file->fp), buffer, len, &read);

        if (!res) {
          uart_putc(13);
          uart_putc(10);
          uart_trace(buffer, 0, read);
        }

#endif
      } else {
        // catalog entry, if that's what we're reading, is already in buffer.
        read = fsize; // 0 if no entry, else size of entry in buffer.
      }

      if (FR_OK == res) {
        for (i = 0; i < read; i++) {
          rc = transmit_byte(buffer[i]);
        }
      }
      else
      {
        rc = FR_RW_ERROR;
      }

      fsize -= read;
    }

    switch (res) {
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

  } else {
    transmit_word(0);      // null file
    rc = HEXSTAT_NOT_FOUND;
    if ( !fs_initialized ) {
      rc = HEXSTAT_DEVICE_ERR;
    }
  }
  transmit_byte( rc ); // status byte transmit
  hex_finish();
  return HEXERR_SUCCESS;
}


/*
   hex_drv_open() -
   open a file for read or write on the SD card.
*/
static uint8_t hex_drv_open(pab_t pab) {
  uint16_t len = 0;
  uint8_t att = 0;
  uint8_t rc;
  BYTE    mode = 0;
  uint16_t fsize = 0;
  file_t* file = NULL;
  BYTE res;

  uart_putc('>');

  len = 0;

  memset(buffer, 0, sizeof(buffer));

  if ( hex_get_data(buffer, pab.datalen) == HEXSTAT_SUCCESS ) {
    len = buffer[ 0 ] + ( buffer[ 1 ] << 8 );
    att = buffer[ 2 ];
  } else {
    hex_release_bus();
    return HEXERR_BAV; // BAV ERR.
  }

#ifdef ARDUINO

  // for now, until we get rid of SD library in Arduino build.
  switch (att & OPENMODE_UPDATE) {
    case 0x00:  // append mode
      mode = FILE_WRITE;
      break;

    case OPENMODE_WRITE: // write, truncate if present. Maybe...
      mode = FILE_WRITE_NEW; // our own, does not include O_APPEND attribute.
      break;

    case OPENMODE_WRITE | OPENMODE_READ:
      mode = FILE_WRITE; // In SD lib, FILE_WRITE includes the O_READ attribute.
      break;
    default: //OPENMODE_READ
      mode = FILE_READ;
      break;
  }

#else
  // map attributes to FatFS file access mode
  switch (att & OPENMODE_UPDATE) {
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

#endif

  if ( !buffer[ 3 ] ) {
    rc = HEXSTAT_OPTION_ERR; // no name?
  } else {
    if ( !fs_initialized ) {
      file = NULL;
    } else {
      file = reserve_lun(pab.lun);
    }
    if (file != NULL) {

      if ( pab.datalen < BUFSIZE - 1 ) {

        if ( ( att & OPENMODE_READ ) == OPENMODE_READ ) {
          if ( buffer[ pab.datalen - 1 ] == '$' ) {
            // Are we attempting to open a catalog?
            buffer[ pab.datalen - 1 ] = '/';
            file->attr |= FILEATTR_CATALOG;
          }
        }

#ifdef ARDUINO
        /*
          if ( ( att & (OPENMODE_READ | OPENMODE_WRITE) ) == OPENMODE_WRITE ) {
          // For now, open for write only, remove pre-existing file.
          if ( SD.exists( (const char *)&buffer[3] ) ) {
            SD.remove( (const char *)&buffer[3] );
          }
          }
        */
        res = FR_OK; // presume success.
        // Now, open our file in proper mode. create it if we need to.
        file->fp = SD.open( (const char *)&buffer[3], mode );

        if ( !(file->attr & FILEATTR_CATALOG ) ) {

          // Any mode other than create new file?
          if ( mode != FILE_WRITE_NEW ) {
            if ( !SD.exists( (const char *)&buffer[3] )) {
              res = FR_DENIED;
            }
          }

          if ( (att & OPENMODE_UPDATE) == OPENMODE_APPEND ) {
            file->fp.seek( file->fp.size() ); // position for append.
          }
        }
#else
        // TODO: manage open of a directory if file->attr == FILEATTR_CATALOG
        if ( pab.datalen < BUFSIZE - 1 ) {
          res = f_open(&fs, &(file->fp), (UCHAR *)&buffer[3], mode);
        }
#endif

        // common.
      } else {
        res = FR_INVALID_NAME; // if the incoming buffer is full, we can't stuff the null.
      }

      switch (res) {
        case FR_OK:
          rc = HEXSTAT_SUCCESS;
          fsize = 0;
          if ( !(file->attr & FILEATTR_CATALOG) ) {
#ifdef ARDUINO
            fsize = file->fp.size();
#else
            fsize = file->fp.fsize;
#endif
          }
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

  if (!hex_is_bav()) { // we can send response
    if ( rc == HEXERR_SUCCESS ) {
      switch (att & OPENMODE_UPDATE) {

        // when opening to write, or read/write
        default:

          if (!(( att & OPENMODE_INTERNAL) || (pab.lun != 0))) { // if NOT open mode = INTERNAL, let's add CR/LF to end of line (display form).
            file->attr |= FILEATTR_DISPLAY;
          }
          // if we don't know how big its going to be... we may need multiple writes.
          if ( len == 0 ) {
            fsize = sizeof(buffer);
          } else {
            // otherwise, we know. and do NOT allow fileattr display under any circumstance.
            fsize = len;
            file->attr &= ~FILEATTR_DISPLAY;
          }
          break;

        // when opening to read-only
        case OPENMODE_READ:
          // open read-only w LUN=0: just return size of file we're reading; always. this is for verify, etc.
          if (pab.lun != 0 ) {
            if (len) {
              fsize = len; // non zero length requested, use it.
            } else {
              fsize = sizeof(buffer);  // on zero length request, return buffer size we use.
            }
          }
          // for len=0 OR lun=0, return fsize.
          break;
      }

      if ( rc == HEXSTAT_SUCCESS ) {
        transmit_word( 4 );
        transmit_word( fsize );
        transmit_word( 0 );      // position
        transmit_byte( HEXERR_SUCCESS );
        hex_finish();
      } else {
        hex_send_final_response( rc );
      }
    } else {
      hex_send_final_response( rc );
    }
    return HEXERR_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}


/*
   hex_drv_close() -
   close the file associated with the LUN in the PAB.
   If the file is open, it is closed and data is sync'd.
   If the file is not open, appropriate status is returned

*/
static uint8_t hex_drv_close(pab_t pab) {
  uint8_t rc;
  file_t* file = NULL;
  BYTE res = 0;

  uart_putc('<');

  file = find_lun(pab.lun);
  if (file != NULL) {
#ifdef ARDUINO
    file->fp.close();
    res = FR_OK;
#else
    res = f_close(&(file->fp));
#endif
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
    return HEXERR_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}

/*
   hex_drv_restore() -
   reset file to beginning
   valid for update, input mode open files.
*/
static uint8_t hex_drv_restore( pab_t pab ) {
  uint8_t  rc = HEXSTAT_SUCCESS;
  file_t*  file = NULL;
  BYTE     res = 0;

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
#ifdef ARDUINO
      // If we are restore on an open directory...rewind to start
      if ( file->attr & FILEATTR_CATALOG ) {
        file->fp.rewindDirectory();
      } else {
        // if we are a normal file, rewind to starting position.
        file->fp.seek( 0 ); // restore back to start of file.
      }
#else
      rc = HEXSTAT_UNSUPP_CMD;
#endif
    }
    hex_send_final_response( rc );
  }
  hex_finish();
  return HEXERR_BAV;
}

/*
   hex_drv_delete() -
   delete a file from the SD card.
*/
static uint8_t hex_drv_delete(pab_t pab) {
  uint8_t rc = HEXSTAT_SUCCESS;
#ifndef ARDUINO
  FRESULT fr;
#endif

  uart_putc('>');

  memset(buffer, 0, sizeof(buffer));

  if ( hex_get_data(buffer, pab.datalen) == HEXSTAT_SUCCESS ) {
  } else {
    hex_release_bus();
    return HEXERR_BAV; // BAV ERR.
  }
  // simplistic removal. doesn't check for much besides
  // existance at this point.  We should be able to know if
  // the file is open or not, and test for that; also should
  // test if it is really a file, or if it is a directory.
  // But for now; this'll do.

  if ( rc == HEXSTAT_SUCCESS
#ifdef ARDUINO
       && fs_initialized
#endif
     ) {
    // If we did not fill buffer, we have a null at end due to memset before retrieval.
    if ( pab.datalen < BUFSIZE - 1 ) {
#ifdef ARDUINO
      if ( SD.exists( (const char *)buffer )) {
        if ( SD.remove( (const char *)buffer )) {
          rc = HEXSTAT_SUCCESS;
          if ( SD.exists((const char*)buffer )) {
            rc = HEXSTAT_WP_ERR;
          }
        } else {
          rc = HEXSTAT_DEVICE_ERR;
        }
      } else {
        rc = HEXSTAT_NOT_FOUND;
      }
      // Now, if we have no open files, go ahead and shut down SD
      // We shut down SD when nothing is open so we can detect a card change if there
      // is no CD logic present.  Once we have known CD logic in place, we don't need
      // to close SD down, unless we get a CD change (i.e.removal of card or insertion).
      if ( !open_files ) {
#ifdef INCLUDE_CLOCK
        SdFile::dateTimeCallbackCancel();
#endif
        SD.end();
        fs_initialized = FALSE;
      }
#else
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
#endif
    } else {
      rc = HEXSTAT_FILE_NAME_INVALID;
    }
  }
  if (!hex_is_bav()) { // we can send response
    hex_send_final_response( rc );
    return HEXERR_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}

/*
    hex_drv_status() -
    initial simplistic implementation
*/
static uint8_t hex_drv_status( pab_t pab ) {
  uint8_t rc = HEXSTAT_SUCCESS;
  uint8_t st = FILE_REQ_STATUS_NONE;
  file_t* file = NULL;

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
#ifdef ARDUINO
      if ( !( file->attr & FILEATTR_CATALOG )) {
        if ( file->fp.position() == file->fp.size() ) {
          st |= FILE_EOF_REACHED;
        }
      }
#else
      // TODO: FatFS EOF check on file
      // We also need to deal properly here and in SD with the CATALOG being open.
#endif
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
  return HEXERR_BAV;
}


static uint8_t hex_drv_reset( __attribute__((unused)) pab_t pab) {

  drv_reset();
  // release the bus ignoring any further action on bus. no response sent.
  hex_finish();
  // wait here while bav is low
  while ( !hex_is_bav() ) {
    ;
  }
  return HEXERR_SUCCESS;
}


/*
   drv_start - open filesystem.
   make- ignore/ empty function.
*/
void drv_start(void) {
  
  if (!fs_initialized) {
    
#ifdef ARDUINO
    // If SD library not initialized, initialize it now
    // and mark it as such.
    if ( SD.begin( chipSelect ) ) {

#ifdef INCLUDE_CLOCK
      SdFile::dateTimeCallback(dateTime);
#endif
      fs_initialized = TRUE;
    }

#else

    if (f_mount(1, &fs) == FR_OK) {

      fs_initialized = TRUE;
    }

#endif

  }
  return;
}


/*
   Command handling registry for device
*/
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


void drv_register(registry_t *registry)
{
  uint8_t i = registry->num_devices;

  registry->num_devices++;
  registry->entry[ i ].device_code_start = DRV_DEV;
  registry->entry[ i ].device_code_end = MAX_DRV; // support 100-109 for disks
  registry->entry[ i ].operation = (cmd_proc *)&fn_table;
  registry->entry[ i ].command = (uint8_t *)&op_table;
  return;
}


void drv_reset( void )
{
  file_t* file = NULL;
  uint8_t lun;

  uart_putcrlf();
  uart_putc('R');

  if ( open_files ) {
    // find file(s) that are open, get file pointer and lun number
    while ( (file = find_file_in_use(&lun) ) != NULL ) {
      // if we found a file open, silently close it, and free its lun.
      if ( fs_initialized ) {
#ifdef ARDUINO
        file->fp.close();  // close and sync file.
#else
        f_close(&(file->fp));  // close and sync file.
#endif
      }
      free_lun(lun);
      // continue until we find no additional files open.
    }
  }
  return;
}


void drv_init(void) {
  uint8_t i;

  for (i = 0; i < MAX_OPEN_FILES; i++) {
    files[i].used = FALSE;
  }
  open_files = 0;
  fs_initialized = FALSE;
  return;
}
