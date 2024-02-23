PREFIX=/usr/local
VERSION=0.12.1
CCONFIG=-std=c11 -Werror -D_XOPEN_SOURCE=700 -D_POSIX_C_SOURCE=200809L -fPIC -ftrapv -fvisibility=hidden \
				-flto -fno-fat-lto-objects -Wl,-flto -fdollars-in-identifiers
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
BUILTIN_OBJS=builtins/array.o builtins/bool.o builtins/color.o builtins/nums.o builtins/functions.o builtins/integers.o \
						 builtins/pointer.o builtins/memory.o builtins/string.o builtins/table.o builtins/types.o

all: libnext.so nextlang

nextlang: nextlang.c SipHash/halfsiphash.o util.o files.o ast.o parse.o environment.o types.o typecheck.o compile.o $(BUILTIN_OBJS)

libnext.so: util.o files.o $(BUILTIN_OBJS) SipHash/halfsiphash.o
	$(CC) $^ $(CFLAGS) $(EXTRA) $(CWARN) $(G) $(O) $(OSFLAGS) $(LDLIBS) -Wl,-soname,libnext.so -shared -o $@

SipHash/halfsiphash.c:
	git submodule update --init --recursive

tags:
	ctags *.[ch] **/*.[ch]

test: nextlang
	for f in tests/*; do echo -e "\x1b[1;4m$$f\x1b[m"; VERBOSE=0 ./nextlang "$$f" || break; done

clean:
	rm -f nextlang *.o builtins/*.o libnext.so

%.1: %.1.md
	pandoc --lua-filter=.pandoc/bold-code.lua -s $< -t man -o $@

.PHONY: all clean install uninstall test tags
