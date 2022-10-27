UBENCH=stream.c
SOURCES:=$(UBENCH)
CFLAGS=-O3
CFLAGS+=-DSTREAM_ARRAY_SIZE=$(STREAM_ARRAY_SIZE) -DNTIMES=$(NTIMES) -DBENCH_CLASS=$(BENCH_CLASS)

TARGET_GEM5_RV64=$(addprefix build/, $(patsubst %.c, %.$(NUM_ELEMS)_$(BENCH_CLASS).GEM5_RV64, $(UBENCH)))
TARGET_AARCH64=$(addprefix build/, $(patsubst %.c, %.$(NUM_ELEMS)_$(BENCH_CLASS).AARCH64, $(UBENCH)))
TARGET_AMD64=$(addprefix build/, $(patsubst %.c, %.$(NUM_ELEMS)_$(BENCH_CLASS).AMD64, $(UBENCH)))

include ../Makefile.common