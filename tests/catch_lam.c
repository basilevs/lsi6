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
    int n,fd, data, i;

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

    while(1){
	ioctl(fd, NAF(n, 0, 25), &data);  /* LAM enable */
	data = 1000;
//	data = -1;
	i = ioctl(fd, CAMAC_LWAIT(CAM_L2G(n)), data); /* wait lam */
	if (i == -1) 
	    printf("timeout\n");
	else
	    printf("LAM detected, time left %d\n", i);
	
	ioctl(fd, NAF(n, 0, 10), &data); /* LAM disable */
    }
    
    close(fd);
    exit(0);

    help:;
    printf("%s N\n", argv[0]);
    exit(-1);
}

