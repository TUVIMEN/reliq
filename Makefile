VERSION = 1.5
CC = gcc -std=c99
CFLAGS = -O3 -march=native -Wall -Wextra -DVERSION=\"${VERSION}\"
LDFLAGS =
TARGET := hgrep

O_PHPTAGS := 0 # support for <?php ?>
O_AUTOCLOSING := 1 # support for autoclosing tags
O_LIB := 0 # compile libhgrep
O_LINKED := 0 # link hgrep to libhgrep

ifeq ($(strip ${O_PHPTAGS}),1)
	CFLAGS += -DPHPTAGS
endif
ifeq ($(strip ${O_AUTOCLOSING}),1)
	CFLAGS += -DAUTOCLOSING
endif

PREFIX ?= /usr
MANPREFIX ?= ${PREFIX}/share/man
BINDIR = ${DESTDIR}${PREFIX}/bin
MANDIR = $(DESTDIR)${MANPREFIX}/man1
LD_LIBRARY_PATH ?= ${PREFIX}/lib
INCLUDE_PATH ?= ${PREFIX}/include

SRC = src/main.c src/flexarr.c src/hgrep.c src/ctype.c
LIB_SRC = src/flexarr.c src/hgrep.c src/ctype.c
ifeq ($(strip ${O_LIB}),1)
	SRC = ${LIB_SRC}
	LDFLAGS = -shared
	CFLAGS += -fPIC
endif

ifeq ($(strip ${O_LINKED}),1)
	CFLAGS += -DLINKED
	SRC = src/flexarr.c src/main.c
	LDFLAGS = -lhgrep
endif

OBJ = ${SRC:.c=.o}

all: options hgrep

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
	install -m644 src/hgrep.h ${INCLUDE_PATH}

hgrep: ${OBJ}
	${CC} ${CFLAGS} ${LDFLAGS} $^ -o ${TARGET}
	strip --discard-all ${TARGET}

%.o: %.c
	${CC} ${CFLAGS} -c $< -o $@

test: clean all
	@./test.sh test.csv test.html
	@[ ${O_PHPTAGS} -eq 1 ] && ./test.sh test-php.csv test.php || true

dist: clean
	mkdir -p ${TARGET}-${VERSION}
	cp -r LICENSE Makefile README.md src hgrep.1 ${TARGET}-${VERSION}
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
