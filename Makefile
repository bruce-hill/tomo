SHELL := /bin/bash
PREFIX=/usr
VERSION=0.0.1
CC=gcc
CCONFIG=-std=c2x -Werror -D_XOPEN_SOURCE=700 -D_POSIX_C_SOURCE=200809L -fPIC -I. \
		-fno-signed-zeros -fno-finite-math-only -fno-signaling-nans -fno-trapping-math \
		-fsanitize=signed-integer-overflow -fno-sanitize-recover -fvisibility=hidden -fdollars-in-identifiers \
		-DGC_THREADS
LTO=-flto=auto -fno-fat-lto-objects -Wl,-flto 
LDFLAGS=
# MAKEFLAGS := --jobs=$(shell nproc) --output-sync=target
CWARN=-Wall -Wextra -Wno-format -Wshadow \
	  -Wno-pedantic \
	  -Wno-pointer-arith \
	  -Wsign-conversion -Wtype-limits -Wunused-result -Wnull-dereference \
	  -Walloc-zero -Walloca -Wcast-align -Wcast-align=strict \
	  -Wdangling-else -Wdate-time -Wdisabled-optimization -Wdouble-promotion -Wduplicated-branches \
	  -Wduplicated-cond -Wexpansion-to-defined -Wno-float-equal \
	  -Wframe-address -Winline -Winvalid-pch -Wjump-misses-init \
	  -Wlogical-op -Wmissing-format-attribute -Wmissing-include-dirs -Wmissing-noreturn \
	  -Wnull-dereference -Woverlength-strings -Wpacked -Wpacked-not-aligned \
	  -Wredundant-decls -Wshadow -Wshadow=compatible-local -Wshadow=global -Wshadow=local \
	  -Wsign-conversion -Wno-stack-protector -Wsuggest-attribute=const -Wsuggest-attribute=noreturn -Wsuggest-attribute=pure -Wswitch-default \
	  -Wsync-nand -Wtrampolines -Wundef -Wunused -Wunused-but-set-variable \
	  -Wunused-const-variable -Wunused-local-typedefs -Wunused-macros -Wvariadic-macros -Wvector-operation-performance \
	  -Wwrite-strings
OSFLAGS != case $$(uname -s) in *BSD|Darwin) echo '-D_BSD_SOURCE';; Linux) echo '-D_GNU_SOURCE';; *) echo '-D_DEFAULT_SOURCE';; esac
EXTRA=
G=-ggdb
O=-Og
CFLAGS=$(CCONFIG) $(EXTRA) $(CWARN) $(G) $(O) $(OSFLAGS) $(LTO)
CFLAGS_PLACEHOLDER="$$(echo -e '\033[2m<flags...>\033[m')" 
LDLIBS=-lgc -lcord -lm -lunistring -lgmp -ldl
COMPILER_OBJS=$(patsubst %.c,%.o,$(wildcard src/*.c))
STDLIB_OBJS=$(patsubst %.c,%.o,$(wildcard src/stdlib/*.c))
TESTS=$(patsubst test/%.tm,test/results/%.tm.testresult,$(wildcard test/*.tm))

all: build/libtomo.so build/tomo

build/tomo: $(STDLIB_OBJS) $(COMPILER_OBJS)
	@mkdir -p build
	@echo $(CC) $(CFLAGS_PLACEHOLDER) $(LDFLAGS) $^ $(LDLIBS) -o $@
	@$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/libtomo.so: $(STDLIB_OBJS)
	@mkdir -p build
	@echo $(CC) $^ $(CFLAGS_PLACEHOLDER) $(OSFLAGS) -lgc -lcord -lm -lunistring -lgmp -ldl -Wl,-soname,libtomo.so -shared -o $@
	@$(CC) $^ $(CFLAGS) $(OSFLAGS) -lgc -lcord -lm -lunistring -lgmp -ldl -Wl,-soname,libtomo.so -shared -o $@

tags:
	ctags src/*.[ch] src/stdlib/*.[ch]

%.o: %.c src/ast.h src/environment.h src/types.h
	@echo $(CC) $(CFLAGS_PLACEHOLDER) -c $< -o $@
	@$(CC) $(CFLAGS) -c $< -o $@

%: %.tm
	tomo -e $<

test/results/%.tm.testresult: test/%.tm build/tomo
	@mkdir -p test/results
	@printf '\x1b[33;1;4m%s\x1b[m\n' $<
	@set -o pipefail; \
	if ! VERBOSE=0 COLOR=1 LC_ALL=C CC=gcc ./build/tomo -O 1 $< 2>&1 | tee $@; then \
		rm -f $@; \
		false; \
	fi

test: $(TESTS)
	@echo -e '\x1b[32;7m ALL TESTS PASSED! \x1b[m'

clean:
	rm -rf build/* $(COMPILER_OBJS) $(STDLIB_OBJS) test/*.tm.testresult test/.build examples/.build examples/*/.build

%: %.md
	pandoc --lua-filter=docs/.pandoc/bold-code.lua -s $< -t man -o $@

examples: examples/commands/commands examples/base64/base64 examples/ini/ini examples/game/game \
		examples/tomodeps/tomodeps examples/tomo-install/tomo-install examples/wrap/wrap examples/colorful/colorful
	./build/tomo -IL examples/commands examples/shell examples/base64 examples/log examples/ini examples/vectors examples/game \
		examples/http examples/threads examples/tomodeps examples/tomo-install examples/wrap examples/pthreads examples/colorful
	./build/tomo examples/learnxiny.tm

install: build/tomo build/libtomo.so
	mkdir -p -m 755 "$(PREFIX)/man/man1" "$(PREFIX)/bin" "$(PREFIX)/include/tomo" "$(PREFIX)/lib" "$(PREFIX)/share/tomo/modules"
	cp -v src/stdlib/*.h "$(PREFIX)/include/tomo/"
	cp -v build/libtomo.so "$(PREFIX)/lib/"
	rm -f "$(PREFIX)/bin/tomo"
	cp -v build/tomo "$(PREFIX)/bin/"
	cp -v docs/tomo.1 "$(PREFIX)/man/man1/"

uninstall:
	rm -rvf "$(PREFIX)/bin/tomo" "$(PREFIX)/include/tomo" "$(PREFIX)/lib/libtomo.so" "$(PREFIX)/share/tomo"; \

.SUFFIXES:
.PHONY: all clean install uninstall test tags examples
