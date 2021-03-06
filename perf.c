/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "stress-ng.h"

#define THOUSAND	(1000.0)
#define MILLION		(THOUSAND * THOUSAND)
#define BILLION		(THOUSAND * MILLION)
#define TRILLION	(THOUSAND * BILLION)
#define QUADRILLION	(THOUSAND * TRILLION)
#define QUINTILLION	(THOUSAND * QUADRILLION)

#if defined(STRESS_PERF_STATS)
/* perf enabled systems */

#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>

/* used for table of perf events to gather */
typedef struct {
	int id;				/* stress-ng perf ID */
	unsigned long type;		/* perf types */
	unsigned long config;		/* perf type specific config */
	char *label;			/* human readable name for perf type */
} perf_info_t;

/* perf data */
typedef struct {
	uint64_t counter;		/* perf counter */
	uint64_t time_enabled;		/* perf time enabled */
	uint64_t time_running;		/* perf time running */
} perf_data_t;

#define PERF_INFO(type, config, label)	\
	{ STRESS_PERF_ ## config, PERF_TYPE_ ## type, PERF_COUNT_ ## config, label }

/* perf counters to be read */
static const perf_info_t perf_info[STRESS_PERF_MAX + 1] = {
	PERF_INFO(HARDWARE, HW_CPU_CYCLES,		"CPU Cycles"),
	PERF_INFO(HARDWARE, HW_INSTRUCTIONS,		"Instructions"),
	PERF_INFO(HARDWARE, HW_CACHE_REFERENCES,	"Cache References"),
	PERF_INFO(HARDWARE, HW_CACHE_MISSES,		"Cache Misses"),
	PERF_INFO(HARDWARE, HW_STALLED_CYCLES_FRONTEND,	"Stalled Cycles Frontend"),
	PERF_INFO(HARDWARE, HW_STALLED_CYCLES_BACKEND,	"Stalled Cycles Backend"),
	PERF_INFO(HARDWARE, HW_BRANCH_INSTRUCTIONS,	"Branch Instructions"),
	PERF_INFO(HARDWARE, HW_BRANCH_MISSES,		"Branch Misses"),
	PERF_INFO(HARDWARE, HW_BUS_CYCLES,		"Bus Cycles"),
	PERF_INFO(HARDWARE, HW_REF_CPU_CYCLES,		"Total Cycles"),

	PERF_INFO(SOFTWARE, SW_PAGE_FAULTS_MIN,		"Page Faults Minor"),
	PERF_INFO(SOFTWARE, SW_PAGE_FAULTS_MAJ,		"Page Faults Major"),
	PERF_INFO(SOFTWARE, SW_CONTEXT_SWITCHES,	"Context Switches"),
	PERF_INFO(SOFTWARE, SW_CPU_MIGRATIONS,		"CPU Migrations"),
	PERF_INFO(SOFTWARE, SW_ALIGNMENT_FAULTS,	"Aligment Faults"),

	{ 0, 0, 0, NULL }
};

/*
 *  perf_open()
 *	open perf, get leader and perf fd's
 */
int perf_open(stress_perf_t *sp)
{
	size_t i;

	if (!sp)
		return -1;

	memset(sp, 0, sizeof(stress_perf_t));
	sp->perf_opened = 0;

	for (i = 0; i < STRESS_PERF_MAX; i++) {
		sp->perf_stat[i].fd = -1;
		sp->perf_stat[i].counter = 0;
	}

	for (i = 0; i < STRESS_PERF_MAX && perf_info[i].label; i++) {
		struct perf_event_attr attr;

		memset(&attr, 0, sizeof(attr));
		attr.type = perf_info[i].type;
		attr.config = perf_info[i].config;
		attr.disabled = 1;
		attr.inherit = 1;
		attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED |
				   PERF_FORMAT_TOTAL_TIME_RUNNING;
		attr.size = sizeof(attr);
		sp->perf_stat[i].fd = syscall(__NR_perf_event_open, &attr, 0, -1, -1, 0);
		if (sp->perf_stat[i].fd > -1)
			sp->perf_opened++;
	}
	if (!sp->perf_opened) {
		pr_dbg(stderr, "perf_event_open failed, no perf events [%u]\n", getpid());
		return -1;
	}

	return 0;
}

/*
 *  perf_enable()
 *	enable perf counters
 */
int perf_enable(stress_perf_t *sp)
{
	size_t i;

	if (!sp)
		return -1;
	if (!sp->perf_opened)
		return 0;

	for (i = 0; i < STRESS_PERF_MAX && perf_info[i].label; i++) {
		int fd = sp->perf_stat[i].fd;

		if (fd > -1) {
			if (ioctl(fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) < 0) {
				(void)close(fd);
				sp->perf_stat[i].fd = -1;
				continue;
			}
			if (ioctl(fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) < 0) {
				(void)close(fd);
				sp->perf_stat[i].fd = -1;
			}
		}
	}
	return 0;
}

/*
 *  perf_disable()
 *	disable perf counters
 */
int perf_disable(stress_perf_t *sp)
{
	size_t i;

	if (!sp)
		return -1;
	if (!sp->perf_opened)
		return 0;

	for (i = 0; i < STRESS_PERF_MAX && perf_info[i].label; i++) {
		int fd = sp->perf_stat[i].fd;

		if (fd > -1) {
			if (ioctl(fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) < 0) {
				(void)close(fd);
				sp->perf_stat[i].fd = -1;
			}
		}
	}
	return 0;
}

/*
 *  perf_close()
 *	read counters and close
 */
int perf_close(stress_perf_t *sp)
{
	size_t i = 0;
	perf_data_t data;
	ssize_t ret;
	int rc = -1;
	double scale;

	if (!sp)
		return -1;
	if (!sp->perf_opened)
		goto out_ok;

	for (i = 0; i < STRESS_PERF_MAX && perf_info[i].label; i++) {
		int fd = sp->perf_stat[i].fd;
		if (fd < 0 ) {
			sp->perf_stat[i].counter = STRESS_PERF_INVALID;
			continue;
		}

		memset(&data, 0, sizeof(data));
		ret = read(fd, &data, sizeof(data));
		if (ret != sizeof(data))
			sp->perf_stat[i].counter = STRESS_PERF_INVALID;
		else {
			/* Ensure we don't get division by zero */
			if (data.time_running == 0) {
				scale = (data.time_enabled == 0) ? 1.0 : 0.0;
			} else {
				scale = (double)data.time_enabled / data.time_running;
			}
			sp->perf_stat[i].counter = (uint64_t)((double)data.counter * scale);
		}
		(void)close(fd);
		sp->perf_stat[i].fd = -1;
	}

out_ok:
	rc = 0;
	for (; i < STRESS_PERF_MAX; i++)
		sp->perf_stat[i].counter = STRESS_PERF_INVALID;

	return rc;
}

/*
 *  perf_get_counter_by_index()
 *	fetch counter and perf ID via index i
 */
int perf_get_counter_by_index(
	const stress_perf_t *sp,
	const int i,
	uint64_t *counter,
	int *id)
{
	if ((i < 0) || (i >= STRESS_PERF_MAX))
		goto fail;

	if (perf_info[i].label) {
		*id = perf_info[i].id;
		*counter = sp->perf_stat[i].counter;
		return 0;
	}

fail:
	*id = -1;
	*counter = 0;
	return -1;
}

/*
 *  perf_get_label_by_index()
 *	fetch label via index i
 */
const char *perf_get_label_by_index(const int i)
{
	if ((i < 0) || (i >= STRESS_PERF_MAX))
		return NULL;

	return perf_info[i].label;
}


/*
 *  perf_get_counter_by_id()
 *	fetch counter and index via perf ID
 */
int perf_get_counter_by_id(
	const stress_perf_t *sp,
	int id,
	uint64_t *counter,
	int *index)
{
	int i;

	for (i = 0; perf_info[i].label; i++) {
		if (perf_info[i].id == id) {
			*index = i;
			*counter = sp->perf_stat[i].counter;
			return 0;
		}
	}

	*index = -1;
	*counter = 0;
	return -1;
}

/*
 *  perf_stat_succeeded()
 *	did perf event open work OK?
 */
bool perf_stat_succeeded(const stress_perf_t *sp)
{
	return sp->perf_opened > 0;
}

#else
/* Non-perf enabled systems */

/*
 *  perf_open()
 *	open perf, get leader and perf fd's, no-op
 */
int perf_open(stress_perf_t *sp)
{
	(void)sp;

	return -1;
}

/*
 *  perf_enable()
 *	enable perf counters, no-op
 */
int perf_enable(stress_perf_t *sp)
{
	(void)sp;

	return -1;
}

/*
 *  perf_disable()
 *	disable perf counters, no-op
 */
int perf_disable(stress_perf_t *sp)
{
	(void)sp;

	return -1;
}

/*
 *  perf_close()
 *	read counters and close, no-op
 */
int perf_close(stress_perf_t *sp)
{
	(void)sp;

	return -1;
}

/*
 *  perf_get_counter_by_index()
 *	fetch counter and perf ID via index i, no-op
 */
int perf_get_counter_by_index(
	const stress_perf_t *sp,
	const int i,
	uint64_t *counter,
	int *id)
{
	(void)sp;
	(void)i;

	*id = -1;
	*counter = 0;
	return -1;
}

/*
 *  perf_get_label_by_index()
 *	fetch label via index i, no-op
 */
const char *perf_get_label_by_index(const int i)
{
	(void)i;

	return NULL;
}

/*
 *  perf_get_counter_by_id()
 *	fetch counter and index via perf ID, no-op
 */
int perf_get_counter_by_id(
	const stress_perf_t *sp,
	int id,
	uint64_t *counter,
	int *index)
{
	(void)sp;
	(void)id;

	*index = -1;
	*counter = 0;
	return -1;
}

/*
 *  perf_stat_succeeded()
 *	did perf event open work OK?
 */
bool perf_stat_succeeded(const stress_perf_t *sp)
{
	(void)sp;

	return false;
}

#endif

typedef struct {
	double		threshold;
	double		scale;
	char 		*suffix;
} perf_scale_t;

static perf_scale_t perf_scale[] = {
	{ THOUSAND,		1.0,		"sec" },
	{ 100 * THOUSAND,	THOUSAND,	"K/sec" },
	{ 100 * MILLION,	MILLION,	"M/sec" },
	{ 100 * BILLION,	BILLION,	"B/sec" },
	{ 100 * TRILLION,	TRILLION,	"T/sec" },
	{ 100 * QUADRILLION,	QUADRILLION,	"P/sec" },
	{ 100 * QUINTILLION,	QUINTILLION,	"E/sec" },
	{ -1, 			-1,		NULL }
};

/*
 *  perf_stat_scale()
 *	scale a counter by duration seconds
 *	into a human readable form
 */
const char *perf_stat_scale(const uint64_t counter, const double duration)
{
	static char buffer[40];
	char *suffix = "E/sec";
	double scale = QUINTILLION;
	size_t i;

	double scaled =
		duration > 0.0 ? (double)counter / duration : 0.0;

	for (i = 0; perf_scale[i].suffix; i++) {
		if (scaled < perf_scale[i].threshold) {
			suffix = perf_scale[i].suffix;
			scale = perf_scale[i].scale;
			break;
		}
	}
	scaled /= scale;

	snprintf(buffer, sizeof(buffer), "%11.2f %-5s",
		scaled, suffix);

	return buffer;
}
