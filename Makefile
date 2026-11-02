CC      ?= gcc
CFLAGS  ?= -O2 -g
CFLAGS  += -Wall -Wextra -Werror -pipe -fsigned-char -fno-strict-aliasing -D_GNU_SOURCE
PREFIX  ?= /usr/local
SRCS    := $(wildcard src/*.c)
OBJS    := $(SRCS:src/%.c=build/%.o)

all: build/vtop

build/vtop: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -MMD -c -o $@ $<

install: build/vtop
	install -D -m 755 build/vtop $(DESTDIR)$(PREFIX)/bin/vtop

clean:
	rm -rf build

-include $(OBJS:.o=.d)
.PHONY: all install clean
