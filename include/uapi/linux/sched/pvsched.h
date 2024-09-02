/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI__LINUX_PVSCHED_H
#define _UAPI__LINUX_PVSCHED_H

#include <linux/types.h>

struct pvsched_guest_area {
	/* In kernel critical section? */
	__u8 kern_cs;
	/* Guest requested sched policy */
	__u8 sched_policy;
	/* Guest requested nice value if CFS */
	__s8 nice;
	/* Guest requested  RT priority */
	__u8 rt_prio;
};

typedef enum {
	VCPU_STATUS_VMENTERED = 1,
	VCPU_STATUS_VMEXITED = 2,
	VCPU_STATUS_HALTED = 3,
	/*
	 * Interrupt could be injected from any of the
	 * above states and hence is represented by the
	 * last 4 bits.
	 */
	VCPU_STATUS_INTR_INJECTED = 0xF0
} pvsched_vcpu_status_t;

struct pvsched_host_area {
	__u8 vcpu_status;
	/* Sched params set by host before VMENTER */
	__u8 sched_policy;
	__u8 nice;
	__u8 rt_prio;
};

struct pvsched_header {
	__u32 vcpu_id;
	__u32 vcpu_pid;
};

union vcpu_sched {
	struct {
		struct pvsched_header header;
		struct pvsched_guest_area guest_area;
		struct pvsched_host_area host_area;
	};
	__u32 pad[4];
};

#define PVSCHED_SHM_HEADER_INDEX	0
#define PVSCHED_SHM_GA_INDEX		2
#define PVSCHED_SHM_HA_INDEX		3

#endif

