PROGS =	vgps

all:	$(PROGS)

vgps:	vgps.o
	$(CC) $(CFLAGS) -o vgps vgps.o $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(PROGS) $(TEMPFILES) *.o

format:
	clang-format -i vgps.c