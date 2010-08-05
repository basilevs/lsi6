/*
 * camt.c: the test program for ppi2camac driver
 * 
 * Copyright (C) 1999 Alexei Nikiforov, BINP, Novosibirsk
 * 
 * Email: A.A.Nikiforov@inp.nsk.su
 *
 * $Id: camt.c,v 1.1.1.1 2000/01/19 04:24:07 camac Exp $
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.

 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include <fcntl.h>
#include<errno.h>
#include<sys/time.h>
#include <sys/ioctl.h>
#include<signal.h>
#include<ctype.h>
#include<readline/readline.h>
#include<readline/history.h>
#include"lsi6camac.h"

int gethex(char **p, int * data)
{
	int i;
	char * pp = *p;
	
	i = strtol( *p, &pp, 16);
	if( *p == pp) return  0;
	*p = pp;
	*data = i;
	return 1;
}

int getint(char **p, int * data)
{
	int i;
	char * pp = *p;
	
	i = strtol( *p, &pp, 0);
	if( *p == pp) return  0;
	*p = pp;
	*data = i;
	return 1;
}

int getname(char ** p, char *s)
{
	int count=0;
	char * in;

	for( in = *p; *in; in++ ) 
		if( ! isspace(*in)) break;
	while( *in ){
		if( isspace(*in)) break;
		*s++ = *in++;
		count++;
	}
	*s = 0;
	*p = in;
	return count;
}

void do_help(){
	printf("\no filename - open device file\n"
	       "s[n] - type/select opened device file descriptor \n"
	       "C - Clear, Z - Zero \n"
	       "n[i], a[i], f[i] - type/set n, a, f\n"
	       "d[i] - type/enter data value \n"
	       "D[val [val ...]] - type/enter buffer data values \n"
	       "F - fill buffer with 0,1,2,....\n"
	       "b[size] - type/enter buffer size in words\n"
	       "h[0|1] - disable/enable high byte\n"
	       "c[count] - carry out CAM  ... \n"
	       "r[count] -    ...    CAM_READ to buffer bs words\n"
	       "w[count] -    ...    CAM_WRITE from buffer bs words\n"
	       "x - type xq or res on the last camac cycle\n"
	       "t - time report\n"
	       "g[i] - type/set group request number\n"
	       "G[i] - type/set LAM request number\n"
	       "T - timeout for lwait\n"
	       "L[count] -    ...    lwait(g,timeout) ... \n"
	       "l[count] -    ...    lseek(fd, NAF(n,a,f) count times\n"
	       "R[count] -    ...    read to buffer bs bytes\n"
	       "W[count] -    ...    write from buffer bs bytes\n"
	       "i[count] -    ...    ioctl  ... \n"
	       "I1 - inhibit on, I0 - inhibit off\n"
	       "I  - type state of the inhibit line\n"
	       );
}


#define TEST( command ) \
i=1;\
getint(&p,&i);\
num = i;\
gettimeofday(&start,NULL);\
while(i--) { command ;}\
gettimeofday(&end,NULL);\
dt=end.tv_sec-start.tv_sec+(end.tv_usec-start.tv_usec)/1000000.;\

char name[100];

unsigned * buf;

void sigint(int sig){
	printf("<Ctrl-C> was pressed. Use q to exit\n");
	signal(SIGINT, sigint);

}

int i, num;
int crate=0, n=0, a=0, f=0, g=0, data=0, G=0;
int is24=0, cnt=2, bs=1024;
unsigned l24=0;
struct timeval start, end;
double dt;
int crate;
int T=-1;
int xq;

int loop(char * line)
{
	char * p = line;
	int i;
	
	while( *p ){
		switch(*p++){

		case 'C': xq = CAM_C(crate); break;
		case 'Z': xq = CAM_Z(crate); break;			
		case 'o': getname(&p,name);
			printf("fd=%d\n",crate=open(name,O_RDWR)); 
			if( crate < 0) perror("");
			break;
		case 'e': /* close crate */ 
			printf("close(%d)=%d\n", crate, close(crate)); 
			break;
		case 's': /* set fd number */ 
			getint(&p,&crate)?:printf("fd=%d\n",crate); 
			break;
		case 'd': /* data */ 
			getint(&p,  &data)?:printf("data=%#x\n",data); 
			break;
		case 'f': 
			getint(&p,&f)?:printf("f=%d\n",f);
			break;
		case 'n': 
			getint(&p,&n)?:printf("n=%d\n",n); 
			break;
		case 'a': 
			getint(&p,&a)?:printf("a=%d\n",a); 
			break;
		case 'g': getint(&p,&g)?:printf("g=%d\n",g); 
			break;
		case 'G': getint(&p,&G);g=CAM_L2G(G);printf("G=%d, g=%d\n",G,g); 
			break;
		case 'T': 
			getint(&p,&T)?:printf("timeout=%d\n",T); 
			break;

/* lseek, ioctl, read, write, lwait, CAM*/
		case 'l': 
			TEST( xq=lseek(crate, l24 | NAF(n,a,f), 0)); 
			break;
		case 'i': 
			TEST( xq=ioctl(crate, l24 | NAF(n,a,f), &data) );
			break;
		case 'r': 
			TEST( xq=CAM_READ(crate, l24|NAF(n,a,f), buf, bs));
			break;
		case 'w': 
			TEST( xq=CAM_WRITE(crate,l24|NAF(n,a,f), buf, bs));
			break;
		case 'b':
			if( !getint(&p,&i)){
				printf("bs=%d\n",bs);
				break;
			}
			if( i > bs){
				unsigned * p = calloc(i, sizeof(unsigned));
				if( !p ){
					xq = -1;
					errno = ENOMEM;
					break;
				}
				memcpy( p, buf, bs*sizeof(unsigned));
				buf = p;
			}
			bs = i;
			break;
		case 'D':
			for( i=0; i<1024; i++)
				if( !getint(&p,buf+i) ) break;
			if( i==0 ) {
				for( ; i<bs; i++)
					printf("%#x ", buf[i]);
				printf("\n");
			}
			break;
		case 'F':
			for( i=0; i < bs; i++)
				buf[i] = i;
			break;
		case 'R': 
			TEST( xq=read(crate, buf, bs));
			break;
		case 'W': 
			TEST( xq=write(crate, buf, bs)); 
			break;
		case 'L': 
			TEST( xq = ioctl(crate, CAMAC_LWAIT(g), T) );
			break;
		case 'c':
			TEST( xq=CAM(crate, l24 | NAF(n,a,f), &data));
			break;
		case 'x':
			printf("xq=%d\n",xq); 
			break;
			/* time report */
		case 't': 
			printf("=> %d times in %f sec,\n %f op/sec, %f usec/loop\n", 
			       num, dt, num/dt, 1000000.*dt/num);
			break;
			/* high byte using */
		case 'h': 
			if(getint(&p,&is24)) 
				if(is24) { 
					cnt=3;
					l24=CAMAC_24;
				}
				else {
					cnt=2;
					l24=0;
				}
			else printf("h%d\n",is24);
			break;
		case 'I':
			i=0;
			if(getint(&p,&i)) i?CAM_ION(crate):CAM_IOFF(crate);
			else printf("%d\n",CAM_I(crate)); 
			break;
			/* help */
		case 'q': return 0;
		case '?': do_help();
		}
		if( xq < 0){
			fprintf(stderr,"errno=%d\n",errno);
			perror("");
			xq = 0;
		}
	}
	return 1;
}

int
main(){
	char * line = NULL;
	int i;

	printf(" camt from ppi2camac 0.3.1a\n");

	buf = calloc( bs, sizeof(unsigned));
	if( !buf){
		fprintf(stderr, "no memory\n");
		exit(1);
	}
	for(i=0;i<bs;i++) 
		buf[i]=i;

	signal(SIGINT, sigint);
	using_history();
	while(1){
		line = readline(">");
		if( ! line ) break;
		if( *line ){
			HIST_ENTRY * he;
			he = history_get( history_length);
			if( !he || strcmp( line, he -> line))
				add_history(line);
		}
		if( ! loop( line)) break;
		free( line);
	}
	return 0;
}
