# Run ./configure.sh to choose installation locations:
ifeq ($(wildcard config.mk),)
all: config.mk
	$(MAKE) all
config.mk: configure.sh
	bash ./configure.sh
else

include config.mk

CC=cc
CCONFIG=-std=c2x -fPIC \
		-fno-signed-zeros -fno-finite-math-only -fno-trapping-math \
		-fvisibility=hidden -fdollars-in-identifiers \
		-DGC_THREADS
LTO=
LDFLAGS=-L/usr/local/lib
INCLUDE_DIRS=-I/usr/local/include
CWARN=-Wall -Wextra -Wno-format -Wshadow \
	  -Wno-pedantic \
	  -Wno-pointer-arith \
	  -Wtype-limits -Wunused-result -Wnull-dereference \
	  -Walloca -Wcast-align \
	  -Wdangling-else -Wdate-time -Wdisabled-optimization -Wdouble-promotion \
	  -Wexpansion-to-defined -Wno-float-equal \
	  -Wframe-address -Winline -Winvalid-pch \
	  -Wmissing-format-attribute -Wmissing-include-dirs -Wmissing-noreturn \
	  -Wnull-dereference -Woverlength-strings -Wpacked \
	  -Wredundant-decls -Wshadow \
	  -Wno-stack-protector -Wswitch-default \
	  -Wundef -Wunused -Wunused-but-set-variable \
	  -Wunused-const-variable -Wunused-local-typedefs -Wunused-macros -Wvariadic-macros \
	  -Wwrite-strings

ifeq ($(shell $(CC) -v 2>&1 | grep -c "gcc version"), 1)
	LTO += -flto=auto -fno-fat-lto-objects -Wl,-flto
	CWARN += -Werror -Wsign-conversion -Walloc-zero -Wduplicated-branches -Wduplicated-cond -Wjump-misses-init \
			 -Wlogical-op -Wpacked-not-aligned -Wshadow=compatible-local -Wshadow=global -Wshadow=local \
			 -Wsuggest-attribute=const -Wsuggest-attribute=noreturn -Wsuggest-attribute=pure \
			 -Wsync-nand -Wtrampolines -Wvector-operation-performance -Wcast-align=strict
	CCONFIG += -fsanitize=signed-integer-overflow -fno-sanitize-recover -fno-signaling-nans
else
	CWARN += -Wno-missing-field-initializers
endif

OS := $(shell uname -s)

OSFLAGS != case $(OS) in *BSD|Darwin) echo '-D_BSD_SOURCE';; Linux) echo '-D_GNU_SOURCE';; *) echo '-D_DEFAULT_SOURCE';; esac
EXTRA=
G=-ggdb
O=-O3
GIT_VERSION=$(shell git log -1 --pretty=format:"$$(git describe --tags --abbrev=0)_%as_%h")
CFLAGS=$(CCONFIG) $(INCLUDE_DIRS) $(EXTRA) $(CWARN) $(G) $(O) $(OSFLAGS) $(LTO) \
	   -DTOMO_HOME='"$(TOMO_HOME)"' -DTOMO_PREFIX='"$(PREFIX)"' -DDEFAULT_C_COMPILER='"$(DEFAULT_C_COMPILER)"' \
	   -DTOMO_VERSION='"$(GIT_VERSION)"'
CFLAGS_PLACEHOLDER="$$(printf '\033[2m<flags...>\033[m\n')" 
LDLIBS=-lgc -lcord -lm -lunistring -lgmp
LIBTOMO_FLAGS=-shared

ifeq ($(OS),OpenBSD)
	LDLIBS += -lexecinfo
endif

AR_FILE=libtomo.a
ifeq ($(OS),Darwin)
	INCLUDE_DIRS += -I/opt/homebrew/include
	LDFLAGS += -L/opt/homebrew/lib
	LIB_FILE=libtomo.dylib
	LIBTOMO_FLAGS += -Wl,-install_name,@rpath/libtomo.dylib
else
	LIB_FILE=libtomo.so
	LIBTOMO_FLAGS += -Wl,-soname,libtomo.so
endif

COMPILER_OBJS=$(patsubst %.c,%.o,$(wildcard src/*.c))
STDLIB_OBJS=$(patsubst %.c,%.o,$(wildcard src/stdlib/*.c))
TESTS=$(patsubst test/%.tm,test/results/%.tm.testresult,$(wildcard test/*.tm))
API_YAML=$(wildcard api/*.yaml)
API_MD=$(patsubst %.yaml,%.md,$(API_YAML))

all: config.mk check-c-compiler check-libs build/lib/$(LIB_FILE) build/lib/$(AR_FILE) build/bin/tomo

check-c-compiler:
	@$(DEFAULT_C_COMPILER) -v 2>/dev/null >/dev/null \
		|| { printf '\033[31;1m%s\033[m\n' "You have set your DEFAULT_C_COMPILER to $(DEFAULT_C_COMPILER) in your config.mk, but I can't run it!"; exit 1; }

check-libs: check-c-compiler
	@echo 'int main() { return 0; }' | $(DEFAULT_C_COMPILER) $(LDLIBS) -x c - -o /dev/null 2>/dev/null >/dev/null \
		|| { printf '\033[31;1m%s\033[m\n' "I expected to find the following libraries on your system, but I can't find them: $(LDLIBS)"; exit 1; }

build/bin/tomo: $(STDLIB_OBJS) $(COMPILER_OBJS)
	@mkdir -p build/bin
	@echo $(CC) $(CFLAGS_PLACEHOLDER) $(LDFLAGS) $^ $(LDLIBS) -o $@
	@$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/lib/$(LIB_FILE): $(STDLIB_OBJS)
	@mkdir -p build/lib
	@echo $(CC) $^ $(CFLAGS_PLACEHOLDER) $(OSFLAGS) $(LDFLAGS) $(LDLIBS) $(LIBTOMO_FLAGS) -o $@
	@$(CC) $^ $(CFLAGS) $(OSFLAGS) $(LDFLAGS) $(LDLIBS) $(LIBTOMO_FLAGS) -o $@

build/lib/$(AR_FILE): $(STDLIB_OBJS)
	@mkdir -p build/lib
	ar -rcs $@ $^

tags:
	ctags src/*.[ch] src/stdlib/*.[ch]

config.mk: configure.sh
	bash ./configure.sh

%.o: %.c src/ast.h src/environment.h src/types.h config.mk
	@echo $(CC) $(CFLAGS_PLACEHOLDER) -c $< -o $@
	@$(CC) $(CFLAGS) -c $< -o $@

%: %.tm
	./local-tomo -e $<

test/results/%.tm.testresult: test/%.tm build/bin/tomo
	@mkdir -p test/results
	@printf '\033[33;1;4m%s\033[m\n' $<
	@set -o pipefail; \
	if ! COLOR=1 LC_ALL=C ./local-tomo -O 1 $< 2>&1 | tee $@; then \
		rm -f $@; \
		false; \
	fi

test: $(TESTS)
	@printf '\033[32;7m ALL TESTS PASSED! \033[m\n'

clean:
	rm -rf build/{lib,bin}/* $(COMPILER_OBJS) $(STDLIB_OBJS) test/*.tm.testresult test/.build lib/*/.build examples/.build examples/*/.build

%: %.md
	pandoc --lua-filter=docs/.pandoc/bold-code.lua -s $< -t man -o $@

%.md: %.yaml scripts/api_gen.py
	./scripts/api_gen.py $< >$@

api/api.md: $(API_YAML)
	./scripts/api_gen.py $^ >$@

.PHONY: api-docs
api-docs: $(API_MD) api/api.md

.PHONY: manpages
manpages: $(API_YAML) man/man1/tomo.1
	rm -f man/man3/*
	./scripts/mandoc_gen.py $(API_YAML)

man/man1/tomo.1: docs/tomo.1.md
	pandoc --lua-filter=docs/.pandoc/bold-code.lua -s $< -t man -o $@

examples:
	./build/bin/tomo -qIL examples/log examples/ini examples/vectors examples/http examples/wrap examples/colorful
	./build/bin/tomo -e examples/game/game.tm examples/http-server/http-server.tm \
		examples/tomodeps/tomodeps.tm examples/tomo-install/tomo-install.tm
	./build/bin/tomo examples/learnxiny.tm

deps:
	bash ./install_dependencies.sh

check-utilities: check-c-compiler
	@which objcopy 2>/dev/null >/dev/null \
		|| { printf '\033[31;1m%s\033[m\n' "I couldn't find 'objcopy' on your system! Try installing the package 'binutils' with your package manager."; exit 1; }
	@if echo | $(DEFAULT_C_COMPILER) -dM -E - | grep -q '__ELF__'; then \
		which patchelf 2>/dev/null >/dev/null \
			|| { printf '\033[31;1m%s\033[m\n' "I couldn't find 'patchelf' on your system! Try installing the package 'binutils' with your package manager."; exit 1; }; \
	else \
		which llvm-objcopy 2>/dev/null >/dev/null \
			|| { printf '\033[31;1m%s\033[m\n' "I couldn't find 'llvm-objcopy' on your system! Try installing the package 'llvm' with your package manager."; exit 1; }; \
	fi
	@which nm 2>/dev/null >/dev/null \
		|| { printf '\033[31;1m%s\033[m\n' "I couldn't find 'nm' on your system! Try installing the package 'binutils' with your package manager."; exit 1; }
	@which awk 2>/dev/null >/dev/null \
		|| { printf '\033[31;1m%s\033[m\n' "I couldn't find 'awk' on your system! Try installing the package 'awk' with your package manager."; exit 1; }
	@which debugedit 2>/dev/null >/dev/null \
		|| { printf '\033[33;1m%s\033[m\n' "I couldn't find 'debugedit' on your system! Try installing the package 'debugedit' with your package manager. (It's not required though)"; exit 1; }

install-files: build/bin/tomo build/lib/$(LIB_FILE) build/lib/$(AR_FILE) check-utilities
	@if ! echo "$$PATH" | tr ':' '\n' | grep -qx "$(PREFIX)/bin"; then \
		echo $$PATH; \
		printf "\033[31;1mError: '$(PREFIX)/bin' is not in your \$$PATH variable!\033[m\n" >&2; \
		printf "\033[31;1mSpecify a different prefix with 'make PREFIX=... install'\033[m\n" >&2; \
		printf "\033[31;1mor add the following line to your .profile:\033[m\n" >&2; \
		printf "\n\033[1mexport PATH=\"$(PREFIX):\$$PATH\"\033[m\n\n" >&2; \
		exit 1; \
	fi
	mkdir -p -m 755 "$(PREFIX)/man/man1" "$(PREFIX)/man/man3" "$(PREFIX)/bin" "$(PREFIX)/include/tomo" "$(PREFIX)/lib" "$(PREFIX)/share/tomo/modules"
	cp src/stdlib/*.h "$(PREFIX)/include/tomo/"
	cp build/lib/$(LIB_FILE) build/lib/$(AR_FILE) "$(PREFIX)/lib/"
	rm -f "$(PREFIX)/bin/tomo"
	cp build/bin/tomo "$(PREFIX)/bin/"
	cp man/man1/* "$(PREFIX)/man/man1/"
	cp man/man3/* "$(PREFIX)/man/man3/"

install-libs: build/bin/tomo check-utilities
	# Coroutines don't work with TCC for now
	if $(DEFAULT_C_COMPILER) --version | grep -q 'tcc version'; then \
		./local-tomo -qIL lib/patterns lib/time lib/commands lib/shell lib/random lib/base64 lib/pthreads lib/uuid lib/core; \
	else \
		./local-tomo -qIL lib/patterns lib/time lib/commands lib/shell lib/random lib/base64 lib/coroutines lib/pthreads lib/uuid lib/core; \
	fi

install: install-files install-libs

uninstall:
	rm -rvf "$(PREFIX)/bin/tomo" "$(PREFIX)/include/tomo" "$(PREFIX)/lib/$(LIB_FILE)" "$(PREFIX)/lib/$(AR_FILE)" "$(PREFIX)/share/tomo";

endif

.SUFFIXES:
.PHONY: all clean install install-files install-libs uninstall test tags examples deps check-utilities check-c-compiler check-libs
