#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/printk.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/timekeeping.h>
#include <linux/sched/deadline.h>
#include <uapi/linux/sched/types.h>
#include <uapi/linux/sched/pvsched.h>

#include "pvsched-host.h"

char *sched_policy[] = {
	"SCHED_NORMAL",
	"SCHED_FIFO",
	"SCHED_RR",
	"SCHED_BATCH",
	"RESERVED",
	"SCHED_IDLE",
	"SCHED_DEADLINE"
};

/*
 * copied from kernel/sched/core.c:__normal_prio()
 */
static inline int __sched_normal_prio(u8 sched_policy, s8 sched_nice, u8 rt_prio)
{
	int prio;

	if (sched_policy == SCHED_DEADLINE)
		prio = MAX_DL_PRIO - 1;
	else if (sched_policy == SCHED_FIFO || sched_policy == SCHED_RR)
		prio = MAX_RT_PRIO - 1 - rt_prio;
	else
		prio = NICE_TO_PRIO(sched_nice);

	return prio;
}

static inline char *pvsched_tp_str(pvsched_tp_t tp)
{
	switch (tp) {
		case PVSCHED_TP_VCPU_VMENTRY:
			return "VCPU_VMENTRY";
		case PVSCHED_TP_VCPU_VMEXIT:
			return "VCPU_VMEXIT";
		case PVSCHED_TP_VCPU_HALT:
			return "VCPU_HALT";
		case PVSCHED_TP_VCPU_INJ_INTR:
			return "VCPU_INJ_INTR";
	}
	return "INVALID_TP";
}

static inline char *vcpu_action_str(vcpu_action_t action)
{
	if (action == VCPU_ACTION_BOOSTED)
		return "boosted";
	else if (action == VCPU_ACTION_THROTTLED)
		return "throttled";
	else return "in unknown state";
}

static inline unsigned char pvsched_tp_to_vcpu_status(pvsched_tp_t tp)
{
	switch (tp) {
		case PVSCHED_TP_VCPU_VMENTRY:
			return VCPU_STATUS_VMENTERED;
		case PVSCHED_TP_VCPU_VMEXIT:
			return VCPU_STATUS_VMEXITED;
		case PVSCHED_TP_VCPU_HALT:
			return VCPU_STATUS_HALTED;
		case PVSCHED_TP_VCPU_INJ_INTR:
			return VCPU_STATUS_INTR_INJECTED;
	}
	return -1;
}

//static int process_pvsched(int pid_nr, struct sched_attr *_attr, union vcpu_sched *sched)
static int process_pvsched(union vcpu_sched *sched, unsigned int policy, int nice, unsigned int prio)
{
	int ret;
	int pid_nr = sched->header.vcpu_pid;
	struct task_struct *p;
	struct sched_attr attr = {
		.sched_policy = policy,
		.sched_nice = nice,
		.sched_priority = prio
	};

	rcu_read_lock();
	p = find_task_by_pid_ns(pid_nr, &init_pid_ns);
	if (p)
		get_task_struct(p);
	rcu_read_unlock();

	if (!p) {
		PVSCHED_PRINTK("Failed to get the task from pid(%d)\n", pid_nr)
		return -1;
	}

	ret = sched_setattr_pi_nocheck(p, &attr, false);

	if (!ret) {
		sched->host_area.sched_policy = attr.sched_policy;
		sched->host_area.nice = attr.sched_nice;
		sched->host_area.rt_prio = attr.sched_priority;
	} else {
		PVSCHED_PRINTK("Failed to process pvsched for vcpu task(%d)\n", pid_nr)
	}
	put_task_struct(p);

	return ret;
}

static inline void set_vcpu_boosted(struct pvsched_vcpu_info *info, u64 ts)
{
	info->action = VCPU_ACTION_BOOSTED;
	info->start_ns = ts;
	info->duration_ns = 0;
}

static inline void set_vcpu_throttled(struct pvsched_vcpu_info *info, u64 ts)
{
	info->action = VCPU_ACTION_THROTTLED;
	info->start_ns = ts;
	info->duration_ns = 0;
}

static inline void unset_vcpu_action(struct pvsched_vcpu_info *info)
{
	info->action = VCPU_ACTION_NONE;
	info->start_ns = info->duration_ns = 0;
}

static inline bool vcpu_boosted(struct pvsched_vcpu_info *info)
{
	return (info->action == VCPU_ACTION_BOOSTED);
}

static inline bool vcpu_throttled(struct pvsched_vcpu_info *info)
{
	return (info->action == VCPU_ACTION_THROTTLED);
}


static inline bool vcpu_boosted_or_throttled(struct pvsched_vcpu_info *info)
{
	return vcpu_boosted(info) || vcpu_throttled(info);
}

static inline u64 vcpu_boost_duration(struct pvsched_vcpu_info *info)
{
	return vcpu_boosted(info) ? info->duration_ns : 0;
}

static inline u64 vcpu_throttled_duration(struct pvsched_vcpu_info *info)
{
	return vcpu_throttled(info) ? info->duration_ns : 0;
}

int vcpu_callback_handler(struct pvsched_vms *vms, unsigned int vcpu_id,
		unsigned int vcpu_pid, pvsched_tp_t tp)
{
	u64 ts;
	int nice;
	int ret = 0;
	bool apply_sched = true;
	unsigned int policy, prio;
	union vcpu_sched *sched = NULL;
	unsigned char prev_vcpu_status;
	int curr_pid = current ? current->pid : 0;
	struct pvsched_vcpu_info *info;
	unsigned int curr_prio, boost_prio, throt_prio;
	struct pvsched_vcpu *vcpu = pvsched_vcpus_get_vcpu(vms, vcpu_pid);

	if (!vcpu || !vcpu->sched) {
		PVSCHED_PRINTK("vcpu[id=%d, pid=%d) not in the list!\n", vcpu_id, vcpu_pid);
		return -1;
	}

	curr_prio = __sched_normal_prio(sched->host_area.sched_policy,
				sched->host_area.nice, sched->host_area.rt_prio);
	boost_prio = __sched_normal_prio(PVSCHED_KERNCS_POLICY,
					PVSCHED_KERNCS_NICE, PVSCHED_KERNCS_PRIO);
	throt_prio = __sched_normal_prio(PVSCHED_THROT_POLICY,
					PVSCHED_THROT_NICE, PVSCHED_THROT_PRIO);
	sched = vcpu->sched;
	policy = sched->guest_area.sched_policy;
	prio = sched->guest_area.rt_prio;
	nice = sched->guest_area.nice;
	if (sched->guest_area.kern_cs ||
			__sched_normal_prio(policy, nice, prio) < boost_prio) {
		policy = PVSCHED_KERNCS_POLICY;
		nice = PVSCHED_KERNCS_NICE;
		prio = PVSCHED_KERNCS_PRIO;
	}

	info = &(vcpu->info);
	prev_vcpu_status = sched->host_area.vcpu_status;
	sched->host_area.vcpu_status = pvsched_tp_to_vcpu_status(tp);

	ts = ktime_get_boot_fast_ns();

	PVSCHED_PRINTK("VCPU(%u:%u): %s, g[0x%x], h[0x%x]!\n",
			vcpu_id, vcpu_pid, pvsched_tp_str(tp), sched->pad[2], sched->pad[3]);

	switch (tp) {
	case PVSCHED_TP_VCPU_VMENTRY:
		apply_sched = false;
		if (vcpu_boosted_or_throttled(info)) {
			info->start_ns = ts;
		}
		break;
	case PVSCHED_TP_VCPU_VMEXIT:
		if (vcpu_boosted_or_throttled(info)) {
			info->duration_ns += (ts - info->start_ns);
			PVSCHED_PRINTK("VCPU(%u:%u): %s for %llu ns\n", vcpu_id, vcpu_pid,
				vcpu_action_str(info->action),
				info->duration_ns);
		}
		if (vcpu_boost_duration(info) >= PVSCHED_BOOST_MAXTIME) {
			set_vcpu_throttled(info, ts);
			PVSCHED_PRINTK("VCPU(%u:%u): throttled!\n", vcpu_id, vcpu_pid)
		} else if (vcpu_throttled_duration(info) >= PVSCHED_THROT_PERIOD) {
			unset_vcpu_action(info);
			PVSCHED_PRINTK("VCPU(%u:%u): unthrottled!\n", vcpu_id, vcpu_pid);
		}
		break;
	case PVSCHED_TP_VCPU_HALT:
		if (vcpu_throttled(info)) {
			apply_sched = false;
		} else {
			unset_vcpu_action(info);
			policy = PVSCHED_KERNCS_POLICY;
			nice = PVSCHED_KERNCS_NICE;
			prio = PVSCHED_KERNCS_PRIO;
		}
		break;
	case PVSCHED_TP_VCPU_INJ_INTR:
		if (!vcpu_throttled(info) &&
				(curr_pid == vcpu_pid || prev_vcpu_status == VCPU_STATUS_VMEXITED)) {
			if (curr_pid != vcpu_pid) {
				PVSCHED_PRINTK("VCPU(%u:%u): INJ_INTR on pid %u while VMEXITED\n",
					vcpu_id, vcpu_pid, curr_pid)
			}
			policy = PVSCHED_KERNCS_POLICY;
			nice = PVSCHED_KERNCS_NICE;
			prio = PVSCHED_KERNCS_PRIO;
		} else {
			apply_sched = false;
		}
	}
	if (apply_sched) {
		if (vcpu_throttled(info)) {
			policy = PVSCHED_THROT_POLICY;
			nice = PVSCHED_THROT_NICE;
			prio = PVSCHED_THROT_PRIO;
		}
		ret = process_pvsched(sched, policy, nice, prio);
		PVSCHED_PRINTK("VCPU(%u:%u): process_pvsched(%u, %d, %u)\n",
				vcpu_id, vcpu_pid, policy, nice, prio)
		if (ret) {
			unset_vcpu_action(info);
			PVSCHED_PRINTK("VCPU(%u:%u): process_pvsched failed(%d)! Unsetting vcpu_action\n",
					vcpu_id, vcpu_pid, ret)
		} else {
			unsigned int new_prio = __sched_normal_prio(policy, nice, prio);
			if (!vcpu_boosted(info) && new_prio < PVSCHED_DEFAULT_PRIO) {
				set_vcpu_boosted(info, ts);
				PVSCHED_PRINTK("VCPU(%u:%u): boosted!\n", vcpu_id, vcpu_pid)
			} else if (vcpu_boosted(info) && new_prio >= PVSCHED_DEFAULT_PRIO) {
				unset_vcpu_action(info);
				PVSCHED_PRINTK("VCPU(%u:%u): unboosted!\n", vcpu_id, vcpu_pid)
			}
		}
	}

	return ret;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("vineeth@bitbyteword.org");
MODULE_DESCRIPTION("Paravirt host driver");
