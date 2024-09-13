#include <linux/trace_events.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/printk.h>

#include <trace/events/kvm_pvsched.h>

#include "pvsched-host.h"

static void vcpu_vmentry_callback(void *data, unsigned int vcpu_id, unsigned int vcpu_pid)
{
	vcpu_callback_handler((struct pvsched_vms *)data, vcpu_id, vcpu_pid, PVSCHED_TP_VCPU_VMENTRY);
}

static void vcpu_vmexit_callback(void *data, unsigned int vcpu_id, unsigned int vcpu_pid)
{
	vcpu_callback_handler((struct pvsched_vms *)data, vcpu_id, vcpu_pid, PVSCHED_TP_VCPU_VMEXIT);
}

static void vcpu_inject_intr_callback(void *data, unsigned int vcpu_id, unsigned int vcpu_pid)
{
	vcpu_callback_handler((struct pvsched_vms *)data, vcpu_id, vcpu_pid, PVSCHED_TP_VCPU_INJ_INTR);
}

static void vcpu_halt_callback(void *data, unsigned int vcpu_id, unsigned int vcpu_pid)
{
	vcpu_callback_handler((struct pvsched_vms *)data, vcpu_id, vcpu_pid, PVSCHED_TP_VCPU_HALT);
}


int pvsched_attach_kvm_tracepoints(struct pvsched_vms *vms)
{
	register_trace_paravirt_vmentry(vcpu_vmentry_callback, vms);
	register_trace_paravirt_vmexit(vcpu_vmexit_callback, vms);
	register_trace_paravirt_vcpu_halt(vcpu_halt_callback, vms);
	register_trace_paravirt_vcpu_inject_intr(vcpu_inject_intr_callback, vms);
	return 0;
}

int pvsched_detach_kvm_tracepoints(struct pvsched_vms *vms)
{
	unregister_trace_paravirt_vmentry(vcpu_vmentry_callback, vms);
	unregister_trace_paravirt_vmexit(vcpu_vmexit_callback, vms);
	unregister_trace_paravirt_vcpu_halt(vcpu_halt_callback, vms);
	unregister_trace_paravirt_vcpu_inject_intr(vcpu_inject_intr_callback, vms);
	return 0;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("vineeth@bitbyteword.org");
MODULE_DESCRIPTION("Paravirt host driver");
