SOURCES:=stream.cpp

TARGET_GEM5_RV64=stream.GEM5_RV64
TARGET_AARCH64=stream.AARCH64
TARGET_AMD64=stream.AMD64

CFLAGS=-O3 -fopenmp -DUSE_PCM
include ../common/Makefile.tests