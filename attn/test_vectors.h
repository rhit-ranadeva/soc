#ifndef ATTN_TEST_VECTORS_H
#define ATTN_TEST_VECTORS_H
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "attn_layout.h"

/* Full HLS m_axi depth even for small cases, as required by RTL co-sim. */
struct attn_vectors {
    float q[ATTN_Q_ELEMS];
    uint64_t k[ATTN_MAX_TOKENS*ATTN_KV_ROW/4];
    uint64_t v[ATTN_MAX_TOKENS*ATTN_KV_ROW/4];
    float bias[ATTN_MAX_TOKENS], out[ATTN_Q_ELEMS];
};
enum pattern { RANDOM_DATA, UNIFORM, MASKED, ALL_MASKED, LARGE_LOGITS, F32_QUERY };
struct attn_case { const char *name; unsigned n; enum pattern pattern; float scale; };
static const struct attn_case attn_cases[] = {
    {"empty",                 0, RANDOM_DATA, 0.125f},
    {"one-token-GQA",         1, RANDOM_DATA, 0.125f},
    {"uniform-mean",          7, UNIFORM,     0.125f},
    {"FP32-query-precision",  2, F32_QUERY,   1.0f},
    {"finite-bias-and-holes",19, MASKED,      0.125f},
    {"all-masked",           17, ALL_MASKED,  0.125f},
    {"large-score-range",    31, LARGE_LOGITS,1.0f},
    {"split-KV-boundary",   512, RANDOM_DATA, 0.125f},
    {"odd-context",         513, MASKED,      0.125f},
    {"capacity", ATTN_MAX_TOKENS, RANDOM_DATA,0.125f},
};
#define ATTN_CASE_COUNT (sizeof(attn_cases)/sizeof(attn_cases[0]))

static inline uint32_t sample_u32(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static inline uint16_t sample_half(uint32_t r) {
    if ((r & 127u)==0) return (uint16_t)((r>>8)&0x83ffu); /* signed subnormal or zero */
    return (uint16_t)(((r>>16)&0x8000) | ((13u+((r>>5)%4u))<<10) | ((r>>13)&1023u));
}

static inline double half_ref(uint16_t bits) {
    int e=(bits>>10)&31, m=bits&1023;
    double x=e==0 ? ldexp((double)m,-24) : e==31 ? (m ? NAN : INFINITY) :
                 ldexp((double)(1024+m),e-25);
    return bits & 0x8000u ? -x : x;
}

static inline double read_lane(const uint64_t *words, unsigned i) {
    return half_ref((uint16_t)(words[i/4] >> (16*(i%4))));
}

static inline void generate(struct attn_vectors *x, const struct attn_case *c) {
    uint32_t rng=271828u+c->n;
    unsigned i,t,d;
    for (i=0; i<ATTN_Q_ELEMS; ++i) {
        float q=((int)(sample_u32(&rng)>>8)-8388608)/8388608.0f;
        if (c->pattern==UNIFORM) q=0;
        if (c->pattern==LARGE_LOGITS) q*=80;
        if (c->pattern==F32_QUERY) q=i%ATTN_DIM==0 ? 1.0001f : i%ATTN_DIM==1 ? -1.0f : 0.0f;
        x->q[i]=q;
        x->out[i]=NAN;
    }
    for (t=0; t<c->n; ++t) {
        float b=((int)(t%9)-4)*0.1875f;
        if (c->pattern==UNIFORM || c->pattern==F32_QUERY) b=0;
        if (c->pattern==ALL_MASKED || (c->pattern==MASKED && t%3==0)) b=-INFINITY;
        x->bias[t]=b;
        for (d=0; d<ATTN_KV_ROW; d+=4) {
            uint64_t kw=0, vw=0;
            unsigned j;
            for (j=0; j<4; ++j) {
                uint16_t k=sample_half(sample_u32(&rng)), v=sample_half(sample_u32(&rng));
                if (c->pattern==F32_QUERY) {
                    k=(t==0 && (d+j)%ATTN_DIM<2) ? 0x6800 : 0; /* 2048 or zero */
                    v=t==0 ? 0x3c00 : 0;
                }
                if (b==-INFINITY) k=v=0x7e00; /* masked rows must be skipped before arithmetic */
                kw |= (uint64_t)k << (16*j);
                vw |= (uint64_t)v << (16*j);
            }
            x->k[(t*ATTN_KV_ROW+d)/4]=kw;
            x->v[(t*ATTN_KV_ROW+d)/4]=vw;
        }
    }
}

/* Independent two-pass stable softmax, FP64 dot and V accumulation. */
static inline int check_output(const struct attn_vectors *x, const struct attn_case *c) {
    double score[ATTN_MAX_TOKENS], worst=0;
    unsigned h,d,t,bad=0;
    for (h=0; h<ATTN_Q_HEADS; ++h) {
        double m=-INFINITY, sum=0, acc[ATTN_DIM]={0};
        unsigned g=h/ATTN_GROUP;
        for (t=0; t<c->n; ++t) {
            double dot=0;
            score[t]=-INFINITY;
            if (x->bias[t]==-INFINITY) continue;
            for (d=0; d<ATTN_DIM; ++d)
                dot+=x->q[h*ATTN_DIM+d]*read_lane(x->k,t*ATTN_KV_ROW+g*ATTN_DIM+d);
            score[t]=dot*c->scale+x->bias[t];
            if (score[t]>m) m=score[t];
        }
        for (t=0; t<c->n; ++t) {
            double w;
            if (score[t]==-INFINITY) continue;
            w=exp(score[t]-m);
            sum+=w;
            for (d=0; d<ATTN_DIM; ++d)
                acc[d]+=w*read_lane(x->v,t*ATTN_KV_ROW+g*ATTN_DIM+d);
        }
        for (d=0; d<ATTN_DIM; ++d) {
            double expected=sum ? acc[d]/sum : 0;
            float actual=x->out[h*ATTN_DIM+d];
            double err=fabs(actual-expected);
            if (err>worst) worst=err;
            if (!isfinite(actual) || err > 2e-4 + 3e-4*fabs(expected)) {
                if (bad++ < 3) fprintf(stderr,"%s head %u dim %u: got %.8g expected %.9g\n",
                                        c->name,h,d,(double)actual,expected);
            }
        }
    }
    printf("%-5s %-23s n=%4u max_abs_error=%.3g\n", bad?"FAIL":"PASS", c->name,c->n,worst);
    return bad==0;
}
#endif
