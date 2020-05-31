
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
#include <string.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <util/delay.h>
#include "config.h"
#include "hexbus.h"
#include "led.h"
#include "timer.h"
#include "ff.h"

#define FR_EOF     255    // We need an EOF error for hexbus_read.


#ifdef ARDUINO

#include <SPI.h>
#include <SD.h>


#ifdef INCLUDE_CLOCK

#include <DS3231.h>
#include <Wire.h>

DS3231            clock_peripheral;       // Our CLOCK : access via the HexBus at device code 233 (E9h = ascii R+T+C)
volatile uint8_t  clock_open = 0;

#endif


#ifdef INCLUDE_SERIAL
#include <SoftwareSerial.h>

SoftwareSerial    serial_peripheral( 8, 9 );  // either can support interrupts, and are otherwise available.
volatile uint8_t  serial_open = 0;

#endif


#ifdef INCLUDE_PRINTER
volatile uint8_t  printer_open = 0;
#endif



volatile uint8_t  sd_initialized = 0;
const uint8_t     chipSelect = 10;
uint8_t buffer[ BUFSIZE ];

#else

#include "diskio.h"

FATFS fs;
uint8_t buffer[BUFSIZE];

#endif

#include "uart.h"

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

typedef uint8_t (*cmd_proc)(pab_t pab);


#ifndef ARDUINO

typedef struct _file_t {
  FIL fp;
  uint8_t attr;
} file_t;

#else

typedef struct _file_t {
  File fp;
  uint8_t attr;
} file_t;




#endif // arduino

typedef struct _luntbl_t {
  uint8_t used;
  uint8_t lun;
  file_t  file;
} luntbl_t;

uint8_t open_files = 0;
luntbl_t files[MAX_OPEN_FILES]; // file number to file mapping


#ifdef INCLUDE_POWERMGMT
static void wakeUp(void)
{
  sleep_disable();
  power_all_enable();
  detachInterrupt(0);
}

// Power use reduction
static void sleep_the_system( void )
{
  // attach interrupt for wakeup to D2
  attachInterrupt(0, wakeUp, LOW );
  set_sleep_mode( SLEEP_MODE_STANDBY ); // cuts measured current use in about half or so...
  cli();
  sleep_enable();
  sleep_bod_disable();
  sei();
  sleep_cpu();
  // BAV low woke us up. Wait to see if we
  // get a HSK low, if so, drop our HSK and then proceed.
  // We do this here, because HSK must be held low after transmitter pulls it low
  // within a very short window of time (< 8us).
  hex_capture_hsk();
  return;
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
  uart_putc('n');
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
#ifdef ARDUINO
      if ( !open_files ) {
        SD.end();
        sd_initialized = 0;
      }
#endif
    }
  }
}

void init_files(void) {
  uint8_t i;

  for (i = 0; i < MAX_OPEN_FILES; i++) {
    files[i].used = FALSE;
  }
}

static uint8_t hex_getdata(uint8_t buf[256], uint16_t len) {
  uint8_t rc = HEXERR_SUCCESS;
  uint16_t i = 0;

  while (i < len && rc == HEXERR_SUCCESS ) {
    buf[ i ] = 1;
    rc = receive_byte( &buf[ i++ ] );
#ifdef ARDUINO
    timer_check(0);
#endif
  }

  if (len > 0) {
    uart_putc(13);
    uart_putc(10);
    uart_trace(buf, 0, len);
  }

  return HEXERR_SUCCESS;
}

/*
   hex_eat_it() -
   Eat any remaining data that is incoming, and send a 0000 length
   response message, along with the provided status.
   Returns either HEXERR_BAV for BAV loss, or SUCCESS.
*/
static void hex_eat_it(uint16_t length, uint8_t status )
{
  uint16_t i = 0;
  uint8_t  data;

  while ( i < length ) {
    data = 1;
    if ( receive_byte( &data ) != HEXERR_SUCCESS ) {
      hex_release_bus();
      return;
    }
    i++;
  }
  // safe to turn around now.  As long as BAV is still
  // low, then go ahead and send a response.
  if (!hex_is_bav()) { // we can send response
    hex_send_final_response( status );
  }
  return;
}


static uint8_t hex_receive_options( pab_t pab ) {
  uint16_t i = 0;
  uint8_t  data;

  while (i < pab.datalen) {
    data = 1;   // tells receive byte to release HSK from previous receipt
    if ( receive_byte( &data ) == HEXERR_SUCCESS ) {
      buffer[ i ] = data;
      i++;
    } else {
      return HEXERR_BAV;
    }
  }
  buffer[ i ] = 0;
  return HEXSTAT_SUCCESS;
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

static uint8_t hex_verify(pab_t pab) {
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

    if ( hex_getdata(buffer, i) ) { // use front half of buffer for incoming data from the host.
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

    first_buffer = 0;
    len -= read;
  }
  // If we haven't read the entire incoming message yet, flush it.
  if ( len ) {
#ifdef ARDUINO
    if ( !sd_initialized ) {
      res = HEXSTAT_DEVICE_ERR;
    }
#endif
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
static uint8_t hex_write(pab_t pab) {
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
    rc = hex_getdata(buffer, i);

    if (file != NULL && res == FR_OK && rc == HEXSTAT_SUCCESS) {

#ifdef ARDUINO
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
#ifdef ARDUINO
    if ( !sd_initialized ) {
      res = HEXSTAT_DEVICE_ERR;
    }
#endif
    hex_eat_it( len, res );
    return HEXERR_BAV;
  }

  if (file != NULL && (file->attr & FILEATTR_DISPLAY)) { // add CRLF to data
    buffer[0] = 13;
    buffer[1] = 10;
#ifdef ARDUINO
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

  uart_putc('>');

  if (!hex_is_bav() ) { // we can send response
    hex_send_final_response( rc );
  } else {
    hex_finish();
  }
  return HEXERR_SUCCESS;
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

  uart_putc('<');

  file = find_lun(pab.lun);

  if (file != NULL) {

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

      if ( fsize < pab.buflen ) {
        fsize = pab.buflen;
      }
    }
    // send how much we are going to send
    rc = transmit_word( fsize );

    // while we have data remaining to send.
    while ( fsize && rc == HEXERR_SUCCESS ) {

      len = fsize;    // remaing amount to read from file
      // while it fit into buffer or not?  Only read as much
      // as we can hold in our buffer.
      len = ( len > sizeof( buffer ) ) ? sizeof( buffer ) : len;

#ifdef ARDUINO
      memset((char *)buffer, 0, sizeof( buffer ));
      read = file->fp.read( (char *)buffer, len );
      timer_check(0);
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
#ifdef ARDUINO
    if ( !sd_initialized ) {
      rc = HEXSTAT_DEVICE_ERR;
    }
#endif
  }
  transmit_byte( rc ); // status byte transmit
  hex_finish();
  return HEXERR_SUCCESS;
}

/*
   hex_open() -
   open a file for read or write on the SD card.
*/
static uint8_t hex_open(pab_t pab) {
  uint16_t len = 0;
  uint8_t att = 0;
  uint8_t rc;
  BYTE    mode = 0;
  uint16_t fsize = 0;
  file_t* file = NULL;
  BYTE res;

  uart_putc('>');

  len = 0;
  if ( hex_receive_options(pab) == HEXSTAT_SUCCESS )
  {
    len = buffer[ 0 ] + ( buffer[ 1 ] << 8 );
    att = buffer[ 2 ];
  } else {
    hex_release_bus();
    return HEXERR_BAV; // BAV ERR.
  }
  
#ifdef ARDUINO

  // for now, until we get rid of SD library in Arduino build.
  switch (att & (0x80 | 0x40)) {
    case 0x00:  // append mode
      mode = FILE_WRITE;
      break;
    case OPENMODE_WRITE: // write, truncate if present. Maybe...
      mode = FILE_WRITE;
      break;
    case OPENMODE_WRITE | OPENMODE_READ:
      mode = FILE_WRITE | FILE_READ;
      break;
    default: //OPENMODE_READ
      mode = FILE_READ;
      break;
  }
  
#else
   // map attributes to FatFS file access mode
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
  
#endif
 
  if ( !buffer[ 3 ] ) {
    rc = HEXSTAT_OPTION_ERR; // no name?
  } else {
#ifdef ARDUINO
    if ( !sd_initialized ) {
      file = NULL;
    } else
#endif
    {
      file = reserve_lun(pab.lun);
    }
    if (file != NULL) {
#ifdef ARDUINO
      timer_check(0);
      file->fp = SD.open( (const char *)&buffer[3], mode );
      if ( SD.exists( (const char *)&buffer[3] )) {
        res = FR_OK;
      } else {
        res = FR_DENIED;
      }
#else
      res = f_open(&fs, &(file->fp), (UCHAR *)&buffer[3], mode);
#endif

      switch (res) {
        case FR_OK:
          rc = HEXSTAT_SUCCESS;
#ifdef ARDUINO
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
#ifdef ARDUINO
      if ( !sd_initialized ) {
        rc = HEXSTAT_DEVICE_ERR;
      }
#endif
    }
  }

  if (!hex_is_bav()) { // we can send response
    if ( rc == HEXERR_SUCCESS ) {
      switch (att & (OPENMODE_WRITE | OPENMODE_READ)) {

        // when opening to write...
        default:

          if (!( att & OPENMODE_INTERNAL)) { // if NOT open mode = INTERNAL, let's add CR/LF to end of line (display form).
            file->attr |= FILEATTR_DISPLAY;
          }

          if ( len <= sizeof( buffer ) ) {
            fsize = len;
          } else {
            fsize = sizeof( buffer );
          }
          break;

        case OPENMODE_READ:
          // open read-only w LUN=0: just return size of file we're reading; always. this is for verify, etc.
          if (pab.lun != 0 ) {
            if ( len ) {
              if ( len <= sizeof( buffer ) ) {
                fsize = len;
              } else {
                fsize = sizeof( buffer );
              }
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
   hex_close() -
   close the file associated with the LUN in the PAB.
   If the file is open, it is closed and data is sync'd.
   If the file is not open, appropriate status is returned

*/
static uint8_t hex_close(pab_t pab) {
  uint8_t rc;
  file_t* file = NULL;
  BYTE res = 0;

  uart_putc('<');

  file = find_lun(pab.lun);
  if (file != NULL) {
#ifdef ARDUINO
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
#ifdef ARDUINO
    if ( !sd_initialized ) {
      rc = HEXSTAT_DEVICE_ERR;
    }
#endif
  }

  if ( !hex_is_bav() ) {
    hex_send_final_response( rc );
    return HEXERR_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}

#ifdef ARDUINO
/*
   hex_delete() -
   delete a file from the SD card.
*/
static uint8_t hex_delete(pab_t pab) {
  uint16_t i = 0;
  uint8_t data;
  uint8_t rc = HEXERR_SUCCESS;
  uint8_t sd_was_not_inited = 0;

  while (i < pab.datalen) {
    data = 1; // tells receive byte to release HSK from previous receipt
    rc = receive_byte( &data );
    if ( rc == HEXERR_SUCCESS ) {
      buffer[ i++ ] = data; // grab additional data into buffer here.
    } else {
      hex_release_bus(); // float the bus
      return HEXERR_BAV; // we'll return with a BAV ERR
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
#else
static uint8_t hex_delete(pab_t pab) {
  hex_eat_it(pab.datalen, HEXSTAT_UNSUPP_CMD );
  return HEXERR_BAV;
}
#endif


static uint8_t hex_format(pab_t pab) {

  uart_putc('>');

  hex_eat_it(pab.datalen, HEXSTAT_UNSUPP_CMD );
  return HEXERR_BAV;
}



static uint8_t hex_null( pab_t pab ) {
  hex_release_bus();
  while (!hex_is_bav() )  // wait for BAV back high, ignore any traffic
    ;
  return HEXERR_SUCCESS;
}


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

  uart_putc(13);
  uart_putc(10);
  uart_putc('R');

  if ( open_files ) {
    // find file(s) that are open, get file pointer and lun number
    while ( (file = find_file_in_use(&lun) ) != NULL ) {
      // if we found a file open, silently close it, and free its lun.
#ifdef ARDUINO
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


#ifdef INCLUDE_PRINTER
  printer_open = 0;  // make sure our printer is closed.
#endif

#ifdef INCLUDE_SERIAL
  if ( serial_open ) {
    serial_peripheral.end();
    serial_open = 0;
  }
#endif
#ifdef INCLDUE_CLOCK
  if ( clock_open ) {
    clock_open = 0;
    Wire.end();
  }
#endif

  // release bus ignoring any further action on bus. no response sent.
  hex_finish();
  // wait here while bav is low
  while ( !hex_is_bav() ) {
#ifdef ARDUINO
    timer_check(0);
#else
    ;
#endif
  }
  return HEXERR_SUCCESS;
}


#ifdef INCLUDE_PRINTER
/*
   hex_printer_open() -
   "opens" the Serial.object for use as a printer at device code 12 (default PC-324 printer).
*/
static uint8_t hex_printer_open(pab_t pab) {
  uint16_t len = 0;
  uint8_t  rc = HEXSTAT_SUCCESS;
  uint8_t  att = 0;
  BYTE     res = HEXSTAT_SUCCESS;

  len = 0;
  if ( hex_receive_options( pab ) == HEXSTAT_SUCCESS ) {
    len = buffer[ 0 ] + ( buffer[ 1 ] << 8 );
    att = buffer[ 2 ];
  }

  if ( (res == HEXSTAT_SUCCESS) && ( att != OPENMODE_WRITE ) ) {
    rc = HEXSTAT_OPTION_ERR;
  }
  if ( res != HEXSTAT_SUCCESS ) {
    rc = res;
  }
  if (!hex_is_bav()) { // we can send response
    if ( rc == HEXSTAT_SUCCESS )
    {
      printer_open = 1;  // our printer is NOW officially open.
      len = len ? len : sizeof(buffer);
      hex_send_size_response( len );
    }
    else
    {
      hex_send_final_response( rc );
    }
    return HEXERR_SUCCESS;
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

  if ( !printer_open ) {
    rc = HEXSTAT_NOT_OPEN;
  }
  printer_open = 0;      // mark printer closed regardless.
  if ( !hex_is_bav() ) {
    // send 0000 response with appropriate status code.
    hex_send_final_response( rc );
  }
  return HEXERR_SUCCESS;
}

/*
    hex_printer_write() -
    write data to serial port when printer is open.
*/
static uint8_t hex_printer_write(pab_t pab) {
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
  } else {
    hex_finish();
  }
  return HEXERR_SUCCESS;
}
#endif


#ifdef INCLUDE_SERIAL
static uint8_t hex_rs232_open(pab_t pab) {
  uint16_t len;
  uint8_t  att;

  len = 0;
  if ( hex_receive_options(pab) == HEXSTAT_SUCCESS )
  {
    len = buffer[ 0 ] + ( buffer[ 1 ] << 8 );
    att = buffer[ 2 ];  // tells us open for read, write or both.
  } else {
    hex_release_bus();
    return HEXERR_BAV; // BAV ERR.
  }
  // Now, we need to parse the input buffer and decide on parameters.
  // realistically, all we can actually support is B=xxx.  Some other
  // parameters we just pretty much ignore D=, P=, C=, N=, S=.
  // The E= echo parameter we can act on.
  // The R= N/C/L we act on (no nl, carriage return, or linefeed.  We also add B for both CR and LF
  // The T= transfer tpe is also acted on, R/C/W.
  // The O= data overrun reporting, Yes No is acted on.
  //--
  // the ATT ributes we support are: OUTPUT mode 10 (write only), INPUT mode 01 (read only), and UPDATE
  // mode, 11 (read/write).  Relative/Sequential is always 0.  Fixed/Variable is ignored.
  // Internal/Display is always 0, other bits not used.
  //
  // work in progress... not ready for prime time.
  if ( !hex_is_bav() ) {
    if ( !serial_open ) {

      if ( att != 0 ) {
        len = len ? len : sizeof( buffer );
        serial_open = att; // 00 attribute = illegal.
        serial_peripheral.begin(9600);
        transmit_word( 4 );
        transmit_word( len );
        transmit_word( 0 );
        transmit_byte( HEXSTAT_SUCCESS );
        return HEXERR_SUCCESS;
      } else {
        att = HEXSTAT_ATTR_ERR;
      }
    } else {
      att = HEXSTAT_ALREADY_OPEN;
    }
    hex_send_final_response( att );
    return HEXERR_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}


static uint8_t hex_rs232_close(pab_t pab) {
  uint8_t rc = HEXSTAT_SUCCESS;

  if ( serial_open ) {
    serial_peripheral.end();
    serial_open = 0;
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }
  if (!hex_is_bav() ) { // we can send response
    hex_send_final_response( rc );
    return HEXERR_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}


static uint8_t hex_rs232_read(pab_t pab) {
  uint16_t len = pab.buflen;
  uint16_t bcount = serial_peripheral.available();
  uint8_t  rc = HEXSTAT_SUCCESS;

  if ( bcount > pab.buflen ) {
    bcount = pab.buflen;
  }
  if ( !hex_is_bav() ) {
    if ( serial_open & OPENMODE_READ ) {
      // send how much we are going to send
      rc = transmit_word( bcount );
      timer_check(0);
      // while we have data remaining to send.
      while ( bcount && rc == HEXERR_SUCCESS ) {

        len = bcount;    // remaing amount to read from file
        // while it fit into buffer or not?  Only read as much
        // as we can hold in our buffer.
        len = ( len > sizeof( buffer ) ) ? sizeof( buffer ) : len;

        bcount -= len;
        while ( len-- && rc == HEXSTAT_SUCCESS ) {
          rc = transmit_byte( serial_peripheral.read() );
        }
      }
      if ( rc != HEXERR_BAV ) {
        transmit_byte( rc );
      }
      hex_finish();
    } else if ( serial_open ) {
      hex_send_final_response( HEXSTAT_INPUT_MODE_ERR );
    } else {
      hex_send_final_response( HEXSTAT_NOT_OPEN );
    }
    return HEXERR_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}

static uint8_t hex_rs232_write(pab_t pab) {
  uint16_t len;
  uint16_t i;
  uint16_t j;
  uint8_t  rc = HEXERR_SUCCESS;

  len = pab.datalen;
  if ( serial_open & OPENMODE_WRITE ) {
    while (len && rc == HEXERR_SUCCESS ) {
      i = (len >= sizeof(buffer) ? sizeof(buffer) : len);
      rc = hex_getdata(buffer, i);
      if (rc == HEXSTAT_SUCCESS) {
        j  = 0;
        while (j < len) {
          serial_peripheral.write( buffer[ j++ ] );
        }
        timer_check(0);
      }
      len -= i;
    }
  } else {
    rc = HEXSTAT_OUTPUT_MODE_ERR;
  }

  if ( len ) {
    hex_eat_it( len, rc );
    return HEXERR_BAV;
  }

  if (!hex_is_bav() ) { // we can send response
    hex_send_final_response( rc );
    return HEXERR_SUCCESS;
  }
  hex_finish();
  return HEXERR_BAV;
}


static uint8_t hex_rs232_rtn_sta(pab_t pab) {
  return HEXERR_SUCCESS;
}


static uint8_t hex_rs232_set_opts(pab_t pab) {
  return HEXERR_SUCCESS;
}
#endif


#ifdef INCLUDE_CLOCK
/*
   Open access to RTC module. Begin Wire, Begin DS3231
*/
static uint8_t hex_rtc_open(pab_t pab) {
  if ( !clock_open ) {
    Wire.begin();
    clock_peripheral.setClockMode(false); // 24h
    clock_open = 1;
  }
  return HEXERR_SUCCESS;
}

/*
   Close access to RTC module. Shut down Wire.
*/
static uint8_t hex_rtc_close(pab_t pab) {
  if ( clock_open ) {
    Wire.end();
    clock_open = 0;
  }
  return HEXERR_SUCCESS;
}

/*
   Return time in format YYMMDD_HHMMSS in 24h form.
   When RTC opened in INPUT or UPDATE mode.
*/
static uint8_t hex_rtc_read(pab_t pab) {
  if ( clock_open )
  {

  }
  return HEXERR_SUCCESS;
}

/*
   Set time when we receive time in format YYMMDD_HHMMSS (13 bytes of data).
   When RTC opened in OUTPUT or UPDATE mode.
*/
static uint8_t hex_rtc_write(pab_t pab) {
  if ( clock_open ) {

  }
  return HEXERR_SUCCESS;
}

#endif // include_clock




// PROGMEM data tables for command processing.
static const cmd_proc handlers[] PROGMEM = {
  // All devices
  hex_reset_bus,
  hex_null,
  // disk device operations
  hex_open,
  hex_close,
  hex_read,
  hex_write,
  hex_delete,
  hex_verify,
  hex_format,
 #ifdef INCLUDE_PRINTER
  // printer (PC-324) operations.
  hex_printer_open,
  hex_printer_close,
  hex_printer_write,
 #endif
 #ifdef INCLUDE_SERIAL
  // serial RS232 peripheral (dev code 20) operations.
  hex_rs232_open,
  hex_rs232_close,
  hex_rs232_read,
  hex_rs232_write,
  hex_rs232_rtn_sta,
  hex_rs232_set_opts,
#endif
#ifdef INCLUDE_CLOCK
  // RTC device
  hex_rtc_open,
  hex_rtc_close,
  hex_rtc_read,
  hex_rtc_write,
 #endif
  //
  // end of table
  NULL
};

static const uint8_t command_codes[] PROGMEM = {
  // All devices
  HEXCMD_RESET_BUS,
  HEXCMD_NULL,
  // disk device operations
  HEXCMD_OPEN,
  HEXCMD_CLOSE,
  HEXCMD_READ,
  HEXCMD_WRITE,
  HEXCMD_DELETE,
  HEXCMD_VERIFY,
  HEXCMD_FORMAT,
 #ifdef INCLUDE_PRINTER
  // printer (PC-324) operations.
  HEXCMD_OPEN,
  HEXCMD_CLOSE,
  HEXCMD_WRITE,
 #endif
 #ifdef INCLUDE_SERIAL
  // serial RS232 peripheral (dev code 20) operations.
  HEXCMD_OPEN,
  HEXCMD_CLOSE,
  HEXCMD_READ,
  HEXCMD_WRITE,
  HEXCMD_RETURN_STATUS,
  HEXCMD_SET_OPTIONS,
 #endif
 #ifdef INCLUDE_CLOCK
  // RTC device
  HEXCMD_OPEN,
  HEXCMD_CLOSE,
  HEXCMD_READ,
  HEXCMD_WRITE,
 #endif
  //
  // end of table
  0
};


static const uint8_t device_codes[] PROGMEM = {
  // All devices
  ANY_DEV,
  ANY_DEV,
  // disk device operations
  DISK_DEV,
  DISK_DEV,
  DISK_DEV,
  DISK_DEV,
  DISK_DEV,
  DISK_DEV,
  DISK_DEV,
 #ifdef INCLUDE_PRINTER
  // printer (PC-324) operations.
  PRINTER_DEV,
  PRINTER_DEV,
  PRINTER_DEV,
 #endif
 #ifdef INCLUDE_SERIAL
  // serial RS232 peripheral (dev code 20) operations.
  RS232_DEV,
  RS232_DEV,
  RS232_DEV,
  RS232_DEV,
  RS232_DEV,
  RS232_DEV,
 #endif
 #ifdef INCLUDE_CLOCK
  // RTC device
  RTC_DEV,
  RTC_DEV,
  RTC_DEV,
  RTC_DEV,
 #endif
  //
  // end of table
  0
};


static void execute_command(pab_t pab)
{
  uint8_t i = 0;
  uint8_t cod;
  uint8_t rup = 0;
  uint8_t cmd;

  cmd_proc  entry = (cmd_proc)pgm_read_word( &handlers[ i ] );

  while ( entry != NULL ) {
    cmd = pgm_read_byte( &command_codes[ i ] );
    if ( pab.cmd == cmd ) {
      cod = pgm_read_byte( &device_codes[ i ] );
      if ( cod == DISK_DEV ) {
        rup = MAX_DISKS;
      }
      if ( (( pab.dev >= cod ) &&
            ( pab.dev <= ( cod + rup ))) ||
           ( cod == ANY_DEV )
         ) {
        (entry)( pab );
        return;
      }
    }
    i++;
    entry = (cmd_proc)pgm_read_word( &handlers[ i ] );
  }
  // didn't find a handler?  then report as an unsupported command
  hex_eat_it(pab.datalen, HEXSTAT_UNSUPP_CMD );
  return;
}




#ifndef ARDUINO
// Non-Arduino makefile entry point
int main(void) __attribute__((OS_main));
int main(void) { 
#else
// Arduino entry for running system
void loop(void) { // Arduino main loop routine.

#endif   // arduino

  // Variables used common to both Arduino and makefile builds
  uint8_t i = 0;
  uint8_t ignore_cmd = FALSE;
  pab_raw_t pabdata;
  BYTE res;

#ifndef ARDUINO

 // setup stuff for main
  board_init();
  hex_init();
  uart_init();
  disk_init();
  leds_init();
  timer_init();
  device_hw_address_init();
  init_files();

  sei();

  res = f_mount(1, &fs);
  
#endif

  open_files = 0;

  pabdata.pab.cmd = 0;
  pabdata.pab.lun = 0;
  pabdata.pab.record = 0;
  pabdata.pab.buflen = 0;
  pabdata.pab.datalen = 0;

  uart_puts_P(PSTR("Device ID: 0x"));
  uart_puthex(device_hw_address());
  uart_putcrlf();

  while (TRUE) {

#ifdef ARDUINO
    set_busy_led( FALSE );
    timer_check(1);
#endif

    while (hex_is_bav()) {

#ifdef ARDUINO
      timer_check(0);
#endif

#ifdef INCLUDE_POWERMGMT
      sleep_the_system();  // sleep until BAV falls. If low, HSK will be low.
#endif

    }

    uart_putc('^');
    
#ifdef ARDUINO
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
#ifdef INCLUDE_PRINTER
                ||
                (( pabdata.pab.dev == PRINTER_DEV ) && (device_hw_address() == DISK_DEV) )
#endif
#ifdef INCLUDE_CLOCK
                ||
                (( pabdata.pab.dev == (RTC_DEV + ((device_hw_address()-DISK_DEV)&3))))
#endif
#ifdef INCLUDE_SERIAL
                ||
                (( pabdata.pab.dev == (RS232_DEV + ((device_hw_address()-DISK_DEV)&3))))
#endif 
              )
           )
        {
          ignore_cmd = TRUE;
        }
      }

      if ( !ignore_cmd ) {
        if (i == 9) {
          // exec command
          uart_putc(13);
          uart_putc(10);
#ifdef ARDUINO
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
          execute_command( pabdata.pab );
          ignore_cmd = TRUE;  // in case someone sends more data, ignore it.
        }
      } else {
        uart_putc('%');
        uart_puthex(pabdata.raw[0]);
        i = 0;
        hex_release_bus();
        while (!hex_is_bav() )  // wait for BAV back high, ignore any traffic
          ;
        ignore_cmd = FALSE;
      }
    }

    uart_putc(13);
    uart_putc(10);
    i = 0;
    ignore_cmd = FALSE;
  }
}
