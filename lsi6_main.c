#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/fs.h>

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <linux/interrupt.h>

#include "k0607.h"
#include "lsi6.h"
#include "lsi6camac.h"
#include "lsi6_lib.h"

#undef DEBUG

#ifdef DEBUG
#define DP(x) x
#else
#define DP(x)
#endif

#define DRV_NAME	"lsi6"
#define DRV_VERSION	"1.06.1"
#define DRV_RELDATE	"23 Sep. 2010"
#define DRV_AUTHOR	"V.Mamkin, P.Cheblakov"

MODULE_AUTHOR(DRV_AUTHOR);
MODULE_DESCRIPTION("lsi6 - line serial interface for CAMAC");
MODULE_VERSION(DRV_VERSION ", " DRV_RELDATE);
MODULE_LICENSE("GPL");

static const char version[] =
KERN_INFO DRV_NAME " camac interface module, " DRV_VERSION ", " DRV_RELDATE ", " DRV_AUTHOR "\n";

static struct class *lsi6_class;

static lsi6_dev_t lsi6_dev[LSI6_NUMCARDS];

static int card_no = -1;

static spinlock_t camlock = SPIN_LOCK_UNLOCKED;
#define CAM_LOCK(x) spin_lock_irqsave(&camlock, x)
#define CAM_UNLOCK(x) spin_unlock_irqrestore(&camlock, x)

static struct pci_device_id lsi6_tbl[] = {
	{ LSI6_VENDOR_ID, LSI6_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, lsi6_tbl);

static int get_device_no(int major)
{
    int i;
    for (i = 0; i <= card_no; i++)
	if (lsi6_dev[i].major == major)
	    return i;
    
    return -1;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19)
static irqreturn_t lsi6_interrupt(int irq, void *dev_id)
#else
static irqreturn_t lsi6_interrupt(int irq, void *dev_id, struct pt_regs *unused)
#endif
{
    lsi6_dev_t *lsi = (lsi6_dev_t *)dev_id;
    lsi6_regs_t *lsi6_regs = (lsi6_regs_t *)lsi->base;
    int intr, lmr, mask, k, chnum;
    unsigned long flags;

    CAM_LOCK(flags);

    intr = readl(&lsi6_regs->intr);
    if (intr & 0x40) {
	writel(0, &lsi6_regs->intr_global);
	for (chnum = 0; chnum < LSI6_NUMCHANNELS; chnum++) {
	    if (intr & (1 << chnum)) {
		k0607_read_lmr(lsi, chnum, &lmr);
		for (k = 0, mask= 0x100; k < K0607_LGROUPS; k++, mask <<=1) {
		    if (lmr & mask) {
			wake_up_interruptible(&lsi->LWQ[chnum][k]);
			lsi->LWQ_flags[chnum][k] = 1;
		    }
		}
		k0607_write_lmr(lsi, chnum, (~(lmr >> 8)) & 0xff);
	    }
	}
	writel(1, &lsi6_regs->intr_global);
    }
    CAM_UNLOCK(flags);

    return (irqreturn_t)IRQ_HANDLED;
}
static int lsi6_open(struct inode * inode, struct file * file)
{
    unsigned long flags;
    int csr;
    unsigned int chnum = MINOR(inode->i_rdev);
    unsigned int card = get_device_no(MAJOR(inode->i_rdev));
    lsi6_dev_t *lsi;
    lsi6_regs_t *regs;
    
    if (card < 0)
	return -ENXIO;
    
    lsi = &lsi6_dev[card];
    regs = (lsi6_regs_t *)lsi->base;

    DP(printk(DRV_NAME ": open channel %d\n", chnum));

    if (!(readl(&regs->exist) & (1 << chnum))) return -1;
    CAM_LOCK(flags);
    if (k0607_read_csr(lsi, chnum, &csr) == -1) {
	CAM_UNLOCK(flags);
	return -1;
    }
    lsi->CSR[chnum] |= K0607_CSR_DE;
    k0607_write_csr(lsi, chnum, lsi->CSR[chnum]);
    CAM_UNLOCK(flags);

    file->private_data=kmalloc(sizeof(int),GFP_USER);
    *(int*)(file->private_data)=0;

    return 0;
}

static int lsi6_ioctl(struct file *file,
		    unsigned int cmd, unsigned long arg)
{
		struct inode *inode = file->f_dentry->d_inode;
    int chnum = MINOR(inode->i_rdev);
    unsigned long * ptr = (unsigned long * ) arg;
    unsigned long x;
    int n,a,f, rc;
    unsigned long flags;
    unsigned long volatile jiffies_start, jiffies_end;
    int jiffies_left;
    unsigned int card = get_device_no(MAJOR(inode->i_rdev));
    lsi6_dev_t *lsi;
    
    if (card < 0)
	return -ENXIO;
    
    lsi = &lsi6_dev[card];

    n = N_NAF(cmd);
    a = A_NAF(cmd);
    f = F_NAF(cmd);

    DP(printk(DRV_NAME ": ioctl channel = %d, n = %d, a=%d, f=%d\n",
	chnum, n,a,f));

    switch(cmd) {
	case CAMAC_NON_DATA: 
	    x = file->f_pos;
	    n = N_NAF(x);
	    a = A_NAF(x);
	    f = F_NAF(x);
	    if (n > 23) return -EINVAL;
	    CAM_LOCK(flags);
	    rc = lsi6_do_naf(lsi, chnum, n,a,f, &x);
	    CAM_UNLOCK(flags);
	    *(int *)(file->private_data)=rc;
	    return ( rc == -1 ) ? -EIO : rc;

	case CAMAC_STATUS: 
	    if(ptr != NULL) {
		x=*(int*)(file->private_data);
		if( copy_to_user(ptr, &x, sizeof(long))) 
		    return -EFAULT;
	    }
	    return 0;

	case CAMAC_ION: 
	    CAM_LOCK(flags);
	    lsi->CSR[chnum] |= K0607_CSR_IF;
	    rc = k0607_write_csr(lsi, chnum, lsi->CSR[chnum]);
	    CAM_UNLOCK(flags);
	    *(int *)(file->private_data)=rc;
	    return ( rc == -1 ) ? -EIO : rc;

	case CAMAC_IOFF: 
	    CAM_LOCK(flags);
	    lsi->CSR[chnum] &= ~K0607_CSR_IF;
	    rc = k0607_write_csr(lsi, chnum, lsi->CSR[chnum]);
	    CAM_UNLOCK(flags);
	    *(int *)(file->private_data)=rc;

	    return ( rc == -1 ) ? -EIO : rc;
    }

    if(n == CAMAC_NLAM) {
	int lgroup = F_NAF(cmd);
	int timeout = arg;

	if(lgroup >= K0607_LGROUPS ) return -EINVAL;

	if( timeout == 0 ) {
	    int req;
	    int mask = 0x100 << lgroup;
	    CAM_LOCK(flags);
	    k0607_read_lmr(lsi, chnum, &req);
	    CAM_UNLOCK(flags);
	    if (mask & req) return 0;
	    return -1;
	}

	CAM_LOCK(flags);
	lsi->LWQ_flags[chnum][lgroup] = 0;
	if (k0607_enable_lgroup(lsi, chnum, lgroup) == -1) {
	    CAM_UNLOCK(flags);
	    return -EIO;
	}
	CAM_UNLOCK(flags);
	if (timeout < 0) {
	    wait_event_interruptible(lsi->LWQ[chnum][lgroup], 
		lsi->LWQ_flags[chnum][lgroup]);
	    return 0;
	}
        jiffies_start = jiffies;
	wait_event_interruptible_timeout(lsi->LWQ[chnum][lgroup], 
		lsi->LWQ_flags[chnum][lgroup], timeout);

	jiffies_end = jiffies;
	
	if (!lsi->LWQ_flags[chnum][lgroup]) {
	    return -ETIME;
	}

	jiffies_left = timeout - (jiffies_end - jiffies_start);
	if (jiffies_left < 0) jiffies_left = 0;
	return jiffies_left;
    }

    if (n > 23) return -EINVAL;

    if (f < 8) { /* read */
	CAM_LOCK(flags);
	if (cmd & CAMAC_24) rc = lsi6_do_naf24(lsi, chnum, n,a,f, &x);
	else rc = lsi6_do_naf(lsi, chnum, n,a,f, &x);
	CAM_UNLOCK(flags);
	*(int *)(file->private_data)=rc;
	if(ptr) 
	    if( copy_to_user(ptr, &x, sizeof(long)))
	        return -EFAULT;
	return ( rc == -1 ) ? -EIO : rc;
    }
    else if ((f >= 16) && (f < 24)){
	/* write */
	if(ptr){
	    if( cmd & CAMAC_24 ) {
	        if( copy_from_user(&x, ptr, 3))
		    return -EFAULT;
		x &= 0xffffff;
	    } 
	    else {
	        if( copy_from_user(&x, ptr, 2))
		    return -EFAULT;
		x &= 0xffff;
	    }
	}
	else
	    return -EINVAL;

	CAM_LOCK(flags);
	if (cmd & CAMAC_24) rc = lsi6_do_naf24(lsi, chnum, n,a,f, &x);
	else rc = lsi6_do_naf(lsi, chnum, n,a,f, &x);
	CAM_UNLOCK(flags);

	*(int *)(file->private_data)=rc;
	return ( rc == -1 ) ? -EIO : rc;
    }
    else {
	x = 0;
	CAM_LOCK(flags);
	rc = lsi6_do_naf(lsi, chnum, n,a,f, &x);
	CAM_UNLOCK(flags);
	*(int *)(file->private_data)=rc;
	return ( rc == -1 ) ? -EIO : rc;
    }
    return 0;
}

static int lsi6_ioctl_locked(struct inode *inode, struct file *file,
		    unsigned int cmd, unsigned long arg) {
	return lsi6_ioctl(file, cmd, arg);
}

static ssize_t lsi6_read(struct file * file, char * buf,
		       size_t count, loff_t *ppos)
{
    unsigned int chnum=MINOR(file->f_dentry->d_inode->i_rdev);
    int naf = *ppos;
    int n,a,f, rc;
    unsigned long x;
    unsigned long flags;
    unsigned int card = get_device_no(MAJOR(file->f_dentry->d_inode->i_rdev));
    lsi6_dev_t *lsi;
    
    if (card < 0)
	return -ENXIO;
    
    lsi = &lsi6_dev[card];

    n = N_NAF(naf);
    a = A_NAF(naf);
    f = F_NAF(naf);

    DP(printk("camac: read(chnum=%d, n=%d, a=%d, f=%d)\n", chnum,n,a,f));

    if ((n > 23)||( f >= 8)) return -EINVAL;
    if (buf == NULL) return -EINVAL;
   
    if(count<3){
	CAM_LOCK(flags);
	rc = lsi6_do_naf(lsi, chnum, n,a,f, &x);
	CAM_UNLOCK(flags);
	*(int *)(file->private_data)=rc;
        if( copy_to_user(buf, &x, 2)) return -EFAULT;
	return ( rc == -1 ) ? -EIO : count;
    }
    else if(count == 3){
	CAM_LOCK(flags);
	rc = lsi6_do_naf24(lsi, chnum, n,a,f, &x);
	CAM_UNLOCK(flags);
	*(int *)(file->private_data)=rc;
	if( copy_to_user(buf, &x, 3)) return -EFAULT;
	return ( rc == -1 ) ? -EIO : count;
    }
    else {
	int count_done = count /4;
	unsigned long *tmpbuf;
	
	tmpbuf = kmalloc(count, GFP_USER);
	if (!tmpbuf) return -ENOMEM;
	
	CAM_LOCK(flags);
	if (naf & CAMAC_24) 
	    rc = lsi6_do_block24(lsi, chnum, n,a,f, tmpbuf, &count_done);
	else 
	    rc = lsi6_do_block(lsi, chnum, n,a,f, tmpbuf, &count_done);
	CAM_UNLOCK(flags);
	*(int *)(file->private_data)=rc;
	if (copy_to_user(buf, tmpbuf, count)) {
	    kfree(tmpbuf);
	    return -EFAULT;
	}
	kfree(tmpbuf);
	return ( rc == -1 ) ? -EIO : (count - count_done * 4);
    }

    return 0;
}
static ssize_t lsi6_write(struct file * file, const char * buf,
		        size_t count, loff_t *ppos)
{
    unsigned int chnum=MINOR(file->f_dentry->d_inode->i_rdev);
    int naf = *ppos;
    int n,a,f, rc;
    unsigned long x;
    unsigned long flags;
    unsigned int card = get_device_no(MAJOR(file->f_dentry->d_inode->i_rdev));
    lsi6_dev_t *lsi;
    
    if (card < 0)
	return -ENXIO;

    lsi = &lsi6_dev[card];

    n = N_NAF(naf);
    a = A_NAF(naf);
    f = F_NAF(naf);

    DP(printk("camac: write(chnum=%d, n=%d, a=%d, f=%d)\n", chnum,n,a,f));

    if ((n > 23)||(( f < 16)&&(f > 23))) return -EINVAL;

    if (buf == NULL) return -EINVAL;

    if(count<3){
	if( copy_from_user(&x, buf, 2))
	    return -EFAULT;
	x &= 0xffff;

	CAM_LOCK(flags);
	rc = lsi6_do_naf(lsi, chnum, n,a,f, &x);
	CAM_UNLOCK(flags);

	*(int *)(file->private_data)=rc;
	return ( rc == -1 ) ? -EIO : count;
    }
    else if(count==3){
	if( copy_from_user(&x, buf, 3))
	    return -EFAULT;
	x &= 0xffffff;
	CAM_LOCK(flags);
	rc = lsi6_do_naf24(lsi, chnum, n,a,f, &x);
	CAM_UNLOCK(flags);

	*(int *)(file->private_data)=rc;
	return ( rc == -1 ) ? -EIO : count;
    }
    else {
	int count_done = count /4;
    	unsigned long *tmpbuf = kmalloc(count,GFP_USER);
	if(!tmpbuf) return -ENOMEM;
	if( copy_from_user(tmpbuf, buf,count)){
	    kfree( tmpbuf );
	    return -EFAULT;
	}
	CAM_LOCK(flags);
	if (naf & CAMAC_24) 
	    rc = lsi6_do_block24(lsi, chnum, n,a,f, tmpbuf, &count_done);
	else 
	    rc = lsi6_do_block(lsi, chnum, n,a,f, tmpbuf, &count_done);
	CAM_UNLOCK(flags);
	*(int *)(file->private_data)=rc;
	kfree(tmpbuf);
	return ( rc == -1 ) ? -EIO : (count - count_done * 4);
    }
    return 0;
}
static loff_t lsi6_lseek(struct file * file, loff_t offset, int orig)
{
    if(orig == 0) {
	DP(printk(DRV_NAME ": llseek(n=%d, a=%d, f=%d)\n",
	    (int)N_NAF(offset), (int)A_NAF(offset),(int)F_NAF(offset)));

	file->f_pos = offset;
	return file->f_pos;
    }
    else
	return -EINVAL;
}
static int lsi6_release(struct inode * inode, struct file * file)
{
    DP(printk(DRV_NAME ": release\n"));

    kfree(file->private_data);

    return 0;
}
static struct file_operations lsi6_fops = {
	owner:		THIS_MODULE,
	write:		lsi6_write,
#ifdef HAVE_UNLOCKED_IOCTL
	unlocked_ioctl:		lsi6_ioctl,
#else
	ioctl:		lsi6_ioctl_locked,
#endif
	open:		lsi6_open,
	release:	lsi6_release,
	read:		lsi6_read,
	llseek:		lsi6_lseek,
};
static int lsi6_init_one (struct pci_dev *pdev,
				     const struct pci_device_id *ent)
{
    int i;
    int lsi6_major;
    lsi6_regs_t *regs;
    lsi6_dev_t *lsi;
    struct device *err_dev;

    card_no++;
    if (card_no > (LSI6_NUMCARDS - 1)) 
	return -ENODEV;

    lsi = &lsi6_dev[card_no];
    memset(lsi, 0, sizeof(lsi6_dev_t));

    i = pci_enable_device (pdev);
    if (i) {
	return i;
    }

    lsi->pciaddr = pci_resource_start (pdev, 0);
    lsi->irq = pdev->irq;
    DP(printk(DRV_NAME ": pciaddr = %lx, irq = %d\n", 
	lsi->pciaddr, lsi->irq));

    if (request_mem_region (lsi->pciaddr, LSI6_WINDOW_SIZE, DRV_NAME) == NULL) {
	printk (KERN_ERR DRV_NAME ": I/O resource 0x%x @ 0x%lx busy\n",
		LSI6_WINDOW_SIZE, lsi->pciaddr);
	return -EBUSY;
    }

    lsi->base = ioremap_nocache(lsi->pciaddr, LSI6_WINDOW_SIZE);
    if (!lsi->base) {
	printk(KERN_ERR DRV_NAME ": Can't map 0x%x @ 0x%lx\n",
	    LSI6_WINDOW_SIZE, lsi->pciaddr);
	goto error_with_release;
    }
    
    lsi6_major = register_chrdev(0, DRV_NAME, &lsi6_fops);
    if (lsi6_major < 0) {
	printk(KERN_ERR DRV_NAME ": unable to register device with error %d\n", lsi6_major);
	goto error_with_unmap;
    }
    
    lsi6_class = class_create(THIS_MODULE, DRV_NAME);
    
    for (i = 0; i < LSI6_NUMCHANNELS; i++) 
    {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	err_dev = device_create(lsi6_class, NULL, MKDEV(lsi6_major, i), NULL, LSI6_DEVNAME, card_no+1, i+1);
#else
	err_dev = device_create(lsi6_class, NULL, MKDEV(lsi6_major, i), LSI6_DEVNAME, card_no+1, i+1);
#endif
    }

    for (i = 0; i < LSI6_NUMCHANNELS; i++) {
	int k;
	for (k = 0; k < K0607_LGROUPS; k++) 
	    init_waitqueue_head(&lsi->LWQ[i][k]);
    }

    regs = (lsi6_regs_t *)lsi->base;
    writel(0, &regs->intr_global);

    if (request_irq(lsi->irq, lsi6_interrupt, IRQF_SHARED, DRV_NAME, lsi)) {
	printk (KERN_ERR DRV_NAME ": Can't request irq %d\n", lsi->irq);
	goto error_with_unmap;
    }

    writel(readl(&regs->exist), &regs->intr_enable);
    writel(1, &regs->intr_global);
    
    lsi->card = card_no;
    lsi->major = lsi6_major;
    pci_set_drvdata(pdev, lsi);

    return 0;

error_with_unmap:
    iounmap(lsi->base);
error_with_release:
    release_mem_region (lsi->pciaddr, LSI6_WINDOW_SIZE);
    return -ENODEV;
}

static void lsi6_remove_one (struct pci_dev *pdev)
{
    int i;
    int lsi6_major;
    lsi6_dev_t *lsi = pci_get_drvdata(pdev);
    lsi6_regs_t *regs = (lsi6_regs_t *)lsi->base;
    lsi6_major = lsi->major;

    writel(0, &regs->intr_global);
    
    for (i = 0; i < LSI6_NUMCHANNELS; i++) 
    {
	device_destroy(lsi6_class, MKDEV(lsi6_major, i));
    }
    class_unregister(lsi6_class);
    class_destroy(lsi6_class);
    unregister_chrdev(lsi6_major, DRV_NAME);

    free_irq(lsi->irq, lsi);

    iounmap(lsi->base);

    release_mem_region (lsi->pciaddr, LSI6_WINDOW_SIZE);
}

static struct pci_driver lsi6_driver = {
	name:		DRV_NAME,
	probe:		lsi6_init_one,
	remove:		lsi6_remove_one,
	id_table:	lsi6_tbl,
};

static int __init lsi6_init(void)
{
	printk(version);
	return pci_register_driver(&lsi6_driver);
}

static void __exit lsi6_cleanup(void)
{
	pci_unregister_driver (&lsi6_driver);
}

module_init(lsi6_init);
module_exit(lsi6_cleanup);

