SHELL=bash -o pipefail
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

# Modified progress counter based on: https://stackoverflow.com/a/35320895
ifndef NO_PROGRESS
ifndef ECHO
T := $(shell $(MAKE) ECHO="COUNTTHIS" $(MAKECMDGOALS) --no-print-directory \
      -n | grep -c "COUNTTHIS")
N := x
C = $(words $N)$(eval N := x $N)
ECHO = echo -e "[`expr $C '*' 100 / $T`%]"
endif
endif
ifndef ECHO
ECHO = echo
endif
# End of progress counter

CC=$(DEFAULT_C_COMPILER)
CCONFIG=-std=c2x -fPIC \
		-fno-signed-zeros -fno-trapping-math \
		-fvisibility=hidden -fdollars-in-identifiers \
		-DGC_THREADS
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
	CWARN += -Werror -Wsign-conversion -Walloc-zero -Wduplicated-branches -Wduplicated-cond -Wjump-misses-init \
			 -Wlogical-op -Wpacked-not-aligned -Wshadow=compatible-local -Wshadow=global -Wshadow=local \
			 -Wsuggest-attribute=const -Wsuggest-attribute=noreturn -Wsuggest-attribute=pure \
			 -Wsync-nand -Wtrampolines -Wvector-operation-performance -Wcast-align=strict
	CCONFIG += -fsanitize=signed-integer-overflow -fno-sanitize-recover -fno-signaling-nans -fno-finite-math-only 
endif

OS := $(shell uname -s)

OSFLAGS != case $(OS) in *BSD|Darwin) echo '-D_BSD_SOURCE';; Linux) echo '-D_GNU_SOURCE';; *) echo '-D_DEFAULT_SOURCE';; esac
EXTRA=
G=-ggdb
O=-O3
# Note: older versions of Make have buggy behavior with hash marks inside strings, so this ugly code is necessary:
TOMO_VERSION=$(shell awk 'BEGIN{hashes=sprintf("%c%c",35,35)} $$1==hashes {print $$2; exit}' CHANGES.md)
GIT_VERSION=$(shell git log -1 --pretty=format:"%as_%h" 2>/dev/null || echo "unknown")
CFLAGS=$(CCONFIG) $(INCLUDE_DIRS) $(EXTRA) $(CWARN) $(G) $(O) $(OSFLAGS) $(LTO) \
	   -DSUDO='"$(SUDO)"' -DDEFAULT_C_COMPILER='"$(DEFAULT_C_COMPILER)"' \
	   -DGIT_VERSION='"$(GIT_VERSION)"'
CFLAGS_PLACEHOLDER="$$(printf '\033[2m<flags...>\033[m\n')" 
LDLIBS=-lgc -lm -lunistring -lgmp
LIBTOMO_FLAGS=-shared

ifeq ($(OS),OpenBSD)
	LDLIBS += -lexecinfo
else
	LDLIBS += -ldl
endif

AR_FILE=libtomo@$(TOMO_VERSION).a
ifeq ($(OS),Darwin)
	INCLUDE_DIRS += -I/opt/homebrew/include
	LDFLAGS += -L/opt/homebrew/lib
	LIB_FILE=libtomo@$(TOMO_VERSION).dylib
	LIBTOMO_FLAGS += -Wl,-install_name,@rpath/libtomo@$(TOMO_VERSION).dylib
else
	LIB_FILE=libtomo@$(TOMO_VERSION).so
	LIBTOMO_FLAGS += -Wl,-soname,libtomo@$(TOMO_VERSION).so
endif
EXE_FILE=tomo@$(TOMO_VERSION)

COMPILER_OBJS=$(patsubst %.c,%.o,$(wildcard src/*.c src/compile/*.c src/parse/*.c src/formatter/*.c))
STDLIB_OBJS=$(patsubst %.c,%.o,$(wildcard src/stdlib/*.c))
TESTS=$(patsubst test/%.tm,test/results/%.tm.testresult,$(wildcard test/[!_]*.tm))
API_YAML=$(wildcard api/*.yaml)
API_MD=$(patsubst %.yaml,%.md,$(API_YAML))

all: config.mk check-c-compiler check-libs build
	@$(ECHO) "All done!"

BUILD_DIR=build/tomo@$(TOMO_VERSION)
headers := $(wildcard src/stdlib/*.h)
build_headers := $(patsubst src/stdlib/%.h, $(BUILD_DIR)/include/tomo@$(TOMO_VERSION)/%.h, $(headers))

# generate corresponding build paths with .gz
build_manpages := $(patsubst %,$(BUILD_DIR)/%.gz,$(wildcard man/man*/*))

# Ensure directories exist
dirs := $(BUILD_DIR)/include/tomo@$(TOMO_VERSION) \
        $(BUILD_DIR)/lib \
        $(BUILD_DIR)/lib/tomo@$(TOMO_VERSION) \
        $(BUILD_DIR)/bin \
        $(BUILD_DIR)/man/man1 \
        $(BUILD_DIR)/man/man3 \
        $(BUILD_DIR)/share/licenses/tomo@$(TOMO_VERSION)

$(dirs):
	mkdir -p $@

# Rule for copying headers
$(BUILD_DIR)/include/tomo@$(TOMO_VERSION)%.h: src/stdlib/%.h | $(BUILD_DIR)/include/tomo@$(TOMO_VERSION)
	cp $< $@

# Rule for gzipping man pages
$(BUILD_DIR)/man/%.gz: man/% | $(BUILD_DIR)/man/man1 $(BUILD_DIR)/man/man3
	gzip -c $< > $@

$(BUILD_DIR)/bin/tomo: $(BUILD_DIR)/bin/tomo@$(TOMO_VERSION) | $(BUILD_DIR)/bin
	ln -sf tomo@$(TOMO_VERSION) $@

$(BUILD_DIR)/bin/$(EXE_FILE): $(STDLIB_OBJS) $(COMPILER_OBJS) | $(BUILD_DIR)/bin
	@$(ECHO) $(CC) $(CFLAGS_PLACEHOLDER) $(LDFLAGS) $^ $(LDLIBS) -o $@
	@$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/lib/$(LIB_FILE): $(STDLIB_OBJS) | $(BUILD_DIR)/lib
	@$(ECHO) $(CC) $^ $(CFLAGS_PLACEHOLDER) $(OSFLAGS) $(LDFLAGS) $(LDLIBS) $(LIBTOMO_FLAGS) -o $@
	@$(CC) $^ $(CFLAGS) $(OSFLAGS) $(LDFLAGS) $(LDLIBS) $(LIBTOMO_FLAGS) -o $@

$(BUILD_DIR)/lib/$(AR_FILE): $(STDLIB_OBJS) | $(BUILD_DIR)/lib
	ar -rcs $@ $^

$(BUILD_DIR)/lib/tomo@$(TOMO_VERSION)/modules.ini: modules/core.ini modules/examples.ini | $(BUILD_DIR)/lib/tomo@$(TOMO_VERSION)
	@cat $^ > $@

$(BUILD_DIR)/share/licenses/tomo@$(TOMO_VERSION)/LICENSE.md: LICENSE.md | $(BUILD_DIR)/share/licenses/tomo@$(TOMO_VERSION)
	cp $< $@

build: $(BUILD_DIR)/bin/tomo $(BUILD_DIR)/bin/tomo@$(TOMO_VERSION) $(BUILD_DIR)/lib/$(LIB_FILE) \
	$(BUILD_DIR)/lib/$(AR_FILE) $(BUILD_DIR)/lib/tomo@$(TOMO_VERSION)/modules.ini \
	$(BUILD_DIR)/share/licenses/tomo@$(TOMO_VERSION)/LICENSE.md $(build_headers) $(build_manpages)

version:
	@echo $(TOMO_VERSION)

check-c-compiler:
	@$(DEFAULT_C_COMPILER) -v 2>/dev/null >/dev/null \
		|| { printf '\033[31;1m%s\033[m\n' "You have set your DEFAULT_C_COMPILER to $(DEFAULT_C_COMPILER) in your config.mk, but I can't run it!"; exit 1; }

check-libs: check-c-compiler
	@echo 'int main() { return 0; }' | $(DEFAULT_C_COMPILER) $(LDFLAGS) $(LDLIBS) -x c - -o /dev/null 2>/dev/null >/dev/null \
		|| { printf '\033[31;1m%s\033[m\n' "I expected to find the following libraries on your system, but I can't find them: $(LDLIBS)"; exit 1; }

tags:
	ctags src/*.{c,h} src/stdlib/*.{c,h} src/compile/*.{c,h} src/parse/*.{c,h} src/formatter/*.{c,h}

config.mk: configure.sh
	bash ./configure.sh

%.o: %.c src/ast.h src/environment.h src/types.h config.mk
	@$(ECHO) $(CC) $(CFLAGS_PLACEHOLDER) -c $< -o $@
	@$(CC) $(CFLAGS) -c $< -o $@

# Integer implementations depend on the shared header:
src/stdlib/int64.o src/stdlib/int32.o src/stdlib/int16.o src/stdlib/int8.o: src/stdlib/intX.c.h src/stdlib/intX.h

# Num implementations depend on the shared header:
src/stdlib/num32.o src/stdlib/num64.o: src/stdlib/numX.c.h

%: %.tm
	./local-tomo -e $<

test/results/%.tm.testresult: test/%.tm build
	@mkdir -p test/results
	@printf '\033[33;1;4m%s\033[m\n' $<
	@if ! COLOR=1 LC_ALL=C ./local-tomo -O 1 $< 2>&1 | tee $@; then \
		rm -f $@; \
		false; \
	fi

test: $(TESTS)
	@printf '\033[32;7m ALL TESTS PASSED! \033[m\n'

clean:
	rm -rf build/* $(COMPILER_OBJS) $(STDLIB_OBJS) test/*.tm.testresult test/.build lib/*/.build examples/.build examples/*/.build

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
	./scripts/mandoc_gen.py $(API_YAML)

man/man1/tomo.1: docs/tomo.1.md
	pandoc --lua-filter=docs/.pandoc/bold-code.lua -s $< -t man -o $@

examples:
	./local-tomo -L modules/examples.ini
	./local-tomo examples/learnxiny.tm

core-libs:
	./local-tomo -L modules/core.ini

deps:
	bash ./install_dependencies.sh

check-utilities: check-c-compiler
	@which debugedit 2>/dev/null >/dev/null \
		|| printf '\033[33;1m%s\033[m\n' "I couldn't find 'debugedit' on your system! Try installing the package 'debugedit' with your package manager. (It's not required though)"

install-files: build check-utilities
	@if ! echo "$$PATH" | tr ':' '\n' | grep -qx "$(PREFIX)/bin"; then \
		echo $$PATH; \
		printf "\033[31;1mError: '$(PREFIX)/bin' is not in your \$$PATH variable!\033[m\n" >&2; \
		printf "\033[31;1mSpecify a different prefix with 'make PREFIX=... install'\033[m\n" >&2; \
		printf "\033[31;1mor add the following line to your .profile:\033[m\n" >&2; \
		printf "\n\033[1mexport PATH=\"$(PREFIX):\$$PATH\"\033[m\n\n" >&2; \
		exit 1; \
	fi
	if ! [ -w "$(PREFIX)" ]; then \
		$(SUDO) -u $(OWNER) $(MAKE) install-files; \
		exit 0; \
	fi; \
	cp -r $(BUILD_DIR)/* $(PREFIX)/

install: install-files

uninstall:
	if ! [ -w "$(PREFIX)" ]; then \
		$(SUDO) -u $(OWNER) $(MAKE) uninstall; \
		exit 0; \
	fi; \
	rm -rvf "$(PREFIX)/bin/tomo" "$(PREFIX)/bin/tomo"* "$(PREFIX)/include/tomo"* \
		"$(PREFIX)/lib/libtomo@"* "$(PREFIX)/lib/tomo@"* "$(PREFIX)/share/licenses/tomo@"*; \

endif

.SUFFIXES:
.PHONY: all build clean install install-files uninstall test tags core-libs examples deps check-utilities check-c-compiler check-libs version
