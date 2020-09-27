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

    serial.cpp: simple SoftwareSerial device functions.
*/

#include <stdlib.h>
#include <string.h>
#include <avr/pgmspace.h>

#include "config.h"
#include "configure.h"
#include "debug.h"
#include "hexbus.h"
#include "hexops.h"
#include "registry.h"
#include "timer.h"
#include "uart.h"
#include "serial.h"

#ifdef INCLUDE_SERIAL

// Global defines
volatile uint8_t  ser_open = 0;
static serialcfg_t _cfg;

#ifdef USE_CMD_LUN

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

static const action_t cmds[] = {
                                  {SER_CMD_BPS,"b"},
                                  {SER_CMD_BPS,".ba"},
                                  {SER_CMD_LEN,"d"},
                                  {SER_CMD_LEN,".da"},
                                  {SER_CMD_PARITY,"p"},
                                  {SER_CMD_PARITY,".pa"},
                                  {SER_CMD_PARCHK,"c"},
                                  {SER_CMD_NULLS,"n"},
                                  {SER_CMD_STOP,"s"},
                                  {SER_CMD_ECHO,"e"},
                                  {SER_CMD_LINE,"r"},
                                  {SER_CMD_TRANSFER,"t"},
                                  {SER_CMD_OVERRUN,"o"},
                                  {SER_CMD_NONE,""}
                                };
static const action_t ti_cmds[] = {
                                  {SER_CMD_TW,"tw"},
                                  {SER_CMD_NU,"nu"},
                                  {SER_CMD_CH,"ch"},
                                  {SER_CMD_EC,"ec"},
                                  {SER_CMD_CR,"cr"},
                                  {SER_CMD_LF,"lf"},
                                  {SER_CMD_NONE,""}
                                };

static inline hexstatus_t ser_exec_cmd(char* buf, uint8_t len, serialcfg_t *cfg) {
  hexstatus_t rc = HEXSTAT_SUCCESS;
  sercmd_t cmd;
  uint8_t i = 0;
  char* buf2;
  uint8_t len2;
  uint32_t value;

  // path, trimmed whitespaces
  trim(&buf, &len);

  cmd = (sercmd_t) parse_equate(cmds, &buf, &len, &buf2, &len2);
  if(cmd != SER_CMD_NONE) {
    //skip spaces
    trim (&buf2, &len2);
  }
  switch (cmd) {
  case SER_CMD_BPS:
    if(!parse_number(buf2, &i, len2, 6, &value)) {
      cfg->bpsrate = value;
      // set bps rate
    } else
      rc = HEXSTAT_DATA_ERR;
    break;
  case SER_CMD_LEN:
    if(!parse_number(buf2, &i, len2, 1, &value)) {
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
    } else
      rc = HEXSTAT_DATA_ERR;
    break;
  case SER_CMD_PARITY:
    switch(lower(buf2[0])) {
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
    switch(lower(buf2[0])) {
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
    if(!parse_number(buf2, &i, len2, 2, &value)) {
      cfg->nulls = (uint8_t)value;
    }
    break;
  case SER_CMD_STOP:
    if(!parse_number(buf2, &i, len2, 2, &value)) {
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
    switch(lower(buf2[0])) {
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
    switch(lower(buf2[0])) {
    case 'n':
      cfg->line = LINE_NONE;
      break;
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
    switch(lower(buf2[0])) {
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
    switch(lower(buf2[0])) {
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
      rc = hex_exec_cmd(buf, len);
    }
  }
  return rc;
}

static inline hexstatus_t ser_exec_cmds(char* buf, uint8_t len, serialcfg_t *cfg) {
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
    rc2 = ser_exec_cmd(buf, len, cfg);
    // pick the last error.
    rc = (rc2 != HEXSTAT_SUCCESS ? rc2 : rc);
  } while(len2);
  return rc;
}


static void ser_write_cmd(pab_t *pab, serialcfg_t *cfg) {
  hexstatus_t rc = HEXSTAT_SUCCESS;

  debug_puts_P(PSTR("Exec Serial Command\n"));

  rc = hex_write_cmd_helper(pab->datalen);
  if(rc != HEXSTAT_SUCCESS) {
    return;
  }
  rc = ser_exec_cmds((char *)buffer, pab->datalen, cfg);
  if (!hex_is_bav() ) { // we can send response
    hex_send_final_response( rc );
  } else {
    hex_finish();
  }
}
#endif

 /*
   her_ser_open()
*/
static void hex_ser_open(pab_t *pab) {
  serialcfg_t cfg;
  uint16_t len;
  uint8_t  att;
  hexstatus_t rc = HEXSTAT_SUCCESS;

  debug_puts_P(PSTR("Open Serial\n"));

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
    att = buffer[ 2 ];  // tells us open for read, write or both.
  } else {
    hex_release_bus();
    return;
  }
#endif

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
    if ( !ser_open ) {
      if ( att != 0 ) {
        len = len ? len : BUFSIZE;
        if ( att & OPENMODE_UPDATE ) {
          ser_open = att; // 00 attribute = illegal.
#ifdef USE_CMD_LUN
          cfg.bpsrate = _cfg.bpsrate;
          cfg.echo = _cfg.echo;
          cfg.length = _cfg.length;
          cfg.line = _cfg.line;
          cfg.nulls = _cfg.nulls;
          cfg.overrun = _cfg.overrun;
          cfg.parchk = _cfg.parchk;
          cfg.parity = _cfg.parity;
          cfg.stopbits = _cfg.stopbits;
          cfg.xfer = _cfg.xfer;
          ser_write_cmd(pab, &cfg);
          uart_config(CALC_BPS(cfg.bpsrate), cfg.length, cfg.parity, cfg.stopbits);
#else
          uart_config(CALC_BPS(baud), UART_LENGTH_8, UART_PARITY_NONE, UART_STOP_1);
#endif
          transmit_word( 4 );
          transmit_word( len );
          transmit_word( 0 );
          transmit_byte( HEXSTAT_SUCCESS );
          hex_finish();
          return;
        } else {
          rc = HEXSTAT_APPEND_MODE_ERR;
        }
      } else {
        rc = HEXSTAT_ATTR_ERR;
      }
    } else {
      rc = HEXSTAT_ALREADY_OPEN;
    }
    hex_send_final_response( rc );
  } else
    hex_finish();
}


/*
   hex_ser_close()
*/
static void hex_ser_close(pab_t *pab __attribute__((unused))) {
  hexstatus_t rc = HEXSTAT_SUCCESS;

#ifdef USE_CMD_LUN
  if (pab->lun == LUN_CMD) {
    // handle command channel close
    hex_close_cmd();
    return;
  }
#endif

  if ( ser_open ) {
    ser_reset();
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }
  if (!hex_is_bav() ) { // we can send response
    hex_send_final_response( rc );
    return;
  } else
    hex_finish();
}


static void hex_ser_read(pab_t *pab) {
  uint16_t len = pab->buflen;
  uint16_t bcount = 0;
  hexstatus_t  rc = HEXSTAT_SUCCESS;

  debug_puts_P(PSTR("Read Serial\n"));

  if ( ser_open ) {
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

  if ( !hex_is_bav() ) {
    if ( ser_open & OPENMODE_READ ) {
      // send how much we are going to send
      rc = (transmit_word( bcount ) == HEXERR_SUCCESS ? HEXSTAT_SUCCESS : HEXSTAT_DATA_ERR);

      // while we have data remaining to send.
      while ( bcount && rc == HEXSTAT_SUCCESS ) {

        len = bcount;    // remaing amount to read from file
        // while it fit into buffer or not?  Only read as much
        // as we can hold in our buffer.
        len = ( len > BUFSIZE ) ? BUFSIZE : len;

        bcount -= len;
        while ( len-- && rc == HEXSTAT_SUCCESS ) {
          rc = (transmit_byte( uart_getc() ) == HEXERR_SUCCESS ? HEXSTAT_SUCCESS : HEXSTAT_DATA_ERR);
        }
      }
      if ( rc == HEXSTAT_SUCCESS ) {
        transmit_byte( rc );
      }
      hex_finish();
    } else if ( ser_open ) {
      // open for OUTPUT only?
      hex_send_final_response( HEXSTAT_INPUT_MODE_ERR );
    } else {
      // not open at all?
      hex_send_final_response( HEXSTAT_NOT_OPEN );
    }
  } else
    hex_finish();
}


static void hex_ser_write(pab_t *pab) {
  uint16_t len;
  uint16_t i;
  uint16_t j;
  hexstatus_t  rc = HEXSTAT_SUCCESS;

#ifdef USE_CMD_LUN
  if(pab->lun == LUN_CMD) {
    // handle command channel
    ser_write_cmd(pab, &_cfg);
    return;
  }
#endif

  debug_puts_P(PSTR("Write Serial\n"));

  len = pab->datalen;
  if ( ser_open & OPENMODE_WRITE ) {
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
  } else if ( ser_open ) {
    rc = HEXSTAT_OUTPUT_MODE_ERR;
  } else {
    rc = HEXSTAT_NOT_OPEN;
  }

  if ( len ) {
    hex_eat_it( len, rc );
    return;
  }

  if (!hex_is_bav() ) { // we can send response
    hex_send_final_response( rc );
  } else
    hex_finish();
}


static void hex_ser_rtn_sta(pab_t *pab __attribute__((unused))) {
  // TBD
}


static void hex_ser_set_opts(pab_t *pab __attribute__((unused))) {
  // TBD
}


static void hex_ser_reset(pab_t *pab __attribute__((unused))) {

  ser_reset();
  // release the bus ignoring any further action on bus. no response sent.
  hex_finish();
  // wait here while bav is low
  while ( !hex_is_bav() ) {
    ;
  }
}


void ser_reset(void) {
  if ( ser_open ) {
    ser_open = FALSE;
  }
  return;
}


/*
   Command handling registry for device
*/

#ifdef USE_NEW_OPTABLE
static const cmd_op_t ops[] PROGMEM = {
                                        {HEXCMD_OPEN,            hex_ser_open},
                                        {HEXCMD_CLOSE,           hex_ser_close},
                                        {HEXCMD_READ,            hex_ser_read},
                                        {HEXCMD_WRITE,           hex_ser_write},
                                        {HEXCMD_RETURN_STATUS,   hex_ser_rtn_sta},
                                        {HEXCMD_SET_OPTIONS,     hex_ser_set_opts},
                                        {HEXCMD_RESET_BUS,       hex_ser_reset},
                                        {(hexcmdtype_t)HEXCMD_INVALID_MARKER,  NULL}
                                      };
#else
static const cmd_proc fn_table[] PROGMEM = {
  hex_ser_open,
  hex_ser_close,
  hex_ser_read,
  hex_ser_write,
  hex_ser_rtn_sta,
  hex_ser_set_opts,
  hex_ser_reset,
  NULL // end of table.
};

static const uint8_t op_table[] PROGMEM = {
  HEXCMD_OPEN,
  HEXCMD_CLOSE,
  HEXCMD_READ,
  HEXCMD_WRITE,
  HEXCMD_RETURN_STATUS,
  HEXCMD_SET_OPTIONS,
  HEXCMD_RESET_BUS,
  HEXCMD_INVALID_MARKER
};
#endif

void ser_register(void) {
#ifdef NEW_REGISTER
#ifdef USE_NEW_OPTABLE
  cfg_register(DEV_SER_START, DEV_SER_DEFAULT, DEV_SER_END, ops);
#else
  cfg_register(DEV_SER_START, DEV_SER_DEFAULT, DEV_SER_END, op_table, fn_table);
#endif
#else
  uint8_t i = registry.num_devices;

  registry.num_devices++;
  registry.entry[ i ].dev_low = DEV_SER_START;
  registry.entry[ i ].dev_cur = DEV_SER_DEFAULT;
  registry.entry[ i ].dev_high = DEV_SER_END; // support 20, 21, 22, 23 as device codes
#ifdef USE_NEW_OPTABLE
  registry.entry[ i ].oplist = (cmd_op_t *)ops;
#else
  registry.entry[ i ].operation = (cmd_proc *)fn_table;
  registry.entry[ i ].command = (uint8_t *)op_table;
#endif
#endif
}


void ser_init(void) {
  ser_open = FALSE;
#ifdef INIT_COMBO
  ser_register();
#endif
  _cfg.bpsrate = 300;
  _cfg.echo = TRUE;  // TODO see exactly what this means.
  _cfg.length = LENGTH_7;
  _cfg.line = LINE_CRLF;
  _cfg.nulls = 0;
  _cfg.overrun = _cfg.overrun;
  _cfg.parchk = FALSE;
  _cfg.parity = PARITY_ODD;
  _cfg.stopbits = STOP_0;
  _cfg.xfer = XFER_REC;
  // get updated values from EEPROM...

}
#endif
