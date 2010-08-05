#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <lsi6camac.h>

#define CHANNEL "/dev/lsi6card1channel2"

#define MUX_N 7
#define ADC_N 9
#define ADC_T 7
#define ADC_D 1	/* 1 - 0.5V, 0 - 8V */

#define DAC_N 11

int crate;

void set_mux(int channel)
{
	int data;
	int xq;

	data = channel;
	xq = CAM(crate, NAF24(MUX_N, 0, 17), &data);
}
void mux_off(void)
{
	int data = 0x20;
	int xq;

	xq = CAM(crate, NAF24(MUX_N, 0, 17), &data);
}

void set_dac(float v)
{
	int data;
	long k;
	int xq;

	v = v * 1000000 / 15.625;

	if (v > 0) k = v;
	else { k = -v; k |= 0x80000; };

	data = k >> 4;
	xq = CAM(crate, NAF24(DAC_N, 0, 18), &data);

	data = k & 0xf;
	xq = CAM(crate, NAF24(DAC_N, 1, 18), &data);

	data = 0;
	xq = CAM(crate, NAF24(DAC_N, 0, 26), &data);
}

float read_adc(void) /* mv */
{
	int xq;
	int data;
	float v, k;

	data = 0x20 | (ADC_D << 3) | ADC_T;
	xq = CAM(crate, NAF24(ADC_N, 3, 16), &data);

	data = 0;
	xq = CAM(crate, NAF24(ADC_N, 2, 16), &data);

	data = 0;
	xq = CAM(crate, NAF24(ADC_N, 1, 16), &data);

	data = 0;
	xq = CAM(crate, NAF24(ADC_N, 0, 25), &data);

	data =0;
#if 0
	usleep(1000000);
#else
	while(1) {
		xq = CAM(crate, NAF24(ADC_N, 0, 8), &data);
		if (!(xq & 0x1)) break;
	}

#endif
	data = 0;
	xq = CAM(crate, NAF24(ADC_N, 1, 16), &data);

	xq = CAM(crate, NAF24(ADC_N, 0, 0), &data);

	printf("data = %08x, ", data);

	if (data & 0x80000) k = data - 0x100000;
	else k = data;
	v = 2 * k / (1 << (ADC_T + (ADC_D << 2)));

printf("v=%9.4f\n",v);

	return v;
}

int main(int arc, char **argv)
{
	int i;

	crate=open(CHANNEL,O_RDWR);
	if (crate < 0) {
	    fprintf(stderr, "Can open %s\n", CHANNEL);
	    exit(-1);
        }


	set_dac(0.25);

#if 0
	while(1) {
		for(i = 0; i< 8; i++) {
			set_mux(i + 8);
			usleep(10000);
			printf(" %9.4f",read_adc());
			fflush(stdout);
		}
		mux_off();
		printf("\n");
		usleep(1000000);
	}

#endif
	while(1) {
	    usleep(10000);
	    read_adc();
	}



}


