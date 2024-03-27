// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024 Google  */

/*
 *  Paravirt scheduling framework
 *
 */

/*
 * Heavily inspired from tcp congestion avoidance implementation.
 * (net/ipv4/tcp_cong.c)
 */

#define pr_fmt(fmt) "PVSCHED: " fmt

#include <linux/module.h>
#include <linux/bpf.h>
#include <linux/gfp.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/jhash.h>
#include <linux/pvsched.h>

static DEFINE_SPINLOCK(pvsched_drv_list_lock);
static int nr_pvsched_drivers = 0;
static LIST_HEAD(pvsched_drv_list);

static struct pvsched_vcpu_ops *pvsched_find_vcpu_ops_name(char *name)
{
	struct pvsched_vcpu_ops *ops;

	list_for_each_entry_rcu(ops, &pvsched_drv_list, list) {
		if (strcmp(ops->name, name) == 0)
			return ops;
	}

	return NULL;
}

static struct pvsched_vcpu_ops *pvsched_find_vcpu_ops_key(u32 key)
{
	struct pvsched_vcpu_ops *ops;

	list_for_each_entry_rcu(ops, &pvsched_drv_list, list) {
		if (ops->key == key)
			return ops;
	}

	return NULL;
}

void pvsched_get_available_drivers(char *buf, size_t maxlen)
{
	struct pvsched_vcpu_ops *ops;
	size_t offs = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(ops, &pvsched_drv_list, list) {
		offs += snprintf(buf + offs, maxlen - offs,
				 "%s%s",
				 offs == 0 ? "" : " ", ops->name);

		if (WARN_ON_ONCE(offs >= maxlen))
			break;
	}
	rcu_read_unlock();
}

int pvsched_register_vcpu_ops(struct pvsched_vcpu_ops *ops)
{
	int ret = 0;

	ops->key = jhash(ops->name, sizeof(ops->name), strlen(ops->name));
	spin_lock(&pvsched_drv_list_lock);
	if (nr_pvsched_drivers > PVSCHED_MAX) {
		ret = -ENOSPC;
	} if (pvsched_find_vcpu_ops_key(ops->key)) {
		ret = -EEXIST;
	} else if (!(ret = pvsched_validate_vcpu_ops(ops))) {
		list_add_tail_rcu(&ops->list, &pvsched_drv_list);
		nr_pvsched_drivers++;
	}
	spin_unlock(&pvsched_drv_list_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(pvsched_register_vcpu_ops);

void pvsched_unregister_vcpu_ops(struct pvsched_vcpu_ops *ops)
{
	spin_lock(&pvsched_drv_list_lock);
	list_del_rcu(&ops->list);
	nr_pvsched_drivers--;
	spin_unlock(&pvsched_drv_list_lock);

	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(pvsched_unregister_vcpu_ops);

struct pvsched_vcpu_ops *pvsched_get_vcpu_ops(char *name)
{
	struct pvsched_vcpu_ops *ops;

	if (!name || (strlen(name) >= PVSCHED_NAME_MAX))
		return NULL;

	rcu_read_lock();
	ops = pvsched_find_vcpu_ops_name(name);
	if (!ops)
		goto out;

	if (unlikely(!bpf_try_module_get(ops, ops->owner))) {
		ops = NULL;
		goto out;
	}

out:
	rcu_read_unlock();
	return ops;
}
EXPORT_SYMBOL_GPL(pvsched_get_vcpu_ops);

void pvsched_put_vcpu_ops(struct pvsched_vcpu_ops *ops)
{
	bpf_module_put(ops, ops->owner);
}
EXPORT_SYMBOL_GPL(pvsched_put_vcpu_ops);

/*
 * NOP vm_ops Sample implementation.
 */
static int nop_pvsched_vcpu_register(struct pid *pid)
{
	return 0;
}
static void nop_pvsched_vcpu_unregister(struct pid *pid)
{
}
static void nop_pvsched_notify_event(void *addr, struct pid *pid, u32 event)
{
}

struct pvsched_vcpu_ops nop_vcpu_ops = {
	.events = PVSCHED_VCPU_VMENTER | PVSCHED_VCPU_VMEXIT | PVSCHED_VCPU_HALT,
	.pvsched_vcpu_register = nop_pvsched_vcpu_register,
	.pvsched_vcpu_unregister = nop_pvsched_vcpu_unregister,
	.pvsched_vcpu_notify_event = nop_pvsched_notify_event,
	.name = "pvsched_nop",
	.owner = THIS_MODULE,
};

static int __init pvsched_init(void)
{
	return WARN_ON(pvsched_register_vcpu_ops(&nop_vcpu_ops));
}

late_initcall(pvsched_init);
