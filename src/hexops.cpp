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

    hexops.cpp: Foundational Hex Bus functions
*/

#include "string.h"
#include <avr/pgmspace.h>

#include "config.h"
#include "debug.h"
#include "eeprom.h"
#include "hexbus.h"
#include "hexops.h"
#include "timer.h"
#include "uart.h"
#include "hexops.h"

// add 1 to buffer size to handle null termination if used as a string
uint8_t buffer[BUFSIZE + 1];

// For reference as we implement per device config:

/*
   hex_cfg_open() -

   Open the configuration device to allow reconfiguration
   of the supported peripheral's addresses.

   If mask is retrieved via command, the bit mask values
   tell which peripheral(s) are supported by the system.

   REC 0 = drive
   REC 1 = printer
   REC 2 = serial
   REC 3 = clock
   REC 4 = unused/reserved
   REC 5 = unused/reserved
   REC 6 = unused/reserved

   OPEN #n,"222",RELATIVE[,INPUT | OUTPUT | UPDATE]
   INPUT #1,REC n,A$
   PRINT #1,REC n,A$
   CALL IO(222,238,STATUS) ! 238 = command to update EEPROM with current data
   CALL IO(222,202,STATUS) ! 202 = command to retrieve support mask (tells us which devices are supported) STATUS=mask
   CLOSE #1
*/


uint8_t parse_number(char** buf, uint8_t *len, uint8_t digits, uint32_t* value) {
  uint8_t i = 0;
  uint8_t digits_found = FALSE;
  uint8_t err = FALSE;

  *value = 0;
  while(!err && i < *len) {
    if((*buf)[i] == ' ') {
      i++;
      if(digits_found) {
        break; // space after digits
      }
    } else if((*buf)[i] >= '0' && (*buf)[i] <= '9') {
      if(digits_found < digits) {
        *value = *value  * 10 + ((*buf)[i++] - '0');
        digits_found = TRUE;
      } else // too many digits
        err = 1;
    } else if((*buf)[i] == ',') {
      i++;
      break;
    }
    else {
      err = TRUE;
    }
  }
  err = !digits_found || err;
  if(!err) {
    *buf = *buf + i;
    (*len) -= i;
  }
  return err;
}


hexstatus_t hex_get_data(uint8_t *buf, uint16_t len) {
  uint16_t i = 0;

  while (i < len) {
    buf[ i ] = 1;
    if(receive_byte( &buf[ i++ ] ) != HEXERR_SUCCESS) {
      // TODO probably should define what errors could happen
      return HEXSTAT_DATA_ERR;
    }
  }
  if (len > 0) {
    debug_trace(buf, 0, len);
  }
  return HEXSTAT_SUCCESS;
}


void hex_eat_it(uint16_t length, hexstatus_t status )
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
}


/*
 * hex_unsupported() should be used for any command on any device
 * where we provide no support for that command.
 */
void hex_unsupported(pab_t *pab) {
  hex_eat_it(pab->datalen, HEXSTAT_UNSUPP_CMD );
}


void hex_null(pab_t *pab __attribute__((unused))) {
  hex_release_bus();
  while (!hex_is_bav() )  // wait for BAV back high, ignore any traffic
    ;
}

#ifdef USE_OPEN_HELPER
hexstatus_t hex_open_helper(pab_t *pab, hexstatus_t err, uint16_t *len, uint8_t *att) {

  if(pab->datalen > BUFSIZE) {
    hex_eat_it( pab->datalen, err );
    return err;
  }

  if ( hex_get_data( buffer, pab->datalen ) == HEXSTAT_SUCCESS ) {
    *len = buffer[ 0 ] + ( buffer[ 1 ] << 8 );
    *att = buffer[ 2 ];
  } else {
    hex_release_bus();
    return HEXSTAT_BUS_ERR;
  }
  return HEXSTAT_SUCCESS;
}
#endif

#ifdef USE_CMD_LUN
/**
 * Remove all leading and trailing whitespaces
 */
void trim(char **buf, uint8_t *blen) {
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


uint8_t parse_cmd(const action_t list[], char **buf, uint8_t *blen) {
  uint8_t i = 0;
  uint8_t j = 0;
  uint8_t  action;
  uint8_t ch;


  trim(buf, blen);
  action = mem_read_byte(list[j].action);
  while(action) {
    //debug_puts("Action: ");
    //debug_puthex(action);
    //debug_puts(": ch = '");
    ch = mem_read_byte(list[j].text[i]);
    //debug_putc(ch);
    //debug_puts("'\r\n");
    if(!ch && (i == *blen || (*buf)[i] == ' ')) { // end of command
      *buf = *buf + i;
      (*blen) -= i;
      return action;
    } else if(i == *blen) {
      // past end of command, try again
      j += 1;
      i = 0;
      action = mem_read_byte(list[j].action);
    } else if(lower((*buf)[i]) == ch) {
      i++;
    } else { // no match, start over
      j += 1;
      i = 0;
      action = mem_read_byte(list[j].action);
    }
  }
  return 0;
}


uint8_t parse_equate(const action_t list[], char **buf, uint8_t *len) {
  uint8_t opt;
  uint8_t i;
  char* buf2;
  uint8_t len2;

  trim(buf, len);
  for(i = 0; i < *len; i++) {
    if((*buf)[i] == '=') {
      buf2 = &((*buf)[i + 1]);
      len2 = *len - i - 1;
      // handle set
      opt = parse_cmd(list, buf, &i);
      if(opt) {
        *buf = buf2;
        *len = len2;
        trim (buf, len);
        //debug_trace(*buf,0, *len);
      }
      return opt;
    } else {
      // no =
    }
  }
  return 0;
}


void split_cmd(char **buf, uint8_t *len, char **buf2, uint8_t *len2) {
  uint8_t i;

  *len2 = 0;
  for(i = 0; i < *len; i++) {
    if((*buf)[i] == ',') {
      // found a separator
        *buf2 = &(*buf)[i + 1];
      *len2 = *len - i - 1;
      *len = i;
      return;
    }
  }
}


void hex_finish_open(uint16_t len, hexstatus_t rc) {

  if (!hex_is_bav()) { // we can send response
    if ( rc == HEXSTAT_SUCCESS ) {
      hex_send_size_response(len, 0);
    } else {
      hex_send_final_response( rc );
    }
  } else
    hex_finish();
  return;
}

typedef enum _execcmd_t {
  EXEC_CMD_NONE = 0,
  EXEC_CMD_DEV,
  EXEC_CMD_STORE
} execcmd_t;

static const action_t cmds[] MEM_CLASS = {
                                        {EXEC_CMD_DEV,"de"},
                                        {EXEC_CMD_DEV,"dev"},
                                        {EXEC_CMD_STORE,"st"},
                                        {EXEC_CMD_STORE,"store"},
                                        {EXEC_CMD_NONE,""}
                                       };


// should be of the form "set <parm>=<value>"
hexstatus_t hex_exec_cmd(char* buf, uint8_t len, uint8_t *dev) {
  hexstatus_t rc = HEXSTAT_SUCCESS;
  execcmd_t cmd;
  uint8_t i = 0;
  uint32_t value = 0;

  debug_puts_P("Exec General Command\r\n");

  cmd = (execcmd_t)parse_cmd(cmds, &buf, &len);
  // if a cmd, i will point to location after cmd
  // and cmd will equal type requested
  if(cmd != EXEC_CMD_NONE) {
    //skip spaces
    trim (&buf, &len);
  }
  switch (cmd) {
  case EXEC_CMD_STORE:
    ee_set_config();
    break;
  default:
    cmd = (execcmd_t)parse_equate(cmds, &buf, &len);
    switch(cmd) {
    case EXEC_CMD_DEV:
      if(dev != NULL) {
        rc = HEXSTAT_DATA_ERR; // value too large or not a number
        if(!parse_number(&buf, &len, 3, &value)) {
          if(value <= DEV_MAX) {
            rc = HEXSTAT_DATA_INVALID; // no such device
            for(i = 0; i < registry.num_devices; i++) {
              if(
                  (registry.entry[i].dev_low <= (uint8_t)value)
                  && (registry.entry[i].dev_high >= (uint8_t)value)
                ) {
                registry.entry[i].dev_cur = (uint8_t)value;
                *dev = (uint8_t)value;
                rc = HEXSTAT_SUCCESS;
                break;
              }
            }
          }
        }
      } else {
        rc = HEXSTAT_DATA_INVALID;
      }
      break;
    default:
      // error
      rc = HEXSTAT_OPTION_ERR;
      break;
    }
    break;
  }
  return rc;
}

hexstatus_t hex_exec_cmds(char* buf, uint8_t len, uint8_t *dev) {
  hexstatus_t rc = HEXSTAT_SUCCESS;
  char *buf2;
  uint8_t len2;

  buf2 = buf;
  len2 = len;
  do {
    buf = buf2;
    len = len2;
    split_cmd(&buf, &len, &buf2, &len2);
    rc = hex_exec_cmd(buf, len, dev);
  } while(rc == HEXSTAT_SUCCESS && len2);
  return rc;
}

hexstatus_t hex_write_cmd_helper(uint16_t len) {
  hexstatus_t rc = HEXSTAT_SUCCESS;

  if (len < BUFSIZE) {
    rc = hex_get_data(buffer, len);
    if (rc != HEXSTAT_SUCCESS) {
      // TODO Do we need to do something here
    }
  } else { // command too long...
    rc = HEXSTAT_TOO_LONG;
    hex_eat_it( len, rc ); // reports status back.
  }
  return rc;
}


void hex_write_cmd(pab_t *pab, uint8_t *dev) {
  hexstatus_t rc = HEXSTAT_SUCCESS;
  uint8_t len;
  char *buf;

  debug_puts_P("Handle Common Command\r\n");

  len = pab->datalen;
  rc = hex_write_cmd_helper(len);
  if(rc != HEXSTAT_SUCCESS) {
    return;
  }
  buf = (char *)buffer;
  // trim whitespace
  trim(&buf, &len);
  rc = hex_exec_cmds(buf, len, dev);
  if (!hex_is_bav() ) { // we can send response
    hex_send_final_response( rc );
  } else {
    hex_finish();
  }
}


void hex_close_cmd(void) {
  if ( !hex_is_bav() ) {
    hex_send_final_response( HEXSTAT_SUCCESS );
  } else
    hex_finish();
}


#define STRLEN(s) ( (sizeof(s)/sizeof(s[0])) - sizeof(s[0]))
const char _version[] PROGMEM = "" VERSION " [" TOSTRING(CONFIG_HARDWARE_NAME) "]";

void hex_read_status(void) {
  hexstatus_t rc;
  uint8_t i;

  rc = (transmit_word( STRLEN(_version) ) == HEXERR_SUCCESS ? HEXSTAT_SUCCESS : HEXSTAT_DATA_ERR);
  for (i = 0; rc == HEXSTAT_SUCCESS && i < STRLEN(_version); i++) {
    rc = (transmit_byte(pgm_read_byte(&_version[i])) == HEXERR_SUCCESS ? HEXSTAT_SUCCESS : HEXSTAT_DATA_ERR);
  }
  transmit_byte(rc);
  hex_finish();
}
#endif


