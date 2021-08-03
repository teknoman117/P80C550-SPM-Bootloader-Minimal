/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef P80C550_H_
#define P80C550_H_

#include <8051.h>

// ADC registers in P80C550
__sfr __at (0xC5) ADCON;
__sfr __at (0xC6) ADAT;
__sbit __at (0xAD) EAD;

// Additional interrupts in P80C550
#define ADC_VECTOR 5

// ADCON bits
#define AADR0 0x01
#define AADR1 0x02
#define AADR2 0x04
#define ADCS 0x08
#define ADCI 0x10

// BV utility
#ifndef _BV
#define _BV(a) (1 << a)
#endif /* _BV */

#endif /* P80C550_H_ */
