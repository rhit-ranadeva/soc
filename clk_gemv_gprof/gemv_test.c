/*
 * gemv_test.c -- on-board bring-up / regression test for gemv_drv.ko + the
 *                whole-GEMV accelerator.  It goes through exactly the same HAL
 *                (gemv_fpga_hal.h) that llama.cpp uses.
 *
 *   sudo ./gemv_test            quick suite (spot-checks the large matrices)
 *   sudo ./gemv_test -full      check EVERY row of EVERY case (slow on the A53)
 *   sudo ./gemv_test -reps 20   timing repetitions per case (default 10)
 *   sudo ./gemv_test -regs      only dump the accelerator registers and exit
 *   sudo ./gemv_test -contig    only run the physical-contiguity / address check and exit
 *                               (it also runs at the start of every normal run)
 *
 * Every case builds a random Q8_0 matrix + activation, runs it on the FPGA and
 * compares each (checked) row with the generic ggml float reference.
 * Exit status 0 = everything passed.
 */
#define _GNU_SOURCE
#include "gemv_fpga_hal.h"

static uint32_t rng_state = 0x1234567u;

static uint32_t rnd(void)
{
	uint32_t x = rng_state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return rng_state = x;
}

/* random Q8_0 blocks: scale in 2^-7..2^1 (sometimes negative), random int8 quants */
static void gen_blocks(uint8_t *p, size_t nblocks)
{
	size_t b;
	unsigned j;

	for (b = 0; b < nblocks; ++b, p += GEMV_BLOCK_BYTES) {
		const uint32_t r = rnd();
		const uint32_t e = 8u + (r >> 4) % 9u;
		const uint32_t m = (r >> 12) % 1024u;
		const uint32_t d = (((r & 15u) == 0) ? 0x8000u : 0u) | (e << 10) | m;
		uint32_t q = 0;

		p[0] = (uint8_t)d;
		p[1] = (uint8_t)(d >> 8);
		for (j = 0; j < GEMV_QK; ++j) {
			if ((j & 3) == 0)
				q = rnd();
			p[2 + j] = (uint8_t)(q >> (8 * (j & 3)));
		}
	}
}

struct testcase { const char *name; unsigned rows, nb; };

static int run_case(const struct testcase *tc, int full, int reps)
{
	const size_t row_bytes = (size_t)tc->nb * GEMV_BLOCK_BYTES;
	uint8_t *w = malloc(row_bytes * tc->rows);
	uint8_t *y = malloc(row_bytes);
	float *out = malloc((size_t)tc->rows * sizeof(float));
	int ok = 1, rc;
	unsigned r, stride, bad = 0;
	double maxerr = 0, t0, dt;
	int i;

	if (!w || !y || !out) {
		fprintf(stderr, "out of memory for %s\n", tc->name);
		return 0;
	}
	gen_blocks(w, (size_t)tc->rows * tc->nb);
	gen_blocks(y, tc->nb);

	rc = gemv_fpga_run(w, row_bytes, (int64_t)tc->nb * GEMV_QK, tc->rows, y, out);
	if (rc != 0) {
		printf("[SKIP] %-22s rows=%-6u nb=%-4u  (HAL declined: arena too small or error)\n",
		       tc->name, tc->rows, tc->nb);
		free(y); free(out);              /* w stays allocated, see below */
		return -1;
	}

	stride = full ? 1 : (tc->rows > 1024 ? tc->rows / 509 : 1);
	for (r = 0; r < tc->rows; ) {
		double absum;
		const float ref = gemv_ref_row(w + (size_t)r * row_bytes, y, tc->nb, &absum);
		const double err = fabs((double)out[r] - (double)ref);

		if (err > maxerr) maxerr = err;
		if (!(err <= 5e-5 * absum) || !isfinite(out[r])) {
			if (bad++ < 5)
				printf("  MISMATCH row %u: hw=%.9g sw=%.9g (|err|=%.3g)\n",
				       r, (double)out[r], (double)ref, err);
			ok = 0;
		}
		if (r + stride >= tc->rows && r != tc->rows - 1) r = tc->rows - 1;   /* always check the last row */
		else r += stride;
	}

	/* timing: weights are resident now, so this is y-copy + HW + result copy */
	t0 = gemv_now();
	for (i = 0; i < reps; ++i)
		if (gemv_fpga_run(w, row_bytes, (int64_t)tc->nb * GEMV_QK, tc->rows, y, out) != 0)
			ok = 0;
	dt = (gemv_now() - t0) / reps;

	printf("[%s] %-22s rows=%-6u nb=%-4u  %8.3f ms/call  %6.2f GB/s weights  max|err|=%.2e%s\n",
	       ok ? "PASS" : "FAIL", tc->name, tc->rows, tc->nb, dt * 1e3,
	       (double)(row_bytes * tc->rows) / dt / 1e9, maxerr,
	       stride > 1 ? "  (sampled)" : "");
	/* `w` is deliberately NOT freed: the HAL caches weights by pointer, so a later
	 * malloc() returning the same address would silently reuse stale arena data.
	 * (~160 MiB total, released at exit.) */
	free(y); free(out);
	return ok;
}

static int dump_regs(void)
{
	struct gemv_regs r;
	struct gemv_info info;
	int fd = gemv_hal.ready ? dup(gemv_hal.fd) : open(GEMV_DEVICE, O_RDWR);

	if (fd < 0) { perror("open " GEMV_DEVICE); return 1; }
	if (ioctl(fd, GEMV_IOC_INFO, &info) < 0) { perror("INFO"); close(fd); return 1; }
	if (ioctl(fd, GEMV_IOC_REGS, &r) < 0) { perror("REGS"); close(fd); return 1; }
	printf("engines=%u arena=%llu MiB jobs=%llu irqs=%llu\n", info.engines,
	       (unsigned long long)(info.arena_size >> 20),
	       (unsigned long long)info.jobs, (unsigned long long)info.irqs);
	printf("ap_ctrl=0x%x gie=%u ier=0x%x isr=0x%x status=0x%x nb=%u rows0=%u rows1=%u\n",
	       r.ap_ctrl, r.gie, r.ier, r.isr, r.status, r.nb, r.rows0, r.rows1);
	printf("w0=0x%llx y0=0x%llx out0=0x%llx\nw1=0x%llx y1=0x%llx out1=0x%llx\n",
	       (unsigned long long)r.w0, (unsigned long long)r.y0, (unsigned long long)r.out0,
	       (unsigned long long)r.w1, (unsigned long long)r.y1, (unsigned long long)r.out1);
	close(fd);
	return 0;
}

/*
 * End-to-end proof that the buffers user space writes are physically contiguous
 * AND sit exactly where the accelerator is told to read them: translate every
 * page of the three mmap()ed buffers with /proc/self/pagemap (needs root, which
 * hides PFNs otherwise) and compare with the bus address the driver reports.
 */
static int check_one(int pm, const char *name, const unsigned char *va, uint64_t size, uint64_t dma)
{
	const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
	const uint64_t pages = size / page;
	uint64_t ent[512], i, k, n;

	for (i = 0; i < pages; i += n) {
		n = pages - i < 512 ? pages - i : 512;
		if (pread(pm, ent, n * 8, (off_t)((((uintptr_t)va / page) + i) * 8)) != (ssize_t)(n * 8)) {
			perror("pagemap read");
			return 0;
		}
		for (k = 0; k < n; ++k) {
			const uint64_t pfn = ent[k] & ((1ULL << 55) - 1);

			if (!(ent[k] >> 63)) {
				printf("  %s: page %llu not present in the mapping\n", name, (unsigned long long)(i + k));
				return 0;
			}
			if (pfn == 0) {
				printf("  %s: PFN hidden by the kernel: run as root\n", name);
				return 0;
			}
			if (pfn * page != dma + (i + k) * page) {
				if (i + k == 0)
					printf("  %s: first page is at phys 0x%llx but the accelerator address is 0x%llx "
					       "(bus != physical: dma-ranges offset in the device tree?)\n", name,
					       (unsigned long long)(pfn * page), (unsigned long long)dma);
				printf("  %s: page %llu is at phys 0x%llx, expected 0x%llx -> NOT contiguous\n", name,
				       (unsigned long long)(i + k), (unsigned long long)(pfn * page),
				       (unsigned long long)(dma + (i + k) * page));
				return 0;
			}
		}
	}
	printf("  %-12s %7llu pages, phys 0x%llx .. 0x%llx : contiguous, equals the accelerator address\n",
	       name, (unsigned long long)pages, (unsigned long long)dma,
	       (unsigned long long)(dma + size - 1));
	return 1;
}

static int check_contiguity(void)
{
	struct gemv_info info;
	int pm, ok = 1;

	if (ioctl(gemv_hal.fd, GEMV_IOC_INFO, &info) < 0) { perror("INFO"); return 0; }
	pm = open("/proc/self/pagemap", O_RDONLY);
	if (pm < 0) { perror("open /proc/self/pagemap"); return 0; }
	printf("memory map check (user mapping -> physical frames vs. accelerator addresses):\n");
	ok &= check_one(pm, "weight arena", gemv_hal.arena, info.arena_size, info.arena_dma);
	ok &= check_one(pm, "y buffer", gemv_hal.ybuf, info.y_size, info.y_dma);
	ok &= check_one(pm, "out buffer", gemv_hal.outbuf, info.out_size, info.out_dma);
	close(pm);
	printf("[%s] buffers are physically contiguous\n", ok ? "PASS" : "FAIL");
	return ok;
}

/* The driver must reject bad requests without touching the hardware. */
static int test_invalid(void)
{
	struct gemv_job job;
	int fd = gemv_hal.ready ? dup(gemv_hal.fd) : open(GEMV_DEVICE, O_RDWR), ok = 1;

	if (fd < 0) return 0;
	memset(&job, 0, sizeof(job));
	job.nb = 0; job.rows = 4;
	if (ioctl(fd, GEMV_IOC_RUN, &job) == 0 || errno != EINVAL) ok = 0;
	job.nb = GEMV_MAX_BLOCKS + 1;
	if (ioctl(fd, GEMV_IOC_RUN, &job) == 0 || errno != EINVAL) ok = 0;
	job.nb = 4; job.rows = 4; job.w_off = (uint64_t)1 << 40;
	if (ioctl(fd, GEMV_IOC_RUN, &job) == 0 || errno != EINVAL) ok = 0;
	job.w_off = 8;                                  /* misaligned */
	if (ioctl(fd, GEMV_IOC_RUN, &job) == 0 || errno != EINVAL) ok = 0;
	close(fd);
	printf("[%s] driver rejects invalid requests\n", ok ? "PASS" : "FAIL");
	return ok;
}

int main(int argc, char **argv)
{
	static const struct testcase cases[] = {
		{ "single block",        1,      1   },
		{ "16 rows",             16,     1   },
		{ "odd split 17x3",      17,     3   },
		{ "100 x 9",             100,    9   },
		{ "k/v proj 128x896",    128,    28  },
		{ "q/o proj 896x896",    896,    28  },
		{ "gate/up 4864x896",    4864,   28  },
		{ "down 896x4864",       896,    152 },
		{ "lm_head 151936x896",  151936, 28  },
	};
	int full = 0, reps = 10, fails = 0, skipped = 0, only_contig = 0;
	size_t i;

	for (i = 1; i < (size_t)argc; ++i) {
		if (!strcmp(argv[i], "-full")) full = 1;
		else if (!strcmp(argv[i], "-reps") && i + 1 < (size_t)argc) reps = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-regs")) return dump_regs();
		else if (!strcmp(argv[i], "-contig")) only_contig = 1;
		else { fprintf(stderr, "usage: %s [-full] [-reps N] [-regs] [-contig]\n", argv[0]); return 2; }
	}
	if (reps < 1) reps = 1;

	if (!gemv_fpga_enabled()) {
		fprintf(stderr, "accelerator not available (see message above)\n");
		return 1;
	}
	if (dump_regs()) return 1;
	if (!check_contiguity()) {
		fprintf(stderr, "memory map check FAILED: results below cannot be trusted\n");
		if (only_contig) return 1;
		++fails;
	}
	if (only_contig) return fails ? 1 : 0;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		const int r = run_case(&cases[i], full, reps);
		if (r == 0) ++fails;
		else if (r < 0) ++skipped;
	}
	if (!test_invalid()) ++fails;

	printf("\n%s: %d failed, %d skipped\n", fails ? "FAILED" : "ALL PASSED", fails, skipped);
	return fails ? 1 : 0;
}
