#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <lsi6camac.h>


#define CHANNEL "/dev/lsi6card1channel1"
#define A 0
#define F_W 16
#define F_R 0

int main(int argc, char **argv)
{
    int crate,xq, counter = 0xffffff;
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
        xq=CAM(crate, NAF24(n,A,F_W), &data);
	if (xq & 2) {
	    fprintf(stderr, "Test failed1, xq = %d\n", xq);
	    break;
	}
        xq=CAM(crate, NAF24(n,A,F_R), &data);
	if (xq & 2) {
	    fprintf(stderr, "Test failed2, xq = %d\n", xq);
	    break;
	}
	if (counter != data) {
	    fprintf(stderr, "Test failed, %x != %x\n", data, counter);
	    break;
	}
	counter++;
	counter &= 0xffffff;
    }
    close(crate);
    exit(-2);
    
    help:;
    printf("%s N\n", argv[0]);
    exit(-1);
}
