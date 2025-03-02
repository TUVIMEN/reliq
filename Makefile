VERSION = 2.9
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
O_EDITING := 1 #support for editing

D := 0 # debug mode

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

LIB_SRC = src/flexarr.c src/sink.c src/html.c src/hnode.c src/reliq.c src/hnode_print.c src/ctype.c src/utils.c src/output.c src/decode_entities.c src/pattern.c src/range.c src/exprs_comp.c src/exprs_exec.c src/format.c src/npattern_comp.c src/npattern_exec.c src/node_exec.c

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

ifeq ($(strip ${D}),1)
	CFLAGS_D += -DDEBUG -O0 -ggdb -fsanitize=address
endif

TEST_FLAGS=$(shell echo "${CFLAGS_D}" | sed -E 's/(^| )-D/\1/g; s/(^| )[a-zA-Z0-9_]+=("[^"]*")?[^ ]*( |$$)//')
CFLAGS_ALL = ${CFLAGS} ${CFLAGS_D}

OBJ = ${SRC:.c=.o}

.PHONY: all options lib lib-install install linked test test-advanced test-errors test-all test-update test-speed install-pc dist uninstall clean reliq-h

all: options reliq

options:
	@echo ${SRC}
	@echo ${TARGET} build options:
	@echo "CFLAGS   = ${CFLAGS_ALL}"
	@echo "LDFLAGS  = $(strip ${LDFLAGS_R})"
	@echo "CC       = ${CC}"

reliq-h:
	@echo ${CFLAGS_D} | cat - src/reliq.h | sed '1{ s/ /\n/g; s/-D/#define /g; s/=/ /g; h; d; }; /^\/\/#RELIQ_COMPILE_FLAGS/{ s/.*//;G;D; }' > reliq.h


lib: clean reliq-h
	@make D=${D} O_LIB=1 TARGET=lib${TARGET}.so

install-pc:
	sed "s/#VERSION#/${VERSION}/g;s|#PREFIX#|${PREFIX}|g;s/#CFLAGS_D#/${CFLAGS_D}/g" reliq.pc > $$(pkg-config --variable pc_path pkg-config | cut -d: -f1)/reliq.pc

lib-install: lib install-pc
	install -m755 lib${TARGET}.so ${LD_LIBRARY_PATH}
	install -m644 reliq.h ${INCLUDE_PATH}

linked: lib lib-install
	@make D=${D} O_LINKED=1

reliq: ${OBJ}
	${CC} ${CFLAGS_ALL} $^ ${LDFLAGS_R} -o ${TARGET}
	[ "${D}" -eq 0 ] && strip ${STRIP_ARGS} ${TARGET} || true

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

test-speed: lib
	@gcc -O3 testspeed.c -o testspeed ./libreliq.so
	@./testspeed

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
	rm -f ${BINDIR}/${TARGET} \
		${MANDIR}/${TARGET}.1.bz2 \
		${LD_LIBRARY_PATH}/lib${TARGET}.so \
		${INCLUDE_PATH}/${TARGET}.h
