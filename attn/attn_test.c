#include "attn_fpga_hal.h"
#include "test_vectors.h"
#include <stdlib.h>
#include <time.h>

static struct attn_vectors x;

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    return ts.tv_sec+ts.tv_nsec*1e-9;
}
int main(int argc, char **argv) {
    unsigned i;
    long reps=1;
    int fd;
    if (argc==3 && !strcmp(argv[1],"--reps")) {
        char *end;
        reps=strtol(argv[2],&end,10);
        if (*end || reps<1 || reps>100000) return 2;
    } else if (argc!=1) {
        fprintf(stderr,"Usage: %s [--reps N]\n",argv[0]);
        return 2;
    }
    fd=attn_open();
    if (fd<0) { perror(ATTN_DEVICE); return 1; }
    /* Exercise the driver ABI directly so the HAL's size guard cannot mask a failure. */
    struct attn_job invalid={0};
    if (ioctl(fd,ATTN_RUN,&invalid)!=-1 || errno!=EINVAL) {
        fprintf(stderr,"Driver accepted wrong ABI version or returned unexpected errno\n");
        close(fd); return 1;
    }
    invalid.version=ATTN_ABI_VERSION;
    invalid.tokens=ATTN_MAX_TOKENS+1;
    if (ioctl(fd,ATTN_RUN,&invalid)!=-1 || errno!=EINVAL) {
        fprintf(stderr,"Driver failed the length guard\n"); close(fd); return 1;
    }
    for (i=0; i<ATTN_CASE_COUNT; ++i) {
        const struct attn_case *c=&attn_cases[i];
        double start,dt;
        long r;
        generate(&x,c);
        start=now();
        for (r=0; r<reps; ++r) {
            int rc=attn_run(fd,c->n,c->scale,x.q,x.k,x.v,x.bias,x.out);
            if (rc) { fprintf(stderr,"%s: %s\n",c->name,strerror(-rc)); close(fd); return 1; }
        }
        dt=(now()-start)/reps;
        if (!check_output(&x,c)) { close(fd); return 1; }
        printf("      %.1f us/job including copies and completion poll\n",dt*1e6);
    }
    close(fd);
    return 0;
}
