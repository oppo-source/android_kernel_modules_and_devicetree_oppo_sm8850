#ifndef __POGO_COMMON_H__
#define __POGO_COMMON_H__
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0))
#define REMOVE_RETURN_TYPE void
#else
#define REMOVE_RETURN_TYPE int
#endif

struct pogo_keyboard_operations {
    char name[32];
    int (*init)(void *port, int value);
    int (*write)(void *param, int value);
    int (*recv)(char *buf, int len);
    int (*resume)(struct platform_device *device);
    int (*suspend)(struct platform_device *device);
    REMOVE_RETURN_TYPE (*remove)(struct platform_device *device);
    bool (*check)(struct uart_port *port);
};

#endif

