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
#define DRV_VERSION	"1.06.2"
#define DRV_RELDATE	"23 Sep. 2010"
#define DRV_AUTHOR	"V.Mamkin, P.Cheblakov, V.Gulevich"

MODULE_AUTHOR(DRV_AUTHOR);
MODULE_DESCRIPTION("lsi6 - line serial interface for CAMAC");
MODULE_VERSION(DRV_VERSION ", " DRV_RELDATE);
MODULE_LICENSE("GPL");

static const char version[] =
KERN_INFO DRV_NAME " camac interface module, " DRV_VERSION ", " DRV_RELDATE ", " DRV_AUTHOR "\n";

static struct class *lsi6_class = 0;

static lsi6_dev_t lsi6_dev[LSI6_NUMCARDS];

static int card_no = -1;

// Returns 1 on success.
// Caller should care about mutex unlock after successful lock.
//Jiffies argument is reserved  for future use. It should be -1 for now (infinite wait)
static int __must_check lsi6_channel_lock_timeout(lsi6_channel * channel, long jiffies) {
    if (!channel) {
        printk(DRV_NAME "lsi6_channel_lock_timeout was given null argument");
        return 0;
    }
    if (mutex_trylock(&channel->mutex))
        return 1;
    DP(printk(DRV_NAME "Failed to acquire mutex immediately."));
    if (mutex_lock_interruptible(&channel->mutex) == 0)
        return 1;
    DP(printk(DRV_NAME ": Wait for channel was interrupted\n"));
    return 0;
}

static void lsi6_channel_unlock(lsi6_channel * channel) {
    if (!channel) {
        printk(DRV_NAME "lsi6_channel_unlock was given null argument");
        return;
    }
    mutex_unlock(&channel->mutex);
}

#define CHAN_LOCK_OR(x) if (!lsi6_channel_lock_timeout(channel, -1)) {x;}
#define CHAN_LOCK_OR_BUSY CHAN_LOCK_OR(return -EBUSY)
#define CHAN_UNLOCK lsi6_channel_unlock(channel)


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
static void lsi6_handleChannelInterrupt(lsi6_channel * channel);
static spinlock_t intrlock = SPIN_LOCK_UNLOCKED;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19)
static irqreturn_t lsi6_interrupt(int irq, void *dev_id)
#else
static irqreturn_t lsi6_interrupt(int irq, void *dev_id, struct pt_regs *unused)
#endif
{
    lsi6_dev_t *lsi = (lsi6_dev_t *)dev_id;
    lsi6_regs_t *lsi6_regs = (lsi6_regs_t *)lsi->base;
    int intr, chnum;
    unsigned long interflags;

    spin_lock_irqsave(&intrlock, interflags);
    intr = readl(&lsi6_regs->intr);
    if (intr & 0x40) {
        unsigned enabled;
        writel(0, &lsi6_regs->intr_global);
        enabled = readl(&lsi6_regs->intr_enable);
        // Bitmask of enabled channel interrupts
        // We are disabling them at the end of this function to reenable in scheduled interruptHandler
        // Note that manipulations with intr_enable should be protected by intrlock
	    for (chnum = 0; chnum < LSI6_NUMCHANNELS; chnum++) {
            lsi6_channel * channel = &lsi->channels[chnum];
	        if (intr & (1 << chnum)) {
                if (0) {
                    lsi6_handleChannelInterrupt(channel);
                } else {
                    enabled &= ~(1 << chnum);
                    schedule_work(&channel->interruptHandler);
                }
	        }
	    }
        writel(enabled, &lsi6_regs->intr_enable);
        writel(1, &lsi6_regs->intr_global);
    }
    spin_unlock_irqrestore(&intrlock, interflags);

    return (irqreturn_t)IRQ_HANDLED;
}

//Returns 1 if channel isvalid, 0 otherwise
static int isValidChannel(lsi6_channel * channel) {
    int rv = 0, i, k;
    for (i = 0; i <= card_no && i < LSI6_NUMCARDS; ++i) {
        for (k = 0; k < LSI6_NUMCHANNELS; ++k) {
            if (&lsi6_dev[i].channels[k] == channel) {
                rv = 1; // a channel was found in global array.
                break;
            }
        }
        if (rv)
            break;
    }
    if (!rv) {
        printk(DRV_NAME ": channel %p can't be found\n", channel);
        return 0;
    }
    if (channel->lsi != &lsi6_dev[i]) {
        printk(DRV_NAME ": channel %p in card %p has wrong lsi field %p\n", &lsi6_dev[i].channels[k], &lsi6_dev[i], channel->lsi);
        return 0;
    }
    return 1;
}

//Scheduled by lsi6_interrupt
//Is stored in lsi6_dev_t::interruptHandler
static void lsi6_interrupt_bottom_half(struct work_struct *work) {
    lsi6_channel * channel = container_of(work, lsi6_channel, interruptHandler);
    lsi6_handleChannelInterrupt(channel);
}

static void lsi6_handleChannelInterrupt(lsi6_channel * channel) {
    lsi6_dev_t * lsi = channel->lsi;
    lsi6_regs_t *lsi6_regs = (lsi6_regs_t *)lsi->base;
    int k, chnum = channel - lsi->channels;
    unsigned long interflags;
    #ifdef DEBUG
    static int fail = 0;
    if (fail) return;
    if (!isValidChannel(channel)) {
        fail=1;
        return;
    }
    #endif
    if (!channel) {
        printk(DRV_NAME ": null channel reference in lsi6_handleChannelInterrupt\n");
        return;
    }
    if (chnum<0 || chnum >= LSI6_NUMCHANNELS) {
        printk(DRV_NAME ": bad channel number %d is calculated in lsi6_handleChannelInterrupt\n", chnum);
        return;
    }
    if (!lsi) {
        printk(DRV_NAME ": null device reference in lsi6_handleChannelInterrupt\n");
        return;
    }

    CHAN_LOCK_OR(return);
    if (0) {
        printk(DRV_NAME ": timeout while locking channel in bottom half IRQ handler. The interrupt was lost.\n");
        //TODO: consider implementing a real timeout here
    } else {
        // Read and reset lam register.
        int lmr = 0;
        if (k0607_read_lmr(lsi, chnum, &lmr) != -1) {
            int mask = lmr & 0xff; //First byte of mask/requests contains enabled groups
            int reqs = (lmr >> 8) & 0xff; //Second byte of mask/requests contains group requests
            DP(printk(DRV_NAME ": mask/requests register in channel %d: %x\n", chnum, lmr));
            reqs &= mask;
            for (k = 0; k < K0607_LGROUPS; k++) {
                if (reqs & (1 << k)) {
                    DP(printk(DRV_NAME ": lam in channel %d, group %d\n", chnum, k));
                    wake_up_interruptible(&lsi->LWQ[chnum][k]);
                    lsi->LWQ_flags[chnum][k] = 1;
                }
            }
            // Disabling corresponding LAM groups (the first byte of the same register)
            k0607_write_lmr(lsi, chnum, mask & (~reqs));            
        }
    }
    CHAN_UNLOCK;
    
    //Reenabling interrupts disabled in interrupt handler
    spin_lock_irqsave(&intrlock, interflags);
    {
        unsigned enabled = readl(&lsi6_regs->intr_enable);
        writel(enabled | (1 << chnum), &lsi6_regs->intr_enable);
    }
    spin_unlock_irqrestore(&intrlock, interflags);
}

static int lsi6_open(struct inode * inode, struct file * file)
{
    int csr;
    unsigned int chnum = MINOR(inode->i_rdev);
    unsigned int card = get_device_no(MAJOR(inode->i_rdev));
    lsi6_channel * channel;
    lsi6_dev_t *lsi;
    lsi6_regs_t *regs;
    
    if (card < 0)
	return -ENXIO;
    
    lsi = &lsi6_dev[card];
    regs = (lsi6_regs_t *)lsi->base;
    channel = &lsi->channels[chnum];

    DP(printk(DRV_NAME ": open channel %d\n", chnum));

    if (!(readl(&regs->exist) & (1 << chnum)))
        return -1;
    CHAN_LOCK_OR_BUSY;
    if (k0607_read_csr(lsi, chnum, &csr) == -1) {
        CHAN_UNLOCK;
        return -1;
    }
    lsi->CSR[chnum] |= K0607_CSR_DE;
    k0607_write_csr(lsi, chnum, lsi->CSR[chnum]);
    CHAN_UNLOCK;

    file->private_data=kmalloc(sizeof(int),GFP_USER);
    *(int*)(file->private_data)=0;

    return 0;
}

static long lsi6_ioctl(struct file *file,
		    unsigned int cmd, unsigned long arg)
{
    struct inode *inode = file->f_dentry->d_inode;
    int chnum = MINOR(inode->i_rdev);
    unsigned long * ptr = (unsigned long * ) arg;
    unsigned long x;
    int n,a,f, rc;
    unsigned long volatile jiffies_start, jiffies_end;
    int jiffies_left;
    unsigned int card = get_device_no(MAJOR(inode->i_rdev));
    lsi6_dev_t *lsi;
    lsi6_channel * channel;
    
    if (card < 0)
        return -ENXIO;
    
    lsi = &lsi6_dev[card];
    channel = &lsi->channels[chnum];

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
	    CHAN_LOCK_OR_BUSY;
	    rc = lsi6_do_naf(lsi, chnum, n,a,f, &x);
	    CHAN_UNLOCK;
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
	    CHAN_LOCK_OR_BUSY;
	    lsi->CSR[chnum] |= K0607_CSR_IF;
	    rc = k0607_write_csr(lsi, chnum, lsi->CSR[chnum]);
	    CHAN_UNLOCK;
	    *(int *)(file->private_data)=rc;
	    return ( rc == -1 ) ? -EIO : rc;

	case CAMAC_IOFF: 
        CHAN_LOCK_OR_BUSY;
	    lsi->CSR[chnum] &= ~K0607_CSR_IF;
	    rc = k0607_write_csr(lsi, chnum, lsi->CSR[chnum]);
	    CHAN_UNLOCK;
	    *(int *)(file->private_data)=rc;

	    return ( rc == -1 ) ? -EIO : rc;
    }

    if(n == CAMAC_NLAM) { //Wait for LAM
        int lgroup = F_NAF(cmd);
        int timeout = arg;
    	if(lgroup >= K0607_LGROUPS )
            return -EINVAL;

	    if( timeout == 0 ) {
	        int req;
	        int mask = 0x100 << lgroup;
	        CHAN_LOCK_OR_BUSY;
	        k0607_read_lmr(lsi, chnum, &req);
	        CHAN_UNLOCK;
	        if (mask & req) return 0;
	        return -1;
	    }

        CHAN_LOCK_OR_BUSY;
	    lsi->LWQ_flags[chnum][lgroup] = 0;
	    if (k0607_enable_lgroup(lsi, chnum, lgroup) == -1) {
	        CHAN_UNLOCK;
	        return -EIO;
	    }
	    CHAN_UNLOCK;

	    if (timeout < 0) {
	        wait_event_interruptible(lsi->LWQ[chnum][lgroup], 
		    lsi->LWQ_flags[chnum][lgroup]);
	        return 0;
	    }
        jiffies_start = jiffies;
        wait_event_interruptible_timeout(lsi->LWQ[chnum][lgroup], lsi->LWQ_flags[chnum][lgroup], timeout);
        jiffies_end = jiffies;
	
	    if (!lsi->LWQ_flags[chnum][lgroup]) {
	        return -ETIME;
	    }

	    jiffies_left = timeout - (jiffies_end - jiffies_start);
	    if (jiffies_left < 0)
            jiffies_left = 0;

	    return jiffies_left;
    } // if(n == CAMAC_NLAM)

    if (n > 23)
        return -EINVAL;

    if (f < 8) { /* read */
        if (!ptr)
            return -EFAULT;
    	CHAN_LOCK_OR_BUSY;
        if (cmd & CAMAC_24) {
            rc = lsi6_do_naf24(lsi, chnum, n,a,f, &x);
        } else {
            rc = lsi6_do_naf(lsi, chnum, n,a,f, &x);
        }
        CHAN_UNLOCK;

        *(int *)(file->private_data)=rc;
        if( copy_to_user(ptr, &x, sizeof(long)))
            return -EFAULT;
        return ( rc == -1 ) ? -EIO : rc;
    } else if ((f >= 16) && (f < 24)) { /* write */        
        if (!ptr)
            return -EFAULT;
        if( cmd & CAMAC_24 ) {
            if(copy_from_user(&x, ptr, 3))
                return -EFAULT;
            x &= 0xffffff;
        } else {
            if(copy_from_user(&x, ptr, 2))
                return -EFAULT;
            x &= 0xffff;
	    }
        CHAN_LOCK_OR_BUSY;
        if (cmd & CAMAC_24)
            rc = lsi6_do_naf24(lsi, chnum, n,a,f, &x);
        else
            rc = lsi6_do_naf(lsi, chnum, n,a,f, &x);
        CHAN_UNLOCK;

        *(int *)(file->private_data)=rc;
        return ( rc == -1 ) ? -EIO : rc;
    } else {
        x = 0;
        CHAN_LOCK_OR_BUSY;
        rc = lsi6_do_naf(lsi, chnum, n,a,f, &x);
        CHAN_UNLOCK;
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
    unsigned int card = get_device_no(MAJOR(file->f_dentry->d_inode->i_rdev));
    lsi6_dev_t *lsi;
    lsi6_channel * channel;
    
    if (card < 0)
        return -ENXIO;
    
    lsi = &lsi6_dev[card];
    channel = &lsi->channels[chnum];

    n = N_NAF(naf);
    a = A_NAF(naf);
    f = F_NAF(naf);

    DP(printk("camac: read(chnum=%d, n=%d, a=%d, f=%d)\n", chnum,n,a,f));

    if ((n > 23)||( f >= 8))
        return -EINVAL;
    if (buf == NULL)
        return -EINVAL;
   
    if(count < 3){
        CHAN_LOCK_OR_BUSY;
        rc = lsi6_do_naf(lsi, chnum, n,a,f, &x);
        CHAN_UNLOCK;
        *(int *)(file->private_data)=rc;
        if( copy_to_user(buf, &x, 2))
            return -EFAULT;
        return ( rc == -1 ) ? -EIO : count;
    } else if(count == 3) {
        CHAN_LOCK_OR_BUSY;
        rc = lsi6_do_naf24(lsi, chnum, n,a,f, &x);
        CHAN_UNLOCK;
        *(int *)(file->private_data)=rc;
        if( copy_to_user(buf, &x, 3))
            return -EFAULT;
        return ( rc == -1 ) ? -EIO : count;
    } else {
        int count_done = count /4;
        unsigned long *tmpbuf;

        tmpbuf = kmalloc(count, GFP_USER);
        if (!tmpbuf)
            return -ENOMEM;

        CHAN_LOCK_OR_BUSY;
        if (naf & CAMAC_24) 
            rc = lsi6_do_block24(lsi, chnum, n,a,f, tmpbuf, &count_done);
        else 
            rc = lsi6_do_block(lsi, chnum, n,a,f, tmpbuf, &count_done);
        CHAN_UNLOCK;
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
    unsigned int card = get_device_no(MAJOR(file->f_dentry->d_inode->i_rdev));
    lsi6_dev_t *lsi;
    lsi6_channel * channel;
    if (card < 0)
	return -ENXIO;

    lsi = &lsi6_dev[card];
    channel = &lsi->channels[chnum];

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

	CHAN_LOCK_OR_BUSY;
	rc = lsi6_do_naf(lsi, chnum, n,a,f, &x);
	CHAN_UNLOCK;

	*(int *)(file->private_data)=rc;
	return ( rc == -1 ) ? -EIO : count;
    }
    else if(count==3){
	if( copy_from_user(&x, buf, 3))
	    return -EFAULT;
	x &= 0xffffff;
	CHAN_LOCK_OR_BUSY;
	rc = lsi6_do_naf24(lsi, chnum, n,a,f, &x);
	CHAN_UNLOCK;

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
	CHAN_LOCK_OR_BUSY;
	if (naf & CAMAC_24) 
	    rc = lsi6_do_block24(lsi, chnum, n,a,f, tmpbuf, &count_done);
	else 
	    rc = lsi6_do_block(lsi, chnum, n,a,f, tmpbuf, &count_done);
	CHAN_UNLOCK;
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

static int lsi6_init_channel(lsi6_channel * chan, lsi6_dev_t * lsi) {
    if (!chan) {
        printk(DRV_NAME ": null channel in lsi6_init_channel\n");
        return -EFAULT;
    }
    mutex_init(&chan->mutex);
    chan->lsi = lsi;
    INIT_WORK(&chan->interruptHandler, lsi6_interrupt_bottom_half);
    if (chan->lsi != lsi || lsi == 0) {
        printk(DRV_NAME ": INIT_WORK corrupted channel structure for channel %p\n", chan);
        return -EFAULT;
    }
    DP(printk(DRV_NAME ": initialized channel %p in card %p, work: %p\n", chan, lsi, &chan->interruptHandler));
    return 0;
}
static int lsi6_init_card(lsi6_dev_t * lsi){
    int i, rc = 0;
    if (!lsi)
        return -EFAULT;
    memset(lsi, 0, sizeof(lsi6_dev_t));
    for (i = 0; i < LSI6_NUMCHANNELS; ++i) {
        rc = lsi6_init_channel(&lsi->channels[i], lsi);
        if (!rc && lsi->channels[i].lsi != lsi) {
            printk(DRV_NAME ": channel %p lsi field: %p, required: %p\n", &lsi->channels[i], &lsi->channels[i].lsi, lsi);
            rc = -EFAULT;
        }
        if (rc) {
            printk(DRV_NAME ": channel %p initialization failed\n", &lsi->channels[i]);
            break;
        }
    }
    return rc;
}
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
    i = lsi6_init_card(lsi);
    if (i)
        return i;

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
    
    for (i = 0; i < LSI6_NUMCHANNELS; i++) 
    {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	err_dev = device_create(lsi6_class, NULL, MKDEV(lsi6_major, i), NULL, LSI6_DEVNAME, card_no+1, i+1);
#else
	err_dev = device_create(lsi6_class, NULL, MKDEV(lsi6_major, i), LSI6_DEVNAME, card_no+1, i+1);
#endif
    }

    regs = (lsi6_regs_t *)lsi->base;
    writel(0, &regs->intr_global);

    for (i = 0; i < LSI6_NUMCHANNELS; i++) {
	int k;
        for (k = 0; k < K0607_LGROUPS; k++) {
            init_waitqueue_head(&lsi->LWQ[i][k]);
        }
        if (readl(&regs->exist) & (1 << i)) {
            k0607_write_lmr(lsi, i, 0);
        }
    }


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
    lsi6_class = class_create(THIS_MODULE, DRV_NAME);
	return pci_register_driver(&lsi6_driver);
}

static void __exit lsi6_cleanup(void)
{
	pci_unregister_driver (&lsi6_driver);
    class_unregister(lsi6_class);
    class_destroy(lsi6_class);
}

module_init(lsi6_init);
module_exit(lsi6_cleanup);

