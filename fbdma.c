#include <errno.h>
#include "fbdma.h"

///////////////////////////
//Dreamcast PVR DMA registers
struct pvr_dma_fast_reg_layout {
	int state;
	int len;
	int dst;
};
struct pvr_dma_mode_reg_layout {
	int mode0, mode1;
};
struct pvr_dma_flex_reg_layout {
	int pvr_addr;
	int sh_addr;
	int byte_count;
	int direction;
	int mode;	//write 0
	int unk0;	//write 1
	int trigger;	//write 1 to start
};

#define PVR_DMA_MAGIC (0x6702007F)

#define PVR_DMA_FAST (*(volatile struct pvr_dma_fast_reg_layout*)0xA05F6800)
#define PVR_DMA_FAST_MODE (*(volatile struct pvr_dma_mode_reg_layout*)0xA05F6884)
#define PVR_DMA_FLEX (*(volatile struct pvr_dma_flex_reg_layout*)0xA05F7C00)
#define PVR_DMA_FLEX_UNK (*(volatile int*)0xA05F7C80)	/* write 0x6702007F */

///////////////////////////
//SuperH-4 DMA controller registers

#define SH_P4_DMAC		(0xFFA00000)
#define SH_DMA_CHAN_CNT	4
struct SH_DMA_CHAN {
	int sar, dar, dmatcr, chcr;
};
struct SH_DMA_LAYOUT {
	struct SH_DMA_CHAN ch[SH_DMA_CHAN_CNT];
	int dmaor;
};

#define SH_DMAC_CHCR_SSA_WIDTH	(3)		/* Source address space attribute specification */
#define SH_DMAC_CHCR_SSA_SHIFT	(29)
#define SH_DMAC_CHCR_STC	(1<<28)		/* Source address wait control select */
#define SH_DMAC_CHCR_DSA_WIDTH	(3)		/* Destination address space attribute specification */
#define SH_DMAC_CHCR_DSA_SHIFT	(25)
#define SH_DMAC_CHCR_DTC	(1<<24)		/* Destination address wait control select */
#define SH_DMAC_CHCR_DS		(1<<19)		/* !DREQ select */
#define SH_DMAC_CHCR_RL		(1<<18)		/* Request check level */
#define SH_DMAC_CHCR_AM		(1<<17)		/* Acknowledge mode */
#define SH_DMAC_CHCR_DM_WIDTH	(2)		/* Destination address mode (0 = fixed, 1 = inc, 2 = dec) */
#define SH_DMAC_CHCR_DM_SHIFT	(14)
#define SH_DMAC_CHCR_SM_WIDTH	(2)		/* Source address mode (0 = fixed, 1 = inc, 2 = dec) */
#define SH_DMAC_CHCR_SM_SHIFT	(12)
#define SH_DMAC_CHCR_RS_WIDTH	(4)		/* Resource select */
#define SH_DMAC_CHCR_RS_SHIFT	(8)
#define SH_DMAC_CHCR_TM		(1<<7)		/* Transmit mode (not set = cycle steal, set = burst) */
#define SH_DMAC_CHCR_TS_WIDTH	(3)		/* Transmit size */
#define SH_DMAC_CHCR_TS_SHIFT	(4)
#define SH_DMAC_CHCR_IE		(1<<2)		/* Interrupt enable */
#define SH_DMAC_CHCR_TE		(1<<1)		/* Transfer complete */
#define SH_DMAC_CHCR_DE		(1<<0)		/* DMAC enable */

#define SH_DMAC_CHCR_DM_FIXED		(0)
#define SH_DMAC_CHCR_DM_INC		(1)
#define SH_DMAC_CHCR_DM_DEC		(2)

#define SH_DMAC_CHCR_SM_FIXED		(0)
#define SH_DMAC_CHCR_SM_INC		(1)
#define SH_DMAC_CHCR_SM_DEC		(2)

#define SH_DMAC_CHCR_RS_EXTAS_TO_EXTAS	(0)
#define SH_DMAC_CHCR_RS_EXTAS_TO_EXTDEV	(2)
#define SH_DMAC_CHCR_RS_EXTDEV_TO_EXTAS	(3)

#define SH_DMAC_CHCR_TS_8BYTE		(0)
#define SH_DMAC_CHCR_TS_1BYTE		(1)
#define SH_DMAC_CHCR_TS_2BYTE		(2)
#define SH_DMAC_CHCR_TS_4BYTE		(3)
#define SH_DMAC_CHCR_TS_32BYTE		(4)

#define SH_DMAC_DMAOR_DDT	(1<<15)	/* On-demand data transfer enable */
#define SH_DMAC_DMAOR_PR_WIDTH	(2)	/* Priority mode */
#define SH_DMAC_DMAOR_PR_SHIFT	(8)
#define SH_DMAC_DMAOR_AE	(1<<2)	/* Address error */
#define SH_DMAC_DMAOR_NMIF	(1<<1)	/* NMI flag */
#define SH_DMAC_DMAOR_DME	(1<<0)	/* DMAC master enable */

#define SH_DMAC_DMAOR_PR_0123	(0)
#define SH_DMAC_DMAOR_PR_0231	(1)
#define SH_DMAC_DMAOR_PR_2013	(2)
#define SH_DMAC_DMAOR_PR_ROBIN	(3)

#define SH_DMA (*(volatile struct SH_DMA_LAYOUT *)(SH_P4_DMAC))


///////////////////////
//DMA driver state
typedef struct {
	semaphore_t done;
	int blocking;
	pvr_dma_callback_t callback;
	ptr_t cbdata;
	volatile int busy;
	int init;
} fbdma_status_t;

fbdma_status_t fbdma_state = {SEM_INITIALIZER(1), 0, NULL, 0, 0, 0};

#define unlikely(x)	__builtin_expect(!!(x), 0)
int fbdma_transfer(const void * src, uint32 dest, uint32 count,  int block, pvr_dma_callback_t callback, ptr_t cbdata) {
	uint32 src_addr = (uint32)src;
	
	//~ printf("src  : %p\n", src);
	//~ printf("dest : %08x\n", (unsigned)dest);
	//~ printf("count: %i\n", (int)count);
	
	if (unlikely(!fbdma_state.init)) {
		dbglog(DBG_ERROR, "fbdma: dma not initialized\n");
		errno = EPERM;
		return -1;
	}
	
	if (unlikely(fbdma_state.busy)) {
		dbglog(DBG_ERROR, "fbdma: dma already in progress\n");
		errno = EINPROGRESS;
		return -1;
	}
	
	if(unlikely((src_addr | dest | count) & 0x1F)) {
		dbglog(DBG_WARNING, "fbdma: src or dest is not 32-byte aligned, or count is not multiple of 32\n");
		errno = EFAULT;
		return -1;
	}
	
	if(unlikely((unsigned)src_addr < 0x0c000000)) {
		dbglog(DBG_ERROR, "fbdma: src address < 0x0c000000\n");
		errno = EFAULT;
		return -1;
	}
	
	dest = (dest & 0xFFFFFF) | 0x11000000;
	
	fbdma_state.busy = 1;
	
	/* This dummy read is important!
	   
	   The TE bit is set after a successful transfer, but we cannot start 
	   a new DMA while it's set. We also can't clear the bit without first
	   reading CHCR. So we read the register so that we can clear it when we
	   set CHCR.
	   
	   We can't rely on the DMA finish IRQ to read CHCR and make the TE bit
	   clearable. When debugging, if we exit to dcload while a DMA is going off,
	   CHCR will be left unread, and the next time we try to start a DMA it
	   will not actually start. So we ALWAYS read before starting.
	 */
	(void)SH_DMA.ch[2].chcr;
	
	SH_DMA.ch[2].sar = src_addr;
	SH_DMA.ch[2].dmatcr = count / 32;
	SH_DMA.ch[2].chcr = (SH_DMAC_CHCR_SM_INC << SH_DMAC_CHCR_SM_SHIFT) //increasing src address
		| SH_DMAC_CHCR_RS_EXTAS_TO_EXTDEV << SH_DMAC_CHCR_RS_SHIFT //mem to device
		| SH_DMAC_CHCR_TM //burst transmit mode
		| SH_DMAC_CHCR_TS_32BYTE << SH_DMAC_CHCR_TS_SHIFT //32 byte units
		| SH_DMAC_CHCR_DE; //dma enable
	
	if(unlikely((SH_DMA.dmaor & 0x8007) != 0x8001)) {
		fbdma_state.busy = 0;
		dbglog(DBG_ERROR, "fbdma: Failed DMAOR check\n");
		errno = EIO;
		return -1;
	}
	
	fbdma_state.blocking = block;
	fbdma_state.callback = callback;
	fbdma_state.cbdata = cbdata;
	
	PVR_DMA_FAST_MODE.mode0 = 1;	//Write to 32-bit format memory instead of 64-bit texture format memory
	PVR_DMA_FAST.state = dest;
	PVR_DMA_FAST.len = count;
	
	sem_destroy(&fbdma_state.done);
	sem_init(&fbdma_state.done, 0);
	
	PVR_DMA_FAST.dst = 1;
	
	if(block)
		sem_wait(&fbdma_state.done);
	
	return 0;
}

static inline void CacheWriteback(uint32_t addr) {
	__asm__ __volatile__("ocbwb @%0" : : "r" (addr) : "memory");
}
static inline void CachePurge(uint32_t addr) {
	__asm__ __volatile__("ocbp @%0" : : "r" (addr) : "memory");
}
static inline void CacheInvalidate(uint32_t addr) {
	__asm__ __volatile__("ocbi @%0" : : "r" (addr) : "memory");
}


#define CACHE_SIZE	(16*1024)
static char CacheFlushArea[CACHE_SIZE] __attribute__ ((aligned (32)));

typedef enum {
	PCT_PURGE_ALL,
	PCT_PURGE_OCI0,
	PCT_PURGE_OCI1,
} pctPurgeType;
static inline void sh4Invalidate(const void *addr) {
	__asm__ __volatile__("ocbi @%0" : : "r" (addr) : "memory");
}
static inline void sh4Purge(const void *addr) {
	__asm__ __volatile__("ocbp @%0" : : "r" (addr) : "memory");
}
static inline void sh4CachelineAllocate(void *addr, int value) {
	register int __value  __asm__("r0") = value;
	__asm__ __volatile__ (
		"movca.l r0,@%0\n\t"
		:  
		: "r" (addr), "r" (__value)
		: "memory"  );
}
static void pctPurgeCache(pctPurgeType banks) {
	volatile unsigned int *CCR = (void*)0xff00001c;
	unsigned int ccrval = *CCR;
	unsigned int size = CACHE_SIZE;
	//Purges the cache by using MOVCA to flush any existing dirty cacheline, then
	//invalidates cacheline to clear dirty bit so MOVCA garbage can't trigger
	//a writeback later
	
	//Treat any invalid value as request to flush entire cache
	if (banks > PCT_PURGE_OCI1)
		banks = PCT_PURGE_ALL;
	
	//Check OCINDEX
	if (ccrval & (1<<7)) {
		//Each bank is half size of full cache
		size >>= 1;
	} else {
		//If not using OCINDEX, ignore banks parameter, only purge cache once
		banks = PCT_PURGE_OCI0;
	}
	
	//Check OCRAM
	if (ccrval & (1<<5)) {
		//Usable cache size is halved
		size >>= 1;
	}
	//~ printf("size %i ", size);
	if (banks != PCT_PURGE_OCI1) {
		void *ptr = CacheFlushArea;
		for(size_t i = 0; i < size; i += 32) {
			sh4CachelineAllocate(ptr + i, 0);
			sh4Invalidate(ptr + i);
		}
	}
	
	if (banks != PCT_PURGE_OCI0) {
		void *ptr = (CacheFlushArea);
		for(size_t i = 0; i < size; i += 32) {
			sh4CachelineAllocate(ptr + i, 0);
			sh4Invalidate(ptr + i);
		}
	}
}

int fbdma_flip(const void * src) {
	//Convert pixel mode to pixel size
	// RGB555   0 -> 2
	// RGB565   1 -> 2
	// RGB888   2 -> 3
	// ARGB8888 3 -> 4
	unsigned int pixelsize = vid_mode->pm;
	pixelsize += pixelsize == 0;
	pixelsize++;
	
	//Size of framebuffer in bytes
	size_t fbsize = vid_mode->width * vid_mode->height * pixelsize;
	
	//If OCI is used, purge only the correct side of the cache
	//If OCI is not used, pctPurgeCache will purge the entire cache regardless of parameter
	pctPurgeCache((uintptr_t)src & (1<<25) ? PCT_PURGE_OCI1 : PCT_PURGE_OCI0);
	
	int retval = fbdma_transfer(src, (uint32_t)vram_l, fbsize, FBDMA_NONBLOCK, 0, 0);
	if (retval == 0) {
		//Don't wait for DMA to complete before flipping, flip now and let DMA race the beam
		vid_flip(-1);
	}
	return retval;
}


void fbdma_fast_irq_hnd(uint32 code) {
	(void)code;
	
	fbdma_state.busy = 0;
	
	if (!(SH_DMA.ch[2].chcr & SH_DMAC_CHCR_TE) || SH_DMA.ch[2].dmatcr)
		dbglog(DBG_INFO, "fbdma_fast_irq_hnd: The dma did not complete successfully\n");

	// Call the callback, if any.
	if(fbdma_state.callback) {
		// This song and dance is necessary because the handler
		// could chain to itself.
		pvr_dma_callback_t cb = fbdma_state.callback;
		ptr_t d = fbdma_state.cbdata;

		fbdma_state.callback = NULL;
		fbdma_state.cbdata = 0;

		cb(d);
	}

	// Signal the calling thread to continue, if any.
	sem_signal(&fbdma_state.done);
	thd_schedule(1, 0);
	fbdma_state.blocking = 0;
}

void fbdma_init(void) {
	sem_init(&fbdma_state.done, 1);
	fbdma_state.blocking = 0;
	fbdma_state.callback = NULL;
	fbdma_state.cbdata = 0;
	fbdma_state.busy = 0;
	fbdma_state.init = 1;
		
	asic_evt_set_handler(ASIC_EVT_PVR_DMA, fbdma_fast_irq_hnd);
	asic_evt_enable(ASIC_EVT_PVR_DMA, ASIC_IRQ_DEFAULT);
}

void fbdma_shutdown(void) {
	if (unlikely(!fbdma_state.init)) {
		dbglog(DBG_ERROR, "fbdma: dma not initialized\n");
		return;
	}
	
	fbdma_wait();
	
	PVR_DMA_FAST.dst = 0;
	SH_DMA.ch[2].chcr = 0;
	fbdma_state.busy = 0;
	fbdma_state.init = 0;
	
	/* Clean up */
	asic_evt_disable(ASIC_EVT_PVR_DMA, ASIC_IRQ_DEFAULT);
	asic_evt_set_handler(ASIC_EVT_PVR_DMA, NULL);

	sem_destroy(&fbdma_state.done);
}

int fbdma_busy(void) {
	if (unlikely(!fbdma_state.init)) {
		dbglog(DBG_ERROR, "fbdma: dma not initialized\n");
		return 0;
	}
	
	return fbdma_state.busy;
}

void fbdma_wait(void) {
	if (unlikely(!fbdma_state.init)) {
		dbglog(DBG_ERROR, "fbdma: dma not initialized\n");
		return;
	}
	
	//No frame buffer should take more than 10 milliseconds to DMA, unless something is very wrong
	if (unlikely(sem_wait_timed(&fbdma_state.done, 10))) {
		if (errno == ETIMEDOUT)
			dbglog(DBG_ERROR, "fbdma: dma taking too long\n");
		else
			dbglog(DBG_ERROR, "fbdma: error waiting for dma to complete\n");
	}
}

