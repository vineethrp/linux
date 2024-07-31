// SPDX-License-Identifier: GPL-2.0+
/*
 *  Pvsched PCI Device Support
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

#include "pvsched.h"

#define PCI_VENDOR_ID_GOOGLE             0x1ae0
#define PCI_DEVICE_ID_GOOGLE_PVSCHED     0x5ced

static int pvsched_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct pvsched_instance *pi;
	void __iomem *base;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret < 0)
		return ret;

	base = pcim_iomap(pdev, PVSCHED_VCPUSCHED_BASE_BAR, 0);
	if (!base)
		return -ENOMEM;

	pi = devm_kmalloc(&pdev->dev, sizeof(*pi), GFP_KERNEL);
	if (!pi)
		return -ENOMEM;

	pi->vcpusched_base_bar = base;

	ret = pvsched_setup_vcpu_shm(pdev, pi);
	if (ret)
		return ret;

	return devm_pvsched_probe(&pdev->dev, pi);
}

static void pvsched_pci_remove(struct pci_dev *pdev) {
	pr_info("pvsched_pci_remove enter\n");
	pvsched_teardown_vcpu_shm(pdev);
	devm_kfree(&pdev->dev, dev_get_drvdata(&pdev->dev));
}

static const struct pci_device_id pvsched_pci_id_tbl[]  = {
	{ PCI_DEVICE(PCI_VENDOR_ID_GOOGLE, PCI_DEVICE_ID_GOOGLE_PVSCHED)},
	{}
};
MODULE_DEVICE_TABLE(pci, pvsched_pci_id_tbl);

static struct pci_driver pvsched_pci_driver = {
	.name =         "pvsched-pci",
	.id_table =     pvsched_pci_id_tbl,
	.probe =        pvsched_pci_probe,
	.remove =	pvsched_pci_remove,
};
module_pci_driver(pvsched_pci_driver);

MODULE_AUTHOR("Vineeth Pillai (Google) <vineeth@bitbyteword.org>");
MODULE_DESCRIPTION("pvsched device driver");
MODULE_LICENSE("GPL");

