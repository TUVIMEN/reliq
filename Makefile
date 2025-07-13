VERSION = 2.11
CC = gcc -std=c18
CFLAGS = -O3 -march=native -Wall -Wextra -Wno-implicit-fallthrough -Wpedantic
LDFLAGS =
TARGET = reliq

HTML_SIZE = 1 #limit max size of html tag names, attrib names, attrib values etc. , set to O_HTML_SMALL
O_HTML_FULL := 0
O_HTML_SMALL := 0
O_HTML_VERY_SMALL := 0

O_SMALL_STACK := 0 # limits for small stack
O_PHPTAGS := 1 # support for <?php ?>
O_AUTOCLOSING := 1 # support for autoclosing tags (tag ommission https://html.spec.whatwg.org/multipage/syntax.html#optional-tags)

D := 0 # debug mode
S := 0 # build with sanitizer

PREFIX ?= /usr
MANPREFIX ?= ${PREFIX}/share/man
BINDIR = ${DESTDIR}${PREFIX}/bin
MANDIR = $(DESTDIR)${MANPREFIX}/man1
LD_LIBRARY_PATH ?= ${PREFIX}/lib
INCLUDE_PATH ?= ${PREFIX}/include

# all of the above variables can be changed from cli

O_LIB := 0 # compile libreliq
O_LINKED := 0 # link reliq to libreliq

STRIP_ARGS =
LDFLAGS_R = ${LDFLAGS}
CFLAGS_D = -DRELIQ_VERSION=\"${VERSION}\"
CFLAGS_R =

LIB_SRC = src/lib/sink.c src/lib/html.c src/lib/hnode.c src/lib/reliq.c src/lib/hnode_print.c src/lib/ctype.c src/lib/utils.c src/lib/output.c src/lib/entities.c src/lib/pattern.c src/lib/range.c src/lib/exprs_comp.c src/lib/exprs_exec.c src/lib/format.c src/lib/npattern_comp.c src/lib/npattern_exec.c src/lib/node_exec.c src/lib/edit.c src/lib/edit_sed.c src/lib/edit_wc.c src/lib/edit_tr.c src/lib/url.c src/lib/scheme.c src/lib/fields.c ${LIB_OTHERS}

CLI_SRC = src/cli/main.c src/cli/usage.c src/cli/pretty.c

ifeq ("$(shell uname -s)","Darwin")
	STRIP_ARGS += -x
endif

ifeq ($(strip ${O_HTML_VERY_SMALL}),1)
	HTML_SIZE=0
endif
ifeq ($(strip ${O_HTML_SMALL}),1)
	HTML_SIZE=1
endif
ifeq ($(strip ${O_HTML_FULL}),1)
	HTML_SIZE=2
endif
CFLAGS_D += -DRELIQ_HTML_SIZE=${HTML_SIZE}

ifeq ($(strip ${O_SMALL_STACK}),1)
	CFLAGS_D += -DRELIQ_SMALL_STACK
endif

ifeq ($(strip ${O_PHPTAGS}),1)
	CFLAGS_D += -DRELIQ_PHPTAGS
endif

ifeq ($(strip ${O_AUTOCLOSING}),1)
	CFLAGS_D += -DRELIQ_AUTOCLOSING
endif

SRC = src/flexarr.c ${LIB_SRC}

ifeq ($(strip ${O_LIB}),1)
	LDFLAGS_R += -shared
	CFLAGS_R += -fPIC
else
	SRC += ${CLI_SRC}
endif

ifeq ($(strip ${O_LINKED}),1)
	CFLAGS_R += -DLINKED
	SRC = ${CLI_SRC}
	LDFLAGS_R += -lreliq
endif

ifeq ($(strip ${S}),1)
	CFLAGS_R += -fsanitize=address
	D = 1
endif

ifeq ($(strip ${D}),1)
	CFLAGS_R += -DDEBUG -O0 -ggdb
endif

TEST_FLAGS=$(shell echo "${CFLAGS_D}" | sed -E 's/(^| )-D/\1/g; s/(^| )[a-zA-Z0-9_]+=("[^"]*")?[^ ]*( |$$)//')
CFLAGS_ALL = ${CFLAGS_D} ${CFLAGS} ${CFLAGS_R}

OBJ = ${SRC:.c=.o}

.PHONY: all options lib lib-install install linked test test-advanced test-errors test-all test-update test-speed install-pc dist uninstall clean reliq-h afl

all: options reliq

afl: clean
	@make CC="afl-clang-fast -std=c18"

options:
	@echo ${SRC}
	@echo ${TARGET} build options:
	@echo "CFLAGS   = ${CFLAGS_ALL}"
	@echo "LDFLAGS  = $(strip ${LDFLAGS_R})"
	@echo "CC       = ${CC}"

reliq-h:
	@echo ${CFLAGS_D} | cat - src/lib/reliq.h | sed '1{ s/ /\n/g; s/-D/#define /g; s/=/ /g; h; d; }; /^\/\/#RELIQ_COMPILE_FLAGS/{ s/.*//;G;D; }' > reliq.h


lib: clean reliq-h
	@make D=${D} S=${S} O_LIB=1 TARGET=lib${TARGET}.so

install-pc:
	sed "s/#VERSION#/${VERSION}/g;s|#PREFIX#|${PREFIX}|g;s/#CFLAGS_D#/${CFLAGS_D}/g" reliq.pc > $$(pkg-config --variable pc_path pkg-config | cut -d: -f1)/reliq.pc

lib-install: lib install-pc
	install -m755 lib${TARGET}.so ${LD_LIBRARY_PATH}
	install -m644 reliq.h ${INCLUDE_PATH}

linked: lib lib-install
	@make D=${D} S=${S} O_LINKED=1

reliq: ${OBJ}
	${CC} ${CFLAGS_ALL} $^ ${LDFLAGS_R} -o ${TARGET}
	[ "${D}" -eq 0 ] && strip ${STRIP_ARGS} ${TARGET} || true

%.o: %.c
	${CC} ${CFLAGS_ALL} -c $< -o $@

test: all
	@./tests/test.sh tests/basic.test . "${TEST_FLAGS}" || true

test-advanced: all
	@./tests/test.sh tests/advanced.test . "${TEST_FLAGS}" || true

test-errors: all
	@./tests/test.sh tests/errors.test . "${TEST_FLAGS}" || true

test-afl: all
	@./tests/test.sh tests/afl.test . "${TEST_FLAGS}" || true

test-pretty:
	@./tests/test.sh tests/pretty.test . "${TEST_FLAGS}" || true

test-all: all
	@./tests/test.sh tests/all.test . "${TEST_FLAGS}" || true
	@./tests/test_urlparse.py || true

test-update: all
	@./tests/test.sh tests/all.test update "${TEST_FLAGS}" || true

test-speed: lib
	@gcc -O3 speed.c -o speed ./libreliq.so
	@./speed

dist: clean
	mkdir -p ${TARGET}-${VERSION}
	cp -r tests LICENSE Makefile README.md src reliq.1 ${TARGET}-${VERSION}
	tar -c ${TARGET}-${VERSION} | xz -e9 > ${TARGET}-${VERSION}.tar.xz
	rm -rf ${TARGET}-${VERSION}

clean:
	rm -f ${TARGET} lib${TARGET}.so ${OBJ} reliq.h ${TARGET}-${VERSION}.tar.xz

install: all
	mkdir -p ${BINDIR}
	install -m755 ${TARGET} ${BINDIR}
	mkdir -p ${MANDIR}
	sed "s/VERSION/${VERSION}/g" ${TARGET}.1 | bzip2 -9 > ${MANDIR}/${TARGET}.1.bz2
	chmod 644 ${MANDIR}/${TARGET}.1.bz2

uninstall:
	rm -f ${BINDIR}/${TARGET} \
		${MANDIR}/${TARGET}.1.bz2 \
		${LD_LIBRARY_PATH}/lib${TARGET}.so \
		${INCLUDE_PATH}/${TARGET}.h
