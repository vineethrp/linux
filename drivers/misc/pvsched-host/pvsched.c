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

#include <asm/errno.h>

#include "pvsched-host.h"

int pvsched_debug = 0;
module_param(pvsched_debug,int,0660);

static struct pvsched_vms *vms = NULL;

static int pvsched_dev_open(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;

	try_module_get(THIS_MODULE);

	return 0;
}

/* Called when a process closes the device file. */
static int pvsched_dev_release(struct inode *inode, struct file *filp)
{
	struct pvsched_vm *vm = (struct pvsched_vm *)filp->private_data;

	if (vm) {
		pvsched_vm_free(vms, vm->vm_id);
	}
	module_put(THIS_MODULE);

	return 0;
}

static ssize_t pvsched_dev_read(struct file *filp, /* see include/linux/fs.h   */
                           char __user *buffer, /* buffer to fill with data */
                           size_t length, /* length of the buffer     */
                           loff_t *offset)
{
	return -ENOTSUPP;
}

static ssize_t pvsched_dev_write(struct file *filp, const char __user *buff,
                            size_t len, loff_t *off)
{
	return -ENOTSUPP;
}

static long pvsched_dev_ioctl(struct file *filp, unsigned int cmd,
                             unsigned long arg)
{
	int ret = 0;
	struct pvsched_info info;

	switch (cmd) {
		case PVSCHED_IOCTL_SET_INFO:
			if (copy_from_user(&info, (int __user *)arg, sizeof(info))) {
				ret = -EFAULT;
				goto out;
			}
			pr_info("vm_id=%u, nr_vcpus=%u, sched_base=%llX\n",
					info.vm_id, info.nr_vcpus, info.vcpu_sched_base);
			struct pvsched_vm *vm = 
				pvsched_vm_init(vms, info.vm_id, info.vcpu_sched_base, info.nr_vcpus);
			if (ret) {
				pr_err("Failed to map the vcpu_sched to driver!\n");
				ret = -EFAULT;
			}
			filp->private_data = vm;
			pvsched_print_vm(vm);
			break;
		case PVSCHED_IOCTL_UNSET_INFO:
			if (copy_from_user(&info, (int __user *)arg, sizeof(info))) {
				ret = -EFAULT;
				goto out;
			}
			pvsched_vm_free(vms, info.vm_id);
			filp->private_data = NULL;
			break;
		default:
			ret = -ENOTTY;
	}
out:
	return ret;
}

static const struct file_operations pvsched_fops = {
	.owner	 = THIS_MODULE,
	.open    = pvsched_dev_open,
	.read    = pvsched_dev_read,
	.write   = pvsched_dev_write,
	.release = pvsched_dev_release,
	.unlocked_ioctl = pvsched_dev_ioctl,
};

static char *pvsched_devnode(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0666;
	return kasprintf(GFP_KERNEL, "%s", dev_name(dev));
}

static struct class *pvsched_dev_class;
static int pvsched_major = 0;
static int pvsched_minor = 0;

static __init int pvsched_init(void) 
{ 
	pr_info("pvsched host module loaded!\n");

	vms = pvsched_vms_init();
	if (!vms) {
		pr_warn("Failed to allocate pvsched_vms!\n");
		return -1;
	}

	pvsched_major = register_chrdev(0, "pvsched", &pvsched_fops);
	if (pvsched_major < 0) {
		pr_err("register character device failed\n");
		return pvsched_major;
	}

	pvsched_dev_class = class_create("pvsched");
	if (IS_ERR(pvsched_dev_class))
		return PTR_ERR(pvsched_dev_class);

	pvsched_dev_class->devnode = pvsched_devnode;

	device_create(pvsched_dev_class, NULL, MKDEV(pvsched_major, pvsched_minor),
		      NULL, "pvsched%d", pvsched_minor);

	pvsched_attach_kvm_tracepoints(vms);

	return 0; 
} 
 
static __exit void pvsched_fini(void) 
{
	pvsched_detach_kvm_tracepoints(vms);
	pvsched_vms_free(vms);
	device_destroy(pvsched_dev_class, MKDEV(pvsched_major, pvsched_minor));
	class_destroy(pvsched_dev_class);
	unregister_chrdev(pvsched_major, "pvsched");
	pr_info("pvsched host module unloaded!\n");
} 

module_init(pvsched_init);
module_exit(pvsched_fini);
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("vineeth@bitbyteword.org");
MODULE_DESCRIPTION("Paravirt host driver");
