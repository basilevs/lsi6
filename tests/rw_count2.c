#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <lsi6camac.h>


#define CHANNEL "/dev/lsi6card1channel2"
#define A 0
#define F_W 16
#define F_R 0

int main(int argc, char **argv)
{
    int crate,xq, counter = 0xffff;
    int data;
    int n;
    
    if (argc != 2) 
	goto help;
	
    sscanf(argv[1], "%d", &n);
    if ((n < 1) || (n > 23)) 
	goto help;

    crate=open(CHANNEL,O_RDWR);
    if (crate < 0) {
	fprintf(stderr, "Can open %s\n", CHANNEL);
	exit(-1);
    }

    while(1) {
	data = counter;
	
	lseek(crate, NAF(n, A, F_W), SEEK_SET);
	if (write(crate, &data, 2) != 2) {
	    printf("write error\n");
	    break;
	}
	if (ioctl(crate, CAMAC_STATUS, &xq) != 0) {
	    printf("ioctl error\n");
	    break;
	}
	if (xq & 2) {
	    printf("xq = %x\n", xq);
	    break;
	}


	lseek(crate, NAF(n, A, F_R), SEEK_SET);
	data = 0;
	if (read(crate, &data, 2) != 2) {
	    printf("read error\n");
	    break;
	}
	if (data != counter) {
	    printf("read != write %x %x\n", data, counter);
	    break;
	}
	
	if (ioctl(crate, CAMAC_STATUS, &xq) != 0) {
	    printf("ioctl error\n");
	    break;
	}
	if (xq & 2) {
	    printf("xq = %x\n", xq);
	    break;
	}
	
	counter++;
	counter &= 0xffff;
    }
    close(crate);
    exit(-2);
    
    help:;
    printf("%s N\n", argv[0]);
    exit(-1);
}
