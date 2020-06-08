#ifndef __VMM_H__
#define __VMM_H__
// virtual memory manager interface

#include <stddef.h>
#include <stdint.h>

/** initialize the memory management unit */
void init_mmu(const char*, const size_t, const size_t, const size_t, const size_t, const size_t);

/** shuts down the mmu engine */
void shutdown_mmu(); 

/** gets the physical address given a logical address */
uint32_t mmu_getphysical(uint32_t);

/** gets the value provided by a physical address */
int8_t mmu_getvalue(uint32_t);

#endif
