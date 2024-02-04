CC=gcc
PREFIX=/usr/local
VERSION=0.12.1
CCONFIG=-std=c11 -Werror -D_XOPEN_SOURCE=700 -D_POSIX_C_SOURCE=200809L -fPIC -ftrapv -fvisibility=hidden -flto -fno-fat-lto-objects -Wl,-flto
LDFLAGS=-Wl,-rpath '-Wl,$$ORIGIN'
# MAKEFLAGS := --jobs=$(shell nproc) --output-sync=target
CWARN=-Wall -Wextra -Wno-format
  # -Wpedantic -Wsign-conversion -Wtype-limits -Wunused-result -Wnull-dereference \
	# -Waggregate-return -Walloc-zero -Walloca -Warith-conversion -Wcast-align -Wcast-align=strict \
	# -Wdangling-else -Wdate-time -Wdisabled-optimization -Wdouble-promotion -Wduplicated-branches \
	# -Wduplicated-cond -Wexpansion-to-defined -Wfloat-conversion -Wfloat-equal -Wformat-nonliteral \
	# -Wformat-security -Wformat-signedness -Wframe-address -Winline -Winvalid-pch -Wjump-misses-init \
	# -Wlogical-op -Wlong-long -Wmissing-format-attribute -Wmissing-include-dirs -Wmissing-noreturn \
	# -Wnull-dereference -Woverlength-strings -Wpacked -Wpacked-not-aligned -Wpointer-arith \
	# -Wredundant-decls -Wshadow -Wshadow=compatible-local -Wshadow=global -Wshadow=local \
	# -Wsign-conversion -Wstack-protector -Wsuggest-attribute=const -Wswitch-default -Wswitch-enum \
	# -Wsync-nand -Wtrampolines -Wundef -Wunsuffixed-float-constants -Wunused -Wunused-but-set-variable \
	# -Wunused-const-variable -Wunused-local-typedefs -Wunused-macros -Wvariadic-macros -Wvector-operation-performance \
	# -Wvla -Wwrite-strings
OSFLAGS != case $$(uname -s) in *BSD|Darwin) echo '-D_BSD_SOURCE';; Linux) echo '-D_GNU_SOURCE';; *) echo '-D_DEFAULT_SOURCE';; esac
EXTRA=
G=-ggdb
O=-Og
CFLAGS=$(CCONFIG) $(EXTRA) $(CWARN) $(G) $(O) $(OSFLAGS)
LDLIBS=-lgc -lgccjit -lcord -lm -lunistring

all: nextlang

nextlang: nextlang.c parse.o files.o util.o ast.o compile.o

SipHash/halfsiphash.c:
	git submodule update --init --recursive

tags:
	ctags **/*.[ch]

clean:
	rm -f nextlang *.o

%.1: %.1.md
	pandoc --lua-filter=.pandoc/bold-code.lua -s $< -t man -o $@

.PHONY: all clean install uninstall test
