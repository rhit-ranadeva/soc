#ifndef ATTN_LAYOUT_H
#define ATTN_LAYOUT_H
/* Shared by HLS, ggml and Linux. Physical AXI words are little endian. */
#define ATTN_DIM        64
#define ATTN_Q_HEADS    14
#define ATTN_KV_HEADS   2
#define ATTN_GROUP      (ATTN_Q_HEADS / ATTN_KV_HEADS)
#define ATTN_MAX_TOKENS 4096
#define ATTN_Q_ELEMS    (ATTN_Q_HEADS * ATTN_DIM)
#define ATTN_KV_ROW     (ATTN_KV_HEADS * ATTN_DIM)
#define ATTN_SUCCESS    0x41544e31u /* ATN1: written by this IP at completion */
#endif
