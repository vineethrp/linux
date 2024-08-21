#if !defined(_TRACE_KVM_PVSCHED_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KVM_PVSCHED_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm_pvsched

DECLARE_TRACE(paravirt_vcpu_halt,
	TP_PROTO(unsigned vcpu_id, unsigned vcpu_pid),
	TP_ARGS(vcpu_id, vcpu_pid));
DECLARE_TRACE(paravirt_vcpu_inject_intr,
	TP_PROTO(unsigned vcpu_id, unsigned vcpu_pid),
	TP_ARGS(vcpu_id, vcpu_pid));

DECLARE_TRACE(paravirt_vmentry,
	TP_PROTO(unsigned int vcpu_id, unsigned int vcpu_pid),
	TP_ARGS(vcpu_id, vcpu_pid));
DECLARE_TRACE(paravirt_vmexit,
	TP_PROTO(unsigned int vcpu_id, unsigned int vcpu_pid),
	TP_ARGS(vcpu_id, vcpu_pid));
#endif /* _TRACE_KVM_PVSCHED_H */

/* This part must be outside protection */
#include <trace/define_trace.h>


