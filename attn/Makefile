ifneq ($(KERNELRELEASE),)
obj-m := attn_drv.o
else
KDIR ?= /lib/modules/$(shell uname -r)/build
CC ?= cc
CXX ?= c++
CFLAGS ?= -O2 -Wall -Wextra
CXXFLAGS ?= -O2 -Wall -Wextra
.PHONY: all driver app host-test check clean

all: driver app

driver:
	@test -f xattn_decode_hw.h || { echo 'Copy the HLS-exported xattn_decode_hw.h here first.'; exit 1; }
	$(MAKE) -C "$(KDIR)" M="$(CURDIR)" modules

app: attn_test

attn_test: attn_test.c attn_fpga_hal.h attn_abi.h attn_layout.h test_vectors.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=gnu11 $< -o $@ $(LDFLAGS) -lm

build/tb: tb_attn_decode.cpp attn_decode.cpp attn_decode.h attn_layout.h test_vectors.h
	mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Wno-unknown-pragmas -std=c++11 tb_attn_decode.cpp attn_decode.cpp -o $@ $(LDFLAGS) -lm

host-test: build/tb
	./build/tb

check: host-test app
	python3 tests/test_patch.py
	$(MAKE) build/test_hook
	./build/test_hook
	./build/test_hook --mismatch

build/test_hook: tests/test_hook.cpp attn_decode.cpp attn_decode.h attn_layout.h attn_abi.h attn_fpga_hal.h attn_ggml.hpp test_vectors.h
	mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Wno-unknown-pragmas -std=c++11 -I. tests/test_hook.cpp attn_decode.cpp -pthread -Wl,--wrap=open,--wrap=ioctl -o $@ $(LDFLAGS) -lm

clean:
	$(RM) attn_test build/tb build/test_hook
	@if [ -d "$(KDIR)" ]; then $(MAKE) -C "$(KDIR)" M="$(CURDIR)" clean; fi
endif
