#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/printk.h>
#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <uapi/linux/sched/pvsched.h>

#include <asm/errno.h>
#include <asm/cacheflush.h>

#include "pvsched-host.h"

static int pvsched_vcpus_insert(struct pvsched_vms *vms, struct pvsched_vcpu *vcpu)
{
	hash_add(vms->pvsched_vcpus, &vcpu->node, vcpu->vcpu_pid);
	return 0;
}

struct pvsched_vcpu *pvsched_vcpus_get_vcpu(struct pvsched_vms *vms, unsigned int vcpu_pid)
{
	struct pvsched_vcpu *vcpu = NULL;
	hash_for_each_possible(vms->pvsched_vcpus, vcpu, node, vcpu_pid) {
		if (vcpu->vcpu_pid == vcpu_pid) {
			return vcpu;
		}
	}
	return NULL;
}

static int pvsched_vm_insert(struct pvsched_vms *vms, struct pvsched_vm *vm) {
	int ret = 0;
	spin_lock(&vms->vms_lock);

	list_add(&vm->vms_list, &vms->vms_list_head);
	spin_unlock(&vms->vms_lock);
	return ret;
}

static struct pvsched_vm *pvsched_vm_remove(struct pvsched_vms *vms, unsigned int vm_id) {
	struct list_head *item, *tmp;
	struct pvsched_vm *vm;
	bool vm_found = false;
	spin_lock(&vms->vms_lock);
	list_for_each_safe(item, tmp, &vms->vms_list_head) {
		vm = list_entry(item, struct pvsched_vm, vms_list);
		if (vm->vm_id == vm_id) {
			list_del(item);
			vm_found = true;
			break;
		}
	}
	spin_unlock(&vms->vms_lock);

	return vm_found ? vm : NULL;
}

static int pvsched_map_schedinfo(struct pvsched_vm_pages *vm_pages, unsigned long uaddr, u32 nr_vcpus)
{
	size_t count = nr_vcpus * sizeof(union vcpu_sched);
	unsigned long end = (uaddr + count + PAGE_SIZE - 1) >> PAGE_SHIFT;
	unsigned long start = uaddr >> PAGE_SHIFT;
	const int nr_pages = end - start;
	int res, i, j;
	struct page **pages;

	if ((uaddr + count) < uaddr)
		return -1;

	if (nr_pages > PVSCHED_MAX_VMPAGES)
		return -1;

	if (count == 0)
		return -1;

	if ((pages = kmalloc(nr_pages * sizeof(*pages), GFP_KERNEL)) == NULL)
		return -1;

	res = get_user_pages_unlocked(
		uaddr,
		nr_pages,
		pages,
		FOLL_WRITE);

	if (res < nr_pages)
		goto out_umap;

	for (i = 0; i < nr_pages; i++) {
		flush_dcache_page(pages[i]);
	}

	vm_pages->offset = uaddr &~PAGE_MASK;
	vm_pages->mapped_pages = pages;
	vm_pages->nr_pages = nr_pages;
	pr_info("mapped pages: %p, nr_pages: %d, offset: %lu\n", 
			vm_pages->mapped_pages, vm_pages->nr_pages, vm_pages->offset);

	return 0;

out_umap:
	if (res > 0) {
		for (j = 0; j < res; j++)
			put_page(pages[j]);
		res = 0;
	}
	kfree(pages);
	return -1;
}

static inline union vcpu_sched *pvsched_vcpu_sched_base_addr(struct pvsched_vm_pages *vm_pages)
{
	if (!vm_pages->mapped_pages)
		return NULL;
	return (union vcpu_sched *)(page_address(vm_pages->mapped_pages[0]) + vm_pages->offset);
}

static int pvsched_unmap_schedinfo(struct pvsched_vm_pages *pages)
{
	int i;

	if (!pages || !pages->mapped_pages)
		return 0;

	for (i = 0; i < pages->nr_pages; i++) {
		struct page *page = pages->mapped_pages[i];

		put_page(page);
	}
	kfree(pages->mapped_pages);
	pages->mapped_pages = NULL;

	return 0;
}

struct pvsched_vm *pvsched_vm_init(struct pvsched_vms *vms, unsigned int vm_id, unsigned long uaddr, u32 nr_vcpus)
{
	struct pvsched_vm *vm = kzalloc(sizeof(struct pvsched_vm), GFP_KERNEL);
	if (pvsched_map_schedinfo(&vm->vm_pages, uaddr, nr_vcpus) < 0) {
		kfree(vm);
		return NULL;
	}
	vm->vcpu_sched_base = pvsched_vcpu_sched_base_addr(&vm->vm_pages);
	vm->vm_id = vm_id;
	vm->nr_vcpus = nr_vcpus;

	vm->vcpus = kzalloc(sizeof(struct pvsched_vcpu) * nr_vcpus, GFP_KERNEL);
	if (!vm->vcpus) {
		pvsched_unmap_schedinfo(&vm->vm_pages);
		kfree(vm);
		return NULL;
	}
	for (int i = 0; i < nr_vcpus; i++) {
		union vcpu_sched *sched = vm->vcpu_sched_base + i;
		vm->vcpus[i].sched = sched;
		vm->vcpus[i].vcpu_id = sched->header.vcpu_id;
		vm->vcpus[i].vcpu_pid = sched->header.vcpu_pid;
		vm->vcpus[i].vm_id = vm_id;
		pr_info("id=%u, pid=%u, vm_id=%u, sched_base=%p, sched=%p\n",
				vm->vcpus[i].vcpu_id, vm->vcpus[i].vcpu_pid,
				vm->vcpus[i].vm_id,
				vm->vcpu_sched_base, sched);

		pvsched_vcpus_insert(vms, &(vm->vcpus[i]));
	}

	pvsched_vm_insert(vms, vm);
	return vm;

}

void pvsched_vm_free(struct pvsched_vms *vms, unsigned int vm_id)
{
	struct pvsched_vm *vm = pvsched_vm_remove(vms, vm_id);
	if (vm) {
		pvsched_unmap_schedinfo(&vm->vm_pages);
		kfree(vm);
	}
}

struct pvsched_vms *pvsched_vms_init(void)
{
	struct pvsched_vms *vms = kzalloc(sizeof(struct pvsched_vms), GFP_KERNEL);
	if (!vms) {
		pr_err("Failed to allocate driver private data!\n");
		return NULL;
	}
	vms->vms_list_head = (struct list_head) LIST_HEAD_INIT(vms->vms_list_head);
	vms->vms_lock = __SPIN_LOCK_UNLOCKED(vms->vms_lock);
	hash_init(vms->pvsched_vcpus);
	return vms;
}

void pvsched_vms_free(struct pvsched_vms *vms)
{
	struct list_head *item, *tmp;

	spin_lock(&vms->vms_lock);
	list_for_each_safe(item, tmp, &vms->vms_list_head) {
		struct pvsched_vm *vm = list_entry(item, struct pvsched_vm, vms_list);
		pr_info("removing vm [id=%u, nr_cpus=%u]\n", vm->vm_id, vm->nr_vcpus);
		list_del(item);
		pvsched_unmap_schedinfo(&vm->vm_pages);
		kfree(vm);
	}
	spin_unlock(&vms->vms_lock);
	kfree(vms);
}

void pvsched_print_vm(struct pvsched_vm *vm)
{
	for (int i = 0; i < vm->nr_vcpus; i++) {
		pr_info("vcpu %d: vcpu_id: %u, pid: %u, pad1: 0X%X, pad2: 0X%X\n",
				i, vm->vcpu_sched_base[i].header.vcpu_id, vm->vcpu_sched_base[i].header.vcpu_pid,
				vm->vcpu_sched_base[i].pad[2], vm->vcpu_sched_base[i].pad[3]);
	}
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("vineeth@bitbyteword.org");
MODULE_DESCRIPTION("Paravirt host driver");
