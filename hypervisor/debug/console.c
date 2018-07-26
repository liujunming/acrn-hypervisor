/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <hypervisor.h>
#include "serial_internal.h"

static spinlock_t lock;

static uint32_t serial_handle = SERIAL_INVALID_HANDLE;
struct timer console_timer;

#define CONSOLE_KICK_TIMER_TIMEOUT  40 /* timeout is 40ms*/

uint32_t get_serial_handle(void)
{
	return serial_handle;
}

static void print_char(char x)
{
	(void)serial_puts(serial_handle, &x, 1);

	if (x == '\n') {
		(void)serial_puts(serial_handle, "\r", 1);
	}
}

void console_init(void)
{
	spinlock_init(&lock);

	serial_handle = serial_open("STDIO");
}

int console_putc(int ch)
{
	int res = -1;

	spinlock_obtain(&lock);

	if (serial_handle != SERIAL_INVALID_HANDLE) {
		print_char(ch);
		res = 0;
	}

	spinlock_release(&lock);

	return res;
}

int console_puts(const char *s_arg)
{
	const char *s = s_arg;
	int res = -1;
	const char *p;

	spinlock_obtain(&lock);

	if (serial_handle != SERIAL_INVALID_HANDLE) {
		res = 0;
		while ((*s) != 0) {
			/* start output at the beginning of the string search
			 * for end of string or '\n'
			 */
			p = s;

			while ((*p != 0) && *p != '\n') {
				++p;
			}

			/* write all characters up to p */
			(void)serial_puts(serial_handle, s, p - s);

			res += p - s;

			if (*p == '\n') {
				print_char('\n');
				++p;
				res += 2;
			}

			/* continue at position p */
			s = p;
		}
	}

	spinlock_release(&lock);

	return res;
}

int console_write(const char *s_arg, size_t len)
{
	const char *s = s_arg;
	int res = -1;
	const char *e;
	const char *p;

	spinlock_obtain(&lock);

	if (serial_handle != SERIAL_INVALID_HANDLE) {
		/* calculate pointer to the end of the string */
		e = s + len;
		res = 0;

		/* process all characters */
		while (s != e) {
			/* search for '\n' or the end of the string */
			p = s;

			while ((p != e) && (*p != '\n')) {
				++p;
			}

			/* write all characters processed so far */
			(void)serial_puts(serial_handle, s, p - s);

			res += p - s;

			/* write '\n' if end of string is not reached */
			if (p != e) {
				print_char('\n');
				++p;
				res += 2;
			}

			/* continue at next position */
			s = p;
		}
	}

	spinlock_release(&lock);

	return res;
}

static void console_read(void)
{
	spinlock_obtain(&lock);

	if (serial_handle != SERIAL_INVALID_HANDLE) {
		/* Get all the data available in the RX FIFO */
		(void)serial_get_rx_data(serial_handle);
	}

	spinlock_release(&lock);
}

static void console_handler(void)
{
	/* Dump the RX FIFO to a circular buffer */
	console_read();

	/* serial Console Rx operation */
	vuart_console_rx_chars(serial_handle);

	/* serial Console Tx operation */
	vuart_console_tx_chars();

	shell_kick_session();
}

static int console_timer_callback(__unused void *data)
{
	/* Kick HV-Shell and Uart-Console tasks */
	console_handler();

	return 0;
}

void console_setup_timer(void)
{
	uint64_t period_in_cycle, fire_tsc;

	if (serial_handle == SERIAL_INVALID_HANDLE) {
		pr_err("%s: no uart, not need setup console timer",
			__func__);
		return;
	}

	period_in_cycle = CYCLES_PER_MS * CONSOLE_KICK_TIMER_TIMEOUT;
	fire_tsc = rdtsc() + period_in_cycle;
	initialize_timer(&console_timer,
			console_timer_callback, NULL,
			fire_tsc, TICK_MODE_PERIODIC, period_in_cycle);

	/* Start an periodic timer */
	if (add_timer(&console_timer) != 0) {
		pr_err("Failed to add console kick timer");
	}
}
