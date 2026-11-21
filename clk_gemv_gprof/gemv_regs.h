/*
 * gemv_regs.h -- AXI-Lite register map of gemv_q8_0_accel.
 *
 * If you copy the HLS-generated header next to this file
 *   proj_opt/solution1/impl/ip/drivers/gemv_q8_0_accel_v1_0/src/xgemv_q8_0_accel_hw.h
 * the driver uses the offsets from THAT file (the tool is the source of truth).
 * Without it the offsets requested by the `offset=` pragmas in gemv_q8_0.cpp
 * are used.  The driver prints which source it used at probe time.
 */
#ifndef GEMV_REGS_H
#define GEMV_REGS_H

#if defined(__has_include)
#  if __has_include("xgemv_q8_0_accel_hw.h")
#    include "xgemv_q8_0_accel_hw.h"
#    define GEMV_REGS_FROM_HLS 1
#  endif
#endif

#ifdef GEMV_REGS_FROM_HLS
#  define GEMV_R_AP_CTRL XGEMV_Q8_0_ACCEL_CTRL_ADDR_AP_CTRL
#  define GEMV_R_GIE     XGEMV_Q8_0_ACCEL_CTRL_ADDR_GIE
#  define GEMV_R_IER     XGEMV_Q8_0_ACCEL_CTRL_ADDR_IER
#  define GEMV_R_ISR     XGEMV_Q8_0_ACCEL_CTRL_ADDR_ISR
#  define GEMV_R_W0      XGEMV_Q8_0_ACCEL_CTRL_ADDR_W0_DATA      /* 64-bit: lo, hi at +4 */
#  define GEMV_R_Y0      XGEMV_Q8_0_ACCEL_CTRL_ADDR_Y0_DATA
#  define GEMV_R_OUT0    XGEMV_Q8_0_ACCEL_CTRL_ADDR_OUT0_DATA
#  define GEMV_R_W1      XGEMV_Q8_0_ACCEL_CTRL_ADDR_W1_DATA
#  define GEMV_R_Y1      XGEMV_Q8_0_ACCEL_CTRL_ADDR_Y1_DATA
#  define GEMV_R_OUT1    XGEMV_Q8_0_ACCEL_CTRL_ADDR_OUT1_DATA
#  define GEMV_R_NB      XGEMV_Q8_0_ACCEL_CTRL_ADDR_NB_DATA
#  define GEMV_R_ROWS0   XGEMV_Q8_0_ACCEL_CTRL_ADDR_ROWS0_DATA
#  define GEMV_R_ROWS1   XGEMV_Q8_0_ACCEL_CTRL_ADDR_ROWS1_DATA
#  define GEMV_R_STATUS  XGEMV_Q8_0_ACCEL_CTRL_ADDR_STATUS_DATA
#  define GEMV_REGS_SRC  "xgemv_q8_0_accel_hw.h (HLS generated)"
#else
#  define GEMV_R_AP_CTRL 0x00
#  define GEMV_R_GIE     0x04
#  define GEMV_R_IER     0x08
#  define GEMV_R_ISR     0x0c
#  define GEMV_R_W0      0x10
#  define GEMV_R_Y0      0x20
#  define GEMV_R_OUT0    0x30
#  define GEMV_R_W1      0x40
#  define GEMV_R_Y1      0x50
#  define GEMV_R_OUT1    0x60
#  define GEMV_R_NB      0x70
#  define GEMV_R_ROWS0   0x80
#  define GEMV_R_ROWS1   0x90
#  define GEMV_R_STATUS  0xA0
#  define GEMV_REGS_SRC  "requested defaults (no generated header found)"
#endif

/* AP_CTRL bits */
#define GEMV_CTRL_START  (1u << 0)
#define GEMV_CTRL_DONE   (1u << 1)   /* clear-on-read: never used for IRQ ownership */
#define GEMV_CTRL_IDLE   (1u << 2)
/* ISR / IER bits */
#define GEMV_IRQ_DONE    (1u << 0)
#define GEMV_IRQ_MASK    ((1u << 0) | (1u << 1))

/* The register file is < 0x100 bytes; require at least that much DT resource. */
#define GEMV_MIN_REG_BYTES 0x100

#endif /* GEMV_REGS_H */
