CC=gcc
#CFLAGS=-std=c99 -O0 -g -Wall -Wextra -pedantic -Wpadded -Wno-gnu-empty-initializer -DDEBUG
CFLAGS=-std=c99 -Ofast -Wall -Wextra -pedantic -Wpadded -Wno-gnu-empty-initializer -DNDEBUG
LIBS=-lm

.PHONY: all

all: cv_intersect intersect

cv_intersect: cv_intersect.c
	indent -linux -l120 -i4 -nut $<
	$(CC) -o $@ $< $(CFLAGS) $(LIBS)

intersect: intersect.c
	indent -linux -l120 -i4 -nut $<
	$(CC) -o $@ $< $(CFLAGS) $(LIBS)

.PHONY: test

test: intersect cv_intersect
	time ./intersect H.dat L.dat
	@echo ""
	time ./cv_intersect H.dat L.dat
	@echo ""

.PHONY: clean

clean:
	rm -f cv_intersect intersect

