
/*
 * hexops.h
 *
 *  Created on: May 31, 2020
 *      Author: brain
 */





#ifndef HEXOPS_H
#define HEXOPS_H

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

#define FILEATTR_READ     1
#define FILEATTR_WRITE    2
#define FILEATTR_PROTECT  4
#define FILEATTR_DISPLAY  8

uint8_t hex_getdata(uint8_t buf[256], uint16_t len);
uint8_t hex_receive_options( pab_t pab );
void hex_eat_it(uint16_t length, uint8_t status );

#endif  // hexops_h
