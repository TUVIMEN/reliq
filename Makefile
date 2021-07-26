SHELL	=	/bin/bash
CC	=	gcc -std=c11
LINK	=
CFLAGS	=	-O2 -march=native -Wall -Wextra
TARGET	=	hgrep

MANDIR  =       /usr/share/man/man1
BINDIR  =       /usr/bin

OBJECTS = $(patsubst %.c, %.o, $(wildcard src/*.c))

all: $(OBJECTS)
	$(CC) $(LINK) $(CFLAGS) $^ -o $(TARGET)
	strip --discard-all $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

install:
	cp -f ${TARGET} ${BINDIR}
	chmod 755 ${BINDIR}/${TARGET}
	cp -f ${TARGET}.1 ${MANDIR}
	chmod 644 ${MANDIR}/${TARGET}.1


uninstall:
	rm /usr/bin/${TARGET}

clean:
	find . -name "*.o" -exec rm "{}" \;
	rm $(TARGET)
