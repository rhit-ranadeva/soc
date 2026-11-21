/*
 * gemv_drv.c -- Linux driver for the whole-GEMV Q8_0 bus-master accelerator
 *               (HLS top: gemv_q8_0_accel).  Creates /dev/fpga_gemv.
 *
 * Differences to the Lab-3 / dot-product drivers:
 *   - The accelerator fetches weights, activation and writes results by itself
 *     over AXI masters, so the driver never copies operands through MMIO.
 *   - Weights live in ONE big physically-contiguous DMA-coherent "arena"
 *     (CMA) that user space mmap()s and fills once.  The y and out buffers are
 *     also coherent (non-cached) buffers -> no cache maintenance anywhere.
 *   - One blocking ioctl (GEMV_IOC_RUN) = one complete GEMV: program 64-bit
 *     addresses and sizes, split the rows over the two engines, start, poll AP_DONE with a bounded timeout.
 *
 * Device-tree node: unchanged from the dot-product dry run, i.e.
 *   compatible = "ece382m,dotprod";  reg = <... base ... size>;  interrupts = <0 89 4>;
 * (only reg size >= 0x100 and the right base/IRQ matter, see BRINGUP.md)
 *
 * Module parameters:
 *   engines    1 or 2  (2 = use both AXI masters / HP ports)      default 2
 *   arena_mb   requested weight-arena size in MiB (shrinks if CMA is small)
 *   timeout_ms default per-job timeout
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/ktime.h>

#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/pfn.h>
#include <linux/sched.h>

#include "gemv_abi.h"
#include "gemv_regs.h"

#define DRV_NAME    "fpga_gemv"
#define DRV_VERSION 1

#define Y_BUF_BYTES   (64u * 1024u)       /* >= 1024 blocks * 34 B = 34816    */
#define OUT_BUF_BYTES (1024u * 1024u)     /* 262144 float results             */
#define MAX_ROWS      (OUT_BUF_BYTES / 4u)

MODULE_AUTHOR("SoC project");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Whole-GEMV Q8_0 bus-master accelerator driver");

static unsigned int engines = 2;
module_param(engines, uint, 0444);
MODULE_PARM_DESC(engines, "1 or 2 accelerator engines (AXI masters)");

static unsigned int arena_mb = 640;
module_param(arena_mb, uint, 0444);
MODULE_PARM_DESC(arena_mb, "weight arena size in MiB (reduced automatically if CMA is smaller)");

static unsigned int timeout_ms = 20000;
module_param(timeout_ms, uint, 0644);
MODULE_PARM_DESC(timeout_ms, "default per-job timeout in ms");

static struct gemv_dev {
	struct device       *dev;
	void __iomem        *base;
	int                  irq;

	void                *arena_cpu;
	dma_addr_t           arena_dma;
	size_t               arena_size;
	void                *y_cpu;
	dma_addr_t           y_dma;
	void                *out_cpu;
	dma_addr_t           out_dma;

	struct mutex         run_lock;      /* one GEMV at a time               */


	u32                  status;        /* status register latched by IRQ   */
	u64                  jobs;
	u64                  irqs;
	bool                 dead;          /* a job timed out: HW state unknown */
	bool                 registered;
} g;

/* ------------------------------------------------------------------ regs */
static inline void gemv_wr64(u32 off, u64 v)
{
	writel(lower_32_bits(v), g.base + off);
	writel(upper_32_bits(v), g.base + off + 4);
}

static inline u64 gemv_rd64(u32 off)
{
	u64 lo = readl(g.base + off);
	u64 hi = readl(g.base + off + 4);
	return lo | (hi << 32);
}

/* ISR is toggle-on-write: only write back the bits that are actually set. */
static void gemv_ack_irq(void)
{
	u32 observed = readl(g.base + GEMV_R_ISR) & GEMV_IRQ_MASK;

	if (observed)
		writel(observed, g.base + GEMV_R_ISR);
	readl(g.base + GEMV_R_ISR);          /* flush posted write */
}

static void gemv_irq_disable(void *base)
{
	writel(0, base + GEMV_R_GIE);
	writel(0, base + GEMV_R_IER);
	readl(base + GEMV_R_GIE);
}

/* ------------------------------------------------------------------ job */
/*
 * Rows are split in two contiguous halves.  Engine 1 starts at
 * w0 + rows0*nb*34 bytes and must be 16-byte aligned, which a multiple-of-8
 * rows0 guarantees (8 rows * nb * 34 B = 272*nb B = 17*nb beats).
 */
static void gemv_split(u32 rows, u32 *r0, u32 *r1)
{
	u32 h;

	*r0 = rows;
	*r1 = 0;
	if (engines < 2 || rows < 16)
		return;
	h = ((rows / 2) + 4) / 8 * 8;         /* multiple of 8 nearest to half  */
	if (h == 0)
		h = 8;
	if (h >= rows)
		return;
	*r0 = h;
	*r1 = rows - h;
}

static long gemv_do_run(struct gemv_job __user *uarg)
{
	struct gemv_job j;
	u64 row_bytes, total, w0, w1, y, o0, o1;
	u32 r0, r1, ctrl, tmo;
	ktime_t deadline;
	unsigned int spins = 0;
	long ret = 0;

	if (copy_from_user(&j, uarg, sizeof(j)))
		return -EFAULT;

	if (j.nb < 1 || j.nb > GEMV_MAX_BLOCKS || j.rows < 1 || j.rows > MAX_ROWS)
		return -EINVAL;
	row_bytes = (u64)j.nb * GEMV_BLOCK_BYTES;
	total = row_bytes * j.rows;
	if ((j.w_off | j.y_off | j.out_off) & 15)
		return -EINVAL;
	/* The IP reads whole 128-bit beats, i.e. up to the next 16-byte boundary. */
	if (j.w_off > g.arena_size || ((total + 15) & ~15ull) > g.arena_size - j.w_off)
		return -EINVAL;
	if (j.y_off > Y_BUF_BYTES || ((row_bytes + 15) & ~15ull) > Y_BUF_BYTES - j.y_off)
		return -EINVAL;
	if (j.out_off > OUT_BUF_BYTES || (u64)j.rows * 4 > OUT_BUF_BYTES - j.out_off)
		return -EINVAL;

	gemv_split(j.rows, &r0, &r1);
	w0 = (u64)g.arena_dma + j.w_off;
	w1 = w0 + (u64)r0 * row_bytes;
	y  = (u64)g.y_dma + j.y_off;
	o0 = (u64)g.out_dma + j.out_off;
	o1 = o0 + (u64)r0 * 4;
	tmo = j.timeout_ms ? j.timeout_ms : timeout_ms;

	mutex_lock(&g.run_lock);
	if (g.dead) {
		ret = -EIO;
		goto out;
	}
	ctrl = readl(g.base + GEMV_R_AP_CTRL);
	if (!(ctrl & GEMV_CTRL_IDLE)) {
		ret = -EBUSY;
		goto out;
	}

	gemv_ack_irq();                       /* drop stale completion          */
	gemv_irq_disable(g.base);

	gemv_wr64(GEMV_R_W0,   w0);
	gemv_wr64(GEMV_R_Y0,   y);
	gemv_wr64(GEMV_R_OUT0, o0);
	gemv_wr64(GEMV_R_W1,   w1);           /* engine 1 idle when r1 == 0:    */
	gemv_wr64(GEMV_R_Y1,   y);            /* addresses are then never used  */
	gemv_wr64(GEMV_R_OUT1, o1);
	writel(j.nb, g.base + GEMV_R_NB);
	writel(r0,   g.base + GEMV_R_ROWS0);
	writel(r1,   g.base + GEMV_R_ROWS1);

	wmb();                                /* operands/registers before start */
	writel(GEMV_CTRL_START, g.base + GEMV_R_AP_CTRL);

	deadline = ktime_add_ms(ktime_get(), tmo ? tmo : 1);
	for (;;) {
		ctrl = readl(g.base + GEMV_R_AP_CTRL);
		if (ctrl & GEMV_CTRL_DONE)
			break;
		if (ktime_compare(ktime_get(), deadline) >= 0) {
			g.dead = true;
			dev_err(g.dev, "poll timeout; reprogram FPGA before reloading driver\n");
			ret = -ETIMEDOUT;
			goto out;
		}
		cpu_relax();
		if (!(++spins & 1023))
			cond_resched();
	}
	g.status = readl(g.base + GEMV_R_STATUS);
	rmb();                                /* results written by the IP       */

	j.status = g.status;
	j.rows0 = r0;
	g.jobs++;
	if (copy_to_user(uarg, &j, sizeof(j))) {
		ret = -EFAULT;
		goto out;
	}
	if (j.status != GEMV_ST_DONE) {
		dev_err(g.dev, "accelerator reported status 0x%x (nb=%u rows=%u)\n",
			j.status, j.nb, j.rows);
		ret = -EIO;
	}
out:
	mutex_unlock(&g.run_lock);
	return ret;
}

static long gemv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *uarg = (void __user *)arg;

	switch (cmd) {
	case GEMV_IOC_INFO: {
		struct gemv_info info = {
			.arena_size = g.arena_size,
			.y_size     = Y_BUF_BYTES,
			.out_size   = OUT_BUF_BYTES,
			.engines    = engines,
			.version    = DRV_VERSION,
			.jobs       = g.jobs,
			.irqs       = g.irqs,
			.arena_dma  = g.arena_dma,
			.y_dma      = g.y_dma,
			.out_dma    = g.out_dma,
		};
		return copy_to_user(uarg, &info, sizeof(info)) ? -EFAULT : 0;
	}
	case GEMV_IOC_RUN:
		return gemv_do_run((struct gemv_job __user *)uarg);
	case GEMV_IOC_REGS: {
		struct gemv_regs r;

		mutex_lock(&g.run_lock);
		r.ap_ctrl = readl(g.base + GEMV_R_AP_CTRL);   /* clears COR bits */
		r.gie     = readl(g.base + GEMV_R_GIE);
		r.ier     = readl(g.base + GEMV_R_IER);
		r.isr     = readl(g.base + GEMV_R_ISR);
		r.nb      = readl(g.base + GEMV_R_NB);
		r.rows0   = readl(g.base + GEMV_R_ROWS0);
		r.rows1   = readl(g.base + GEMV_R_ROWS1);
		r.status  = readl(g.base + GEMV_R_STATUS);
		r.w0      = gemv_rd64(GEMV_R_W0);
		r.y0      = gemv_rd64(GEMV_R_Y0);
		r.out0    = gemv_rd64(GEMV_R_OUT0);
		r.w1      = gemv_rd64(GEMV_R_W1);
		r.y1      = gemv_rd64(GEMV_R_Y1);
		r.out1    = gemv_rd64(GEMV_R_OUT1);
		mutex_unlock(&g.run_lock);
		return copy_to_user(uarg, &r, sizeof(r)) ? -EFAULT : 0;
	}
	default:
		return -ENOTTY;
	}
}

/* ------------------------------------------------------------------ mmap */
static int gemv_mmap(struct file *file, struct vm_area_struct *vma)
{
	size_t len = vma->vm_end - vma->vm_start;
	void *cpu;
	dma_addr_t dma;
	size_t size;

	switch (vma->vm_pgoff) {
	case GEMV_MMAP_ARENA: cpu = g.arena_cpu; dma = g.arena_dma; size = g.arena_size; break;
	case GEMV_MMAP_Y:     cpu = g.y_cpu;     dma = g.y_dma;     size = Y_BUF_BYTES;  break;
	case GEMV_MMAP_OUT:   cpu = g.out_cpu;   dma = g.out_dma;   size = OUT_BUF_BYTES; break;
	default:
		return -EINVAL;
	}
	if (!cpu || len > PAGE_ALIGN(size))
		return -EINVAL;

	vma->vm_pgoff = 0;                    /* offset is a selector, not a real offset */
	return dma_mmap_coherent(g.dev, vma, cpu, dma, len);
}

/* Buffers are shared: permit only one owning process at a time. */
static atomic_t opened = ATOMIC_INIT(0);
static int gemv_open(struct inode *inode, struct file *file)
{
 if (atomic_cmpxchg(&opened, 0, 1)) return -EBUSY;
 return 0;
}
static int gemv_release(struct inode *inode, struct file *file)
{
 atomic_set(&opened, 0);
 return 0;
}
static const struct file_operations gemv_fops = {
 .open = gemv_open,
 .release = gemv_release,
	.owner          = THIS_MODULE,
	.llseek         = no_llseek,
	.unlocked_ioctl = gemv_ioctl,
	.mmap           = gemv_mmap,
};

static struct miscdevice gemv_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = DRV_NAME,
	.fops  = &gemv_fops,
	.mode  = 0666,
};

/* ------------------------------------------------------------------ /proc */
static int gemv_proc_show(struct seq_file *f, void *v)
{
	seq_printf(f, "engines     %u\n", engines);
	seq_printf(f, "arena       %zu MiB @ dma 0x%llx\n", g.arena_size >> 20,
		   (unsigned long long)g.arena_dma);
	seq_printf(f, "y buffer    dma 0x%llx\n", (unsigned long long)g.y_dma);
	seq_printf(f, "out buffer  dma 0x%llx\n", (unsigned long long)g.out_dma);
	seq_printf(f, "jobs        %llu\n", (unsigned long long)g.jobs);
	seq_printf(f, "interrupts  %llu\n", (unsigned long long)g.irqs);
	seq_printf(f, "disabled    %s\n", g.dead ? "yes (timeout)" : "no");
	return 0;
}

static int gemv_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, gemv_proc_show, NULL);
}

static const struct proc_ops gemv_proc_ops = {
	.proc_open    = gemv_proc_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

/* ------------------------------------------------------------------ buffers */

/*
 * The accelerator addresses each buffer with ONE base address and walks it
 * linearly, so the buffer must be physically contiguous.  dma_alloc_coherent()
 * guarantees that (it allocates from CMA), but we do not rely on it blindly:
 * walk every page of the kernel mapping and check that frame i == frame 0 + i
 * and that frame 0 is the bus address handed to the hardware.
 */
static unsigned long gemv_pfn_of(void *cpu, size_t off)
{
	void *p = (char *)cpu + off;

	return is_vmalloc_addr(p) ? vmalloc_to_pfn(p) : PHYS_PFN(virt_to_phys(p));
}

static bool gemv_verify_contig(struct device *dev, const char *name, void *cpu,
			       dma_addr_t dma, size_t size)
{
	const size_t pages = size >> PAGE_SHIFT;
	const unsigned long pfn0 = gemv_pfn_of(cpu, 0);
	size_t i;

	if (((u64)pfn0 << PAGE_SHIFT) != (u64)dma)
		dev_warn(dev, "%s: bus address 0x%llx != physical 0x%llx (dma-ranges offset?)\n",
			 name, (unsigned long long)dma, (unsigned long long)pfn0 << PAGE_SHIFT);
	for (i = 1; i < pages; i++) {
		if (gemv_pfn_of(cpu, i << PAGE_SHIFT) != pfn0 + i) {
			dev_err(dev, "%s NOT contiguous: page %zu is frame 0x%lx, expected 0x%lx\n",
				name, i, gemv_pfn_of(cpu, i << PAGE_SHIFT), pfn0 + i);
			return false;
		}
		if (!(i & 0x3fff))
			cond_resched();
	}
	dev_info(dev, "%s: %zu pages physically contiguous from 0x%llx: OK\n",
		 name, pages, (unsigned long long)pfn0 << PAGE_SHIFT);
	return true;
}

static void gemv_free_buffers(struct device *dev)
{
	if (g.dead) {
		dev_warn(dev, "retaining DMA buffers after timeout to prevent late DMA corruption; reboot to reclaim\n");
		return;
	}
	if (g.arena_cpu)
		dma_free_coherent(dev, g.arena_size, g.arena_cpu, g.arena_dma);
	if (g.y_cpu)
		dma_free_coherent(dev, Y_BUF_BYTES, g.y_cpu, g.y_dma);
	if (g.out_cpu)
		dma_free_coherent(dev, OUT_BUF_BYTES, g.out_cpu, g.out_dma);
	g.arena_cpu = g.y_cpu = g.out_cpu = NULL;
}

static int gemv_alloc_buffers(struct device *dev)
{
	size_t sz = (size_t)arena_mb << 20;
	int rv;

	/* Same guard the Lab-3 bus-master driver needed. */
	if (!dev->dma_mask)
		dev->dma_mask = &dev->coherent_dma_mask;
	rv = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (rv) {
		dev_err(dev, "no usable DMA mask (%d)\n", rv);
		return rv;
	}

	g.y_cpu   = dma_alloc_coherent(dev, Y_BUF_BYTES, &g.y_dma, GFP_KERNEL);
	g.out_cpu = dma_alloc_coherent(dev, OUT_BUF_BYTES, &g.out_dma, GFP_KERNEL);
	if (!g.y_cpu || !g.out_cpu) {
		dev_err(dev, "cannot allocate y/out buffers\n");
		gemv_free_buffers(dev);
		return -ENOMEM;
	}

	/* The arena needs physically contiguous CMA; shrink until it fits. */
	if (sz < SZ_1M)
		sz = SZ_1M;                  /* minimum streaming arena */
	while (sz >= SZ_1M) {
		g.arena_cpu = dma_alloc_coherent(dev, sz, &g.arena_dma,
						 GFP_KERNEL | __GFP_NOWARN);
		if (g.arena_cpu)
			break;
		dev_warn(dev, "arena of %zu MiB does not fit in CMA, retrying smaller\n",
			 sz >> 20);
		sz = (sz / 4) * 3;
		sz &= ~((size_t)SZ_1M - 1);
	}
	if (!g.arena_cpu) {
		dev_err(dev, "cannot allocate any weight arena: no contiguous buffer available\n");
		gemv_free_buffers(dev);
		return -ENOMEM;
	}
	g.arena_size = sz;
	if (!gemv_verify_contig(dev, "weight arena", g.arena_cpu, g.arena_dma, sz) ||
	    !gemv_verify_contig(dev, "y buffer", g.y_cpu, g.y_dma, Y_BUF_BYTES) ||
	    !gemv_verify_contig(dev, "out buffer", g.out_cpu, g.out_dma, OUT_BUF_BYTES)) {
		gemv_free_buffers(dev);
		return -EIO;
	}
	dev_info(dev, "weight arena %zu MiB at dma 0x%llx, y 0x%llx, out 0x%llx\n",
		 sz >> 20, (unsigned long long)g.arena_dma,
		 (unsigned long long)g.y_dma, (unsigned long long)g.out_dma);
	if (sz < ((size_t)arena_mb << 20))
		dev_warn(dev, "arena smaller than requested: HAL can stream nonresident weights\n");
	return 0;
}

/* ------------------------------------------------------------------ probe */
static int gemv_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *r_mem;
	void __iomem *base;
	int rv;

	if (g.base)
		return -EBUSY;
	if (engines < 1 || engines > 2) {
		dev_err(dev, "engines must be 1 or 2\n");
		return -EINVAL;
	}

	g.dev = dev;
	mutex_init(&g.run_lock);



	base = devm_platform_get_and_ioremap_resource(pdev, 0, &r_mem);
	if (IS_ERR(base)) {
		dev_err(dev, "cannot map accelerator registers\n");
		return PTR_ERR(base);
	}
	if (resource_size(r_mem) < GEMV_MIN_REG_BYTES) {
		dev_err(dev, "register resource too small (0x%llx)\n",
			(unsigned long long)resource_size(r_mem));
		return -EINVAL;
	}
	g.base = base;
	dev_info(dev, "registers 0x%llx size 0x%llx, offsets from %s\n",
		 (unsigned long long)r_mem->start,
		 (unsigned long long)resource_size(r_mem), GEMV_REGS_SRC);

	/* NOTE: the PL must already be programmed, otherwise this read hangs the bus. */
	if (!(readl(g.base + GEMV_R_AP_CTRL) & GEMV_CTRL_IDLE)) {
		dev_err(dev, "accelerator is not idle (wrong bitstream or register map?)\n");
		rv = -ENODEV;
		goto fail;
	}
	gemv_irq_disable(g.base);
	gemv_ack_irq();

	rv = gemv_alloc_buffers(dev);
	if (rv)
		goto fail;

	rv = devm_add_action_or_reset(dev, gemv_irq_disable, g.base);
	if (rv)
		goto fail_buf;

	if (!proc_create(DRV_NAME, 0444, NULL, &gemv_proc_ops)) {
		rv = -ENOMEM;
		goto fail_buf;
	}

	gemv_irq_disable(g.base);

	rv = misc_register(&gemv_miscdev);
	if (rv) {
		remove_proc_entry(DRV_NAME, NULL);
		goto fail_irq;
	}
	g.registered = true;
	dev_info(dev, "polling, %u engine(s), /dev/%s ready\n", engines, DRV_NAME);
	return 0;

fail_irq:
	gemv_irq_disable(g.base);
fail_buf:
	gemv_free_buffers(dev);
fail:
	g.base = NULL;
	return rv;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static void gemv_remove(struct platform_device *pdev)
#else
static int gemv_remove(struct platform_device *pdev)
#endif
{
	gemv_irq_disable(g.base);

	misc_deregister(&gemv_miscdev);
	remove_proc_entry(DRV_NAME, NULL);
	gemv_free_buffers(&pdev->dev);
	g.registered = false;
	g.base = NULL;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	return 0;
#endif
}

static const struct of_device_id gemv_of_match[] = {
	{ .compatible = "ece382m,dotprod" },    /* node kept from the dry run */
	{ .compatible = "ece382m,gemv" },       /* optional new name          */
	{ /* end */ },
};
MODULE_DEVICE_TABLE(of, gemv_of_match);

static struct platform_driver gemv_driver = {
	.driver = {
		.name                = DRV_NAME,
		.of_match_table      = gemv_of_match,
		.suppress_bind_attrs = true,   /* no manual unbind while mmap()ed */
	},
	.probe  = gemv_probe,
	.remove = gemv_remove,
};
module_platform_driver(gemv_driver);
