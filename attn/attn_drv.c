// SPDX-License-Identifier: GPL-2.0
/* Ultra96 direct PL-to-DDR path with the fixed Vivado control window.
 * The manual platform device gives this IP its own MMIO reservation and DMA
 * device in the board's flat, non-IOMMU 32-bit PL DMA address space.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/sizes.h>
#include <linux/version.h>
#include "attn_abi.h"
#include "xattn_decode_hw.h"

#define REG(name) XATTN_DECODE_CTRL_ADDR_ ## name

/* HLS can export the C argument "out" as "out_r" in the register map. */
#if defined(XATTN_DECODE_CTRL_ADDR_OUT_R_DATA)
#define ATTN_OUT_REG REG(OUT_R_DATA)
#define ATTN_OUT_BITS XATTN_DECODE_CTRL_BITS_OUT_R_DATA
#else
#define ATTN_OUT_REG REG(OUT_DATA)
#define ATTN_OUT_BITS XATTN_DECODE_CTRL_BITS_OUT_DATA
#endif

#if XATTN_DECODE_CTRL_BITS_Q_DATA != 64 || XATTN_DECODE_CTRL_BITS_K_DATA != 64 || \
    XATTN_DECODE_CTRL_BITS_V_DATA != 64 || XATTN_DECODE_CTRL_BITS_BIAS_DATA != 64 || \
    ATTN_OUT_BITS != 64
#error "Export the attn_decode IP with 64-bit m_axi address registers"
#endif

#define CTRL_START BIT(0)
#define CTRL_DONE  BIT(1)
#define CTRL_IDLE  BIT(2)

#define ATTN_CTRL_BASE 0xa0010000UL
#define ATTN_CTRL_SIZE SZ_64K

static unsigned int timeout_ms = 2000;
module_param(timeout_ms, uint, 0444);

enum buffer_index { QUERY, KEY, VALUE, BIAS, OUTPUT, BUFFER_COUNT };

struct attn_buffer {
    size_t size;
    unsigned int reg;
    void *cpu;
    dma_addr_t dma;
};

static struct attn_buffer buffers[BUFFER_COUNT] = {
    [QUERY]  = { ATTN_Q_ELEMS * 4, REG(Q_DATA) },
    [KEY]    = { ATTN_MAX_TOKENS * ATTN_KV_ROW * 2, REG(K_DATA) },
    [VALUE]  = { ATTN_MAX_TOKENS * ATTN_KV_ROW * 2, REG(V_DATA) },
    [BIAS]   = { ATTN_MAX_TOKENS * 4, REG(BIAS_DATA) },
    [OUTPUT] = { ATTN_Q_ELEMS * 4, ATTN_OUT_REG },
};

static struct platform_device *attn_pdev;
static void __iomem *base;
static DEFINE_MUTEX(run_lock);
static bool dead;

static long attn_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct attn_job job;
    size_t kv_bytes;
    unsigned int ctrl;
    int i, ret;

    if (cmd != ATTN_RUN)
        return -ENOTTY;
    if (copy_from_user(&job, (void __user *)arg, sizeof(job)))
        return -EFAULT;
    if (job.version != ATTN_ABI_VERSION || job.reserved ||
        job.tokens > ATTN_MAX_TOKENS || (job.scale_bits & 0x7f800000) == 0x7f800000)
        return -EINVAL;

    kv_bytes = job.tokens * ATTN_KV_ROW * 2;
    if (mutex_lock_interruptible(&run_lock))
        return -EINTR;

    ret = -EIO;
    if (dead)
        goto unlock;

    /* Reading AP_CTRL also consumes any stale ap_done. */
    ret = -EBUSY;
    if (!(readl(base + REG(AP_CTRL)) & CTRL_IDLE))
        goto unlock;

    ret = -EFAULT;
    if (copy_from_user(buffers[QUERY].cpu, u64_to_user_ptr(job.q), buffers[QUERY].size))
        goto unlock;
    if (job.tokens &&
        (copy_from_user(buffers[KEY].cpu, u64_to_user_ptr(job.k), kv_bytes) ||
         copy_from_user(buffers[VALUE].cpu, u64_to_user_ptr(job.v), kv_bytes) ||
         copy_from_user(buffers[BIAS].cpu, u64_to_user_ptr(job.bias), job.tokens * 4)))
        goto unlock;

    for (i = 0; i < BUFFER_COUNT; ++i) {
        writel(lower_32_bits(buffers[i].dma), base + buffers[i].reg);
        writel(upper_32_bits(buffers[i].dma), base + buffers[i].reg + 4);
    }

    writel(job.tokens, base + REG(TOKENS_DATA));
    writel(job.scale_bits, base + REG(SCALE_DATA));
    dma_wmb();
    writel(CTRL_START, base + REG(AP_CTRL));

    /* Bounded, sleepable MMIO polling. ap_done is clear-on-read. */
    ret = readl_poll_timeout(base + REG(AP_CTRL), ctrl, ctrl & CTRL_DONE,
                             5, (u64)timeout_ms * 1000);
    if (ret) {
        dead = true;
        /* Late DMA can still target our buffers. Keep the module and buffers
         * pinned until a board reset, even after the last userspace fd closes.
         */
        __module_get(THIS_MODULE);
        dev_err(&attn_pdev->dev, "attention timeout; DMA buffers pinned until reboot\n");
        goto unlock;
    }

    ret = -EIO;
    if (readl(base + REG(STATUS_DATA)) != ATTN_SUCCESS)
        goto unlock;

    dma_rmb();
    ret = copy_to_user(u64_to_user_ptr(job.out), buffers[OUTPUT].cpu,
                       buffers[OUTPUT].size) ? -EFAULT : 0;

unlock:
    mutex_unlock(&run_lock);
    return ret;
}

static const struct file_operations attn_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = attn_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = compat_ptr_ioctl,
#endif
    .llseek = no_llseek,
};

static struct miscdevice attn_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "fpga_attn",
    .fops = &attn_fops,
    .mode = 0600,
};

static void free_buffers(struct device *dev)
{
    int i;

    for (i = 0; i < BUFFER_COUNT; ++i) {
        if (buffers[i].cpu) {
            dma_free_coherent(dev, buffers[i].size, buffers[i].cpu, buffers[i].dma);
            buffers[i].cpu = NULL;
        }
    }
}

static int attn_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct resource *mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    int i, ret;

    if (pdev != attn_pdev)
        return -ENODEV;

    base = devm_ioremap_resource(dev, mem);
    if (IS_ERR(base))
        return PTR_ERR(base);
    if (!(readl(base + REG(AP_CTRL)) & CTRL_IDLE))
        return -ENODEV;

    writel(0, base + REG(GIE));
    writel(0, base + REG(IER));

    ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
    if (ret)
        return ret;

    for (i = 0; i < BUFFER_COUNT; ++i) {
        buffers[i].cpu = dma_alloc_coherent(dev, buffers[i].size, &buffers[i].dma, GFP_KERNEL);
        if (!buffers[i].cpu) {
            free_buffers(dev);
            return -ENOMEM;
        }
    }

    attn_misc.parent = dev;
    ret = misc_register(&attn_misc);
    if (ret) {
        free_buffers(dev);
        return ret;
    }

    dev_info(dev, "polling attention at %pa, /dev/fpga_attn ready\n", &mem->start);
    return 0;
}

static struct platform_driver attn_driver = {
    .driver = {
        .name = "qwen-attn-mmio",
        .suppress_bind_attrs = true,
    },
};

static int __init attn_init(void)
{
    struct resource mem = DEFINE_RES_MEM(ATTN_CTRL_BASE, ATTN_CTRL_SIZE);
    struct platform_device_info info = {
        .name = "qwen-attn-mmio",
        .id = PLATFORM_DEVID_NONE,
        .res = &mem,
        .num_res = 1,
        .dma_mask = DMA_BIT_MASK(32),
    };
    int ret;

    BUILD_BUG_ON(sizeof(struct attn_job) != 56);
    BUILD_BUG_ON(REG(STATUS_DATA) + sizeof(u32) > ATTN_CTRL_SIZE);
    if (!timeout_ms || timeout_ms > 60000)
        return -EINVAL;

    attn_pdev = platform_device_register_full(&info);
    if (IS_ERR(attn_pdev))
        return PTR_ERR(attn_pdev);

    ret = platform_driver_probe(&attn_driver, attn_probe);
    if (ret)
        platform_device_unregister(attn_pdev);

    return ret;
}

static void __exit attn_exit(void)
{
    misc_deregister(&attn_misc);
    free_buffers(&attn_pdev->dev);
    platform_driver_unregister(&attn_driver);
    platform_device_unregister(attn_pdev);
}

module_init(attn_init);
module_exit(attn_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Polling Qwen decode attention accelerator");
