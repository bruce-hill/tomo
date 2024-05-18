PREFIX=/usr
VERSION=0.0.1
CCONFIG=-std=c11 -Werror -D_XOPEN_SOURCE=700 -D_POSIX_C_SOURCE=200809L -fPIC -I. \
				-fsanitize=signed-integer-overflow -fno-sanitize-recover -fvisibility=hidden -fdollars-in-identifiers
LTO=-flto=auto -fno-fat-lto-objects -Wl,-flto 
LDFLAGS=-Wl,-rpath '-Wl,$$ORIGIN'
# MAKEFLAGS := --jobs=$(shell nproc) --output-sync=target
CWARN=-Wall -Wextra -Wno-format -Wshadow
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
LDLIBS=-lgc -lcord -lm -lunistring -ldl -L. -ltomo
BUILTIN_OBJS=builtins/array.o builtins/bool.o builtins/nums.o builtins/functions.o builtins/integers.o \
						 builtins/pointer.o builtins/memory.o builtins/text.o builtins/c_string.o builtins/table.o \
						 builtins/types.o builtins/util.o builtins/files.o

all: libtomo.so tomo

tomo: tomo.c SipHash/halfsiphash.o ast.o parse.o environment.o types.o typecheck.o structs.o enums.o compile.o repl.o

libtomo.so: $(BUILTIN_OBJS) SipHash/halfsiphash.o
	$(CC) $^ $(CFLAGS) $(EXTRA) $(CWARN) $(G) $(O) $(OSFLAGS) -lgc -lcord -lm -lunistring -ldl -Wl,-soname,libtomo.so -shared -o $@

SipHash/halfsiphash.c:
	git submodule update --init --recursive

tags:
	ctags *.[ch] **/*.[ch]

test: tomo
	rm -f test/*.tm.[cho]
	for f in $$(ls test/*.tm | shuf); do echo -e "\x1b[1;4m$$f\x1b[m"; \
		if ! VERBOSE=0 CC=tcc ./tomo "$$f"; then \
			echo "FAILURE!"; exit 1; \
		fi; \
	done; echo -e '\x1b[32;7m ALL TESTS PASSED! \x1b[m'

clean:
	rm -f tomo *.o SipHash/halfsiphash.o builtins/*.o libtomo.so

%.1: %.1.md
	pandoc --lua-filter=.pandoc/bold-code.lua -s $< -t man -o $@

install: tomo libtomo.so
	mkdir -p -m 755 "$(PREFIX)/man/man1" "$(PREFIX)/bin" "$(PREFIX)/include/tomo" "$(PREFIX)/lib" "$(PREFIX)/share/tomo/modules"
	cp -v builtins/*.h "$(PREFIX)/include/tomo/"
	cp -v libtomo.so "$(PREFIX)/lib/"
	rm -f "$(PREFIX)/bin/tomo"
	cp -v tomo "$(PREFIX)/bin/"

uninstall:
	rm -rvf "$(PREFIX)/bin/tomo" "$(PREFIX)/include/tomo" "$(PREFIX)/lib/libtomo.so" "$(PREFIX)/share/tomo"; \

.PHONY: all clean install uninstall test tags
