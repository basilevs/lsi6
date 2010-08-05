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
        xq=CAM(crate, NAF24(n,A,F_W), &data);
	sleep(1);
    }
    close(crate);
    exit(-2);
    
    help:;
    printf("%s N\n", argv[0]);
    exit(-1);
}
