SHELL=/bin/bash -o pipefail
# Run ./configure.sh to choose installation locations:
ifeq ($(wildcard config.mk),)
all: config.mk
	$(MAKE) all
install: config.mk
	$(MAKE) install
install-files: config.mk
	$(MAKE) install-files
install-lib: config.mk
	$(MAKE) install-lib
test: config.mk
	$(MAKE) test
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
LDFLAGS=
INCLUDE_DIRS=
CWARN=-Wall -Wextra -Wno-format -Wno-format-security -Wshadow \
	  -Wno-pedantic \
	  -Wno-pointer-arith \
	  -Wtype-limits -Wunused-result -Wnull-dereference \
	  -Walloca -Wcast-align \
	  -Wdangling-else -Wdate-time -Wdisabled-optimization -Wdouble-promotion \
	  -Wexpansion-to-defined -Wno-float-equal \
	  -Wframe-address -Winline -Winvalid-pch \
	  -Wmissing-format-attribute -Wmissing-include-dirs -Wmissing-noreturn \
	  -Wno-missing-field-initializers \
	  -Wnull-dereference -Woverlength-strings -Wpacked \
	  -Wredundant-decls -Wshadow \
	  -Wno-stack-protector -Wswitch-default \
	  -Wundef -Wunused -Wunused-but-set-variable \
	  -Wunused-const-variable -Wunused-local-typedefs -Wunused-macros -Wvariadic-macros \
	  -Wwrite-strings

ifeq ($(SUDO),)
ifeq ($(shell command -v doas 2>/dev/null),)
	SUDO=sudo
else
	SUDO=doas
endif
endif

OWNER=$(shell ls -ld '$(PREFIX)' | awk '{print $$3}')

ifeq ($(shell $(CC) -v 2>&1 | grep -c "gcc version"), 1)
	LTO += -flto=auto -fno-fat-lto-objects -Wl,-flto
	CWARN += -Werror -Wsign-conversion -Walloc-zero -Wduplicated-branches -Wduplicated-cond -Wjump-misses-init \
			 -Wlogical-op -Wpacked-not-aligned -Wshadow=compatible-local -Wshadow=global -Wshadow=local \
			 -Wsuggest-attribute=const -Wsuggest-attribute=noreturn -Wsuggest-attribute=pure \
			 -Wsync-nand -Wtrampolines -Wvector-operation-performance -Wcast-align=strict
	CCONFIG += -fsanitize=signed-integer-overflow -fno-sanitize-recover -fno-signaling-nans
endif

OS := $(shell uname -s)

OSFLAGS != case $(OS) in *BSD|Darwin) echo '-D_BSD_SOURCE';; Linux) echo '-D_GNU_SOURCE';; *) echo '-D_DEFAULT_SOURCE';; esac
EXTRA=
G=-ggdb
O=-O3
TOMO_VERSION=$(shell awk '/^## / {print $$2; exit}' CHANGES.md)
GIT_VERSION=$(shell git log -1 --pretty=format:"%as_%h")
CFLAGS=$(CCONFIG) $(INCLUDE_DIRS) $(EXTRA) $(CWARN) $(G) $(O) $(OSFLAGS) $(LTO) \
	   -DTOMO_PREFIX='"$(PREFIX)"' -DSUDO='"$(SUDO)"' -DDEFAULT_C_COMPILER='"$(DEFAULT_C_COMPILER)"' \
	   -DTOMO_VERSION='"$(TOMO_VERSION)"' -DGIT_VERSION='"$(GIT_VERSION)"'
CFLAGS_PLACEHOLDER="$$(printf '\033[2m<flags...>\033[m\n')" 
LDLIBS=-lgc -lcord -lm -lunistring -lgmp -ldl -lmpdec
LIBTOMO_FLAGS=-shared

DEFINE_AS_OWNER=as_owner() { \
	if [ "$$USER" = "$(OWNER)" ]; then \
		"$$@"; \
	else \
		$(SUDO) -u "$(OWNER)" "$$@"; \
	fi; \
} \

ifeq ($(OS),OpenBSD)
	LDLIBS += -lexecinfo
endif

AR_FILE=libtomo_$(TOMO_VERSION).a
ifeq ($(OS),Darwin)
	INCLUDE_DIRS += -I/opt/homebrew/include
	LDFLAGS += -L/opt/homebrew/lib
	LIB_FILE=libtomo_$(TOMO_VERSION).dylib
	LIBTOMO_FLAGS += -Wl,-install_name,@rpath/libtomo_$(TOMO_VERSION).dylib
else
	LIB_FILE=libtomo_$(TOMO_VERSION).so
	LIBTOMO_FLAGS += -Wl,-soname,libtomo_$(TOMO_VERSION).so
endif
EXE_FILE=tomo_$(TOMO_VERSION)

COMPILER_OBJS=$(patsubst %.c,%.o,$(wildcard src/*.c))
STDLIB_OBJS=$(patsubst %.c,%.o,$(wildcard src/stdlib/*.c))
TESTS=$(patsubst test/%.tm,test/results/%.tm.testresult,$(wildcard test/*.tm))
API_YAML=$(wildcard api/*.yaml)
API_MD=$(patsubst %.yaml,%.md,$(API_YAML))

all: config.mk check-c-compiler check-libs build/lib/$(LIB_FILE) build/lib/$(AR_FILE) build/bin/$(EXE_FILE)

check-c-compiler:
	@$(DEFAULT_C_COMPILER) -v 2>/dev/null >/dev/null \
		|| { printf '\033[31;1m%s\033[m\n' "You have set your DEFAULT_C_COMPILER to $(DEFAULT_C_COMPILER) in your config.mk, but I can't run it!"; exit 1; }

check-libs: check-c-compiler
	@echo 'int main() { return 0; }' | $(DEFAULT_C_COMPILER) $(LDFLAGS) $(LDLIBS) -x c - -o /dev/null 2>/dev/null >/dev/null \
		|| { printf '\033[31;1m%s\033[m\n' "I expected to find the following libraries on your system, but I can't find them: $(LDLIBS)"; exit 1; }

build/bin/$(EXE_FILE): $(STDLIB_OBJS) $(COMPILER_OBJS)
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

%.o: %.c src/ast.h src/environment.h src/types.h config.mk CHANGES.md
	@echo $(CC) $(CFLAGS_PLACEHOLDER) -c $< -o $@
	@$(CC) $(CFLAGS) -c $< -o $@

%: %.tm
	./local-tomo -e $<

test/results/%.tm.testresult: test/%.tm build/bin/$(EXE_FILE)
	@mkdir -p test/results
	@printf '\033[33;1;4m%s\033[m\n' $<
	@if ! COLOR=1 LC_ALL=C ./local-tomo -O 1 $< 2>&1 | tee $@; then \
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
	./local-tomo -qIL examples/log examples/ini examples/vectors examples/http examples/wrap examples/colorful
	./local-tomo -e examples/game/game.tm examples/http-server/http-server.tm
	./local-tomo examples/learnxiny.tm

deps:
	bash ./install_dependencies.sh

check-utilities: check-c-compiler
	@which debugedit 2>/dev/null >/dev/null \
		|| printf '\033[33;1m%s\033[m\n' "I couldn't find 'debugedit' on your system! Try installing the package 'debugedit' with your package manager. (It's not required though)"

install-files: build/bin/$(EXE_FILE) build/lib/$(LIB_FILE) build/lib/$(AR_FILE) check-utilities
	@if ! echo "$$PATH" | tr ':' '\n' | grep -qx "$(PREFIX)/bin"; then \
		echo $$PATH; \
		printf "\033[31;1mError: '$(PREFIX)/bin' is not in your \$$PATH variable!\033[m\n" >&2; \
		printf "\033[31;1mSpecify a different prefix with 'make PREFIX=... install'\033[m\n" >&2; \
		printf "\033[31;1mor add the following line to your .profile:\033[m\n" >&2; \
		printf "\n\033[1mexport PATH=\"$(PREFIX):\$$PATH\"\033[m\n\n" >&2; \
		exit 1; \
	fi
	$(DEFINE_AS_OWNER); \
	as_owner mkdir -p -m 755 "$(PREFIX)/man/man1" "$(PREFIX)/man/man3" "$(PREFIX)/bin" "$(PREFIX)/include/tomo_$(TOMO_VERSION)" "$(PREFIX)/lib"; \
	as_owner cp src/stdlib/*.h "$(PREFIX)/include/tomo_$(TOMO_VERSION)/"; \
	as_owner cp build/lib/$(LIB_FILE) build/lib/$(AR_FILE) "$(PREFIX)/lib/"; \
	as_owner rm -f "$(PREFIX)/bin/$(EXE_FILE)"; \
	as_owner cp build/bin/$(EXE_FILE) "$(PREFIX)/bin/"; \
	as_owner cp man/man1/* "$(PREFIX)/man/man1/"; \
	as_owner cp man/man3/* "$(PREFIX)/man/man3/"; \
	as_owner sh link_versions.sh

install-libs: build/bin/$(EXE_FILE) check-utilities
	$(DEFINE_AS_OWNER); \
	./local-tomo -qIL lib/patterns lib/time lib/commands lib/shell lib/random lib/base64 lib/pthreads lib/uuid lib/core

install: install-files install-libs

uninstall:
	$(DEFINE_AS_OWNER); \
	as_owner rm -rvf "$(PREFIX)/bin/tomo" "$(PREFIX)/bin/tomo"[0-9]* "$(PREFIX)/bin/tomo_v"* "$(PREFIX)/include/tomo_v"* "$(PREFIX)/lib/libtomo_v*" "$(PREFIX)/share/tomo_$(TOMO_VERSION)"; \
	as_owner sh link_versions.sh

endif

.SUFFIXES:
.PHONY: all clean install install-files install-libs uninstall test tags examples deps check-utilities check-c-compiler check-libs
