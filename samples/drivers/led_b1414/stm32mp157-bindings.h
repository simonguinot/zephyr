/*
 * Copyright (c) 2019, Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SAMPLES_DRIVERS_LED_B1414_H_
#define ZEPHYR_SAMPLES_DRIVERS_LED_B1414_H_

/*
 *
 * T0H = 300 ns, T0L = 900 ns
 * T1H = 900 ns, T1L = 300 ns
 *
 * Allowance is +/- 80 ns.
 *
 * At 6.25 MHz (50 MHz / 8), 1 bit is transmitted in 160 ns.
 *
 * 2 bits -> 320 ns.
 * 6 bits -> 960 ns.
 */
#define SPI_FREQ	6250000
#define ZERO_FRAME	0x60
#define ONE_FRAME	0x7e

#endif
