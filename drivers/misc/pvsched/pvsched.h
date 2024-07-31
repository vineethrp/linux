// SPDX-License-Identifier: GPL-2.0+
/*
 *  Pvsched Device Support
 *
 *  Copyright (C) 2024 Google.
 */

#ifndef PVSCHED_H_
#define PVSCHED_H_

#define PVSCHED_VCPUCOUNT_BAR		0
#define PVSCHED_VCPUSCHED_BASE_BAR	1

struct pvsched_instance {
	void __iomem *vcpusched_base_bar;
	void __iomem *vcpucount_bar;
	void *vcpu_sched_base;
	struct page *vcpu_sched_page;
	phys_addr_t vcpu_sched_paddr;
};

int pvsched_setup_vcpu_shm(struct pci_dev *pdev, struct pvsched_instance *pi);
void pvsched_teardown_vcpu_shm(struct pci_dev *pdev);

int devm_pvsched_probe(struct device *dev, struct pvsched_instance *pi);

void pvsched_attach_sched_callbacks(union vcpu_sched *sched);
void pvsched_detach_sched_callbacks(union vcpu_sched *sched);
#endif /* PVSCHED_H_ */
