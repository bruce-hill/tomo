PREFIX=/usr
VERSION=0.0.1
CC=gcc
CCONFIG=-std=c23 -Werror -D_XOPEN_SOURCE=700 -D_POSIX_C_SOURCE=200809L -fPIC -I. \
		-fno-signed-zeros -fno-finite-math-only -fno-signaling-nans -fno-trapping-math \
		-fsanitize=signed-integer-overflow -fno-sanitize-recover -fvisibility=hidden -fdollars-in-identifiers
LTO=-flto=auto -fno-fat-lto-objects -Wl,-flto 
LDFLAGS=
# MAKEFLAGS := --jobs=$(shell nproc) --output-sync=target
CWARN=-Wall -Wextra -Wno-format -Wshadow \
	  -Wno-pedantic \
	  -Wno-pointer-arith \
	  -Wsign-conversion -Wtype-limits -Wunused-result -Wnull-dereference \
	  -Walloc-zero -Walloca -Warith-conversion -Wcast-align -Wcast-align=strict \
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
BUILTIN_OBJS=stdlib/siphash.o stdlib/arrays.o stdlib/bools.o stdlib/bytes.o stdlib/channels.o stdlib/nums.o stdlib/integers.o \
						 stdlib/pointers.o stdlib/memory.o stdlib/text.o stdlib/threads.o stdlib/c_strings.o stdlib/tables.o \
						 stdlib/types.o stdlib/util.o stdlib/files.o stdlib/ranges.o stdlib/shell.o stdlib/paths.o stdlib/rng.o \
						 stdlib/optionals.o stdlib/patterns.o stdlib/metamethods.o stdlib/functiontype.o stdlib/stdlib.o stdlib/datetime.o
TESTS=$(patsubst %.tm,%.tm.testresult,$(wildcard test/*.tm))

all: libtomo.so tomo

tomo: tomo.o $(BUILTIN_OBJS) ast.o parse.o environment.o types.o typecheck.o structs.o enums.o compile.o repl.o cordhelpers.o
	@echo $(CC) $(CFLAGS_PLACEHOLDER) $(LDFLAGS) $^ $(LDLIBS) -o $@
	@$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

libtomo.so: $(BUILTIN_OBJS)
	@echo $(CC) $^ $(CFLAGS_PLACEHOLDER) $(OSFLAGS) -lgc -lcord -lm -lunistring -lgmp -ldl -Wl,-soname,libtomo.so -shared -o $@
	@$(CC) $^ $(CFLAGS) $(OSFLAGS) -lgc -lcord -lm -lunistring -lgmp -ldl -Wl,-soname,libtomo.so -shared -o $@

tags:
	ctags *.[ch] **/*.[ch]

%.o: %.c ast.h environment.h types.h
	@echo $(CC) $(CFLAGS_PLACEHOLDER) -c $< -o $@
	@$(CC) $(CFLAGS) -c $< -o $@

%.tm.testresult: %.tm tomo
	@printf '\x1b[33;1;4m%s\x1b[m\n' $<
	@set -o pipefail; \
	if ! VERBOSE=0 COLOR=1 CC=gcc O=1 ./tomo $< 2>&1 | tee $@; then \
		rm -f $@; \
		false; \
	fi

test: $(TESTS)
	@echo -e '\x1b[32;7m ALL TESTS PASSED! \x1b[m'

clean:
	rm -f tomo *.o stdlib/*.o libtomo.so test/*.tm.{c,h,o,testresult} examples/**/*.tm.{c,h,o}

%: %.md
	pandoc --lua-filter=.pandoc/bold-code.lua -s $< -t man -o $@

install: tomo libtomo.so tomo.1
	mkdir -p -m 755 "$(PREFIX)/man/man1" "$(PREFIX)/bin" "$(PREFIX)/include/tomo" "$(PREFIX)/lib" "$(PREFIX)/share/tomo/modules"
	cp -v stdlib/*.h "$(PREFIX)/include/tomo/"
	cp -v libtomo.so "$(PREFIX)/lib/"
	rm -f "$(PREFIX)/bin/tomo"
	cp -v tomo "$(PREFIX)/bin/"
	cp -v tomo.1 "$(PREFIX)/man/man1/"

uninstall:
	rm -rvf "$(PREFIX)/bin/tomo" "$(PREFIX)/include/tomo" "$(PREFIX)/lib/libtomo.so" "$(PREFIX)/share/tomo"; \

.SUFFIXES:
.PHONY: all clean install uninstall test tags
