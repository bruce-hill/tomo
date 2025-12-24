GMP_VERSION=6.3.0
UNISTRING_VERSION=1.4.1
GC_VERSION=8.2.8

all: build-gc/include/gc.h build-gc/lib/libgc.a build-gmp/lib/libgmp.a build-unistring/lib/libunistring.a

# Boehm-Demers-Weiser Garbage collector
gc-$(GC_VERSION).tar.gz:
	curl -LOJ 'https://hboehm.info/gc/gc_source/gc-$(GC_VERSION).tar.gz'

gc-$(GC_VERSION)/configure: gc-$(GC_VERSION).tar.gz
	tar xzfm $<

gc-$(GC_VERSION)/Makefile: gc-$(GC_VERSION)/configure
	prefix=$$(realpath ./build-gc); \
	cd gc-$(GC_VERSION); \
	./configure \
		--enable-static \
		--disable-shared \
		--prefix="$$prefix"; \

build-gc/include/gc.h build-gc/lib/libgc.a: gc-$(GC_VERSION)/Makefile
	cd gc-$(GC_VERSION); \
	$(MAKE) -j install

# GNU Multiple Precision Arithmetic Library
gmp-$(GMP_VERSION).tar.xz:
	curl -LOJ 'https://gmplib.org/download/gmp/gmp-$(GMP_VERSION).tar.xz'

gmp-$(GMP_VERSION)/configure: gmp-$(GMP_VERSION).tar.xz
	tar xJfm $<
	cd gmp-$(GMP_VERSION) && patch -p0 -N < ../gmp-configure-fix.patch

gmp-$(GMP_VERSION)/Makefile: gmp-$(GMP_VERSION)/configure
	prefix=$$(realpath ./build-gmp); \
	cd gmp-$(GMP_VERSION); \
	./configure \
		--enable-static \
		--disable-shared \
		--prefix="$$prefix"

build-gmp/lib/libgmp.a: gmp-$(GMP_VERSION)/Makefile
	$(MAKE) -C gmp-$(GMP_VERSION) -j
	$(MAKE) -C gmp-$(GMP_VERSION) check
	$(MAKE) -C gmp-$(GMP_VERSION) install

# Lib Unistring
libunistring-$(UNISTRING_VERSION).tar.gz: 
	curl -LOJ 'https://ftp.gnu.org/gnu/libunistring/libunistring-$(UNISTRING_VERSION).tar.gz'

libunistring-$(UNISTRING_VERSION)/configure: libunistring-$(UNISTRING_VERSION).tar.gz
	tar xzfm $<

libunistring-$(UNISTRING_VERSION)/Makefile: libunistring-$(UNISTRING_VERSION)/configure
	prefix=$$(realpath ./build-unistring); \
	cd libunistring-$(UNISTRING_VERSION); \
	./configure \
		--enable-static \
		--disable-shared \
		--prefix="$$prefix"

build-unistring/lib/libunistring.a: libunistring-$(UNISTRING_VERSION)/Makefile
	$(MAKE) -C libunistring-$(UNISTRING_VERSION) -j
	$(MAKE) -C libunistring-$(UNISTRING_VERSION) check
	$(MAKE) -C libunistring-$(UNISTRING_VERSION) install

clean:
	rm -rf build-gc build-gmp build-unistring

.PHONY: all clean
