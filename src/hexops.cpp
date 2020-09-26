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


#include "config.h"
#include "debug.h"
#include "hexbus.h"
#include "hexops.h"
#include "timer.h"
#include "uart.h"

#include "hexops.h"

// add 1 to buffer size to handle null termination if used as a string
uint8_t buffer[BUFSIZE + 1];

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
    debug_putcrlf();
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

#ifdef JIM_PARSER
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
  uint8_t i;
  uint8_t j;
  trim(buf, blen);
  j = 0;
  i = 0;
  while(list[j].action) {
    if(!(list[j].text[i])) { // end of command
      *buf = *buf + i;
      (*blen) -= i;
      return list[j].action;
      break;
    } else if(i == *blen) {
      // past end of command, try again
      j += 1;
      i = 0;
    } else if(lower((*buf)[i]) == list[j].text[i]) {
      i++;
    } else { // no match, start over
      j += 1;
      i = 0;
    }
  }
  return 0;
}


void hex_open_cmd(pab_t pab) {
  uint16_t len;
  uint8_t att;

  debug_puts_P(PSTR("\n\rOpen Command\n\r"));

  // we need one more byte for the null terminator, so check.
  if(hex_open_helper(pab, &len, &att) != HEXSTAT_SUCCESS)
    return;

  // we should check length, as it should be 0, and att should be WRITE or UPDATE

  if(!hex_is_bav()) { // we can send response
    transmit_word(2);
    transmit_word(BUFSIZE);
    transmit_byte(HEXSTAT_SUCCESS);    // status code
    hex_finish();
  } else
    hex_finish();
}

typedef enum _set_opt_t {
  SET_OPT_NONE = 0,
  SET_OPT_DEVICE,
} set_opt_t;

static const action_t opts[] = {
                                {SET_OPT_DEVICE,"dev"},
                                {SET_OPT_DEVICE,"device"},
                                {SET_OPT_NONE,""}
                               };


typedef enum _execcmd_t {
  EXEC_CMD_NONE = 0,
  EXEC_CMD_SET,
} execcmd_t;

static const action_t cmds[] = {
                                        {EXEC_CMD_SET,"set"},
                                        {EXEC_CMD_NONE,""}
                                       };


// should be of the form "set <parm>=<value>"
hexstatus_t hex_exec_cmd(char* buf, uint8_t len) {
  hexstatus_t rc = HEXSTAT_SUCCESS;
  char *buf2;
  execcmd_t cmd;
  set_opt_t opt;
  uint8_t i = 0;
  uint8_t j = 0;

  debug_puts_P(PSTR("\n\rExec General Command\n\r"));

  cmd = (execcmd_t)parse_cmd(cmds, &buf, &len);
  // if a cmd, i will point to location after cmd
  // and cmd will equal type requested
  if(cmd != EXEC_CMD_NONE) {
    //skip spaces
    trim (&buf, &len);
  }
  switch (cmd) {
  case EXEC_CMD_SET:
    while(i < len) {
      if(buf[i] == '=') {
        buf2 = &buf[i + 1];
        j = len - i - 1;
        // handle set
        trim (&buf, &i);
        opt = (set_opt_t)parse_cmd(opts, &buf, &len);
        if(opt) {
          trim (&buf2, &j);
          switch(opt) {
          case SET_OPT_DEVICE:
            // set device id to parm
            rc = HEXSTAT_SUCCESS;
            break;
          default:
            // error
            rc = HEXSTAT_OPTION_ERR;
            break;
          }
        }
        break;
      } else {
        // no =
        i++;
      }
    }
    break;
  default:
    // error
    rc = HEXSTAT_OPTION_ERR;
    break;
  }
  return rc;
}

hexstatus_t hex_write_cmd_helper(uint16_t len) {
  hexstatus_t rc = HEXSTAT_SUCCESS;

  if (len < (BUFSIZE)) {
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


void hex_write_cmd(pab_t pab) {
  hexstatus_t rc = HEXSTAT_SUCCESS;
  uint8_t len;
  char *buf;

  debug_puts_P(PSTR("\n\rHandle Common Command\n\r"));

  len = pab.datalen;
  rc = hex_write_cmd_helper(len);
  if(rc != HEXSTAT_SUCCESS) {
    return;
  }
  buf = (char *)buffer;
  // path, trimmed whitespaces
  trim(&buf, &len);
  rc = hex_exec_cmd(buf, len);
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
#endif
