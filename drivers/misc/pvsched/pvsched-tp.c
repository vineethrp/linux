#include <linux/trace_events.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/sched/deadline.h>
#include <linux/printk.h>
#include <linux/pci.h>

#include <asm/msr.h>
#include <uapi/asm/kvm_para.h>
#include <uapi/linux/sched/pvsched.h>

#include <trace/events/sched.h>
#include <trace/events/pvsched_guest.h>

#include "pvsched.h"

/*
 * copied from kernel/sched/core.c:__normal_prio()
 */
static inline int __sched_normal_prio(u8 sched_policy, u8 rt_prio, s8 sched_nice)
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

static inline int sched_normal_prio(struct task_struct *p)
{
	return __sched_normal_prio(p->policy, p->rt_priority, PRIO_TO_NICE(p->static_prio));
}

static void update_vcpu_sched(union vcpu_sched *sched, struct task_struct *p, pvsched_kerncs_t kern_cs, bool set_kern_cs)
{
	union vcpu_sched _sched = { 0 };
	_sched.pad[PVSCHED_SHM_GA_INDEX] = sched->pad[PVSCHED_SHM_GA_INDEX];
	_sched.guest_area.sched_policy = p->policy;
	_sched.guest_area.rt_prio = p->rt_priority;
	_sched.guest_area.nice = PRIO_TO_NICE(p->static_prio);
	if (set_kern_cs)
		_sched.guest_area.kern_cs |= kern_cs;
	else
		_sched.guest_area.kern_cs &= ~kern_cs;
	sched->pad[PVSCHED_SHM_GA_INDEX] = _sched.pad[PVSCHED_SHM_GA_INDEX];
}

static void sched_switch_callback(void *data, bool preempt,
		struct task_struct *prev,
		struct task_struct *next, unsigned int prev_state)
{
	union vcpu_sched *sched = (union vcpu_sched *)data + smp_processor_id();
	update_vcpu_sched(sched, next, 0, true);

}

static void sched_need_resched_callback(void *data, struct task_struct *p, struct task_struct *curr)
{
	int cpu = task_cpu(p);
	union vcpu_sched *sched = (union vcpu_sched *)data + cpu;
	int next_prio = sched_normal_prio(p);
	int prev_prio = sched_normal_prio(current);
	if (next_prio > prev_prio) {
		update_vcpu_sched(sched, p, 0, true);
	}
}

static void exit_touser_callback(void *data, struct task_struct *curr)
{
	union vcpu_sched *sched = (union vcpu_sched *)data + smp_processor_id();
	int curr_prio = sched_normal_prio(curr);
	int prev_prio = __sched_normal_prio(sched->host_area.sched_policy,
			sched->host_area.rt_prio, sched->host_area.nice);
	update_vcpu_sched(sched, curr, 0, true);
	if (prev_prio > curr_prio) {
		/*
		 * induce VMEXIT to drop the vcpu priority and let
		 * host schedule()
		 */
		unsigned int l, h;
		rdmsr(MSR_KVM_STEAL_TIME, l, h);
	}
}

static void kerncs_entry_callback(void *data, pvsched_kerncs_t type)
{
	union vcpu_sched *sched = (union vcpu_sched *)data + smp_processor_id();
	trace_printk("KERNCS entry: %x\n", type);
	update_vcpu_sched(sched, current, type, true);
}

static void kerncs_exit_callback(void *data, pvsched_kerncs_t type)
{
	union vcpu_sched *sched = (union vcpu_sched *)data + smp_processor_id();
	trace_printk("KERNCS exit: %x\n", type);
	update_vcpu_sched(sched, current, type, false);
}

void pvsched_attach_sched_callbacks(union vcpu_sched *sched)
{
	register_trace_sched_switch(sched_switch_callback, sched);
	register_trace_pvsched_need_resched(sched_need_resched_callback, sched);
	register_trace_pvsched_exit_to_user(exit_touser_callback, sched);
	register_trace_pvsched_kerncs_entry(kerncs_entry_callback, sched);
	register_trace_pvsched_kerncs_exit(kerncs_exit_callback, sched);
}

void pvsched_detach_sched_callbacks(union vcpu_sched *sched)
{
	unregister_trace_sched_switch(sched_switch_callback, sched);
	unregister_trace_pvsched_need_resched(sched_need_resched_callback, sched);
	unregister_trace_pvsched_exit_to_user(exit_touser_callback, sched);
	unregister_trace_pvsched_kerncs_entry(kerncs_entry_callback, sched);
	unregister_trace_pvsched_kerncs_exit(kerncs_exit_callback, sched);
}

MODULE_AUTHOR("Vineeth Pillai (Google) <vineeth@bitbyteword.org>");
MODULE_DESCRIPTION("pvsched device driver");
MODULE_LICENSE("GPL");
