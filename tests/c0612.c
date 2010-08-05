/*
    CAMAC C0612
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <signal.h>

#include <lsi6camac.h>

#define BUS "/dev/lsi6card1channel2"

#define ADC_N 9
#define ADC_T 7
#define ADC_D 1	/* 1 - 0.5V, 0 - 8V */

int main(int argc, char **argv)
{
    int n,fd, data;
    float v, k;

    if (argc != 2) 
	goto help;
	
    sscanf(argv[1], "%d", &n);
    if ((n < 1) || (n > 23)) 
	goto help;


    fd = open(BUS,O_RDWR | O_EXCL);
    if(fd < 0){
       fprintf(stderr, "Can't open " BUS "\n");
       exit(-1);
    }

    data = 0;
    if (ioctl(fd, NAF24(n, 0, 10), &data)) /* clear lam */
	goto xqerror;

    data = 0x20 | (ADC_D << 3) | ADC_T;
    if (ioctl(fd, NAF24(n, 3, 16), &data))
	goto xqerror;

    data = 0;
    if (ioctl(fd, NAF24(n, 2, 16), &data))
	goto xqerror;

    data = 0;
    if (ioctl(fd, NAF24(n, 1, 16), &data))
	goto xqerror;

    
    while(1){
        data = 0;
	/* start adc */
	if (ioctl(fd, NAF24(n, 0, 25), &data))
	    goto xqerror;

	/* wait */
	data =0;
	while(1) {
	    if (!(ioctl(fd, NAF24(n, 0, 8), &data) & 0x1)) break;
	}

	data = 0;
	if (ioctl(fd, NAF24(n, 1, 16), &data))
	    goto xqerror;

	if (ioctl(fd, NAF24(n, 0, 0), &data))
	    goto xqerror;

	printf("ADC code: %05x",data);

	if (data & 0x80000) k = data - 0x100000;
	else k = data;
	v = 2 * k / (1 << (ADC_T + (ADC_D << 2)));

	if (ioctl(fd, NAF24(n, 0, 0), &data)) 
	    break;
	
	printf("  u (mv) = %+.3f\n",v);
    }
    close(fd);
    exit(0);

    help:;
    printf("%s N\n", argv[0]);
    exit(-1);
    
    xqerror:;
    fprintf(stderr,"xq error\n");
    exit(-2);
}

