// SPDX-License-Identifier: GPL-2.0+
/*
 *  Pvsched Device Support
 *
 *  Copyright (C) 2024 Google.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/highmem-internal.h>

#include <uapi/linux/sched/pvsched.h>

#include "pvsched.h"

int pvsched_setup_vcpu_shm(struct pci_dev *pdev, struct pvsched_instance *pi)
{
	struct page *vcpu_sched_page;
	union vcpu_sched *vcpu_sched_base;
	void __iomem *vcpucount_bar;
	u64 nr_vcpus; // As reported by host.
	int nr_cpus;

	vcpucount_bar = pcim_iomap(pdev, PVSCHED_VCPUCOUNT_BAR, 0);
	if (!vcpucount_bar)
		return -ENOMEM;
	nr_vcpus = ioread64(vcpucount_bar);
	nr_cpus = num_present_cpus();
	if (nr_vcpus != nr_cpus) {
		pr_warn("guest vcpus do not match VMM(%d != %llu)\n", nr_cpus, nr_vcpus);
		return -EFAULT;
	}

	pr_info("pvsched VCPUCOUNT_BAR - bus addr: %llx, vaddr: %p, nr_vcpus: %llu\n",
			pci_resource_start(pdev, PVSCHED_VCPUCOUNT_BAR), vcpucount_bar,nr_vcpus);
	pr_info("pvsched VCPUSCHED_BASE_BAR - bus addr: %llx, vaddr: %p\n",
			pci_resource_start(pdev, PVSCHED_VCPUSCHED_BASE_BAR),
			pi->vcpusched_base_bar);

	vcpu_sched_page = alloc_page(GFP_KERNEL);
	if (!vcpu_sched_page) {
		pr_warn("Failed to allocate vcpu_sched_page\n");
		return -ENOMEM;
	}
	vcpu_sched_base = (union vcpu_sched *)kmap(vcpu_sched_page);
	/*
	vcpu_sched_base = devm_kmalloc(&pdev->dev, sizeof(union vcpu_sched) * nr_cpus, GFP_KERNEL);
	if (!vcpu_sched_base) {
		pr_warn("Failed to allocate vcpu_sched shared mem!\n");
		return -ENOMEM;
	}
	*/

	for (int i = 0; i < nr_cpus; i++) {
		vcpu_sched_base[i].header.vcpu_id = i;
		pr_info("before: cpu: %d : vcpu_id: %d, pid: %d, [%X - %X]\n",
				i, vcpu_sched_base[i].header.vcpu_id, vcpu_sched_base[i].header.vcpu_pid,
				vcpu_sched_base[i].pad[PVSCHED_SHM_GA_INDEX], vcpu_sched_base[i].pad[PVSCHED_SHM_HA_INDEX]);
	}

	pi->vcpucount_bar = vcpucount_bar;
	pi->vcpu_sched_page = vcpu_sched_page;
	pi->vcpu_sched_base = vcpu_sched_base;
	pi->vcpu_sched_paddr = virt_to_phys(vcpu_sched_base);
	iowrite64(pi->vcpu_sched_paddr, pi->vcpusched_base_bar);
	pr_info("vcpu_sched_base=%p, vcpu_sched_paddr=%llX,  bar0= %llX\n",
			vcpu_sched_base, pi->vcpu_sched_paddr, ioread64(pi->vcpusched_base_bar));

	for (int i = 0; i < nr_cpus; i++) {
		pr_info("after: cpu: %d : vcpu_id: %d, pid: %d, [%X - %X]\n",
				i, vcpu_sched_base[i].header.vcpu_id, vcpu_sched_base[i].header.vcpu_pid,
				vcpu_sched_base[i].pad[PVSCHED_SHM_GA_INDEX], vcpu_sched_base[i].pad[PVSCHED_SHM_HA_INDEX]);
	}

	pvsched_attach_sched_callbacks(vcpu_sched_base);

	return 0;
}

void pvsched_teardown_vcpu_shm(struct pci_dev *pdev)
{
	struct pvsched_instance *pi = (struct pvsched_instance *)dev_get_drvdata(&pdev->dev);

	/*
	 * Ask the host to unmap the address.
	 */
	iowrite64(0, pi->vcpusched_base_bar);
	pvsched_detach_sched_callbacks(pi->vcpu_sched_base);
	free_page((unsigned long)pi->vcpu_sched_page);
}

static void pvsched_remove(void *param)
{
}

int devm_pvsched_probe(struct device *dev, struct pvsched_instance *pi)
{
	if (!pi || !pi->vcpusched_base_bar)
		return -EINVAL;

	dev_set_drvdata(dev, pi);

	return devm_add_action_or_reset(dev, pvsched_remove, pi);
}

static int pvsched_init(void)
{
	return 0;
}
module_init(pvsched_init);

static void pvsched_exit(void)
{

}
module_exit(pvsched_exit);

MODULE_AUTHOR("Vineeth Pillai (Google) <vineeth@bitbyteword.org>");
MODULE_DESCRIPTION("pvsched device driver");
MODULE_LICENSE("GPL");
