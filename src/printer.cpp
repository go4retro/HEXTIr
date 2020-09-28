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

    printer.cpp: Printer-based (over Serial) device functions.
*/

#include <string.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#include "config.h"
#include "configure.h"
#include "debug.h"
#include "hexbus.h"
#include "hexops.h"
#include "registry.h"
#include "swuart.h"
#include "timer.h"
#include "printer.h"

#ifdef INCLUDE_PRINTER

// Global defines
static volatile uint8_t  prn_open = 0;
static printcfg_t _defaultcfg;
static printcfg_t _cfg;

#ifdef USE_CMD_LUN

typedef enum _prncmd_t {
                          PRN_CMD_NONE = 0,
                          PRN_CMD_CRLF,
                          PRN_CMD_COMP
} prncmd_t;

static const action_t cmds[] PROGMEM = {
                                        {PRN_CMD_CRLF,  "c"},
                                        {PRN_CMD_COMP,  "s"},
                                        {PRN_CMD_NONE,  ""}
                                       };

static inline hexstatus_t prn_exec_cmd(char* buf, uint8_t len, printcfg_t *cfg) {
  hexstatus_t rc = HEXSTAT_SUCCESS;
  prncmd_t cmd;

  // path, trimmed whitespaces
  trim(&buf, &len);

  cmd = (prncmd_t) parse_equate(cmds, &buf, &len);
  if(cmd != PRN_CMD_NONE) {
    //skip spaces
    trim (&buf, &len);
  }
  switch (cmd) {
  case PRN_CMD_COMP:
    switch(lower(buf[0])) {
    case 'y':
    case '1':
      // compressed chars
      break;
    case 'n':
    case '0':
      // uncompressed chars
      break;
    default:
      rc = HEXSTAT_DATA_ERR;
      break;
    }
    break;
  case PRN_CMD_CRLF:
    switch(lower(buf[0])) {
    case 'n':
      // don't send a CRLF
      cfg->line = FALSE;
      break;
    case 'l':
      cfg->line = TRUE;
      // send a CRLF
      break;
    default:
      rc = HEXSTAT_DATA_ERR;
      break;
    }
    break;
  default:
    rc = hex_exec_cmd(buf, len);
  }
  return rc;
}


static inline hexstatus_t prn_exec_cmds(char* buf, uint8_t len, printcfg_t *cfg) {
  char * buf2;
  uint8_t len2;
  hexstatus_t rc = HEXSTAT_SUCCESS;
  hexstatus_t rc2;

  buf2 = buf;
  len2 = len;
  do {
    buf = buf2;
    len = len2;
    split_cmd(&buf, &len, &buf2, &len2);
    rc2 = prn_exec_cmd(buf, len, cfg);
    // pick the last error.
    rc = (rc2 != HEXSTAT_SUCCESS ? rc2 : rc);
  } while(len2);
  return rc;
}

static inline void prn_write_cmd(pab_t *pab, printcfg_t *cfg) {
  hexstatus_t rc = HEXSTAT_SUCCESS;

  debug_puts_P("Exec Printer Command\n");

  rc = hex_write_cmd_helper(pab->datalen);
  if(rc != HEXSTAT_SUCCESS) {
    return;
  }
  rc = prn_exec_cmds((char *)buffer, pab->datalen, cfg);
  if (!hex_is_bav() ) { // we can send response
    hex_send_final_response( rc );
  } else {
    hex_finish();
  }
}
#endif



/*
   hex_prn_open() -
   "opens" the Serial.object for use as a printer at device code 12 (default PC-324 printer).
*/
static void hex_prn_open(pab_t *pab) {
  uint16_t len = 0;
  char *buf;
  uint8_t blen;
  hexstatus_t  rc = HEXSTAT_SUCCESS;
  uint8_t  att = 0;

  debug_puts_P("Open Printer\n");

#ifdef USE_OPEN_HELPER
  rc = hex_open_helper(pab, HEXSTAT_TOO_LONG, &len, &att);
  if(rc != HEXSTAT_SUCCESS)
    return;
#else
  if(pab->datalen > BUFSIZE) {
    hex_eat_it( pab->datalen, HEXSTAT_TOO_LONG );
    return;
  }

  if ( hex_get_data( buffer, pab->datalen ) == HEXSTAT_SUCCESS ) {
    len = buffer[ 0 ] + ( buffer[ 1 ] << 8 );
    att = buffer[ 2 ];
  } else {
    hex_release_bus();
    return;
  }
#endif

#ifdef USE_CMD_LUN
  if(pab->lun == LUN_CMD) {
    blen = pab->datalen;
    buf = (char *)buffer;
    trim(&buf, &blen);
    // we should check length, as it should be 0, and att should be WRITE or UPDATE
    rc = prn_exec_cmds(buf, blen, &_defaultcfg);
    if (!hex_is_bav() ) { // we can send response
      hex_send_final_response( rc );
    } else {
      hex_finish();
    }
    return;
  }
#endif

  if ( !prn_open ) {
    if ( att != OPENMODE_WRITE ) {
      rc = HEXSTAT_OPTION_ERR;
    }
  } else {
    rc = HEXSTAT_ALREADY_OPEN;
  }
  if(rc == HEXSTAT_SUCCESS ) {
    _cfg.line = _defaultcfg.line;
    rc = prn_exec_cmds((char *)buffer, pab->datalen, &_cfg);
    prn_open = 1;  // our printer is NOW officially open.
    len = len ? len : BUFSIZE;
  }
  if (!hex_is_bav()) { // we can send response
    if ( rc == HEXSTAT_SUCCESS ) {
      hex_send_size_response(len, 0);
    } else {
      hex_send_final_response( rc );
    }
  } else
    hex_finish();
}


/*
   hex_prn_close() -
   closes printer at device 12 for use from host.
*/
static void hex_prn_close(pab_t *pab) {
  hexstatus_t rc = HEXSTAT_SUCCESS;

  debug_puts_P("Close Printer\n");

#ifdef USE_CMD_LUN
  if (pab->lun == LUN_CMD) {
    // handle command channel close
    hex_close_cmd();
    return;
  }
#endif

  if ( !prn_open ) {
    rc = HEXSTAT_NOT_OPEN;
  }
  prn_open = 0;      // mark printer closed regardless.
  if ( !hex_is_bav() ) {
    // send 0000 response with appropriate status code.
    hex_send_final_response( rc );
  }
}


/*
    hex_prn_write() -
    write data to serial port when printer is open.
*/
static void hex_prn_write(pab_t *pab) {
  uint16_t len;
  uint16_t i;
  uint8_t  written = 0;
  hexstatus_t  rc = HEXSTAT_SUCCESS;

  debug_puts_P("Write Printer\n");

  len = pab->datalen;

#ifdef USE_CMD_LUN
  if(pab->lun == LUN_CMD) {
    // handle command channel
    prn_write_cmd(pab, &_defaultcfg);
    return;
  }
#endif

  if(prn_open) {
    while ( len && rc == HEXSTAT_SUCCESS ) {
      i = (len >= BUFSIZE ? BUFSIZE : len);
      rc = hex_get_data(buffer, i);
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
      if ( rc == HEXSTAT_SUCCESS && prn_open ) {
  //#ifdef ARDUINO
  //      Serial.write(buffer, i);
  //      delayMicroseconds( i*72 );
  //      /* use 72 us per character sent delay.
  //         digital logic analyzer confirms that @ 115200 baud, the data is
  //         flushed over the wire BEFORE we continue HexBus operations.
  //      */
  //#else
        for(uint8_t j = 0; j < i; j++) {
          swuart_putc(0, buffer[j]);
        }
        swuart_flush();
  //#endif
        written = 1; // indicate we actually wrote some data
      }
      len -= i;
    }
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }

  if ( len ) {
    hex_eat_it( len, rc );
    return;
  }

  /* if we've written data and our printer is open, finish the line out with
      a CR/LF.
  */
  if ( written && prn_open && _cfg.line) {
    swuart_putcrlf(0);
    swuart_flush();
  }

  /*
     send response and finish operation
  */
  if (!hex_is_bav() ) {
    hex_send_final_response( rc );
  } else {
    hex_finish();
  }
}


static void hex_prn_reset( __attribute__((unused)) pab_t *pab) {
  
  prn_reset();
  // release the bus ignoring any further action on bus. no response sent.
  hex_finish();
  // wait here while bav is low
  while ( !hex_is_bav() ) {
    ;
  }
}


void prn_reset( void ) {
  prn_open = 0; // make sure our printer is closed.
}

/*
 * Command handling registry for device
 */
#ifdef USE_NEW_OPTABLE
static const cmd_op_t ops[] PROGMEM = {
                                        {HEXCMD_OPEN,            hex_prn_open},
                                        {HEXCMD_CLOSE,           hex_prn_close},
                                        {HEXCMD_WRITE,           hex_prn_write},
                                        {HEXCMD_RESET_BUS,       hex_prn_reset},
                                        {HEXCMD_INVALID_MARKER,  NULL}
                                      };
#else
static const cmd_proc fn_table[] PROGMEM = {
  hex_prn_open,
  hex_prn_close,
  hex_prn_write,
  hex_prn_reset,
  NULL // end of table.
};

static const uint8_t op_table[] PROGMEM = {
  HEXCMD_OPEN,
  HEXCMD_CLOSE,
  HEXCMD_WRITE,
  HEXCMD_RESET_BUS,
  HEXCMD_INVALID_MARKER
};
#endif

void prn_register(void) {
#ifdef NEW_REGISTER
#ifdef USE_NEW_OPTABLE
  cfg_register(DEV_PRN_START, DEV_PRN_DEFAULT, DEV_PRN_END, ops);
#else
  cfg_register(DEV_PRN_START, DEV_PRN_DEFAULT, DEV_PRN_END, op_table, fn_table);
#endif
#else
  uint8_t i = registry.num_devices;
  
  registry.num_devices++;
  registry.entry[ i ].dev_low = DEV_PRN_START;
  registry.entry[ i ].dev_cur = DEV_PRN_DEFAULT;
  registry.entry[ i ].dev_high = DEV_PRN_END; // support 10 thru 19 as device codes.
#ifdef USE_NEW_OPTABLE
  registry.entry[ i ].oplist = (cmd_op_t *)ops;
#else
  registry.entry[ i ].operation = (cmd_proc *)fn_table;
  registry.entry[ i ].command = (uint8_t *)op_table;
#endif
#endif
}

void prn_init( void ) {

  prn_open = 0;
  swuart_setrate(0, SB115200);
#ifdef INIT_COMBO
  prn_register();
#endif
  _defaultcfg.line = TRUE;
}
#endif
