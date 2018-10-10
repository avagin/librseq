// SPDX-License-Identifier: LGPL-2.1
/*
 * rseq.c
 *
 * Copyright (C) 2016 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; only
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syscall.h>
#include <assert.h>
#include <signal.h>

#include <rseq/rseq.h>

#define ARRAY_SIZE(arr)	(sizeof(arr) / sizeof((arr)[0]))

/*
 * linux/rseq.h defines struct rseq as aligned on 32 bytes. The kernel ABI
 * size is 20 bytes. For support of multiple rseq users within a process,
 * user-space defines an extra 4 bytes field as a reference count, for a
 * total of 24 bytes.
 */
struct libc_rseq {
	/* kernel-userspace ABI. */
	__u32 cpu_id_start;
	__u32 cpu_id;
	__u64 rseq_cs;
	__u32 flags;
	/* user-space ABI. */
	__u32 refcount;
} __attribute__((aligned(4 * sizeof(__u64))));

__attribute__((visibility("hidden"))) __thread
volatile struct libc_rseq __lib_rseq_abi = {
	.cpu_id = RSEQ_CPU_ID_UNINITIALIZED,
};

extern __attribute__((weak, alias("__lib_rseq_abi"))) __thread
volatile struct rseq __rseq_abi;

static int sys_rseq(volatile struct rseq *rseq_abi, uint32_t rseq_len,
		    int flags, uint32_t sig)
{
	return syscall(__NR_rseq, rseq_abi, rseq_len, flags, sig);
}

int rseq_available(void)
{
	int rc;

	rc = sys_rseq(NULL, 0, 0, 0);
	if (rc != -1)
		abort();
	switch (errno) {
	case ENOSYS:
		return 0;
	case EINVAL:
		return 1;
	default:
		abort();
	}
}

static void signal_off_save(sigset_t *oldset)
{
	sigset_t set;
	int ret;

	sigfillset(&set);
	ret = pthread_sigmask(SIG_BLOCK, &set, oldset);
	if (ret)
		abort();
}

static void signal_restore(sigset_t oldset)
{
	int ret;

	ret = pthread_sigmask(SIG_SETMASK, &oldset, NULL);
	if (ret)
		abort();
}

int rseq_register_current_thread(void)
{
	int rc, ret = 0;
	sigset_t oldset;

	signal_off_save(&oldset);
	if (__lib_rseq_abi.refcount++)
		goto end;
	rc = sys_rseq(&__rseq_abi, sizeof(struct rseq), 0, RSEQ_SIG);
	if (!rc) {
		assert(rseq_current_cpu_raw() >= 0);
		goto end;
	}
	if (errno != EBUSY)
		__rseq_abi.cpu_id = RSEQ_CPU_ID_REGISTRATION_FAILED;
	ret = -1;
	__lib_rseq_abi.refcount--;
end:
	signal_restore(oldset);
	return ret;
}

int rseq_unregister_current_thread(void)
{
	int rc, ret = 0;
	sigset_t oldset;

	signal_off_save(&oldset);
	if (--__lib_rseq_abi.refcount)
		goto end;
	rc = sys_rseq(&__rseq_abi, sizeof(struct rseq),
		      RSEQ_FLAG_UNREGISTER, RSEQ_SIG);
	if (!rc)
		goto end;
	ret = -1;
end:
	signal_restore(oldset);
	return ret;
}

int32_t rseq_fallback_current_cpu(void)
{
	int32_t cpu;

	cpu = sched_getcpu();
	if (cpu < 0) {
		perror("sched_getcpu()");
		abort();
	}
	return cpu;
}
