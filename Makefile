CC=gcc
#CFLAGS=-std=c99 -O0 -g -Wall -Wextra -pedantic -Wpadded -Wno-gnu-empty-initializer -DDEBUG
CFLAGS=-std=c99 -Ofast -Wall -Wextra -pedantic -Wpadded -Wno-gnu-empty-initializer -DNDEBUG
LIBS=-lm
INDENT_OPTS=-nbad -bap -nbc -bbo -hnl -br -brs -c33 -cd33 -ncdb -ce -ci4 -cli0 -d0 -di1 -nfc1 -i4 -ip0 -l120 -lp -npcs -nprs -npsl -sai -saf -saw -ncs -nsc -sob -nfca -cp33 -ss -ts8 -il1


.PHONY: all

all: cv_intersect intersect intersect_thr

cv_intersect: cv_intersect.c
<<<<<<< HEAD
	indent $(INDENT_OPTS) -nut $<
	$(CC) -o $@ $< $(CFLAGS) $(LIBS)

intersect: intersect.c
	indent $(INDENT_OPTS) -nut $<
	$(CC) -o $@ $< $(CFLAGS) $(LIBS)

intersect_thr: intersect.c
	indent $(INDENT_OPTS) -nut $<
=======
#		indent -linux -l120 -i4 -nut $<
	$(CC) -o $@ $< $(CFLAGS) $(LIBS)

intersect: intersect.c
#	indent -linux -l120 -i4 -nut $<
	$(CC) -o $@ $< $(CFLAGS) $(LIBS)

intersect_thr: intersect.c
#	indent -linux -l120 -i4 -nut $<
>>>>>>> Write output to buf and dump in 1 go
	$(CC) -o $@ $< $(CFLAGS) -DTHREADS -DNUM_THREADS=4 $(LIBS) -pthread

.PHONY: test

test: intersect intersect_thr cv_intersect
	time ./intersect H.dat L.dat
	@echo ""
	time ./intersect_thr H.dat L.dat
	@echo ""
	time ./cv_intersect H.dat L.dat
	@echo ""

.PHONY: clean

clean:
	rm -f cv_intersect intersect intersect_thr

