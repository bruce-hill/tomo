PREFIX=$(HOME)/.local
VERSION=0.0.1
CC=cc
CCONFIG=-std=c2x -Werror -D_XOPEN_SOURCE=700 -D_POSIX_C_SOURCE=200809L -fPIC -I/usr/local/include \
		-fno-signed-zeros -fno-finite-math-only -fno-signaling-nans -fno-trapping-math \
		-fsanitize=signed-integer-overflow -fno-sanitize-recover -fvisibility=hidden -fdollars-in-identifiers \
		-DGC_THREADS
LTO=-flto=auto -fno-fat-lto-objects -Wl,-flto 
LDFLAGS=-L/usr/local/lib
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
CFLAGS_PLACEHOLDER="$$(printf '\033[2m<flags...>\033[m\n')" 
LDLIBS=-lgc -lcord -lm -lunistring -lgmp
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
	@echo $(CC) $^ $(CFLAGS_PLACEHOLDER) $(OSFLAGS) $(LDLIBS) -Wl,-soname,libtomo.so -shared -o $@
	@$(CC) $^ $(CFLAGS) $(OSFLAGS) $(LDLIBS) -Wl,-soname,libtomo.so -shared -o $@

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
	@printf '\x1b[32;7m ALL TESTS PASSED! \x1b[m\n'

clean:
	rm -rf build/* $(COMPILER_OBJS) $(STDLIB_OBJS) test/*.tm.testresult test/.build examples/.build examples/*/.build

%: %.md
	pandoc --lua-filter=docs/.pandoc/bold-code.lua -s $< -t man -o $@

examples: examples/base64/base64 examples/ini/ini examples/game/game \
		examples/tomodeps/tomodeps examples/tomo-install/tomo-install examples/wrap/wrap examples/colorful/colorful
	./build/tomo -qIL examples/commands examples/shell examples/base64 examples/log examples/ini examples/vectors examples/game \
		examples/http examples/threads examples/tomodeps examples/tomo-install examples/wrap examples/pthreads examples/colorful
	./build/tomo examples/learnxiny.tm

deps: check-gcc
	./install_dependencies.sh

check-gcc:
	@GCC_VERSION=$$(gcc --version | awk '{print $$3;exit}'); \
	if [ "$$(printf '%s\n' "$$GCC_VERSION" "12.0.0" | sort -V | head -n 1)" = "12.0.0" ]; then \
		printf "\033[32;1mGCC version is good!\033[m\n"; \
		exit 0; \
	else \
		printf "\033[31;1mGCC version is lower than the required 12.0.0!\033[m\n" >&2; \
		exit 1; \
	fi

install: build/tomo build/libtomo.so
	@if ! echo "$$PATH" | tr ':' '\n' | grep -qx "$(PREFIX)/bin"; then \
		printf "\033[31;1mError: '$(PREFIX)' is not in your \$$PATH variable!\033[m\n" >&2; \
		printf "\033[31;1mSpecify a different prefix with 'make PREFIX=... install'\033[m\n" >&2; \
		printf "\033[31;1mor add the following line to your .profile:\033[m\n" >&2; \
		printf "\n\033[1mexport PATH=\"$(PREFIX):\$$PATH\"\033[m\n\n" >&2; \
		exit 1; \
	fi
	mkdir -p -m 755 "$(PREFIX)/man/man1" "$(PREFIX)/bin" "$(PREFIX)/include/tomo" "$(PREFIX)/lib" "$(PREFIX)/share/tomo/modules"
	cp -v src/stdlib/*.h "$(PREFIX)/include/tomo/"
	cp -v build/libtomo.so "$(PREFIX)/lib/"
	rm -f "$(PREFIX)/bin/tomo"
	cp -v build/tomo "$(PREFIX)/bin/"
	cp -v docs/tomo.1 "$(PREFIX)/man/man1/"

uninstall:
	rm -rvf "$(PREFIX)/bin/tomo" "$(PREFIX)/include/tomo" "$(PREFIX)/lib/libtomo.so" "$(PREFIX)/share/tomo"; \

.SUFFIXES:
.PHONY: all clean install uninstall test tags examples deps check-gcc
