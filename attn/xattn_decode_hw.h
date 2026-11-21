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
// 0x10 : Data signal of q
//        bit 31~0 - q[31:0] (Read/Write)
// 0x14 : Data signal of q
//        bit 31~0 - q[63:32] (Read/Write)
// 0x18 : reserved
// 0x20 : Data signal of k
//        bit 31~0 - k[31:0] (Read/Write)
// 0x24 : Data signal of k
//        bit 31~0 - k[63:32] (Read/Write)
// 0x28 : reserved
// 0x30 : Data signal of v
//        bit 31~0 - v[31:0] (Read/Write)
// 0x34 : Data signal of v
//        bit 31~0 - v[63:32] (Read/Write)
// 0x38 : reserved
// 0x40 : Data signal of bias
//        bit 31~0 - bias[31:0] (Read/Write)
// 0x44 : Data signal of bias
//        bit 31~0 - bias[63:32] (Read/Write)
// 0x48 : reserved
// 0x50 : Data signal of out_r
//        bit 31~0 - out_r[31:0] (Read/Write)
// 0x54 : Data signal of out_r
//        bit 31~0 - out_r[63:32] (Read/Write)
// 0x58 : reserved
// 0x60 : Data signal of tokens
//        bit 31~0 - tokens[31:0] (Read/Write)
// 0x64 : reserved
// 0x70 : Data signal of scale
//        bit 31~0 - scale[31:0] (Read/Write)
// 0x74 : reserved
// 0x80 : Data signal of status
//        bit 31~0 - status[31:0] (Read)
// 0x84 : Control signal of status
//        bit 0  - status_ap_vld (Read/COR)
//        others - reserved
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

#define XATTN_DECODE_CTRL_ADDR_AP_CTRL     0x00
#define XATTN_DECODE_CTRL_ADDR_GIE         0x04
#define XATTN_DECODE_CTRL_ADDR_IER         0x08
#define XATTN_DECODE_CTRL_ADDR_ISR         0x0c
#define XATTN_DECODE_CTRL_ADDR_Q_DATA      0x10
#define XATTN_DECODE_CTRL_BITS_Q_DATA      64
#define XATTN_DECODE_CTRL_ADDR_K_DATA      0x20
#define XATTN_DECODE_CTRL_BITS_K_DATA      64
#define XATTN_DECODE_CTRL_ADDR_V_DATA      0x30
#define XATTN_DECODE_CTRL_BITS_V_DATA      64
#define XATTN_DECODE_CTRL_ADDR_BIAS_DATA   0x40
#define XATTN_DECODE_CTRL_BITS_BIAS_DATA   64
#define XATTN_DECODE_CTRL_ADDR_OUT_R_DATA  0x50
#define XATTN_DECODE_CTRL_BITS_OUT_R_DATA  64
#define XATTN_DECODE_CTRL_ADDR_TOKENS_DATA 0x60
#define XATTN_DECODE_CTRL_BITS_TOKENS_DATA 32
#define XATTN_DECODE_CTRL_ADDR_SCALE_DATA  0x70
#define XATTN_DECODE_CTRL_BITS_SCALE_DATA  32
#define XATTN_DECODE_CTRL_ADDR_STATUS_DATA 0x80
#define XATTN_DECODE_CTRL_BITS_STATUS_DATA 32
#define XATTN_DECODE_CTRL_ADDR_STATUS_CTRL 0x84

