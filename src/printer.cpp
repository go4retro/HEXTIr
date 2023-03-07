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
#include "debug.h"
#include "eeprom.h"
#include "hexbus.h"
#include "hexops.h"
#include "registry.h"
#include "swuart.h"
#include "timer.h"
#include "printer.h"

/*
 * Punch List
 *
 * TODO: Add Status command
 *
 */

#ifdef INCLUDE_PRINTER

// Global defines
static volatile uint8_t  _prn_open = FALSE;
static printcfg_t _cfg;

typedef enum _prncmd_t {
                          PRN_CMD_NONE = 0,
                          PRN_CMD_CRLF,
                          PRN_CMD_SPACING,
                          PRN_CMD_COMP
} prncmd_t;

static const action_t cmds[] PROGMEM = {
                                        {PRN_CMD_CRLF,      "c"},   // ALC printer/plotter
                                        {PRN_CMD_CRLF,      "r"},   // 80 column printer
                                        {PRN_CMD_COMP,      "s"},   // alc printer/plotter
                                        {PRN_CMD_SPACING,   "l"},   // 80 column printer
                                        {PRN_CMD_NONE,      ""}
                                       };

static inline hexstatus_t prn_exec_cmd(char* buf, uint8_t len, uint8_t *dev, printcfg_t *cfg) {
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
    case 'l':
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
    case PRN_CMD_SPACING:
      switch(lower(buf[0])) {
      case 's':
        // single cr/lf
        cfg->spacing = 1;
        break;
      case 'd':
        // double cr/lf
        cfg->spacing = 1;
        break;
      default:
        // hanmdle 3+ lines per lf.
        break;
      }
    default:
      rc = HEXSTAT_DATA_ERR;
      break;
    }
    break;
  default:
    rc = hex_exec_cmd(buf, len, dev);
  }
  return rc;
}


static inline hexstatus_t prn_exec_cmds(char* buf, uint8_t len, uint8_t *dev, printcfg_t *cfg) {
  hexstatus_t rc = HEXSTAT_SUCCESS;
  char * buf2;
  uint8_t len2;

  buf2 = buf;
  len2 = len;
  do {
    buf = buf2;
    len = len2;
    split_cmd(&buf, &len, &buf2, &len2);
    rc = prn_exec_cmd(buf, len, dev, cfg);
  } while(rc == HEXSTAT_SUCCESS && len2);
  return rc;
}

static inline void prn_write_cmd(pab_t *pab, uint8_t *dev, printcfg_t *cfg) {
  hexstatus_t rc = HEXSTAT_SUCCESS;

  debug_puts_P("Exec Printer Command\r\n");

  rc = hex_write_cmd_helper(pab->datalen);
  if(rc != HEXSTAT_SUCCESS) {
    return;
  }
  rc = prn_exec_cmds((char *)buffer, pab->datalen, dev, cfg);
  hex_send_final_response( rc );
}



/*
   prn_open() -
   "opens" the Serial.object for use as a printer at device code 12 (default PC-324 printer).
*/
static void prn_open(pab_t *pab) {
  uint16_t len = 0;
  char *buf;
  uint8_t blen;
  hexstatus_t  rc = HEXSTAT_SUCCESS;
  uint8_t  att = 0;

  debug_puts_P("Open Printer\r\n");

  rc = hex_open_helper(pab, HEXSTAT_TOO_LONG, &len, &att);
  if(rc != HEXSTAT_SUCCESS)
    return;

  blen = pab->datalen - 3;
  buf = (char *)(buffer + 3);
  trim(&buf, &blen);

  if(pab->lun == LUN_CMD) {
    // we should check att, as it should be WRITE or UPDATE
    if(blen)
      rc = prn_exec_cmds(buf, blen, &(_config.prn_dev), &(_config.prn));
    hex_finish_open(BUFSIZE, rc);
    return;
  }

  if ( !_prn_open ) {
    if ( att != OPENMODE_WRITE ) {
      rc = HEXSTAT_OPTION_ERR;
    }
  } else {
    rc = HEXSTAT_ALREADY_OPEN;
  }
  if(rc == HEXSTAT_SUCCESS ) {
    _cfg.line = _config.prn.line;
    _cfg.spacing = _config.prn.spacing;
    if(blen)
      rc = prn_exec_cmds(buf, blen, NULL, &_cfg);
    _prn_open = TRUE;  // our printer is NOW officially open.
    len = len ? len : BUFSIZE;
  }
  hex_finish_open(len, rc);
}


/*
   prn_close() -
   closes printer at device 12 for use from host.
*/
static void prn_close(pab_t *pab) {
  hexstatus_t rc = HEXSTAT_SUCCESS;

  debug_puts_P("Close Printer\r\n");

  if (pab->lun == LUN_CMD) {
    // handle command channel close
    hex_close_cmd();
    return;
  }

  if ( !_prn_open ) {
    rc = HEXSTAT_NOT_OPEN;
  }
  _prn_open = FALSE;      // mark printer closed regardless.
  // send 0000 response with appropriate status code.
  hex_send_final_response( rc );
}


static void prn_read(pab_t *pab) {

  debug_puts_P("Read Printer Status\r\n");

  if(pab->lun == LUN_CMD) {
    hex_read_status();
  } else {
    // TODO I don't think this is needed here.  
  	//if ( !hex_is_bav() ) {
    //  rc = (hex_send_word( 0 ) == HEXERR_SUCCESS ? HEXSTAT_SUCCESS : HEXSTAT_DATA_ERR);
    //}
    // normally you cannot get here, but just in case.
    hex_send_final_response( HEXSTAT_INPUT_MODE_ERR );
  }
}


/*
    prn_write() -
    write data to serial port when printer is open.
*/
static void prn_write(pab_t *pab) {
  uint16_t len;
  uint16_t i;
  uint8_t  written = 0;
  hexstatus_t  rc = HEXSTAT_SUCCESS;

  debug_puts_P("Write Printer\r\n");

  len = pab->datalen;

  if(pab->lun == LUN_CMD) {
    // handle command channel
    prn_write_cmd(pab, &(_config.prn_dev), &(_config.prn));
    return;
  }

  if(_prn_open) {
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
      if ( rc == HEXSTAT_SUCCESS && _prn_open ) {
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
  if ( written && _prn_open && _cfg.line) {
    for(uint8_t n = 0; n < _cfg.spacing; n++) {
      swuart_putcrlf(0);
    }
    swuart_flush();
  }

  /*
     send response and finish operation
  */
  hex_send_final_response( rc );
}


static void prn_reset_dev( __attribute__((unused)) pab_t *pab) {
  
  prn_reset();
  // release the bus ignoring any further action on bus. no response sent.
  hex_finish();
  // wait here while bav is low
  while ( !hex_is_bav() ) {
    ;
  }
}


void prn_reset( void ) {
  _prn_open = 0; // make sure our printer is closed.
}

/*
 * Command handling registry for device
 */
static const cmd_op_t ops[] PROGMEM = {
                                        {HEXCMD_OPEN,            prn_open},
                                        {HEXCMD_CLOSE,           prn_close},
                                        {HEXCMD_WRITE,           prn_write},
                                        {HEXCMD_READ,            prn_read},
                                        {HEXCMD_RESET_BUS,       prn_reset_dev},
                                        {HEXCMD_INVALID_MARKER,  NULL}
                                      };

static uint8_t is_cfg_valid(void) {
  return (_config.valid && _config.prn_dev >= DEV_PRN_START && _config.prn_dev <= DEV_PRN_END);
}


void prn_register(void) {
  uint8_t prn_dev = DEV_PRN_DEFAULT;

  if(is_cfg_valid()) {
    prn_dev = _config.prn_dev;
  }
  reg_add(DEV_PRN_START, prn_dev, DEV_PRN_END, ops);
}


uint8_t prn_is_open(void) {
  return (_prn_open != FALSE);
}


void prn_init( void ) {

  _prn_open = FALSE;
  swuart_init();
  swuart_setrate(0, SB115200);
  prn_register();
  if(!is_cfg_valid()) {
    _config.prn.line = TRUE;
    _config.prn.spacing = 1;
  }
}
#endif
