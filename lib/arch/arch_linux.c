/*
 *                                                        ____  _____
 *                            ________  _________  ____  / __ \/ ___/
 *                           / ___/ _ \/ ___/ __ \/ __ \/ / / /\__ \
 *                          / /  /  __/ /__/ /_/ / / / / /_/ /___/ /
 *                         /_/   \___/\___/\____/_/ /_/\____//____/
 *
 * ======================================================================
 *
 *   title:        Architecture specific code - Linux
 *
 *   project:      ReconOS
 *   author:       Christoph Rüthing, University of Paderborn
 *   description:  Functions needed for ReconOS which are architecure
 *                 specific and are implemented here.
 *
 * ======================================================================
 */

#ifdef RECONOS_OS_linux

#include "arch.h"
#include "../utils.h"

#include "../../linux/driver/include/reconos.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "pthread.h"

#define PROC_CONTROL_DEV "/dev/reconos-proc-control"
#define OSIF_INTC_DEV "/dev/reconos-osif-intc"

unsigned int NUM_HWTS = 0;


/* == OSIF related functions ============================================ */

#define OSIF_FIFO_BASE_ADDR       0x75A00000
#define OSIF_FIFO_MEM_SIZE        0x10
#define OSIF_FIFO_RECV_REG        0
#define OSIF_FIFO_SEND_REG        1
#define OSIF_FIFO_RECV_STATUS_REG 2
#define OSIF_FIFO_SEND_STATUS_REG 3

#define OSIF_FIFO_RECV_STATUS_EMPTY_MASK 0x1 << 31
#define OSIF_FIFO_SEND_STATUS_FULL_MASK  0x1 << 31

#define OSIF_FIFO_RECV_STATUS_FILL_MASK 0xFFFF
#define OSIF_FIFO_SEND_STATUS_REM_MASK  0xFFFF

struct osif_fifo_dev {
	unsigned int index;

	volatile uint32_t *ptr;

	unsigned int fifo_fill;
	unsigned int fifo_rem;
};

int osif_intc_fd, proc_control_fd;
struct osif_fifo_dev *osif_fifo_dev;

int reconos_osif_open(int num) {
	debug("[reconos-osif-%d] "
	      "opening ...\n", num);

	if (num < 0 || num >= NUM_HWTS)
		return -1;
	else
		return num;
}

static inline unsigned int osif_fifo_hw2sw_fill(struct osif_fifo_dev *dev) {
	uint32_t reg;

	reg = dev->ptr[OSIF_FIFO_RECV_STATUS_REG];
	if (reg & OSIF_FIFO_RECV_STATUS_EMPTY_MASK)
		return 0;
	else
		return (reg & OSIF_FIFO_RECV_STATUS_EMPTY_MASK) + 1;
}

static inline unsigned int osif_fifo_sw2hw_rem(struct osif_fifo_dev *dev) {
	uint32_t reg;

	reg = dev->ptr[OSIF_FIFO_SEND_STATUS_REG];

	if (reg & OSIF_FIFO_SEND_STATUS_FULL_MASK)
		return 0;
	else
		return (reg & OSIF_FIFO_SEND_STATUS_REM_MASK) + 1;
}

uint32_t reconos_osif_read(int fd) {
	struct osif_fifo_dev *dev = &osif_fifo_dev[fd];
	uint32_t data;

	if (dev->fifo_fill == 0) {
		debug("[reconos-osif-%d] "
		      "reading, waiting for data ...\n", fd);

		dev->fifo_fill = osif_fifo_hw2sw_fill(dev);

		while (dev->fifo_fill == 0) {
			ioctl(osif_intc_fd, RECONOS_OSIF_INTC_WAIT, &fd);
			dev->fifo_fill = osif_fifo_hw2sw_fill(dev);
		}
	}

	data = dev->ptr[OSIF_FIFO_RECV_REG];
	dev->fifo_fill--;

	debug("[reconos-osif-%d] "
	      "reading finished 0x%x\n", fd, data);


	return data;
}

void reconos_osif_write(int fd, uint32_t data) {
	struct osif_fifo_dev *dev = &osif_fifo_dev[fd];

	debug("[reconos-osif-%d] "
	      "writing 0x%x ...\n", fd, data);

	// do busy waiting here
	do {
		dev->fifo_rem = osif_fifo_sw2hw_rem(dev);
	} while (dev->fifo_rem == 0);

	dev->ptr[OSIF_FIFO_SEND_REG] = data;

	debug("[reconos-osif-%d] "
	      "writing finished\n", fd, data);

}

void reconos_osif_close(int fd) {
	debug("[reconos-osif-%d] "
	      "closing ...\n", fd);
}


/* == Proc control related functions ==================================== */

int reconos_proc_control_open() {
	return proc_control_fd;
}

int reconos_proc_control_get_num_hwts(int fd) {
	int data;

	ioctl(fd, RECONOS_PROC_CONTROL_GET_NUM_HWTS, &data);

	return data;
}

int reconos_proc_control_get_tlb_hits(int fd) {
	int data;

	ioctl(fd, RECONOS_PROC_CONTROL_GET_TLB_HITS, &data);

	return data;
}

int reconos_proc_control_get_tlb_misses(int fd) {
	int data;

	ioctl(fd, RECONOS_PROC_CONTROL_GET_TLB_MISSES, &data);

	return data;
}

uint32_t reconos_proc_control_get_fault_addr(int fd) {
	uint32_t data;

	ioctl(fd, RECONOS_PROC_CONTROL_GET_FAULT_ADDR, &data);

	return data;
}

void reconos_proc_control_clear_page_fault(int fd) {
	ioctl(fd, RECONOS_PROC_CONTROL_CLEAR_PAGE_FAULT, NULL);
}

void reconos_proc_control_set_pgd(int fd) {
	ioctl(fd, RECONOS_PROC_CONTROL_SET_PGD_ADDR, NULL);
}

void reconos_proc_control_sys_reset(int fd) {
	ioctl(fd, RECONOS_PROC_CONTROL_SYS_RESET, NULL);
}

void reconos_proc_control_hwt_reset(int fd, int num, int reset) {
	if (reset)
		ioctl(fd, RECONOS_PROC_CONTROL_SET_HWT_RESET, &num);
	else
		ioctl(fd, RECONOS_PROC_CONTROL_CLEAR_HWT_RESET, &num);
}

void reconos_proc_control_cache_flush(int fd) {
#ifdef	RECONOS_ARCH_microblaze
	ioctl(fd, RECONOS_PROC_CONTROL_CACHE_FLUSH, NULL);
#endif
}

void reconos_proc_control_close(int fd) {
	close(fd);
}


/* == Reconfiguration related functions ================================= */

#ifdef RECONOS_ARCH_zynq

int is_configured = 0;
pthread_mutex_t mutex;

inline void init_xdevcfg() {
	if (!is_configured) {
		pthread_mutex_init(&mutex, NULL);
		is_configured = 1;
	}
}

int load_partial_bitstream(uint32_t *bitstream, unsigned int bitstream_length) {
	int fd;
	char d = '1';

	init_xdevcfg();

	//printf("... Programming FPGA with partial bitstream\n");
	//printf("... Bitstream has size of %d bytes and begins with 0x%x 0x%x 0x%x\n", bitstream_length * 4, bitstream[0], bitstream[1], bitstream[2]);

	pthread_mutex_lock(&mutex);

	fd = open("/sys/class/xdevcfg/xdevcfg/device/is_partial_bitstream", O_WRONLY);
	if (!fd) {
		printf("[xdevcfg lib] failed to open\n");
		exit(EXIT_FAILURE);
	}
	write(fd, &d, 1);
	close(fd);

	fd = open("/dev/xdevcfg", O_WRONLY);
	if (!fd) {
		printf("[xdevcfg lib] failed to open\n");
		exit(EXIT_FAILURE);
	}
	write(fd, bitstream, bitstream_length * 4);
	close(fd);

	fd = open("/sys/class/xdevcfg/xdevcfg/device/prog_done", O_RDONLY);
	if (!fd) {
		printf("[xdevcfg lib] failed to open\n");
		exit(EXIT_FAILURE);
	}
	do {
		read(fd, &d, 1);
		//printf("... Waiting for programming to finish, currently reading %c\n", d);
	} while(d != '1');
	close(fd);

	pthread_mutex_unlock(&mutex);

	return 0;
}

#elif RECONOS_ARCH_microblaze

int load_partial_bitstream(uint32_t *bitstream, unsigned int bitstream_length) {
	panic("NOT IMPLEMENTED YET\n");

	return 0;
}

#endif


/* == Initialization function =========================================== */

void reconos_drv_init() {
	int i;
	int fd;
	char *mem;


	// opening proc control device
	fd = open(PROC_CONTROL_DEV, O_RDWR);
	if (fd < 0)
		panic("[reconos-core] "
		      "error while opening proc control\n");
	else
		proc_control_fd = fd;


	// opening osif intc device
	fd = open(OSIF_INTC_DEV, O_RDWR);
	if (fd < 0)
		panic("[reconos-core] "
		      "error while opening osif intc\n");
	else
		osif_intc_fd = fd;


	// reset entire system
	reconos_proc_control_sys_reset(proc_control_fd);


	// get number of hardware threads
	NUM_HWTS = reconos_proc_control_get_num_hwts(proc_control_fd);


	// create mapping for osif
	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0)
		panic("[reconos-osif] "
		      "failed to open /dev/mem\n");

	mem = (char *)mmap(0, 0x10000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, OSIF_FIFO_BASE_ADDR);
	if (mem == MAP_FAILED)
		panic("[reconos-osif] "
		      "failed to mmap osif memory\n");


	// allocate and initialize osif devices
	osif_fifo_dev = (struct osif_fifo_dev*)malloc(NUM_HWTS * sizeof(struct osif_fifo_dev));
	if (!osif_fifo_dev)
		panic("[reconos-osif] "
		      "failed to allocate memory\n");

	for (i = 0; i < NUM_HWTS; i++) {
		osif_fifo_dev[i].index = i;
		osif_fifo_dev[i].ptr = (uint32_t *)(mem + i * OSIF_FIFO_MEM_SIZE);
		osif_fifo_dev[i].fifo_fill = 0;
		osif_fifo_dev[i].fifo_rem = 0;
	}
}

#endif
