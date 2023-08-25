CC ?= cc
CFLAGS ?= -O3
NAME ?= fd
DESTDIR ?= /usr/local/bin

.PHONY: all install

all:
	$(CC) fd.c $(CFLAGS) -o $(NAME)

install:
	mkdir -p $(DESTDIR)
	install $(NAME) -m 755 $(DESTDIR)/$(NAME)
