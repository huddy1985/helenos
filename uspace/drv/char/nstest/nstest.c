/*
 * Copyright (c) 2010 Lenka Trochtova
 * Copyright (c) 2017 Jiri Svoboda
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @addtogroup ns8250
 * @{
 */

/** @file
 */

#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <str_error.h>
#include <stdbool.h>
#include <fibril_synch.h>
#include <stdlib.h>
#include <str.h>
#include <ctype.h>
#include <macros.h>
#include <stdlib.h>
#include <dirent.h>
#include <ddi.h>

#include <ddf/driver.h>
#include <ddf/interrupt.h>
#include <ddf/log.h>
#include <io/chardev_srv.h>
// SNOW
#include <io/kio.h>

#include <device/hw_res.h>
#include <ipc/serial_ctl.h>

#include "cyclic_buffer.h"

#define NAME "nstest"

/** The driver data for the serial port devices. */
typedef struct nstest {
	/** DDF device node */
	ddf_dev_t *dev;
	/** DDF function node */
	ddf_fun_t *fun;
	/** Character device service */
	chardev_srvs_t cds;
	/** The buffer for incoming data. */
	cyclic_buffer_t input_buffer;
	/** The fibril mutex for synchronizing the access to the device. */
	fibril_mutex_t mutex;
	/** Indicates that some data has become available */
	fibril_condvar_t input_buffer_available;
	/** True if device is removed. */
	bool removed;
} nstest_t;

static errno_t nstest_open(chardev_srvs_t *srvs, chardev_srv_t *srv)
{
	//printf("SNOW nstest_open\n");
	return EOK;
}

static errno_t nstest_close(chardev_srv_t *srv)
{
	//printf("SNOW nstest_close\n");
	return EOK;
}

static errno_t nstest_read(chardev_srv_t *srv, void *buf, size_t count, size_t *nread)
{
	//printf("SNOW nt_read count:%d\n", count);
	/*
	const char *inputs = "ls\r";
	static int num = 0;
	uint8_t *bp = (uint8_t*)buf;
	if(num < 512) {
		*bp = (uint8_t)inputs[num%3];	
		*nread = 1;
		num++;
		return EOK;
	} else
		while(1);
	*/
	return nskio_read(buf, count, nread);
	// return EOK;
}

static errno_t nstest_write(chardev_srv_t *srv, const void *buf, size_t count,
    size_t *nwritten)
{
	//printf("SNOW nt_write count:%d\n", count);
	return nskio_write(buf, count, nwritten);
}

static void nstest_default_handler(chardev_srv_t *srv, ipc_call_t *call)
{
	printf("SNOW nstest_default_handler\n");
	while(1);
}

static chardev_ops_t nstest_chardev_ops = {
	.open = nstest_open,
	.close = nstest_close,
	.read = nstest_read,
	.write = nstest_write,
	.def_handler = nstest_default_handler
};

static void nstest_init(void)
{
	ddf_log_init(NAME);
}

static void nstest_char_conn(ipc_call_t *icall, void *arg)
{
	ddf_dev_t *dev = ddf_fun_get_dev((ddf_fun_t*)arg);
	nstest_t *ns = (nstest_t*)ddf_dev_data_get(dev);
	chardev_conn(icall, &ns->cds);

}

static errno_t nstest_dev_add(ddf_dev_t *dev)
{
	nstest_t *ns = NULL;
	ddf_fun_t *fun = NULL;
	errno_t rc = EOK;
	bool bound = false;

	ddf_msg(LVL_DEBUG, "nstest_dev_add %s (handle = %d)",
	    ddf_dev_get_name(dev), (int) ddf_dev_get_handle(dev));

	ns = ddf_dev_data_alloc(dev, sizeof(nstest_t));
	if (ns == NULL) {
		rc = ENOMEM;
		goto fail;
	}

	fibril_mutex_initialize(&ns->mutex);
	fibril_condvar_initialize(&ns->input_buffer_available);
	ns->dev = dev;

	fun = ddf_fun_create(dev, fun_exposed, "a");
	if (fun == NULL) {
		ddf_msg(LVL_ERROR, "Failed creating function.");
		goto fail;
	}

	ddf_fun_set_conn_handler(fun, nstest_char_conn);

	chardev_srvs_init(&ns->cds);
	ns->cds.ops = &nstest_chardev_ops;
	ns->cds.sarg = ns;

	rc = ddf_fun_bind(fun);
	if (rc != EOK) {
		ddf_msg(LVL_ERROR, "Failed binding function.");
		goto fail;
	}
	bound = true;

	rc = ddf_fun_add_to_category(fun, "serial");
	if (rc != EOK) {
		ddf_msg(LVL_ERROR, "Error adding function to category 'serial'.");
		goto fail;
	}

	ddf_msg(LVL_NOTE, "Device %s successfully initialized.",
	    ddf_dev_get_name(dev));
	return EOK;
fail:
	if (bound)
		ddf_fun_unbind(fun);
	if (fun != NULL)
		ddf_fun_destroy(fun);
	return rc;
}

static driver_ops_t nstest_ops = {
	.dev_add = &nstest_dev_add,
};

/** The serial port device driver structure. */
static driver_t nstest_driver = {
	.name = NAME,
	.driver_ops = &nstest_ops
};

int main(int argc, char *argv[])
{
	//size_t rw = 0;
	//char test[] = "Hello\n";
	//char test[2] = {0};
	printf(NAME ": HelenOS serial port driver\n");
	nstest_init();
	//nstest_read(NULL, test, 1, &rw);
	//printf("SNOW rw:%d\n", rw);
	//printf("SNOW read: %s\n", test);
	return ddf_driver_main(&nstest_driver);
}
