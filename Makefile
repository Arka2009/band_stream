UBENCH=stream.c
SOURCES:=$(UBENCH)
CFLAGS=-O3
CFLAGS+=-DSTREAM_ARRAY_SIZE=$(STREAM_ARRAY_SIZE) -DBENCH_CLASS=$(BENCH_CLASS)


TARGET_GEM5_RV64=$(addprefix build/, $(patsubst %.c, %.$(STREAM_ARRAY_SIZE)_$(BENCH_CLASS).GEM5_RV64, $(UBENCH)))
TARGET_AARCH64=$(addprefix build/, $(patsubst %.c, %.$(STREAM_ARRAY_SIZE)_$(BENCH_CLASS).AARCH64, $(UBENCH)))
TARGET_AMD64=$(addprefix build/, $(patsubst %.c, %.$(STREAM_ARRAY_SIZE)_$(BENCH_CLASS).AMD64, $(UBENCH)))

include ../Makefile.common