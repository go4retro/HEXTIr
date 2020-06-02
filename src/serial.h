/*
 * serial.h
 *
 *  Created on: May 31, 2020
 *      Author: brain
 */

#ifndef SERIAL_H_
#define SERIAL_H_

#include "config.h"
#include "hexops.h"
#include "registry.h"


void ser_reset(void);
void ser_register(void);
void ser_init(void);

#endif /* SERIAL_H */
