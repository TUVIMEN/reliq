VERSION = 2.3
CC = gcc -std=c99
CFLAGS = -O3 -march=native -Wall -Wextra -Wno-implicit-fallthrough -DVERSION=\"${VERSION}\"
LDFLAGS =
TARGET := reliq

O_PHPTAGS := 1 # support for <?php ?>
O_AUTOCLOSING := 1 # support for autoclosing tags, without it some tests will fail (as intended)
O_EDITING := 1 #support for editing
O_LIB := 0 # compile libreliq
O_LINKED := 0 # link reliq to libreliq

PREFIX ?= /usr
MANPREFIX ?= ${PREFIX}/share/man
BINDIR = ${DESTDIR}${PREFIX}/bin
MANDIR = $(DESTDIR)${MANPREFIX}/man1
LD_LIBRARY_PATH ?= ${PREFIX}/lib
INCLUDE_PATH ?= ${PREFIX}/include

SRC = src/main.c src/flexarr.c src/html.c src/reliq.c src/ctype.c src/utils.c src/output.c
LIB_SRC = src/flexarr.c src/html.c src/reliq.c src/ctype.c src/utils.c src/output.c

ifeq ($(strip ${O_PHPTAGS}),1)
	CFLAGS += -DRELIQ_PHPTAGS
endif

ifeq ($(strip ${O_AUTOCLOSING}),1)
	CFLAGS += -DRELIQ_AUTOCLOSING
endif

ifeq ($(strip ${O_EDITING}),1)
	SRC += src/edit.c
	LIB_SRC += src/edit.c
	CFLAGS += -DRELIQ_EDITING
endif

ifeq ($(strip ${O_LIB}),1)
	SRC = ${LIB_SRC}
	LDFLAGS = -shared
	CFLAGS += -fPIC
endif

ifeq ($(strip ${O_LINKED}),1)
	CFLAGS += -DLINKED
	SRC = src/flexarr.c src/main.c
	LDFLAGS += -lreliq
endif

OBJ = ${SRC:.c=.o}

all: options reliq

options:
	@echo ${SRC}
	@echo ${TARGET} build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

linked: lib lib-install
	@make O_LINKED=1

lib: clean
	@make O_LIB=1 TARGET=lib${TARGET}.so

lib-install: lib
	install -m755 lib${TARGET}.so ${LD_LIBRARY_PATH}
	install -m644 src/reliq.h ${INCLUDE_PATH}

reliq: ${OBJ}
	${CC} ${CFLAGS} ${LDFLAGS} $^ -o ${TARGET}
	strip ${TARGET}

%.o: %.c
	${CC} ${CFLAGS} -c $< -o $@

test: clean all
	@./test.sh test/1.csv test/1.html
	@[ ${O_PHPTAGS} -eq 1 ] && ./test.sh test/php.csv test/php.php || true
	@[ ${O_EDITING} -eq 1 ] && ./test.sh test/editing.csv test/editing.html || true
	@[ ${O_EDITING} -eq 1 ] && ./test.sh test/editing-output.csv test/editing-output.html || true
	@./test.sh test/output.csv test/output.html || true

test-update: test
	@./test.sh test/1.csv test/1.html update
	@[ ${O_PHPTAGS} -eq 1 ] && ./test.sh test/php.csv test/php.php update || true
	@[ ${O_EDITING} -eq 1 ] && ./test.sh test/editing.csv test/editing.html update || true
	@[ ${O_EDITING} -eq 1 ] && ./test.sh test/editing-output.csv test/editing-output.html update || true
	@./test.sh test/output.csv test/output.html update || true

dist: clean
	mkdir -p ${TARGET}-${VERSION}
	cp -r LICENSE Makefile README.md src reliq.1 ${TARGET}-${VERSION}
	tar -c ${TARGET}-${VERSION} | xz -e9 > ${TARGET}-${VERSION}.tar.xz
	rm -rf ${TARGET}-${VERSION}

clean:
	rm -f ${TARGET} lib${TARGET}.so ${OBJ} ${TARGET}-${VERSION}.tar.xz

install: all
	mkdir -p ${BINDIR}
	install -m755 ${TARGET} ${BINDIR}
	mkdir -p ${MANDIR}
	sed "s/VERSION/${VERSION}/g" ${TARGET}.1 | bzip2 -9 > ${MANDIR}/${TARGET}.1.bz2
	chmod 644 ${MANDIR}/${TARGET}.1.bz2

uninstall:
	rm -f ${BINDIR}/${TARGET}\
		${MANDIR}/${TARGET}.1.bz2
