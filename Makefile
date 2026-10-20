SHELL := /bin/bash
SDK ?= /opt/ps5-payload-sdk
PS5_CC := $(SDK)/bin/prospero-clang
CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -g
.PHONY: all test clean
all: dist/muse-hello.elf dist/mic-probe.elf dist/gate.o
dist:
	mkdir -p dist
dist/muse-hello.elf: src/hello.c | dist
	$(PS5_CC) $(CFLAGS) $< -o $@
dist/gate.o: src/gate.c src/gate.h | dist
	$(PS5_CC) $(CFLAGS) -Isrc -c $< -o $@
dist/mic-probe.elf: prototypes/mic_probe.c | dist
	$(PS5_CC) $(CFLAGS) $< -lSceUserService -o $@
test: | dist
	clang-18 $(CFLAGS) -fsanitize=address,undefined -Isrc src/gate.c tests/gate_test.c -o dist/gate-test
	./dist/gate-test
clean:
	rm -f dist/muse-hello.elf dist/mic-probe.elf dist/gate.o dist/gate-test
