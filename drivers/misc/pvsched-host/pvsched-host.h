#pragma once
#include <linux/hashtable.h>

#define PVSCHED_THROT_PERIOD	50000000
#define PVSCHED_BOOST_MAXTIME	100000000

#define PVSCHED_KERNCS_POLICY	SCHED_RR
#define PVSCHED_KERNCS_NICE	0
#define PVSCHED_KERNCS_PRIO	8

#define PVSCHED_THROT_POLICY	SCHED_NORMAL
#define PVSCHED_THROT_NICE	0
#define PVSCHED_THROT_PRIO	0

#define PVSCHED_DEFAULT_PRIO	DEFAULT_PRIO

typedef enum {
	PVSCHED_TP_VCPU_VMENTRY,
	PVSCHED_TP_VCPU_VMEXIT,
	PVSCHED_TP_VCPU_HALT,
	PVSCHED_TP_VCPU_INJ_INTR
} pvsched_tp_t;

typedef enum {
	VCPU_ACTION_NONE,
	VCPU_ACTION_BOOSTED,
	VCPU_ACTION_THROTTLED,
} vcpu_action_t;

struct pvsched_vcpu_info {
	vcpu_action_t	action;
	u64 start_ns;
	u64 duration_ns;
};

struct pvsched_vcpu {
	unsigned vm_id;
	unsigned vcpu_id;
	unsigned vcpu_pid;
	union vcpu_sched *sched;
	struct pvsched_vcpu_info info;
	struct hlist_node node;
};

struct pvsched_vm_pages {
	struct page **mapped_pages;
	size_t offset;
	unsigned nr_pages;
};

struct pvsched_vm {
	unsigned vm_id;
	unsigned nr_vcpus;
	struct pvsched_vm_pages vm_pages;
	struct pvsched_vcpu *vcpus;
	union vcpu_sched *vcpu_sched_base;
	struct list_head vms_list;
};

#define PVSCHED_MAX_VMPAGES	2
#define PVSCHED_MAX_HTABLE_BITS	8

struct pvsched_vms {
	int nr_vms;
	struct list_head	vms_list_head;
	spinlock_t		vms_lock;
	DECLARE_HASHTABLE(pvsched_vcpus, PVSCHED_MAX_HTABLE_BITS);
};

struct pvsched_info {
	u32 vm_id;
	u32 nr_vcpus;
	u64 vcpu_sched_base;
};

#define PVSCHED_IOCTL	'\xF0'

#define PVSCHED_IOCTL_SET_INFO _IOW(PVSCHED_IOCTL, 0, struct pvsched_info)
#define PVSCHED_IOCTL_UNSET_INFO _IOW(PVSCHED_IOCTL, 1, struct pvsched_info)

struct pvsched_vms *pvsched_vms_init(void);
void pvsched_vms_free(struct pvsched_vms *vms);

struct pvsched_vm *pvsched_vm_init(struct pvsched_vms *vms,
		unsigned int vm_id, unsigned long uaddr, u32 nr_vcpus);
void pvsched_vm_free(struct pvsched_vms *vms, unsigned int vm_id);
void pvsched_print_vm(struct pvsched_vm *vm);

struct pvsched_vcpu *pvsched_vcpus_get_vcpu(struct pvsched_vms *vms,
		unsigned int vcpu_pid);

int pvsched_attach_kvm_tracepoints(struct pvsched_vms *vms);
int pvsched_detach_kvm_tracepoints(struct pvsched_vms *vms);

int vcpu_callback_handler(struct pvsched_vms *vms, unsigned int vcpu_id,
		unsigned int vcpu_pid, pvsched_tp_t tp);

extern int pvsched_debug;
#define PVSCHED_PRINTK(...) { \
	if (pvsched_debug) { \
		trace_printk(__VA_ARGS__); \
	} \
}
