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

    hexbus.h: TI Hex Bus definitions and low level functions
*/

#ifndef SRC_HEXBUS_H_
#define SRC_HEXBUS_H_
#ifdef __cplusplus
extern "C"{
#endif

typedef enum _hexcmdtype_t {
               HEXCMD_OPEN = 0,
               HEXCMD_CLOSE,
               HEXCMD_DELETE_OPEN,
               HEXCMD_READ,
               HEXCMD_WRITE,
               HEXCMD_RESTORE,
               HEXCMD_DELETE,
               HEXCMD_RETURN_STATUS,
               HEXCMD_SVC_REQ_ENABLE,
               HEXCMD_SVC_REQ_DISABLE,
               HEXCMD_SVC_REQ_POLL,
               HEXCMD_MASTER,
               HEXCMD_VERIFY,
               HEXCMD_FORMAT,
               HEXCMD_CATALOG,
               HEXCMD_SET_OPTIONS,
               HEXCMD_XMIT_BREAK,
               HEXCMD_WP_FILE,
               HEXCMD_READ_SECTORS,
               HEXCMD_WRITE_SECTORS,
               HEXCMD_RENAME_FILE,
               HEXCMD_READ_FD,
               HEXCMD_WRITE_FD,
               HEXCMD_READ_FILE_SECTORS,
               HEXCMD_WRITE_FILE_SECTORS,
               HEXCMD_LOAD,
               HEXCMD_SAVE,
               HEXCMD_INQ_SAVE,
               HEXCMD_HOME_COMP_STATUS,
               HEXCMD_HOME_COMP_VERIFY,
               HEXCMD_NULL = 0xfe,
               HEXCMD_RESET_BUS
            } hexcmdtype_t;

typedef enum _hexstatus_t {
              HEXSTAT_SUCCESS = 0,
              HEXSTAT_OPTION_ERR,
              HEXSTAT_ATTR_ERR,
              HEXSTAT_NOT_FOUND,
              HEXSTAT_NOT_OPEN,
              HEXSTAT_ALREADY_OPEN,
              HEXSTAT_DEVICE_ERR,
              HEXSTAT_EOF,
              HEXSTAT_TOO_LONG,
              HEXSTAT_WP_ERR,
              HEXSTAT_NOT_REQUEST,
              HEXSTAT_DIR_FULL,
              HEXSTAT_BUF_SIZE_ERR,
              HEXSTAT_UNSUPP_CMD,
              HEXSTAT_NOT_WRITE,
              HEXSTAT_NOT_READ,
              HEXSTAT_DATA_ERR,
              HEXSTAT_FILE_TYPE_ERR,
              HEXSTAT_FILE_PROT_ERR,
              HEXSTAT_APPEND_MODE_ERR,
              HEXSTAT_OUTPUT_MODE_ERR,
              HEXSTAT_INPUT_MODE_ERR,
              HEXSTAT_UPDATE_MODE_ERR,
              HEXSTAT_FILE_TYPE_INT_DISP_ERR,
              HEXSTAT_VERIFY_ERR,
              HEXSTAT_BATT_LOW,
              HEXSTAT_UNFORMATTED,
              HEXSTAT_BUS_ERR,
              HEXSTAT_DEL_PROTECT,
              HEXSTAT_CART_NOT_PRESENT,
              HEXSTAT_RESTORE_NOT_ALLOWED,
              HEXSTAT_FILE_NAME_INVALID,
              HEXSTAT_MEDIA_FULL,
              HEXSTAT_MAX_LUNS,
              HEXSTAT_DATA_INVALID,
              HEXSTAT_ILLEGAL_SLAVE = 0xfe,
              HEXSTAT_TIMEOUT
            } hexstatus_t;

typedef enum _hexerror_t {
              HEXERR_SUCCESS = 0,
              HEXERR_BAV = -1,
              HEXERR_TIMEOUT = -2
            } hexerror_t;

typedef enum _openmode_t {
              OPENMODE_READ =     0x40,
              OPENMODE_WRITE =    0x80,
              OPENMODE_RELATIVE = 0x20,
              OPENMODE_FIXED =    0x10,
              OPENMODE_INTERNAL = 0x08
            } openmode_t ;

uint8_t hex_is_bav(void);
void hex_release_data( void );
void hex_release_bus(void);
uint8_t receive_byte( uint8_t *inout);
uint8_t transmit_byte( uint8_t xmit );
uint8_t transmit_word( uint16_t value );
void hex_finish( void );
void hex_send_size_response( uint16_t len );
void hex_send_final_response( uint8_t rc );
void hex_init(void);
#ifdef __cplusplus
} // extern "C"
#endif

#endif /* SRC_HEXBUS_H_ */
