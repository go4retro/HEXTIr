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

    serial.cpp: simple RS232 device functions.
*/

#include <stdlib.h>
#include <string.h>
#include <avr/pgmspace.h>

#include "config.h"
#include "debug.h"
#include "eeprom.h"
#include "hexbus.h"
#include "hexops.h"
#include "registry.h"
#include "timer.h"
#include "uart.h"
#include "serial.h"

#ifdef INCLUDE_SERIAL

// Global defines
volatile uint8_t  _ser_open = FALSE;
static serialcfg_t _cfg;

typedef enum _sercmd_t {
                          SER_CMD_NONE = 0,
                          SER_CMD_BPS,
                          SER_CMD_LEN,
                          SER_CMD_PARITY,
                          SER_CMD_PARCHK,
                          SER_CMD_NULLS,
                          SER_CMD_STOP,
                          SER_CMD_ECHO,
                          SER_CMD_LINE,
                          SER_CMD_TRANSFER,
                          SER_CMD_OVERRUN,
                          SER_CMD_TW,
                          SER_CMD_NU,
                          SER_CMD_CH,
                          SER_CMD_EC,
                          SER_CMD_CR,
                          SER_CMD_LF
} sercmd_t;

static const action_t cmds[] PROGMEM = {
                                        {SER_CMD_BPS,       "b"},
                                        {SER_CMD_BPS,       ".ba"},
                                        {SER_CMD_LEN,       "d"},
                                        {SER_CMD_LEN,       ".da"},
                                        {SER_CMD_PARITY,    "p"},
                                        {SER_CMD_PARITY,    ".pa"},
                                        {SER_CMD_PARCHK,    "c"},
                                        {SER_CMD_NULLS,     "n"},
                                        {SER_CMD_STOP,      "s"},
                                        {SER_CMD_ECHO,      "e"},
                                        {SER_CMD_LINE,      "r"},
                                        {SER_CMD_TRANSFER,  "t"},
                                        {SER_CMD_OVERRUN,   "o"},
                                        {SER_CMD_NONE,      ""}
                                       };
static const action_t ti_cmds[] PROGMEM = {
                                            {SER_CMD_TW,    ".tw"},
                                            {SER_CMD_NU,    ".nu"},
                                            {SER_CMD_CH,    ".ch"},
                                            {SER_CMD_EC,    ".ec"},
                                            {SER_CMD_CR,    ".cr"},
                                            {SER_CMD_LF,    ".lf"},
                                            {SER_CMD_NONE,  ""}
                                          };

static inline hexstatus_t ser_exec_cmd(char* buf, uint8_t len, uint8_t *dev, serialcfg_t *cfg) {
  hexstatus_t rc = HEXSTAT_SUCCESS;
  sercmd_t cmd;
  uint32_t value;

  // path, trimmed whitespaces
  trim(&buf, &len);

  cmd = (sercmd_t) parse_equate(cmds, &buf, &len);
  if(cmd != SER_CMD_NONE) {
    //skip spaces
    trim (&buf, &len);
  }
  switch (cmd) {
  case SER_CMD_BPS:
    if(!parse_number(&buf, &len, 6, &value)) {
      cfg->bpsrate = value;
      // set bps rate
    } else {
      rc = HEXSTAT_DATA_ERR;
    }
    break;
  case SER_CMD_LEN:
    if(!parse_number(&buf, &len, 1, &value)) {
      switch((uint8_t)value) {
      case 5:
        cfg->length = LENGTH_5;
        break;
      case 6:
        cfg->length = LENGTH_6;
        break;
      case 7:
        cfg->length = LENGTH_7;
        break;
      case 8:
        cfg->length = LENGTH_8;
        break;
      default:
        rc = HEXSTAT_DATA_ERR;
        break;
      }
    } else {
      rc = HEXSTAT_DATA_ERR;
    }
    break;
  case SER_CMD_PARITY:
    switch(lower(buf[0])) {
    case 'o':
      cfg->parity = PARITY_ODD;
      break;
    case 'e':
      cfg->parity = PARITY_EVEN;
      break;
    case 'n':
      cfg->parity = PARITY_NONE;
      break;
    case 's':
      // TODO What is space parity?
      break;
    case 'm':
      // TODO What is MARK parity?
      break;
    default:
      rc = HEXSTAT_DATA_ERR;
      break;
    }
    break;
  case SER_CMD_PARCHK:
    switch(lower(buf[0])) {
    case 'y':
    case '1':
      cfg->parchk = TRUE;
      break;
    case 'n':
    case '0':
      cfg->parchk = FALSE;
      break;
    default:
      rc = HEXSTAT_DATA_ERR;
      break;
    }
    break;
  case SER_CMD_NULLS:
    if(!parse_number(&buf, &len, 2, &value)) {
      cfg->nulls = (uint8_t)value;
    }
    break;
  case SER_CMD_STOP:
    if(!parse_number(&buf, &len, 2, &value)) {
      switch((uint8_t)value) {
      case 1:
        cfg->stopbits = STOP_0;
        break;
      case 2:
        cfg->stopbits = STOP_1;
        break;
      default:
        rc = HEXSTAT_DATA_ERR;
        break;
      }
     }
    break;
  case SER_CMD_ECHO:
    switch(lower(buf[0])) {
    case 'y':
    case '1':
      cfg->echo = TRUE;
      break;
    case 'n':
    case '0':
      cfg->echo = FALSE;
      break;
    default:
      rc = HEXSTAT_DATA_ERR;
      break;
    }
    break;
  case SER_CMD_CR:
    switch(lower(buf[0])) {
    case 'n':
      cfg->line = LINE_NONE;
      break;
    case 'c':
      cfg->line = LINE_CR;
      break;
    case 'l':
      cfg->line = LINE_CRLF;
      break;
    default:
      rc = HEXSTAT_DATA_ERR;
      break;
    }
    break;
  case SER_CMD_TRANSFER:
    switch(lower(buf[0])) {
    case 'r':
      cfg->xfer = XFER_REC;
      break;
    case 'c':
      cfg->xfer = XFER_CHAR;
      break;
    case 'w':
      cfg->xfer = XFER_CHARWAIT;
      break;
    default:
      rc = HEXSTAT_DATA_ERR;
      break;
    }
    break;
  case SER_CMD_OVERRUN:
    switch(lower(buf[0])) {
    case 'y':
    case '1':
      cfg->overrun = TRUE;
      break;
    case 'n':
    case '0':
      cfg->overrun = FALSE;
      break;
    default:
      rc = HEXSTAT_DATA_ERR;
      break;
    }
    break;
  default:
    cmd = (sercmd_t) parse_cmd(ti_cmds, &buf, &len);
    switch (cmd) {
    case SER_CMD_TW:
      cfg->stopbits = STOP_1;
      break;
    case SER_CMD_NU:
      cfg->length = LENGTH_6;
      break;
    case SER_CMD_CH:
      cfg->parchk = TRUE;
      break;
    case SER_CMD_EC:
      cfg->echo = FALSE;
      break;
    case SER_CMD_CR:
      cfg->line = LINE_NONE; // seems wrong
      break;
    case SER_CMD_LF:
      cfg->line = LINE_CR; // Seems wrong
      break;
    default:
      rc = hex_exec_cmd(buf, len, dev);
    }
  }
  return rc;
}

static inline hexstatus_t ser_exec_cmds(char* buf, uint8_t len, uint8_t *dev, serialcfg_t *cfg) {
  hexstatus_t rc = HEXSTAT_SUCCESS;
  char * buf2;
  uint8_t len2;

  buf2 = buf;
  len2 = len;
  do {
    buf = buf2;
    len = len2;
    split_cmd(&buf, &len, &buf2, &len2);
    rc = ser_exec_cmd(buf, len, dev, cfg);
  } while(rc == HEXSTAT_SUCCESS && len2);
  return rc;
}

static inline void ser_write_cmd(pab_t *pab, uint8_t * dev, serialcfg_t *cfg) {
  hexstatus_t rc = HEXSTAT_SUCCESS;

  debug_puts_P("Exec Serial Command\r\n");

  rc = hex_write_cmd_helper(pab->datalen);
  if(rc != HEXSTAT_SUCCESS) {
    return;
  }
  rc = ser_exec_cmds((char *)buffer, pab->datalen, dev, cfg );
  hex_send_final_response( rc );
}

 /*
   ser_open()
*/
static void ser_open(pab_t *pab) {
  uint16_t len;
  char *buf;
  uint8_t blen;
  uint8_t  att;
  hexstatus_t rc = HEXSTAT_SUCCESS;

  debug_puts_P("Open Serial\r\n");

  rc = hex_open_helper(pab, HEXSTAT_TOO_LONG, &len, &att);
  if(rc != HEXSTAT_SUCCESS)
    return;

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

  blen = pab->datalen - 3;
  buf = (char *)(buffer + 3);
  trim(&buf, &blen);

  if(pab->lun == LUN_CMD) {
    // we should check att, as it should be WRITE or UPDATE
    if(blen)
      rc = ser_exec_cmds(buf, blen, &(_config.ser_dev), &(_config.ser));
    hex_finish_open(BUFSIZE, rc);
    return;
  }

  // we used to quit if serial was open, but that's not a valid error from open.
  if ( att != 0 ) {
    len = len ? len : BUFSIZE;
    if ( att & OPENMODE_UPDATE ) {
      _ser_open = att; // 00 attribute = illegal.
      _cfg.bpsrate = _config.ser.bpsrate;
      _cfg.echo = _config.ser.echo;
      _cfg.length = _config.ser.length;
      _cfg.line = _config.ser.line;
      _cfg.nulls = _config.ser.nulls;
      _cfg.overrun = _config.ser.overrun;
      _cfg.parchk = _config.ser.parchk;
      _cfg.parity = _config.ser.parity;
      _cfg.stopbits = _config.ser.stopbits;
      _cfg.xfer = _config.ser.xfer;
      if(blen)
        rc = ser_exec_cmds(buf, blen, NULL, &_cfg);
      uart_config(CALC_BPS(_cfg.bpsrate), _cfg.length, _cfg.parity, _cfg.stopbits);
    } else {
      rc = HEXSTAT_APPEND_MODE_ERR;
    }
  } else {
    rc = HEXSTAT_ATTR_ERR;
  }

  hex_finish_open(len, rc);
}


/*
   ser_close()
*/
static void ser_close(pab_t *pab) {
  hexstatus_t rc = HEXSTAT_SUCCESS;

  debug_puts_P("Close Serial\r\n");

  if (pab->lun == LUN_CMD) {
    // handle command channel close
    hex_close_cmd();
    return;
  }

  if ( _ser_open ) {
    ser_reset();
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }
  hex_send_final_response( rc );
}


static void ser_read(pab_t *pab) {
  uint16_t len = pab->buflen;
  uint16_t bcount = 0;
  hexstatus_t  rc = HEXSTAT_SUCCESS;

  debug_puts_P("Read Serial\r\n");

  if(pab->lun == LUN_CMD) {
    hex_read_status();
    return;
  }

  if ( _ser_open ) {
    // protect access via ser_open since serial_peripheral is not present
    // if ser_open = 0.
//#ifdef ARDUINO
//    bcount = serial_peripheral.available();
//#else
    bcount = (uart0_data_available() ? 1 : 0); // TODO  need to implement true count
//#endif
    if ( bcount > pab->buflen ) {
      bcount = pab->buflen;
    }
  }

  if ( _ser_open & OPENMODE_READ ) {
    if ( !hex_is_bav() ) {
      // send how much we are going to send
      rc = (hex_send_word( bcount ) == HEXERR_SUCCESS ? HEXSTAT_SUCCESS : HEXSTAT_DATA_ERR);

      // while we have data remaining to send.
      while ( bcount && rc == HEXSTAT_SUCCESS ) {

        len = bcount;    // remaing amount to read from file
        // while it fit into buffer or not?  Only read as much
        // as we can hold in our buffer.
        len = ( len > BUFSIZE ) ? BUFSIZE : len;

        bcount -= len;
        while ( len-- && rc == HEXSTAT_SUCCESS ) {
          rc = (hex_send_byte( uart_getc() ) == HEXERR_SUCCESS ? HEXSTAT_SUCCESS : HEXSTAT_DATA_ERR);
        }
      }
      if ( rc == HEXSTAT_SUCCESS ) {
        hex_send_byte( rc );
      }
      hex_finish();
    }
  } else if ( _ser_open ) {
    // open for OUTPUT only?
    hex_send_final_response( HEXSTAT_INPUT_MODE_ERR );
  } else {
    // not open at all?
    hex_send_final_response( HEXSTAT_NOT_OPEN );
  }
}


static void ser_write(pab_t *pab) {
  uint16_t len;
  uint16_t i;
  uint16_t j;
  hexstatus_t  rc = HEXSTAT_SUCCESS;


  debug_puts_P("Write Serial\r\n");

  len = pab->datalen;

  if(pab->lun == LUN_CMD) {
    // handle command channel
    ser_write_cmd(pab, &(_config.ser_dev), &(_config.ser));
    return;
  }

  if ( _ser_open & OPENMODE_WRITE ) {
    while (len && rc == HEXSTAT_SUCCESS ) {
      i = (len >= BUFSIZE ? BUFSIZE : len);
      rc = hex_get_data(buffer, i);
      if (rc == HEXSTAT_SUCCESS) {
        j  = 0;
        while (j < len) {
          uart_putc(buffer[j++]);
        }
      }
      len -= i;
    }
  } else if ( _ser_open ) {
    rc = HEXSTAT_OUTPUT_MODE_ERR;
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }

  if ( len ) {
    hex_eat_it( len, rc );
    return;
  }

  hex_send_final_response( rc );
}


static void ser_rtn_sta(pab_t *pab __attribute__((unused))) {
  // TBD
}


static void ser_set_opts(pab_t *pab __attribute__((unused))) {
  // TBD
}


static void ser_reset_dev(pab_t *pab __attribute__((unused))) {

  ser_reset();
  // release the bus ignoring any further action on bus. no response sent.
  hex_finish();
  // wait here while bav is low
  while ( !hex_is_bav() ) {
    ;
  }
}


void ser_reset(void) {
  if ( _ser_open ) {
    _ser_open = FALSE;
  }
  return;
}


/*
   Command handling registry for device
*/

static const cmd_op_t ops[] PROGMEM = {
                                        {HEXCMD_OPEN,            ser_open},
                                        {HEXCMD_CLOSE,           ser_close},
                                        {HEXCMD_READ,            ser_read},
                                        {HEXCMD_WRITE,           ser_write},
                                        {HEXCMD_RETURN_STATUS,   ser_rtn_sta},
                                        {HEXCMD_SET_OPTIONS,     ser_set_opts},
                                        {HEXCMD_RESET_BUS,       ser_reset_dev},
                                        {(hexcmdtype_t)HEXCMD_INVALID_MARKER,  NULL}
                                      };

static uint8_t is_cfg_valid(void) {
  return (_config.valid && _config.ser_dev >= DEV_SER_START && _config.ser_dev <= DEV_SER_END);
}


void ser_register(void) {
  uint8_t ser_dev = DEV_SER_DEFAULT;

  if(is_cfg_valid()) {
    ser_dev = _config.ser_dev;
  }
  reg_add(DEV_SER_START, ser_dev, DEV_SER_END, ops);
}


uint8_t ser_is_open(void) {
  return (_ser_open != FALSE);
}


void ser_init(void) {
  uart_init();
  _ser_open = FALSE;
  ser_register();
  if(!is_cfg_valid()) {
    _config.ser.bpsrate = 300;
    _config.ser.echo = TRUE;  // TODO see exactly what this means.
    _config.ser.length = LENGTH_7;
    _config.ser.line = LINE_CRLF;
    _config.ser.nulls = 0;
    _config.ser.overrun = _cfg.overrun;
    _config.ser.parchk = FALSE;
    _config.ser.parity = PARITY_ODD;
    _config.ser.stopbits = STOP_0;
    _config.ser.xfer = XFER_REC;
  }
}
#endif
