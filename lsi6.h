#define LSI6_WINDOW_SIZE 	0x100000
#define LSI6_VENDOR_ID		0x1172
#define LSI6_DEVICE_ID		0x3333
//#define LSI6_MAJOR 		122
#define LSI6_NUMCHANNELS	6
#define LSI6_NUMCARDS		4
#define LSI6_DEVNAME		"lsi6card%dchannel%d"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,2,18)
typedef struct wait_queue *wait_queue_head_t;
#endif

typedef struct {
    spinlock_t lock;
} lsi6_channel;

typedef struct {
    long pciaddr;
    char *base;
    int irq;
    int card;
    int major;
    lsi6_channel channels[LSI6_NUMCHANNELS];
    unsigned short CSR[LSI6_NUMCHANNELS];
    wait_queue_head_t LWQ[LSI6_NUMCHANNELS][K0607_LGROUPS];
    int LWQ_flags[LSI6_NUMCHANNELS][K0607_LGROUPS];
} lsi6_dev_t;

typedef struct {
#define LSI6_STATUS_AT		0x01
#define LSI6_STATUS_AE		0x02
#define LSI6_STATUS_AQ		0x04
#define LSI6_STATUS_AX		0x08
#define LSI6_STATUS_LT		0x10
#define LSI6_STATUS_LE		0x20
#define LSI6_STATUS_LQ		0x40
#define LSI6_STATUS_LX		0x80
    unsigned int status;
    unsigned int addr;
    unsigned int data_wout_init;
    unsigned int data_with_init;
} channel_regs_t;

typedef struct {
    unsigned int busy;
    unsigned int intr;
    unsigned int intr_enable;
    unsigned int intr_global;
    unsigned int exist;
} lsi6_regs_t;
