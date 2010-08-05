/*
 * ppi2camac.h: part of the ppi2camac driver
 * 
 * Copyright (C) 1999 Alexei Nikiforov, BINP, Novosibirsk
 * 
 * Email: A.A.Nikiforov@inp.nsk.su
 *
 * $Id: ppi2camac.h,v 1.4 2001/12/24 03:42:38 camac Exp $
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.

 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef _LINUX_CAMAC_LSI6_H
#define _LINUX_CAMAC_LSI6_H

#ifndef __KERNEL__
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#define NAF(n,a,f) (((f&0x1f)<<16) | ((n&0x1f)<<4) | (a&0xf))
#define NAF24(n,a,f) (((f&0x1f)<<16) | ((n&0x1f)<<4) | (a&0xf) | CAMAC_24)

#define N_NAF(naf) (((naf) >> 4 ) & 0x1f)
#define A_NAF(naf) ((naf) & 0xf)
#define F_NAF(naf) (((naf) >> 16 ) & 0x1f)

#ifdef __KERNEL__
#define IS_N(naf) ((naf) & 0x1f0)
#endif 

#define CAMAC_TEST_CRATE 	3
#define CAMAC_STATUS 		4	/*  read X and Q flags */
/*
#define CAMAC_C			5
#define CAMAC_Z			6
*/
#define CAMAC_NON_DATA		7
/* #define CAMAC_LWAIT		8 */
#define CAMAC_ION		9
#define CAMAC_IOFF		10
#define CAMAC_24		0x8000
#define CAMAC_READ_LM		NAF(0,1,0)
#define CAMAC_WRITE_LM		NAF(0,1,16)
#define CAMAC_READ_HB		NAF(0,2,0)
#define CAMAC_WRITE_HB		NAF(0,2,16)
#define CAMAC_READ_CONTROL	NAF(0,0,0)
#define CAMAC_WRITE_CONTROL	NAF(0,0,16)


#define CAMAC_NLAM		27
#define CAMAC_LWAIT(lgroup) NAF(CAMAC_NLAM,0,lgroup)


#ifndef __KERNEL__

static __inline__ int CAM(int fd, int naf, unsigned * p){
	return ioctl(fd, naf, p);
}

static __inline__ int CAMW(int fd, int naf, unsigned p){
	return ioctl(fd, naf, &p);
}


static __inline__ int CAM24(int fd, int naf, unsigned long * p){
	return ioctl(fd, naf | CAMAC_24, p);
}

static __inline__ int CAM24W(int fd, int naf, unsigned long p){
	return ioctl(fd, naf | CAMAC_24, &p);
}

static __inline__ int CAM_READ( int fd, int naf, unsigned * buf, int num){
	int txq, res;
	lseek(fd, naf, SEEK_SET);
	if( (res=read(fd, (char*)buf, 4*num)) < 0) return res;
	if( (res=ioctl(fd, CAMAC_STATUS, &txq)) < 0) return res;
	return txq;
}

static __inline__ int CAM_WRITE( int fd, int naf, unsigned * buf, int num){
	int txq, res;
	lseek(fd, naf, SEEK_SET);
	if( (res=write(fd, (char*)buf, 4*num)) < 0) return res;
	if( (res=ioctl(fd, CAMAC_STATUS, &txq)) < 0) return res;
	return txq;
}

static __inline__ int CAM_C(int fd){
	unsigned data=0x140;
	return ioctl(fd, CAMAC_WRITE_CONTROL, &data);
}

static __inline__ int CAM_Z(int fd){
	unsigned data=0x240;
	return ioctl(fd, CAMAC_WRITE_CONTROL, &data);
}

static char l2g[24]={
	0,0,0,0,
	1,1,1,1,
	2,2,2,
	3,3,3,
	4,4,4,
	5,5,5,
	6,6,6,
	7
};

static __inline__ int CAM_L2G(int l){ 
	return l2g[l-1];
}

static __inline__ int CAM_LWAIT(int fd, int l, int timeout){
	return ioctl(fd, CAMAC_LWAIT(CAM_L2G(l)), timeout);
}

static __inline__ int CAM_LWAITG(int fd, int g, int timeout){
	return ioctl(fd, CAMAC_LWAIT(g), timeout);
}

static __inline__ int CAM_ION(int fd){
	return ioctl(fd, CAMAC_ION,NULL);
}

static __inline__ int CAM_IOFF(int fd){
	return ioctl(fd, CAMAC_IOFF,NULL);
}
	
static __inline__ int CAM_I(int fd){
	int temp;
	if( ioctl(fd, CAMAC_READ_CONTROL, &temp) < 0)
		return -1;
	return (temp & 0x1000)?1:0;
}
#endif /* __KERNEL__ */
#endif /* _LINUX_CAMAC_LSI6_H */
