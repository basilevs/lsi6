/*
    CAMAC C0609 (ADC-20) test
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

int main(int argc, char **argv)
{
    int x,u;
    int n,fd, data;

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
    if (ioctl(fd, NAF(n, 0, 10), &data)) /* clear lam */
	goto xqerror;

    data = 6; /* T = 6 (320ms), 8V */
    if (ioctl(fd, NAF(n, 0, 16), &data))
	goto xqerror;
    
    while(1){
	data = 0;
	if (ioctl(fd, NAF(n, 0, 26), &data))	/* start adc */
	    break;
	
	data = -1;
	if (ioctl(fd, CAMAC_LWAIT(CAM_L2G(n)), &data)) /* wait lam */
	    break;
	
	if (ioctl(fd, NAF(n, 0, 0), &x)) 
	    break;
	
	printf("ADC code: %x",x);
	u = 0x2400 - (x >> 6);
	printf("  u (mv) = %d\n",u);
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

