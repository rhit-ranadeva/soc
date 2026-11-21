// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2022.2 (64-bit)
// Tool Version Limit: 2019.12
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// ==============================================================
// CTRL
// 0x00 : Control signals
//        bit 0  - ap_start (Read/Write/COH)
//        bit 1  - ap_done (Read/COR)
//        bit 2  - ap_idle (Read)
//        bit 3  - ap_ready (Read/COR)
//        bit 7  - auto_restart (Read/Write)
//        bit 9  - interrupt (Read)
//        others - reserved
// 0x04 : Global Interrupt Enable Register
//        bit 0  - Global Interrupt Enable (Read/Write)
//        others - reserved
// 0x08 : IP Interrupt Enable Register (Read/Write)
//        bit 0 - enable ap_done interrupt (Read/Write)
//        bit 1 - enable ap_ready interrupt (Read/Write)
//        others - reserved
// 0x0c : IP Interrupt Status Register (Read/TOW)
//        bit 0 - ap_done (Read/TOW)
//        bit 1 - ap_ready (Read/TOW)
//        others - reserved
// 0x10 : Data signal of w0
//        bit 31~0 - w0[31:0] (Read/Write)
// 0x14 : Data signal of w0
//        bit 31~0 - w0[63:32] (Read/Write)
// 0x18 : reserved
// 0x20 : Data signal of y0
//        bit 31~0 - y0[31:0] (Read/Write)
// 0x24 : Data signal of y0
//        bit 31~0 - y0[63:32] (Read/Write)
// 0x28 : reserved
// 0x30 : Data signal of out0
//        bit 31~0 - out0[31:0] (Read/Write)
// 0x34 : Data signal of out0
//        bit 31~0 - out0[63:32] (Read/Write)
// 0x38 : reserved
// 0x40 : Data signal of w1
//        bit 31~0 - w1[31:0] (Read/Write)
// 0x44 : Data signal of w1
//        bit 31~0 - w1[63:32] (Read/Write)
// 0x48 : reserved
// 0x50 : Data signal of y1
//        bit 31~0 - y1[31:0] (Read/Write)
// 0x54 : Data signal of y1
//        bit 31~0 - y1[63:32] (Read/Write)
// 0x58 : reserved
// 0x60 : Data signal of out1
//        bit 31~0 - out1[31:0] (Read/Write)
// 0x64 : Data signal of out1
//        bit 31~0 - out1[63:32] (Read/Write)
// 0x68 : reserved
// 0x70 : Data signal of nb
//        bit 31~0 - nb[31:0] (Read/Write)
// 0x74 : reserved
// 0x80 : Data signal of rows0
//        bit 31~0 - rows0[31:0] (Read/Write)
// 0x84 : reserved
// 0x90 : Data signal of rows1
//        bit 31~0 - rows1[31:0] (Read/Write)
// 0x94 : reserved
// 0xa0 : Data signal of status
//        bit 31~0 - status[31:0] (Read)
// 0xa4 : Control signal of status
//        bit 0  - status_ap_vld (Read/COR)
//        others - reserved
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

#define XGEMV_Q8_0_ACCEL_CTRL_ADDR_AP_CTRL     0x00
#define XGEMV_Q8_0_ACCEL_CTRL_ADDR_GIE         0x04
#define XGEMV_Q8_0_ACCEL_CTRL_ADDR_IER         0x08
#define XGEMV_Q8_0_ACCEL_CTRL_ADDR_ISR         0x0c
#define XGEMV_Q8_0_ACCEL_CTRL_ADDR_W0_DATA     0x10
#define XGEMV_Q8_0_ACCEL_CTRL_BITS_W0_DATA     64
#define XGEMV_Q8_0_ACCEL_CTRL_ADDR_Y0_DATA     0x20
#define XGEMV_Q8_0_ACCEL_CTRL_BITS_Y0_DATA     64
#define XGEMV_Q8_0_ACCEL_CTRL_ADDR_OUT0_DATA   0x30
#define XGEMV_Q8_0_ACCEL_CTRL_BITS_OUT0_DATA   64
#define XGEMV_Q8_0_ACCEL_CTRL_ADDR_W1_DATA     0x40
#define XGEMV_Q8_0_ACCEL_CTRL_BITS_W1_DATA     64
#define XGEMV_Q8_0_ACCEL_CTRL_ADDR_Y1_DATA     0x50
#define XGEMV_Q8_0_ACCEL_CTRL_BITS_Y1_DATA     64
#define XGEMV_Q8_0_ACCEL_CTRL_ADDR_OUT1_DATA   0x60
#define XGEMV_Q8_0_ACCEL_CTRL_BITS_OUT1_DATA   64
#define XGEMV_Q8_0_ACCEL_CTRL_ADDR_NB_DATA     0x70
#define XGEMV_Q8_0_ACCEL_CTRL_BITS_NB_DATA     32
#define XGEMV_Q8_0_ACCEL_CTRL_ADDR_ROWS0_DATA  0x80
#define XGEMV_Q8_0_ACCEL_CTRL_BITS_ROWS0_DATA  32
#define XGEMV_Q8_0_ACCEL_CTRL_ADDR_ROWS1_DATA  0x90
#define XGEMV_Q8_0_ACCEL_CTRL_BITS_ROWS1_DATA  32
#define XGEMV_Q8_0_ACCEL_CTRL_ADDR_STATUS_DATA 0xa0
#define XGEMV_Q8_0_ACCEL_CTRL_BITS_STATUS_DATA 32
#define XGEMV_Q8_0_ACCEL_CTRL_ADDR_STATUS_CTRL 0xa4

