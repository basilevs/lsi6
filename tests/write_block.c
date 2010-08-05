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

int buffer[1024];

int main(int argc, char **argv)
{
    int crate,xq;
    int n,i;
    
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

    for(i = 0; i < 1024; i++) {
	buffer[i] = 0xaaaaaa;
    }

    while(1) {
	lseek(crate, NAF(n, A, F_W), SEEK_SET);
	if ((i = write(crate, buffer, 1024*4)) != 1024*4) {
	    printf("write error %d\n", i);
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

    }
    close(crate);
    exit(-2);
    
    help:;
    printf("%s N\n", argv[0]);
    exit(-1);
}
