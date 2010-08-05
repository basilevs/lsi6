
all: naf indicator c0609 c0612 catch_lam rw_count2 rw_count3 write_block read_block

CFLAGS= -I/usr/include -I../ -O2 -Wall
HEADERS = ../camac.h

indicator: indicator.c
	$(CC) $(CFLAGS) $< -o $@

smi: smi.c
	$(CC) $(CFLAGS) $< -o $@

naf: naf.c
	$(CC) $(CFLAGS) $< -o $@


c0609: c0609.c
	$(CC) $(CFLAGS) $< -o $@

c0612: c0612.c
	$(CC) $(CFLAGS) $< -o $@

catch_lam: catch_lam.c
	$(CC) $(CFLAGS) $< -o $@

test_intr: test_intr.c
	$(CC) $(CFLAGS) $< -o $@

rw_count2: rw_count2.c
	$(CC) $(CFLAGS) $< -o $@

rw_count3: rw_count3.c
	$(CC) $(CFLAGS) $< -o $@

write_block: write_block.c
	$(CC) $(CFLAGS) $< -o $@

read_block: read_block.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f *.o
