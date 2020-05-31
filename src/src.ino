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

    main.c: Main application /or  hexbus.ino - used for building with arduino (same file).
*/

#include <stddef.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include "config.h"

#include "hexbus.h"
#include "led.h"
#include "timer.h"

#ifdef BUILD_USING_ARDUINO

#include <SPI.h>
#include <SD.h>

const uint8_t chipSelect = 10;
volatile uint8_t sd_initialized = 0;

uint8_t buffer[256];

#else

#include "diskio.h"
#include "ff.h"
#include "uart.h"

FATFS fs;
uint8_t buffer[40];

#endif

/*
    PAB (Peripheral Access Block) data structure
*/
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

#ifndef BUILD_USING_ARDUINO

typedef struct _file_t {
  FIL fp;
  uint8_t attr;
} file_t;

#else

typedef struct _file_t {
  File fp;
  uint8_t attr;
} file_t;

uint8_t printer_open = 0;

#endif // build-using-arduino

typedef struct _luntbl_t {
  uint8_t used;
  uint8_t lun;
  file_t  file;
} luntbl_t;


uint8_t open_files = 0;
luntbl_t files[MAX_OPEN_FILES]; // file number to file mapping


#ifndef BUILD_USING_ARDUINO
#include "crc7asm.s"

#include "sdcard.c"
#include "diskio.c"
#include "ff.c"
#include "spi.c"
#include "uart.c"

#endif // build-using-arduino

#include "timer.c"

#include "hexbus.c"
#include "led.c"

#ifdef BUILD_USING_ARDUINO

unsigned long t_time;

void timer_init(void) {
  t_time = millis() + 10;
  ticks = 0;
}

void timer_check(uint8_t flag) {
  unsigned long t = millis();
  if ( t > t_time || flag ) {
    t_time = t;
    ticks++;
    if (led_state & LED_ERROR) {
      if ((ticks & 15) == 0) {
        toggle_led();  // blink LED as error, 1 flash every 150 ms or so.
      }
    } else {
      set_led(led_state & LED_BUSY);
    }
  }
}

#endif


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
#ifndef BUILD_USING_ARDUINO
  uart_putc('n');
#endif
  return NULL;
}

static file_t* reserve_lun(uint8_t lun) {
  uint8_t i;

  for (i = 0; i < MAX_OPEN_FILES; i++) {
    if (!files[i].used) {
      files[i].used = TRUE;
      files[i].lun = lun;
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
#ifdef BUILD_USING_ARDUINO
      if ( !open_files ) {
        SD.end();
        sd_initialized = 0;
      }
#endif
    }
  }
}

static void init_files(void) {
  uint8_t i;

  for (i = 0; i < MAX_OPEN_FILES; i++) {
    files[i].used = FALSE;
  }
}

static uint8_t hex_getdata(uint8_t buf[256], uint16_t len) {
  uint8_t rc = HEXERR_SUCCESS;
  uint16_t i = 0;
  int inlen = len;

  while (i < len && rc == HEXERR_SUCCESS ) {
    buf[ i ] = 1;
    rc = receive_byte( &buf[ i++ ] );
#ifdef BUILD_USING_ARDUINO
    timer_check(0);
#endif
  }

#ifndef BUILD_USING_ARDUINO
  if (len > 0) {
    uart_putc(13);
    uart_putc(10);
    uart_trace(buf, 0, len);
  }
#endif

  return HEXERR_SUCCESS;
}

/*
   hex_eat_it() -
   Eat any remaining data that is incoming, and send a 0000 length
   response message, along with the provided status.
   Returns either HEXERR_BAV for BAV loss, or SUCCESS.
*/
static uint8_t hex_eat_it(uint16_t length, uint8_t status )
{
  uint16_t i = 0;
  uint8_t  data;

  while ( i < length ) {
    data = 1;
    if ( receive_byte( &data ) != HEXERR_SUCCESS ) {
      hex_release_bus();
      return HEXERR_BAV;
    }
    i++;
  }
  // safe to turn around now.  As long as BAV is still
  // low, then go ahead and send a response.
  if (!hex_is_bav()) { // we can send response
    hex_send_final_response( status );
    return HEXERR_SUCCESS;
  }
  return HEXERR_BAV;
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

static int8_t hex_verify(pab_t pab) {
  uint16_t len_prog_mem = 0;
  uint16_t len_prog_stored = 0;
  uint8_t  *data = &buffer[ sizeof(buffer) / 2 ]; // split our buffer in half
  // so we do not use all of our limited amount of RAM on buffers...
  UINT     read;
  uint16_t len;
  file_t*  file;
  BYTE     res = FR_OK;
  uint8_t  i;
  uint8_t  first_buffer = 1;

#ifndef BUILD_USING_ARDUINO
  uart_putc('>');
#endif

  file = find_lun(pab.lun);
  len = pab.datalen;   // this is the size of the object to verify

  res = (file != NULL ? FR_OK : FR_NO_FILE);

  while (len && res == FR_OK) {
    i = (len >= sizeof(buffer) / 2 ? sizeof(buffer) / 2 : len);
    if ( hex_getdata(buffer, i) ) { // use front half of buffer for incoming data from the host.
      return HEXERR_BAV;
    }

    if (res == FR_OK) {

      // length of program in memory
      if (first_buffer) {
        len_prog_mem = buffer[2] | ( buffer[3] << 8 );
      }

      i = len_prog_mem - len; // remaing amount to read from file
      // while it fit into buffer or not?  Only read as much
      // as we can hold in our buffer.
      i = ( i > sizeof( buffer ) / 2 ) ? sizeof( buffer ) / 2 : i;

#ifdef BUILD_USING_ARDUINO

      read = file->fp.read( (char *)data, i );
      timer_check(0);
      if ( read ) {
        res = FR_OK;
      } else {
        res = FR_RW_ERROR;
      }

#else

      res = f_read(&(file->fp), data, i, &read);

      if (res == FR_OK) {
        uart_putc(13);
        uart_putc(10);
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
    if (first_buffer) {
      first_buffer = 0;
    }
    len -= read;
  }
  // If we haven't read the entire incoming message yet, flush it.
  if ( len ) {
#ifdef BUILD_USING_ARDUINO
    if ( !sd_initialized ) {
      res = HEXSTAT_DEVICE_ERR;
    }
#endif
    hex_eat_it( len, res ); // reports status back.
    return HEXSTAT_SUCCESS;
  } else {
#ifndef BUILD_USING_ARDUINO
    uart_putc('>');
#endif
    if (!hex_is_bav()) { // we can send response
      hex_send_final_response( res );
      res = HEXSTAT_SUCCESS;
    } else {
      hex_finish();
      res = HEXERR_BAV;
    }
  }
  return res;
}

/*
   hex_write() -
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
static int8_t hex_write(pab_t pab) {
  uint8_t rc = HEXERR_SUCCESS;
  uint16_t len;
  uint16_t i;
  UINT written;
  file_t* file = NULL;
  BYTE res = FR_OK;

#ifndef BUILD_USING_ARDUINO
  uart_putc('>');
#endif

  file = find_lun(pab.lun);
  len = pab.datalen;
  res = (file != NULL ? FR_OK : FR_NO_FILE);

  while (len && rc == HEXERR_SUCCESS && res == FR_OK ) {
    i = (len >= sizeof(buffer) ? sizeof(buffer) : len);
    rc = hex_getdata(buffer, i);

    if (file != NULL && res == FR_OK && rc == HEXSTAT_SUCCESS) {

#ifdef BUILD_USING_ARDUINO
      written = (file->fp).write( buffer, i );
      if ( written != i ) {
        res = FR_DENIED;
      }
      timer_check(0);
#else
      res = f_write(&(file->fp), buffer, i, &written);
#endif

    }
    len -= i;
  }

  if ( len ) {
#ifdef BUILD_USING_ARDUINO
    if ( !sd_initialized ) {
      res = HEXSTAT_DEVICE_ERR;
    }
#endif
    hex_eat_it( len, res );
    return HEXSTAT_SUCCESS;
  }

  if (file != NULL && (file->attr & FILEATTR_DISPLAY)) { // add CRLF to data
    buffer[0] = 13;
    buffer[1] = 10;
#ifdef BUILD_USING_ARDUINO
    written = (file->fp).write( buffer, 2 );
    timer_check(0);
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

#ifndef BUILD_USING_ARDUINO
  uart_putc('>');
#endif

  if (!hex_is_bav() ) { // we can send response
    hex_send_final_response( rc );
  } else {
    hex_finish();
  }
  return rc;
}

/*
   hex_read() -
   read data from currently open file associated with the LUN
   in the PAB.
*/
static uint8_t hex_read(pab_t pab) {
  uint8_t rc;
  uint8_t i;
  uint16_t len = 0;
  uint16_t fsize;
  UINT read;
  BYTE res = FR_OK;
  file_t* file;

#ifndef BUILD_USING_ARDUINO
  uart_putc('<');
#endif

  file = find_lun(pab.lun);

  if (file != NULL) {

#ifdef BUILD_USING_ARDUINO
    fsize = (uint16_t)file->fp.size();
#else
    fsize = file->fp.fsize;
#endif

    rc = transmit_word( fsize );

    while (len < fsize && rc == HEXERR_SUCCESS ) {

#ifdef BUILD_USING_ARDUINO

      i = fsize - len; // remaing amount to read from file
      // while it fit into buffer or not?  Only read as much
      // as we can hold in our buffer.
      i = ( i > sizeof( buffer ) ) ? sizeof( buffer ) : i;

      read = file->fp.read( (char *)buffer, i );
      timer_check(0);
      if ( read ) {
        res = FR_OK;
      } else {
        res = FR_RW_ERROR;
      }

#else

      res = f_read(&(file->fp), buffer, sizeof(buffer), &read);

      if (!res) {
        uart_putc(13);
        uart_putc(10);
        uart_trace(buffer, 0, read);
      }

#endif

      if (FR_OK == res) {
        for (i = 0; i < read; i++) {
          rc = transmit_byte(buffer[i]);
        }
        len += read;
      }
      else
      {
        rc = FR_RW_ERROR;
      }
    }

    switch (res) {
      case FR_OK:
        rc = HEXSTAT_SUCCESS;
        break;
      default:
        rc = HEXSTAT_DEVICE_ERR;
        break;
    }

#ifndef BUILD_USING_ARDUINO
    uart_putc('>');
#endif

  } else {
    transmit_word(0);      // null file
    rc = HEXSTAT_NOT_OPEN;
#ifdef BUILD_USING_ARDUINO
    if ( !sd_initialized ) {
      rc = HEXSTAT_DEVICE_ERR;
    }
#endif
  }
  if ( rc != HEXERR_BAV ) {
    transmit_byte( rc );
  }
  hex_finish();
  return 0;
}

/*
   hex_open() -
   open a file for read or write on the SD card.
*/
static uint8_t hex_open(pab_t pab) {
  uint16_t i = 0;
  uint8_t data;
  uint16_t len = 0;
  uint16_t index = 0;
  uint8_t att = 0;
  uint8_t rc;
  BYTE    mode = 0;
  uint16_t fsize = 0;
  file_t* file;
  BYTE res;

#ifndef BUILD_USING_ARDUINO
  uart_putc('>');
#endif

  len = 0;

  while (i < pab.datalen) {
    data = 1; // tells receive byte to release HSK from previous receipt
    rc = receive_byte( &data );

    if ( rc == HEXERR_SUCCESS ) {
      switch (i) {
        case 0:
          len = data;
          break;
        case 1:
          len |= data << 8; // length of buffer to use.
          break;
        case 2:
          att = data;
          break;
        default:
          buffer[ index++ ] = data; // grab additional data into buffer here.
          break;
      }
    } else {
      hex_release_bus(); // float the bus
      return rc; // we'll return with a BAV ERR
    }
    i++;
  }

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
  // ensure potential filename is null terminated
  buffer[ index ] = 0;

  // Beware.  If len actually does == size of buffer, the buffer below
  // in the f_open will NOT have a null-terminator as part of the name.
  // also, if we have NO name, report an error.  Have to have a name to
  // open something.
  if ( index == 0 ) {
    rc = HEXSTAT_OPTION_ERR;
  } else {
#ifdef BUILD_USING_ARDUINO
    if ( !sd_initialized ) {
      file = NULL;
    } else
#endif
    {
      file = reserve_lun(pab.lun);
    }
    if (file != NULL) {
#ifdef BUILD_USING_ARDUINO
      timer_check(0);
      file->fp = SD.open( (const char *)buffer, mode );
      if ( SD.exists( (const char *)buffer )) {
        res = FR_OK;
      } else {
        res = FR_DENIED;
      }
#else
      res = f_open(&fs, &(file->fp), (UCHAR *)buffer, mode);
#endif

      switch (res) {
        case FR_OK:
          rc = HEXSTAT_SUCCESS;
#ifdef BUILD_USING_ARDUINO
          fsize = file->fp.size();
#else
          fsize = file->fp.fsize;
#endif
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
#ifdef BUILD_USING_ARDUINO
      if ( !sd_initialized ) {
        rc = HEXSTAT_DEVICE_ERR;
      }
#endif
    }
  }

  if (!hex_is_bav()) { // we can send response
    if ( rc == HEXERR_SUCCESS ) {
      switch (att & (OPENMODE_WRITE | OPENMODE_READ)) {
        default:
          if (!len) // len = 0 means list "<device>.<filename>" or open #<> for writing, thus we should add CRLF to each line.
            file->attr |= FILEATTR_DISPLAY;
          fsize = len ? len : sizeof(buffer);
        // fall to break
        case OPENMODE_READ:
          break;
      }
      hex_send_size_response( fsize );
      return HEXERR_SUCCESS;
    } else {
      hex_send_final_response( rc );
      return HEXERR_SUCCESS;
    }
  }
  hex_finish();
  return HEXERR_BAV;
}

/*
   hex_close() -
   close the file associated with the LUN in the PAB.
   If the file is open, it is closed and data is sync'd.
   If the file is not open, appropriate status is returned

*/
static uint8_t hex_close(pab_t pab) {
  uint8_t rc;
  file_t* file = NULL;
  BYTE res = 0;

#ifndef BUILD_USING_ARDUINO
  uart_putc('<');
#endif

  file = find_lun(pab.lun);
  if (file != NULL) {
#ifdef BUILD_USING_ARDUINO
    file->fp.close();
    res = FR_OK;
    timer_check(0);
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
#ifdef BUILD_USING_ARDUINO
    if ( !sd_initialized ) {
      rc = HEXSTAT_DEVICE_ERR;
    }
#endif
  }

  if ( !hex_is_bav() ) {
    hex_send_final_response( rc );
    return HEXERR_SUCCESS;
  }
  return HEXERR_BAV;
}


#ifdef BUILD_USING_ARDUINO
/*
   hex_delete() -
   delete a file from the SD card.
*/
static uint8_t hex_delete(pab_t pab) {
  uint16_t i = 0;
  uint8_t data;
  uint8_t rc = HEXERR_SUCCESS;
  uint8_t sd_was_not_inited = 0;
  BYTE res;

  while (i < pab.datalen) {
    data = 1; // tells receive byte to release HSK from previous receipt
    rc = receive_byte( &data );
    if ( rc == HEXERR_SUCCESS ) {
      buffer[ i++ ] = data; // grab additional data into buffer here.
    } else {
      hex_release_bus(); // float the bus
      return rc; // we'll return with a BAV ERR
    }
  }

  // ensure potential filename is null terminated
  buffer[ i ] = 0;
  // simplistic removal. doesn't check for much besides
  // existance at this point.  We should be able to know if
  // the file is open or not, and test for that; also should
  // test if it is really a file, or if it is a directory.
  // But for now; this'll do.
  if ( !sd_initialized ) {
    if ( SD.begin(chipSelect) ) {
      sd_initialized = 1;
      sd_was_not_inited = 1;
    } else {
      rc = HEXSTAT_DEVICE_ERR;
    }
  }
  if ( rc == HEXERR_SUCCESS && sd_initialized ) {
    if ( SD.exists( (const char *)buffer )) {
      if ( SD.remove( (const char *)buffer )) {
        rc = HEXSTAT_SUCCESS;
      } else {
        rc = HEXSTAT_WP_ERR;
      }
    } else {
      rc = HEXSTAT_NOT_FOUND;
    }
    if ( sd_was_not_inited ) {
      SD.end();
      sd_initialized = 0;
    }
  }

  if (!hex_is_bav()) { // we can send response
    hex_send_final_response( rc );
    return HEXERR_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}

#endif

static uint8_t hex_format(pab_t pab) {
  uint8_t rc = HEXERR_BAV;

#ifndef BUILD_USING_ARDUINO
  uart_putc('>');
#endif

  // get options, if any
  if (!hex_getdata(buffer, pab.datalen)) {

    // if we were going to do something, here's where we'd do it.
    // however, since we do NOT actually support formatting SD cards
    // with either the Arduino SD library, or the FatFS for non-Arduino builds
    // at this time, the proper response is to return 0000 as the response length
    // and a status of unsupported command, regardless of any options that may
    // have been sent to us.
    if (!hex_is_bav()) { // we can send response
      hex_send_final_response(HEXSTAT_UNSUPP_CMD);
      rc = HEXERR_SUCCESS;
    }
  }
  return rc;
}



#ifdef BUILD_USING_ARDUINO
/*
   hex_printer_open() -
   "opens" the Serial.object for use as a printer at device code 12 (default PC-324 printer).
*/
static uint8_t hex_printer_open(pab_t pab) {
  uint16_t i   = 0;
  uint16_t len = 0;
  uint8_t  data;
  uint8_t  rc;
  uint8_t  att;
  BYTE     res = HEXERR_SUCCESS;

  len = 0;

  while (i < pab.datalen) {
    data = 1; // tells receive byte to release HSK from previous receipt
    rc = receive_byte( &data );
    if ( rc == HEXERR_SUCCESS ) {
      switch (i) {
        case 0:
          len = data;
          break;
        case 1:
          len |= data << 8; // length of buffer to use.
          break;
        case 2:
          att = data;
          break;
        default:
          res = HEXSTAT_TOO_LONG; // we're going to report an error.  too much data sent.
          break;
      }
    } else {
      hex_release_bus(); // float the bus
      return rc; // we'll return with a BAV ERR
    }
    i++;
  }
  if ( ( att != OPENMODE_WRITE ) || ( len > sizeof(buffer) ) ) {
    rc = HEXSTAT_OPTION_ERR;
  }
  if ( res != HEXERR_SUCCESS ) {
    rc = res;
  }
  if (!hex_is_bav()) { // we can send response
    if ( rc == HEXERR_SUCCESS )
    {
      printer_open = 1;  // our printer is NOW officially open.
      len = len ? len : sizeof(buffer);
      hex_send_size_response( len );
      hex_finish();
      return HEXERR_SUCCESS;
    }
    else
    {
      hex_send_final_response( rc );
      return HEXERR_SUCCESS;
    }
  }
  hex_finish();
  return HEXERR_BAV;
}


/*
   hex_printer_close() -
   closes printer at device 12 for use from host.
*/
static uint8_t hex_printer_close(pab_t pab) {
  uint8_t rc = HEXSTAT_SUCCESS;
  BYTE    res = 0;

  if ( !printer_open ) {
    rc = HEXSTAT_NOT_OPEN;
  }
  printer_open = 0;      // mark printer closed regardless.
  if ( !hex_is_bav() ) {
    // send 0000 response with appropriate status code.
    hex_send_final_response( rc );
    return HEXERR_SUCCESS;
  }
  return HEXERR_BAV;
}

/*
    hex_printer_write() -
    write data to serial port when printer is open.
*/
static int8_t hex_printer_write(pab_t pab) {
  uint16_t len;
  uint16_t i;
  UINT     written = 0;
  uint8_t  rc = HEXERR_SUCCESS;

  len = pab.datalen;

  while ( len && rc == HEXERR_SUCCESS ) {
    i = (len >= sizeof(buffer) ? sizeof(buffer) : len);
    rc = hex_getdata(buffer, i);
    timer_check(0);
    /*
        printer open? print a buffer of data.  We hold off
        on continuation until we've actually got the data
        flushed from the serial buffers before we continue.
        Digital logic analyzer shows some "glitchy" behavior
        on our HSK signal that may be due (somehow) to serial
        port use.  Rather than take the chance, ensure that
        serial data is flushed before proceeding to make sure
        that HexBus operations are not compromised.
    */
    if ( rc == HEXSTAT_SUCCESS && printer_open ) {
      Serial.write(buffer, i);
      delayMicroseconds( i * 72 );
      /* use 72 us per character sent delay.
         digital logic analyzer confirms that @ 115200 baud, the data is
         flushed over the wire BEFORE we continue HexBus operations.
      */
      written = 1; // indicate we actually wrote some data
    }

    len -= i;
  }

  /* if we've written data and our printer is open, finish the line out with
      a CR/LF.
  */
  if ( written && printer_open ) {
    buffer[0] = 13;
    buffer[1] = 10;
    Serial.write(buffer, 2);
    delayMicroseconds(176);
  }
  /*
     if printer is NOT open, report such status back
  */
  if ( !printer_open ) {
    rc = HEXSTAT_NOT_OPEN;   // if printer is NOT open, report such status
  }

  /*
     send response and finish operation
  */
  if (!hex_is_bav() ) {
    hex_send_final_response( rc );
    rc = HEXSTAT_SUCCESS;
  } else {
    hex_finish();
    rc = HEXERR_BAV;
  }
  return rc;
}

#endif // build-using-arduino


/*
   hex_reset_bus() -
   This command is normally used with device code zero, and must actually
   do something if files are open (or printer is open).
   Close any open files to ensure they are sync'd properly.  Close the printer if
   open, and ensure our file lun tables are reset.
   There is NO response to this command.
*/
static uint8_t hex_reset_bus(pab_t pab) {
  file_t* file = NULL;
  uint8_t lun;
  BYTE    res;

#ifndef BUILD_USING_ARDUINO
  uart_putc(13);
  uart_putc(10);
  uart_putc('R');
#endif

  if ( open_files ) {
    // find file(s) that are open, get file pointer and lun number
    while ( (file = find_file_in_use(&lun) ) != NULL ) {
      // if we found a file open, silently close it, and free its lun.
#ifdef BUILD_USING_ARDUINO
      timer_check(0);
      if ( sd_initialized ) {
        file->fp.close();  // close and sync file.
      }
#else
      f_close(&(file->fp));  // close and sync file.
#endif
      free_lun(lun);
      // continue until we find no additional files open.
    }
  }

#ifdef BUILD_USING_ARDUINO
  printer_open = 0;  // make sure our printer is closed.
#endif

  // release bus ignoring any further action on bus. no response sent.
  hex_finish();
  // wait here while bav is low
  while ( !hex_is_bav() ) {
#ifdef BUILD_USING_ARDUINO
    timer_check(0);
#else
    ;
#endif
  }

  return HEXSTAT_SUCCESS;
}



/*
   setup() - In Arduino, this will be run once automatically.
   Building non-Arduino, we'll call it once at the beginning
   of the main() function.
*/
void setup(void) {

  BYTE res;

  board_init();
  hex_init();

#ifndef BUILD_USING_ARDUINO
  uart_init();
  disk_init();
#endif

  leds_init();
  timer_init();
  device_hw_address_init();
  init_files();

  sei();

#ifdef BUILD_USING_ARDUINO
  printer_open = 0;
  sd_initialized = 0;

  Serial.begin(115200);
  // Ensure serial initialized before proceeding.
  while (!Serial) {
    ;
  }

#endif

}


#ifndef BUILD_USING_ARDUINO
int main(void) __attribute__((OS_main));
int main(void) {
#else
void loop(void) { // Arduino main loop routine.
#endif

  uint8_t i = 0;
  int16_t data;
  uint8_t ignore_cmd = FALSE;
  pab_raw_t pabdata;
  BYTE res;

#ifndef BUILD_USING_ARDUINO
  setup();  // non-arduino- lets set up our environment.

  res = f_mount(1, &fs);

#endif

  pabdata.pab.cmd = 0;
  pabdata.pab.lun = 0;
  pabdata.pab.record = 0;
  pabdata.pab.buflen = 0;
  pabdata.pab.datalen = 0;

#ifndef BUILD_USING_ARDUINO
  uart_puts_P(PSTR("Device ID: 0x"));
  uart_puthex(device_hw_address());
  uart_putcrlf();
#endif

  while (TRUE) {

    hex_hsk_hi();
    hex_release_data();
#ifdef BUILD_USING_ARDUINO
    set_busy_led( FALSE );
    timer_check(1);
#endif
    while (hex_is_bav()) {
#ifdef BUILD_USING_ARDUINO
      timer_check(0);
#endif
    }

#ifndef BUILD_USING_ARDUINO
    uart_putc('^');
#else
    set_busy_led( TRUE );
    timer_check(1);
#endif

    while (!hex_is_bav()) {
      while ( i < 9 ) {
        pabdata.raw[ i ] = i;
        res = receive_byte( &pabdata.raw[ i ] );
        if ( res == HEXERR_SUCCESS ) {
          i++;
        } else {
          ignore_cmd = TRUE;
          i = 9;
          hex_release_bus();
        }
      }

      if ( !ignore_cmd ) {
        if ( !( ( pabdata.pab.dev == 0 ) ||
                ( pabdata.pab.dev == device_hw_address() )
#ifdef BUILD_USING_ARDUINO
                ||
                ( pabdata.pab.dev == PRINTER_DEV ) )
#endif
           )
        {
          ignore_cmd = TRUE;
        }
      }

      if ( !ignore_cmd ) {
        if (i == 9) {
          // exec command
#ifndef BUILD_USING_ARDUINO
          uart_putc(13);
          uart_putc(10);
#else
          timer_check(1);
          /*
             If we are attempting to use the SD card, we
             initialize it NOW.  If it fails (no card present)
             or other reasons, the various SD file usage commands
             will be failed with a device-error, simply by
             testing the sd_initialized flag as needed.
          */
          if ( pabdata.pab.dev == device_hw_address() ) {
            if ( sd_initialized == 0 ) {
              if ( SD.begin(chipSelect) ) {
                sd_initialized = 1;
              }
            }
          }
#endif

          if ( pabdata.pab.dev == 0 && pabdata.pab.cmd != HEXCMD_RESET_BUS ) {
            pabdata.pab.cmd = HEXCMD_NULL; // change out to NULL operation and let bus float.
          }

          switch (pabdata.pab.cmd) {

            case HEXCMD_OPEN:
#ifdef BUILD_USING_ARDUINO
              if ( pabdata.pab.dev == PRINTER_DEV ) {
                hex_printer_open(pabdata.pab);
              } else {
                hex_open(pabdata.pab);
              }
#else
              hex_open(pabdata.pab);
#endif
              break;

            case HEXCMD_CLOSE:
#ifdef BUILD_USING_ARDUINO
              if ( pabdata.pab.dev == PRINTER_DEV ) {
                hex_printer_close(pabdata.pab);
              } else {
                hex_close(pabdata.pab);
              }
#else
              hex_close(pabdata.pab);
#endif
              break;

            case HEXCMD_DELETE_OPEN:
#ifdef BUILD_USING_ARDUINO
              if ( pabdata.pab.dev != PRINTER_DEV ) {
                // eat the incoming message and report unsupported
                hex_eat_it(pabdata.pab.datalen, HEXSTAT_UNSUPP_CMD );
              }
#endif
              break;

            case HEXCMD_READ:
#ifdef BUILD_USING_ARDUINO
              if ( pabdata.pab.dev != PRINTER_DEV )
#endif
                hex_read(pabdata.pab);
              break;

            case HEXCMD_WRITE:
#ifdef BUILD_USING_ARDUINO
              if ( pabdata.pab.dev == PRINTER_DEV ) {
                hex_printer_write(pabdata.pab);
              } else {
                hex_write(pabdata.pab);
              }
#else
              hex_write(pabdata.pab);
#endif
              break;

#ifdef BUILD_USING_ARDUINO

            case HEXCMD_DELETE:
              if ( pabdata.pab.dev != PRINTER_DEV )
                hex_delete(pabdata.pab); // currently supported on SD only.
              break;

#endif


            case HEXCMD_VERIFY:
#ifdef BUILD_USING_ARDUINO
              if ( pabdata.pab.dev != PRINTER_DEV )
#endif
                hex_verify(pabdata.pab);
              break;

            case HEXCMD_NULL:
              hex_release_bus();
              while (!hex_is_bav() )  // wait for BAV back high, ignore any traffic
                ;
              break;

            case HEXCMD_RESET_BUS:
              // On a RESET, we should close any files that are OPEN, then simply
              // abort any further bus operation.  No response.
              hex_reset_bus(pabdata.pab);
              break;

            case HEXCMD_FORMAT:
#ifdef BUILD_USING_ARDUINO
              if ( pabdata.pab.dev != PRINTER_DEV )
#endif
                hex_format(pabdata.pab);
              break;

            case HEXCMD_SVC_REQ_POLL:
              hex_send_final_response( HEXSTAT_NOT_REQUEST );
              break;

            default:
#ifndef BUILD_USING_ARDUINO
              uart_putc(13);
              uart_putc(10);
              uart_putc('E');
#endif
              hex_release_bus();
              break;
          }
          ignore_cmd = TRUE;  // in case someone sends more data, ignore it.
        }
      } else {

#ifndef BUILD_USING_ARDUINO
        uart_putc('%');
        uart_puthex(data);
#endif
        i = 0;
        hex_release_bus();
        while (!hex_is_bav() )  // wait for BAV back high, ignore any traffic
          ;
        ignore_cmd = FALSE;
      }
    }
#ifndef BUILD_USING_ARDUINO
    uart_putc(13);
    uart_putc(10);
#endif
    i = 0;
    ignore_cmd = FALSE;
  }
}
