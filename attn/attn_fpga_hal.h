#ifndef ATTN_FPGA_HAL_H
#define ATTN_FPGA_HAL_H
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "attn_abi.h"

static inline int attn_open(void) {
    return open(ATTN_DEVICE, O_RDWR | O_CLOEXEC);
}

/* Returns 0 or -errno. Caller owns the fd and all packed buffers. */
static inline int attn_run(int fd, unsigned tokens, float scale,
                           const void *q, const void *k, const void *v,
                           const float *bias, float *out) {
    struct attn_job j;
    if (tokens > ATTN_MAX_TOKENS || !q || !out || (tokens && (!k || !v || !bias)))
        return -EINVAL;
    memset(&j, 0, sizeof(j));
    j.version = ATTN_ABI_VERSION;
    j.tokens = tokens;
    memcpy(&j.scale_bits, &scale, sizeof(scale));
    j.q = (uintptr_t)q;
    j.k = (uintptr_t)k;
    j.v = (uintptr_t)v;
    j.bias = (uintptr_t)bias;
    j.out = (uintptr_t)out;
    return ioctl(fd, ATTN_RUN, &j) < 0 ? -errno : 0;
}
#endif
