#ifndef ATTN_ABI_H
#define ATTN_ABI_H
#include <linux/ioctl.h>
#include <linux/types.h>
#include "attn_layout.h"

#define ATTN_DEVICE "/dev/fpga_attn"
#define ATTN_ABI_VERSION 1u
/* Fixed-size user virtual addresses, never DMA addresses. All data is copied
 * synchronously. The arrays and scale use the layout defined in attn_decode.h. */
struct attn_job {
    __u32 version;
    __u32 tokens;
    __u32 scale_bits;
    __u32 reserved;
    __aligned_u64 q, k, v, bias, out;
};
#define ATTN_RUN _IOW('A', 0x51, struct attn_job)
#endif
