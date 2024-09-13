#if !defined(_TRACE_PVSCHED_GUEST_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PVSCHED_GUEST_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM pvsched_guest

#if !defined(__TRACE_PVSCHED_GUEST_H)
#define __TRACE_PVSCHED_GUEST_H
typedef enum {
	PVSCHED_KERNCS_NMI = 0x1,
	PVSCHED_KERNCS_HARDIRQ = 0x2,
	PVSCHED_KERNCS_SOFTIRQ = 0x4,
	PVSCHED_KERNCS_PREEMPT_DISABLED = 0x8
} pvsched_kerncs_t ;
#endif

DECLARE_TRACE(pvsched_kerncs_entry,
		TP_PROTO(pvsched_kerncs_t cs_type),
		TP_ARGS(cs_type)
);
DECLARE_TRACE(pvsched_kerncs_exit,
		TP_PROTO(pvsched_kerncs_t cs_type),
		TP_ARGS(cs_type)
);

DECLARE_TRACE(pvsched_exit_to_user, TP_PROTO(struct task_struct *p), TP_ARGS(p));
#endif /* _TRACE_PVSCHED_GUEST_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

