#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/init.h>
#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include "k0607.h"
#include "lsi6.h"
#include "lsi6camac.h"

int lsi6_wait_channel(lsi6_dev_t *lsi, int chnum)
{
    int chmask = 1 << chnum;
    lsi6_regs_t *regs = (lsi6_regs_t *)lsi->base;
    channel_regs_t *chan = 
	(channel_regs_t *)(lsi->base + (0x2000 << chnum));
    int to = 0;
    int status;

    while((readl(&regs->busy) & chmask) == 0) {
        schedule();
        to++;
        if (to == 100000) {
	    printk("lsi6: unexpected timeout !\n");
	    return -1;
	}
    }
    status = readl(&chan->status);
    if (status & LSI6_STATUS_LT) {
printk("lsi6: LT timeout !\n");
	return -1;
}
    return ((status & (LSI6_STATUS_LX | LSI6_STATUS_LQ)) >> 6);
}

int k0607_write_csr(lsi6_dev_t *lsi, int chnum, int CSR)
{
    channel_regs_t *chan = 
	(channel_regs_t *)(lsi->base + (0x2000 << chnum));

    writel(K0607_CSR_A << 1, &chan->addr);
    writel(CSR, &chan->data_with_init);
    return lsi6_wait_channel(lsi, chnum);
}
int k0607_read_csr(lsi6_dev_t *lsi, int chnum, int *CSR)
{
    channel_regs_t *chan = 
	(channel_regs_t *)(lsi->base + (0x2000 << chnum));
    int status;
    
    writel(K0607_CSR_A << 1, &chan->addr);
    readl(&chan->data_with_init);
    status = lsi6_wait_channel(lsi, chnum);
    if (status == -1) return -1;
    *CSR = readl(&chan->data_wout_init);
    return status;
}
int k0607_enable_lgroup(lsi6_dev_t *lsi, int chnum, int lgroup)
{
    channel_regs_t *chan = 
	(channel_regs_t *)(lsi->base + (0x2000 << chnum));
    int status, mask;

    writel(K0607_LMR_A << 1, &chan->addr);
    readl(&chan->data_with_init);
    status = lsi6_wait_channel(lsi, chnum);
    if (status == -1) return -1;
    mask = readl(&chan->data_wout_init);
    mask |= 1 << lgroup;
    writel(mask, &chan->data_with_init);
    return lsi6_wait_channel(lsi, chnum);
}
int k0607_read_lmr(lsi6_dev_t *lsi, int chnum, int *lmr)
{
    channel_regs_t *chan = 
	(channel_regs_t *)(lsi->base + (0x2000 << chnum));
    int status;
    
    writel(K0607_LMR_A << 1, &chan->addr);
    readl(&chan->data_with_init);
    status = lsi6_wait_channel(lsi, chnum);
    if (status == -1) return -1;
    *lmr = readl(&chan->data_wout_init);
    return status;
}
int k0607_write_lmr(lsi6_dev_t *lsi, int chnum, int lmr)
{
    channel_regs_t *chan = 
	(channel_regs_t *)(lsi->base + (0x2000 << chnum));
    
    writel(K0607_LMR_A << 1, &chan->addr);
    writel(lmr, &chan->data_with_init);
    return lsi6_wait_channel(lsi, chnum);
}

int k0607_write_hb(lsi6_dev_t *lsi, int chnum, int hb)
{
    channel_regs_t *chan = 
	(channel_regs_t *)(lsi->base + (0x2000 << chnum));

    writel(K0607_HB_A << 1, &chan->addr);
    writel(hb & 0xff, &chan->data_with_init);
    return lsi6_wait_channel(lsi, chnum);
}
int k0607_read_hb(lsi6_dev_t *lsi, int chnum, int *hb)
{
    channel_regs_t *chan = 
	(channel_regs_t *)(lsi->base + (0x2000 << chnum));
    int status;

    writel(K0607_HB_A << 1, &chan->addr);
    readl(&chan->data_with_init);
    status = lsi6_wait_channel(lsi, chnum);
    if (status == -1) return -1;
    *hb = readl(&chan->data_wout_init) & 0xff;
    return status;
}
int lsi6_do_naf(lsi6_dev_t *lsi, int chnum, int n, int a, int f, unsigned long *data)
{
    channel_regs_t *chan = 
	(channel_regs_t *)(lsi->base + (0x2000 << chnum));
    int status;

    if (k0607_write_csr(lsi, chnum, lsi->CSR[chnum] | f) == -1) return -1;
    writel(((n << 5) | (a << 1)), &chan->addr);

    if (f < 8) readl(&chan->data_with_init);
	else writel(*data, &chan->data_with_init);

    status = lsi6_wait_channel(lsi, chnum);
    if (status == -1) return -1;

    if (f < 8) *data = readl(&chan->data_wout_init);
    return status;
}
int lsi6_do_block(lsi6_dev_t *lsi, int chnum, int n, int a, int f, unsigned long *data, int *count)
{
    channel_regs_t *chan = 
	(channel_regs_t *)(lsi->base + (0x2000 << chnum));
    int status = 0, i;

    if (k0607_write_csr(lsi, chnum, lsi->CSR[chnum] | f) == -1) return -1;

    writel(((n << 5) | (a << 1)), &chan->addr);

    for (i = 0; i < *count; i++) {
	if (f < 8) readl(&chan->data_with_init);
	    else writel(data[i], &chan->data_with_init);

	status = lsi6_wait_channel(lsi, chnum);
	if ((status < 0) || (status & 0x02)) break;

        if (f < 8) data[i] = readl(&chan->data_wout_init);
    }

    *count -= i;

    return status;
}
int lsi6_do_naf24(lsi6_dev_t *lsi, int chnum, int n, int a, int f, unsigned long *data)
{
    channel_regs_t *chan = 
	(channel_regs_t *)(lsi->base + (0x2000 << chnum));
    int status, hb;

    if (f >= 8) {
	hb = (*data >> 16) & 0xff;
	if (k0607_write_hb(lsi, chnum, hb) == -1) return -1;
    }

    if (k0607_write_csr(lsi, chnum, lsi->CSR[chnum] | f) == -1) return -1;

    writel(((n << 5) | (a << 1)), &chan->addr);

    if (f < 8) readl(&chan->data_with_init);
	else writel(*data & 0xffff, &chan->data_with_init);

    status = lsi6_wait_channel(lsi, chnum);
    if (status == -1) return -1;
    
    if (f < 8) {
	unsigned long x;

	x = readl(&chan->data_wout_init) & 0xffff;
	
	if (k0607_read_hb(lsi, chnum, &hb) == -1) return -1;

	*data = x | (hb << 16);
    }
    return status; 
}
int lsi6_do_block24(lsi6_dev_t *lsi, int chnum, int n, int a, int f, unsigned long *data, int *count)
{
    channel_regs_t *chan = 
	(channel_regs_t *)(lsi->base + (0x2000 << chnum));
    int status = 0, hb, i;
    
    if (k0607_write_csr(lsi, chnum, lsi->CSR[chnum] | f) == -1) return -1;

    for (i = 0; i < *count; i++) {
	if (f >= 8) {
	    hb = (data[i] >> 16) & 0xff;
	    if (k0607_write_hb(lsi, chnum, hb) == -1) break;
	}

	writel(((n << 5) | (a << 1)), &chan->addr);

	if (f < 8) readl(&chan->data_with_init);
	    else writel(data[i] & 0xffff, &chan->data_with_init);

	status = lsi6_wait_channel(lsi, chnum);
	if ((status < 0) || (status & 0x02)) break;
    
        if (f < 8) {
	    unsigned long x;

	    x = readl(&chan->data_wout_init) & 0xffff;
	
	    if (k0607_read_hb(lsi, chnum, &hb) == -1) return -1;
	    data[i] = x | (hb << 16);
        }
    }
    *count -= i;
    return status; 
}
