/*
 * rtc.h
 *
 *  Created on: May 31, 2020
 *      Author: brain
 */

#ifndef SRC_RTC_H_
#define SRC_RTC_H_

#include "config.h"
#include "hexops.h"
#include "registry.h"


void rtc_reset(void);
void rtc_register(void);
void rtc_init(void);

#endif /* SRC_RTC_H_ */
