#ifndef _VIRTIO_TESTDEV_H_
#define _VIRTIO_TESTDEV_H_
/*
 * virtio-testdev is a driver for the virtio-testdev qemu device.
 * The virtio-testdev device exposes a simple control interface to
 * qemu for kvm-unit-tests through virtio.
 *
 * Copyright (C) 2014, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include "libcflat.h"

extern void virtio_testdev_init(void);
extern void virtio_testdev_version(u32 *version);
extern void virtio_testdev_clear(void);
extern void virtio_testdev_exit(int code);
#endif
