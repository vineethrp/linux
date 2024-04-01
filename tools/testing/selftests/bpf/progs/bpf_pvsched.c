// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */

#include <stddef.h>
#include <linux/bpf.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <errno.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

/*
 * Dummy declaration of struct pid
 */
struct pid
{
	char pad[96];
	
	//refcount_t count;
	//unsigned int level;
	//spinlock_t lock;
	/* lists of tasks that use this pid */
	//struct hlist_head tasks[PIDTYPE_MAX];
	//struct hlist_head inodes;
	/* wait queue for pidfd notifications */
	//wait_queue_head_t wait_pidfd;
	//struct rcu_head rcu;
	//struct upid numbers[];
	
};

#define PVSCHED_NAME_MAX 32

struct pvsched_vcpu_ops {
	int (*pvsched_vcpu_register)(struct pid *pid);
	void (*pvsched_vcpu_unregister)(struct pid *pid);

	void (*pvsched_vcpu_notify_event)(void *addr, struct pid *pid, __u32 event);

	char name[PVSCHED_NAME_MAX];
	void *owner;
	__u32 events;
};

SEC("struct_ops/pvsched_vcpu_reg")
int BPF_PROG(pvsched_vcpu_reg, struct pid *pid)
{
	bpf_printk("pvsched_vcpu_reg: pid: %p", pid);
	return 0;
}

SEC("struct_ops/pvsched_vcpu_unreg")
void BPF_PROG(pvsched_vcpu_unreg, struct pid *pid)
{
	bpf_printk("pvsched_vcpu_unreg: pid: %p", pid);
}

SEC("struct_ops/pvsched_vcpu_notify_event")
void BPF_PROG(pvsched_vcpu_notify_event, void *addr, struct pid *pid, __u32 event)
{
	bpf_printk("pvsched_vcpu_notify: pid: %p, event:%u", pid, event);
}

SEC(".struct_ops")
struct pvsched_vcpu_ops pvsched_ops = {
	.pvsched_vcpu_register		= (void *)pvsched_vcpu_reg,
	.pvsched_vcpu_unregister	= (void *)pvsched_vcpu_unreg,
	.pvsched_vcpu_notify_event	= (void *)pvsched_vcpu_notify_event,
	.events				= 0x6,
	.name				= "bpf_pvsched_ops",
};
