CC = gcc
CFLAGS := $(CFLAGS) -std=c99 -O2

all:
	$(CC) -o build/asm.exe $(CFLAGS) src/asm.c
	$(CC) -o build/vm.exe $(CFLAGS) src/vm.c
