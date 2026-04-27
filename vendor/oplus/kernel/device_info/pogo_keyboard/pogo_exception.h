/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _POGO_EXCEPTION_
#define _POGO_EXCEPTION_

typedef enum {
	/*0-99 for tri key*/
	/*100-150 for kernel*/
	EXCEP_HARDWARE = 100,
	EXCEP_BUS = 101 ,
	EXCEP_BUS_READY = 102,
	/* 151-254 for future use */
    EXCEP_ERROR = 255,
} pogo_excep_type;

#define MAX_BUS_ERROR_COUNT                 15
#define MAX_BUS_UPDATE_COUNT                2
#define MAX_BUS_NOT_READY_UPDATE_COUNT      1

struct exception_data {
	void  *chip_data; /*debug info data*/
	unsigned int exception_upload_count;
	unsigned int bus_error_count;
	unsigned int bus_error_upload_count;
	unsigned int bus_not_ready_upload_count;
};

int pogo_exception_report(void *tp_exception_data, pogo_excep_type excep_tpye, void *summary, unsigned int summary_size);

#endif /*_POGO_EXCEPTION_*/
