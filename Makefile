VERSION = 2.7
CC = gcc -std=c18
CFLAGS = -O3 -march=native -Wall -Wextra -Wno-implicit-fallthrough -Wpedantic
LDFLAGS =
TARGET = reliq

O_SMALL_STACK := 0 # limits for small stack
O_PHPTAGS := 1 # support for <?php ?>
O_AUTOCLOSING := 1 # support for autoclosing tags (tag ommission https://html.spec.whatwg.org/multipage/syntax.html#optional-tags)
O_EDITING := 1 #support for editing

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

LIB_SRC = src/flexarr.c src/sink.c src/html.c src/reliq.c src/hnode_print.c src/ctype.c src/utils.c src/output.c src/htmlescapecodes.c src/pattern.c src/range.c src/npattern.c src/exprs_comp.c src/exprs_exec.c src/format.c

ifeq ("$(shell uname -s)","Darwin")
	STRIP_ARGS += -x
endif

ifeq ($(strip ${O_SMALL_STACK}),1)
	CFLAGS_D += -DRELIQ_SMALL_STACK
endif

ifeq ($(strip ${O_PHPTAGS}),1)
	CFLAGS_D += -DRELIQ_PHPTAGS
endif

ifeq ($(strip ${O_AUTOCLOSING}),1)
	CFLAGS_D += -DRELIQ_AUTOCLOSING
endif

ifeq ($(strip ${O_EDITING}),1)
	LIB_SRC += src/edit.c src/edit_sed.c src/edit_wc.c src/edit_tr.c
	CFLAGS_D += -DRELIQ_EDITING
endif

SRC = src/main.c ${LIB_SRC}

ifeq ($(strip ${O_LIB}),1)
	SRC = ${LIB_SRC}
	LDFLAGS_R += -shared
	CFLAGS_D += -fPIC
endif

ifeq ($(strip ${O_LINKED}),1)
	CFLAGS_D += -DLINKED
	SRC = src/main.c
	LDFLAGS_R += -lreliq
endif

TEST_FLAGS=$(shell echo "${CFLAGS_D}" | sed -E 's/(^| )-D/\1/g; s/(^| )[a-zA-Z0-9_]+=("[^"]*")?[^ ]*( |$$)//')
CFLAGS_ALL = ${CFLAGS} ${CFLAGS_D}

OBJ = ${SRC:.c=.o}

.PHONY: all options reliq lib lib-install install linked test test-advanced test-errors test-all test-update dist uninstall

all: options reliq

options:
	@echo ${SRC}
	@echo ${TARGET} build options:
	@echo "CFLAGS   = ${CFLAGS_ALL}"
	@echo "LDFLAGS  = $(strip ${LDFLAGS_R})"
	@echo "CC       = ${CC}"

reliq-h:
	@echo ${CFLAGS_D} | cat - src/reliq.h | sed '1{ s/ /\n/g; s/-D/#define /g; s/=/ /; h; d; }; /^\/\/#RELIQ_COMPILE_FLAGS/{ s/.*//;G;D; }' > reliq.h


lib: clean reliq-h
	@make O_LIB=1 TARGET=lib${TARGET}.so

install-pc:
	sed "s/#VERSION#/${VERSION}/g;s|#PREFIX#|${PREFIX}|g;s/#CFLAGS_D#/${CFLAGS_D}/g" reliq.pc > $$(pkg-config --variable pc_path pkg-config | cut -d: -f1)/reliq.pc

lib-install: lib install-pc
	install -m755 lib${TARGET}.so ${LD_LIBRARY_PATH}
	install -m644 reliq.h ${INCLUDE_PATH}

linked: lib lib-install
	@make O_LINKED=1

reliq: ${OBJ}
	${CC} ${CFLAGS_ALL} $^ ${LDFLAGS_R} -o ${TARGET}
	strip ${STRIP_ARGS} ${TARGET}

%.o: %.c
	${CC} ${CFLAGS_ALL} -c $< -o $@

test: all
	@./test.sh test/basic.test . "${TEST_FLAGS}" || true

test-advanced: all
	@./test.sh test/advanced.test . "${TEST_FLAGS}" || true

test-errors: all
	@./test.sh test/errors.test . "${TEST_FLAGS}" || true

test-all: all
	@./test.sh test/all.test . "${TEST_FLAGS}" || true

test-update: test
	@./test.sh test/all.test update "${TEST_FLAGS}" || true

dist: clean
	mkdir -p ${TARGET}-${VERSION}
	cp -r test LICENSE Makefile README.md src reliq.1 ${TARGET}-${VERSION}
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
	rm -f ${BINDIR}/${TARGET}\
		${MANDIR}/${TARGET}.1.bz2
