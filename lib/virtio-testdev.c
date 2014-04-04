/*
 * Copyright (C) 2014, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include "libcflat.h"
#include "asm/spinlock.h"
#include "virtio.h"

#define TESTDEV_NAME		"virtio-testdev"
#define VIRTIO_ID_TESTDEV	0xffff
#define TESTDEV_MAJOR_VER	1
#define TESTDEV_MINOR_VER	1

#define VIRTIO_CONFIG_SIZE	64

enum {
	VERSION = 1,
	CLEAR,
	EXIT,
};

#define TOKEN_OFFSET		0x0
#define NARGS_OFFSET		0x4
#define NRETS_OFFSET		0x8
#define ARG_OFFSET(n)		(0xc + (n) * 4)
#define __RET_OFFSET(nargs, n)	(ARG_OFFSET(nargs) + (n) * 4)

static struct spinlock lock;
static struct virtio_device *vdev;

static u32 testdev_readl(unsigned offset)
{
	assert(offset <= (VIRTIO_CONFIG_SIZE - 4));
	return virtio_config_readl(vdev, offset);
}

static void testdev_writel(unsigned offset, u32 val)
{
	assert(offset <= (VIRTIO_CONFIG_SIZE - 4));
	virtio_config_writel(vdev, offset, val);
}

/*
 * We have to write all args; nargs, nrets, ... first to avoid executing
 * the token's operation until all args are in place. Then issue the op,
 * and then read the return values. Reading the return values (or just
 * sanity checking by reading token) will read a zero into qemu's copy
 * of the token, which allows us to prepare additional ops without
 * re-executing the last one.
 */
void virtio_testdev(u32 token, u32 nargs, u32 nrets, ...)
{
	va_list va;
	unsigned off;
	u32 n;

	if (!vdev)
		return;

	spin_lock(&lock);

	testdev_writel(NARGS_OFFSET, nargs);
	testdev_writel(NRETS_OFFSET, nrets);

	va_start(va, nrets);

	off = ARG_OFFSET(0);
	n = nargs;
	while (n--) {
		testdev_writel(off, va_arg(va, unsigned));
		off += 4;
	}

	/* this runs the op, but then resets token to zero */
	testdev_writel(TOKEN_OFFSET, token);
	assert(testdev_readl(TOKEN_OFFSET) == 0);

	off = __RET_OFFSET(nargs, 0);
	n = nrets;
	while (n--) {
		u32 *r = va_arg(va, unsigned *);
		*r = testdev_readl(off);
		off += 4;
	}

	spin_unlock(&lock);
	va_end(va);
}

void virtio_testdev_version(u32 *version)
{
	virtio_testdev(VERSION, 0, 1, version);
}

void virtio_testdev_clear(void)
{
	virtio_testdev(CLEAR, 0, 0);
}

void virtio_testdev_exit(int code)
{
	virtio_testdev(EXIT, 1, 0, code);
}

void virtio_testdev_init(void)
{
	u16 major, minor;
	u32 version;

	vdev = virtio_bind(VIRTIO_ID_TESTDEV);
	if (vdev == NULL) {
		printf("%s: can't find " TESTDEV_NAME ". "
		       "Is '-device " TESTDEV_NAME "' "
		       "on the qemu command line?\n", __func__);
		abort();
	}

	virtio_testdev_version(&version);
	major = version >> 16;
	minor = version & 0xffff;

	if (major != TESTDEV_MAJOR_VER || minor < TESTDEV_MINOR_VER) {
		char *u = "qemu";
		if (major > TESTDEV_MAJOR_VER)
			u = "kvm-unit-tests";
		printf("%s: incompatible version of " TESTDEV_NAME ": "
		       "major = %d, minor = %d. Update %s\n",
		       __func__, major, minor, u);
		abort();
	}

	if (minor > TESTDEV_MINOR_VER)
		printf("%s: " TESTDEV_NAME " has new features. "
		       "An update of kvm-unit-tests may be possible.\n",
		       __func__);
}
