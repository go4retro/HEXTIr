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

    hexbus.c: Routines to support the Texas Instruments HexBus protocol
*/

#include <util/delay.h>
#include "config.h"
#include "hexbus.h"
#include "integer.h"
#include "uart.h"

/*
   hex_is_bav() -
     read BAV value
     return 0 for BAV low (bus in use)
     return NOT 0 for BAV (bus available)
*/
uint8_t hex_is_bav(void) {
  return HEX_BAV_IN & HEX_BAV_PIN;
}

/*
    hex_bav_hi() -
    release peripheral side BAV to HI-Z
    the 2 us delay accounts for signal rise time w/o
    the normal 8.2k ohm pullup resistor.
*/
static void hex_bav_hi(void) {
  HEX_BAV_DDR &= ~HEX_BAV_PIN;   // HI-Z BAV
  HEX_BAV_OUT |= HEX_BAV_PIN;    // Bring pullups online
  _delay_us(2);                  // Allow signal to reach HI-Z (need delay since we do not monitor this)
}

/*
   hex_bav_lo() -
   drive peripheral side BAV to logic 0 (bus in use) and hold
*/
static void hex_bav_lo(void) {
  HEX_BAV_OUT &= ~HEX_BAV_PIN;  // BAV output latch to zero
  HEX_BAV_DDR |= HEX_BAV_PIN;   // drive BAV direction output
}

/*
   hex_hsk_hi() -
   release peripheral side HSK to HI-Z
   the 2 us delay accounts for signal rise time w/o
   the normal 8.2k ohm pullup resistor.
*/
static void hex_hsk_hi(void) {
  HEX_HSK_DDR &= ~HEX_HSK_PIN;  // HI-Z HSK
  HEX_HSK_OUT |= HEX_HSK_PIN;   // bring pullups online
}

/*
   hex_hsk_lo() -
   drive peripheral side HSK to logic 0 (data valid) and hold
*/
static void hex_hsk_lo(void) {
  HEX_HSK_OUT &= ~HEX_HSK_PIN;  // HSK output latch to zero
  HEX_HSK_DDR |= HEX_HSK_PIN;   // drive HSK direction output
  asm("nop");                   // As fall time is not zero,
  asm("nop");                   // give data lines just a few cycles to stabilize.
  // a few cycles is all it takes.
  return;
}

/*
   hex_is_hsk() -
   read peripheral side HSK value
   return 0 for HSK low (data valid)
   return NOT 0 for HSK high (no data, HSK released)
*/
static uint8_t hex_is_hsk(void) {
  return HEX_HSK_IN & HEX_HSK_PIN;  // Read HSK line state
}

/*
   hex_release_data() -
   set peripheral side data nibble lines to HI-Z state
   and ensure output latches are set.
*/
void hex_release_data( void ) {
  HEX_DATA_DDR &= ~HEX_DATA_PIN;   // HI-Z data lines.
  HEX_DATA_OUT |= HEX_DATA_PIN;    // ensure output latches are set.
  _delay_us(2);                    // lets line raise. need delay since we do not monitor these
  // w/o pullups of 8.2k, 2 us is enough.
  return;
}


/*
    hex_release_bus() -
    release peripheral side HSK signal and monitor
    for signal high (host side release)
    While waiting for HSK high, if BAV goes high, we've lost
    the bus, so exit under that condition as well.
*/
void hex_release_bus(void) {
  hex_hsk_hi();                 // Float HSK line
  while (!hex_is_hsk()) {       // Wait for HSK to become HI level
    if (hex_is_bav() )          // if while doing so the BAV signal is lost,
      return;                   // exit also.
    // continue to wait for HSK to be high
  }
}


/*
   hex_finish() -
   release peripheral side HSK, data lines, and BAV signal
   this is done when we have completed a full command/response
   transaction on the bus.
*/
void hex_finish( void )
{
  hex_release_bus();
  hex_release_data();
  hex_bav_hi();
}

/*
 * hex_wait_for_hsk_lo() -
 * wait while HSK is high for a low edge. 
 * If we lose BAV, return err.
 */
static hexerror_t hex_wait_for_hsk_lo( void ) {
  do
  {
    if (hex_is_bav()) {
      return HEXERR_BAV;
    }
  } while ( hex_is_hsk() );
  return HEXERR_SUCCESS;
}

/*
 * hex_capture_hsk() -
 * wait for HSK to be brought low, then hold HSK low
 * and resume.  This is done to satisfy bus timing when
 * we wake up from a lower power state by BAV being driven
 * low by host.  We must hold HSK low within 5-6 us of host
 * driving it low or we run the risk of losing the data.
 * 
 * STANDBY mode resumes system operation quickly enough that
 * we can do this.  LOW POWER mode where it takes many milliseconds
 * to resume would require additional external logic to capture
 * and hold the incoming HSK signal for us.  Without that logic,
 * we cannot use LOW POWER mode; STANDBY is the best we can do.
 */
hexerror_t hex_capture_hsk( void ) {
  if ( !hex_wait_for_hsk_lo() ) {
    hex_hsk_lo();
  }
  return HEXERR_SUCCESS;
}

/*
   receive_byte() -
   incoming byte == 0: do not check for HSK HI. (reading first byte of PAB)
   incoming byte != 0 : release HSK (reading "next" byte of incoming message)
   outgoing byte stored in parameter is the byte of data read.
   return value is status (HEX_BAVERR or HEXERR_SUCCESS)
*/

hexerror_t receive_byte( uint8_t *inout)
{
  uint8_t lsn, msn;
  
#ifdef INCLUDE_POWERMGMT
  if ( *inout == 0 ) 
    goto lowhsk;
#endif

  // reading NEXT byte of message: release HSK
  if ( *inout != 0 ) {
    hex_release_bus();
  }

  // monitor BAV (if lose BAV, abort)
  // Waiting for HSK low while BAV is low.
  if ( hex_wait_for_hsk_lo() ) {
    return HEXERR_BAV;
  }

#ifdef INCLUDE_POWERMGMT
lowhsk:  // Host has driven HSK low. Peripheral side must now hold it low.
#endif
  hex_hsk_lo();

  // Read lower 4 bits of incoming data.
  lsn = (HEX_DATA_IN & HEX_DATA_PIN);

  // Peripheral side release of HSK, wait for host to also release.
  // This manages to let host guarantee bus timing w/o local delays
  hex_release_bus();

  // wait for next host-side drive of HSK low
  do
  {
    if (hex_is_bav()) {
      return HEXERR_BAV;
    }
  } while ( hex_is_hsk() );
  // host has driven HSK low, hold it low from peripheral side.
  hex_hsk_lo();
  // read data nibble for upper 4 bits of data.
  msn = (HEX_DATA_IN & HEX_DATA_PIN); // MSN
  msn <<= 4;
  // build our response data and return success
  // We leave it held low for our next byte receipt to release.
  *inout = (msn | lsn);
  return HEXERR_SUCCESS;

}

/*
   transmit_byte() -
   send 1 byte over bus.  holds BAV lo throughout.
   when we are completed, we will externally release BAV to let
   host know we are done with response frame.

*/

hexerror_t transmit_byte( uint8_t xmit )
{
  uint8_t nibble = xmit;

  // Hold BAV low to initiate response if not already low
  hex_bav_lo();
  // release HSK to HI-Z and wait for host to release high as well
  hex_release_bus();
  // hi, wait 8 us interbyte timing as required by bus spec
  _delay_us(8);
  nibble &= 0x0f;
  // place LSN of data on output lines
  HEX_DATA_OUT = (HEX_DATA_OUT & ~HEX_DATA_PIN) | nibble;
  HEX_DATA_DDR = (HEX_DATA_OUT & ~HEX_DATA_PIN) | (~nibble & HEX_DATA_PIN);
  // signal HSK low to indicate to host that data is available
  hex_hsk_lo();  // drive low
  // hold HSK low at least 8 us for bus timing
  _delay_us(8);
  // then, release HSK to HI-Z and wait for host to release as well
  hex_release_bus();
  // go grab the MSN nibble to send
  nibble = xmit;
  nibble >>= 4;
  nibble &= 0x0f;
  // ensure we have at least 8 us high per bus spec.
  _delay_us(8);
  // place MSN of data on output lines
  HEX_DATA_OUT = (HEX_DATA_OUT & ~HEX_DATA_PIN) | nibble;
  HEX_DATA_DDR = (HEX_DATA_OUT & ~HEX_DATA_PIN) | (~nibble & HEX_DATA_PIN);
  // drive HSK low to signal data available to host system and hold it.
  hex_hsk_lo();
  // guarantee we are low at least 8 us bus timing
  _delay_us(8);
  return HEXERR_SUCCESS;
}


/*
   transmit_word() -
   use transmit_byte() to send LSB/MSB of a word over the bus.
*/
hexerror_t transmit_word( uint16_t value )
{
  hexerror_t rc;

  // Send LSB of word over bus
  rc = transmit_byte( value & 0xff );
  if ( rc == HEXERR_SUCCESS ) {
    // Send MSB of word over bus
    rc = transmit_byte( value >> 8 );
  }
  // return to caller.
  return rc;
}

/*
   hex_send_size_response() -
   send a 4 byte response with a
   * length/data field of two bytes
   * a record value (potentially meaningless) of two bytes
   * a success status response.
*/
void hex_send_size_response( uint16_t len , uint16_t record) {
  transmit_word( 4 );
  transmit_word( len );
  transmit_word( record );
  transmit_byte( HEXERR_SUCCESS );
  hex_finish();
  return;
}

/*
   hex_send_final_response() -
   used to send an error response to a bus command sent
   from the host.  Send 0 length data message and single
   byte of error code response, then release and finish
   up the bus cycle.
*/
void hex_send_final_response( hexstatus_t rc )
{
  transmit_word( 0 );
  transmit_byte( rc );
  hex_finish();
  return;
}

/*
   hex_wait_for_next_bus() -
   release HSK hi on the peripheral side, and then wait until the
   host has released BAVh high before proceeding.
*/
void hex_wait_for_next_bus( void )
{
  hex_release_bus();
  while (!hex_is_bav() )  // wait for BAV back high, ignore any traffic
    ;
  return;
}


/*
   hex_init() -
   set up initial port states for use as hexbus.
*/
void hex_init(void) {

  HEX_BAV_DDR &= ~HEX_BAV_PIN;
  HEX_BAV_OUT |= HEX_BAV_PIN;

  HEX_HSK_DDR &= ~HEX_HSK_PIN;  // bring HSK hi-Z
  HEX_HSK_OUT |= HEX_HSK_PIN;

  HEX_DATA_DDR &= ~HEX_DATA_PIN;
  HEX_DATA_OUT |= HEX_DATA_PIN;
  return;
}
