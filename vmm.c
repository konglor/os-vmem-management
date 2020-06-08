/******************************************************************
 * Virtual Memory Management (VMM)
 *
 * a very simple virtual memory management implemention
 * to simulate steps and components involve in translating logical address to physical address.
 *
 * Components:
 * @mmu - Memory Management Unit (engine) - only the mmu will consume interfaces from the other components
 * @physical - Physical Address
 * @pmt - Page Table
 * @backing - Demand Paging
 * @tlb - Table Lookup Buffer
 *
 * TODO:
 * fix data types
 * refactor and clean
 ******************************************************************/
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#define DEBUG 1 // or -DDEBUG flag, set to 0 to not display DEBUG info

typedef enum {
  PAGE_FAULT,
  PAGE_SUCCESS
} PAGE_RESULT;

typedef enum {
  TLB_MISS,
  TLB_HIT
} TLB_RESULT;

typedef struct {
  /* 0x0 */ uint8_t page;
  /* 0x1 */ uint8_t offset;
} virtualmem_t; // 2 bytes

// mmu funcs
static uint32_t mmu_findframe(uint32_t);

// pmt funcs
static void init_pmt(const size_t, const size_t);
static void shutdown_pmt();
static PAGE_RESULT pmt_search(const uint8_t);
static uint32_t pmt_get(const uint8_t);
static size_t pmt_page_n();
static void pmt_insert(const uint8_t, const uint32_t); 

// tlb funcs
static void init_tlb(const size_t);
static void shutdown_tlb();
static TLB_RESULT tlb_search(const uint8_t);
static void tlb_insert(const uint8_t, const uint32_t);
static uint32_t tlb_get(const uint8_t);

// demand paging funcs
static void init_backingstore(const char*);
static void shutdown_backingstore();
static uint8_t* backingstore_get(const uint8_t, size_t);

// physical address
static void init_physical(const size_t, const size_t);
static void shutdown_physical();
static void physical_insert(uint8_t*, size_t);
static uint32_t physical_index();
static uint8_t physical_value(int);

// utils
static void page_offset(virtualmem_t*, uint32_t);
static void display(const char*, const char*, ...);
#define debug(...) display(__func__, __VA_ARGS__)

/******************************************************************
 * Memory Management Unit (MMU)
 *
 * structures:
 * @mmu_initialized - set to true upon initializing mmu
 *
 * functions:
 * @init_mmu - initialize the Virtual Memory Manager
 * @shutdown_mmu - frees resource of all components
 * @mmu_multisearch - calls mmu_search on each given logical address
 * @mmu_translate - find physical frame given a logical addreess and returns it
 ******************************************************************/

// globals
static bool mmu_initialized = false;
static int g_pagefault;
static int g_tlbhit;
static int g_translated;
 
void init_mmu(const char* store, 
        const size_t page_n, const size_t page_size, 
        const size_t frame_n, const size_t frame_size,
        const size_t tlb_size)
{
  debug("memory management unit initialized...\n");
  
  mmu_initialized = true;

  g_pagefault = 0;
  g_tlbhit = 0;
  g_translated = 0;

  init_physical(frame_n, frame_size);
  init_backingstore(store);
  init_pmt(page_n, page_size);
  init_tlb(tlb_size);
}

void shutdown_mmu() {
  assert(mmu_initialized); // this function is exposed in vmm.h

  debug("memory management unit shutting down...\n");
  shutdown_physical();
  shutdown_pmt();
  shutdown_backingstore();
  shutdown_tlb();
  
  float pagefault_perc = ((float)g_pagefault / g_translated) * 100;
  float tlbhit_perc = ((float)g_tlbhit / g_translated) * 100;
  printf("Page Fault: %2.2f%%\n", pagefault_perc);
  printf("TLB HIT: %2.2f%%\n", tlbhit_perc);
}

uint32_t mmu_getphysical(uint32_t logical) {
  assert(mmu_initialized); // this function is exposed in vmm.h

  uint32_t frame, phys_addr;
  virtualmem_t virt_mem;
  
  // increment number of translated addresses
  g_translated++;

  // get the page and offset
  page_offset(&virt_mem, logical);
  
  // find the frame from either backing store, page table, or tlb
  frame = mmu_findframe(logical);
  
  phys_addr = frame + virt_mem.offset;
  return phys_addr;
}

int8_t mmu_getvalue(uint32_t physical) {
  assert(mmu_initialized); // this function is exposed in vmm.h
  return physical_value(physical);
}

// TODO: clean function
static uint32_t mmu_findframe(uint32_t addr) {
  virtualmem_t virt_mem;
  PAGE_RESULT in_pmt;
  TLB_RESULT in_tlb;
  
  // backing store
  uint8_t* phys_mem;

  // return value
  uint32_t frame;
  
  // get page and offset of the virtual addr
  page_offset(&virt_mem, addr);
  
  // look in table lookaside buffer (high speed memory)
  in_tlb = tlb_search(virt_mem.page);
  if (in_tlb == TLB_HIT) {
    debug("0x%-4X TLB HIT!\n", addr);
    g_tlbhit++;

    // no further actions needed
    frame = tlb_get(virt_mem.page);
    return frame;
  }

  // look in the page table
  in_pmt = pmt_search(virt_mem.page);
  switch(in_pmt) {
    case PAGE_SUCCESS: 
    {
      debug("0x%-4X Found in Page Table\n", addr);
      frame = pmt_get(virt_mem.page);

      // dont return yet, need to cache it into TLB
      break;
    };
    case PAGE_FAULT: 
    {
      debug("0x%-4X PAGE FAULT! Searching in Backing Store\n", addr);
      g_pagefault++;
      
      size_t page_n = pmt_page_n();
      
      // get address from backing store (read in a 256-byte page from BACKING_STORE)
      phys_mem = backingstore_get(virt_mem.page, page_n);

      // store it in an available page frame in physical memory
      physical_insert(phys_mem, page_n);
      
      frame = physical_index(); // this

      // insert into page table
      pmt_insert(virt_mem.page, frame);

      free(phys_mem);

      // dont return yet, need to cache it into TLB
      break;
    };
  }

  // insert it into TLB 
  tlb_insert(virt_mem.page, frame);

  return frame;
}

/******************************************************************
 * Physical Addresses / Frame / "Memory"
 *
 * functions:
 * @init_physical - set up the physical memory
 * @shutdown_physical - free resources
 * @physical_insert - insert values into physical memory
 * @physical_index - returns the current frame
 * @physical_value - returns the value
 ******************************************************************/
static size_t FRAME_SIZE;
static size_t FRAME_N;

static uint8_t* g_physical; // physical memory
static uint8_t g_physical_index;

static void init_physical(const size_t entries, const size_t size) {
  debug("physical memories initialized...\n");
  FRAME_SIZE = size;
  FRAME_N = entries;

  g_physical_index = -1;
  g_physical = malloc(size * entries);
  if (g_physical == NULL) {
    debug("allocation failed!\n");
    exit(EXIT_FAILURE);
  }
}

static void shutdown_physical() {
  debug("physical shutting down...\n");
  free(g_physical);
}

static void physical_insert(uint8_t* addr, size_t size) {
  g_physical_index++;
  size_t index = FRAME_N * g_physical_index;
  memcpy(g_physical + index, addr, size);
}

static uint32_t physical_index() {
  uint32_t index = FRAME_N * g_physical_index;
  return index;
}

static uint8_t physical_value(int addr) {
  return g_physical[addr];
}

/******************************************************************
 * Page <Map> Table (PMT) - turning virtual page number -> physical page frame
 *
 * structures:
 * @g_pmt - the page table
 *
 * functions:
 * @init_pmt - initialize the Page Table
 * @shutdown_pmt - shutdown Page Table and release resources
 * @pmt_search - checks in a page is in the Page Table
 * @pmt_get - gets the frame
 ******************************************************************/
static size_t PAGE_SIZE;
// specifications says each entry should be 2^8 bytes (256 bytes) ...
static size_t PAGE_N;

// TODO: a struct for pmt entries (256 bytes with dirty/accessed/... bits)
static int32_t* g_pmt; // the page table

static void init_pmt(const size_t entries, const size_t size) {
  debug("page table initialized...\n");
  int i;
  PAGE_N = entries;
  PAGE_SIZE = size;

  g_pmt = malloc(entries * sizeof(int32_t));
  if (g_pmt == NULL) {
    debug("allocation failed!\n");
    exit(EXIT_FAILURE);
  }
  for (i = 0; i < size; i++) {
    // TODO: check -1 for uints
    g_pmt[i] = -1;
  }
}

static void shutdown_pmt() {
  debug("page table shutting down...\n");
  free(g_pmt);
}

static PAGE_RESULT pmt_search(const uint8_t page) {
  assert(g_pmt != NULL);
  return (g_pmt[page] == -1) ? PAGE_FAULT : PAGE_SUCCESS;
}

static uint32_t pmt_get(const uint8_t page) {
  assert(g_pmt != NULL);
  return g_pmt[page];
}

static void pmt_insert(const uint8_t page, const uint32_t frame) {
  g_pmt[page] = frame;
}

static size_t pmt_page_n() {
  return PAGE_N;
}

/******************************************************************
 * Demand Paging - often very slow, typically resides in the drive itself
 * to emulate slow speed, do not memcpy file into memory
 * 
 * functions:
 * @init_backingstore - io connection to the backing store
 * @shutdown_backingstore - closes connection to backing store
 * @backingstore_get - takes in an page number from a virtual address and finds physical
 ******************************************************************/
static FILE* g_backing;

static void init_backingstore(const char* f) {
  debug("demand paging initialized...\n");
  g_backing = fopen(f, "rb"); // read binary
  if (g_backing == NULL) {
    debug("fopen() failed!\n");
    exit(EXIT_FAILURE);
  }
}

static void shutdown_backingstore() {
  debug("demand paging shutting down...\n");
  fclose(g_backing);
}

static uint8_t* backingstore_get(const uint8_t offset, size_t page_n) {
  assert(g_backing != NULL);
  uint8_t* phys_addr;
  size_t ret;
  size_t location;
  size_t blocks = 1;
  
  phys_addr = (uint8_t*)malloc(sizeof(uint8_t) * page_n);
  if (!phys_addr) {
    debug("allocation failed!\n");
    exit(EXIT_FAILURE);
  }
  location = offset * page_n;
  // for the sake of being slow, no memcpy
  fseek(g_backing, location, SEEK_SET);

  // read 1 block of 256 from backstore
  ret = fread(phys_addr, page_n, blocks, g_backing);
  if (ret != blocks) {
    debug("fread() failed!\n");
    exit(EXIT_FAILURE);
  }
  return phys_addr;
}

/******************************************************************
 * Table Lookaside Buffer (TLB) - high speed cache for finding frame.
 * to emulate high speed, use a hash table
 *
 * functions:
 * @hashCode - computes hash code
 * @init_tlb - sets the size of the tlb
 * @shutdown_tlb - free resources (the tlb)
 * @tlb_insert - inserts K,V pair of page,frame into tlb
 * @tlb_search - check if frame is in tlb
 * @tlb_get - gets the frame from given page
 ******************************************************************/
static size_t TLB_SIZE;

typedef struct {
  uint8_t page;
  uint32_t frame; // TODO: change to 256 bytes as specified?
} tlb_t;

static tlb_t* g_tlb;
static uint8_t g_tlb_index;

static int hashCode(uint8_t key) {
  return key % TLB_SIZE;
}

static void init_tlb(const size_t size) {
  debug("table lookaside buffer initialized...\n");
  int i;
  TLB_SIZE = size;
  g_tlb_index = 0;
  
  g_tlb = malloc(sizeof(tlb_t) * size);
  if (!g_tlb) {
    debug("allocation failed!\n");
    exit(EXIT_FAILURE);
  }
  for (i = 0; i < size; i++) {
    g_tlb[i].page = -1;
    g_tlb[i].frame = -1;
  };
}

static void shutdown_tlb() {
  debug("table lookaside buffer shutting down...\n");
  free(g_tlb);
}

static void tlb_insert(const uint8_t page, const uint32_t frame) {
  int hash = hashCode(page);
  g_tlb[hash].page = page;
  g_tlb[hash].frame = frame;
}

static TLB_RESULT tlb_search(const uint8_t page) {
  int hash = hashCode(page);
  return (g_tlb[hash].page == page) ? TLB_HIT : TLB_MISS;
}

static uint32_t tlb_get(const uint8_t page) {
  int hash = hashCode(page);
  if (g_tlb[hash].page == page)
    return g_tlb[hash].frame;
  return -1;
}

/******************************************************************
 * Utilities
 *
 * functions:
 * @page_offset - extracts page# and offset from a 32 bit virtual address
 * @display - debug print
 ******************************************************************/

static void page_offset(virtualmem_t* mem, uint32_t addr) {
  uint32_t eax;
  uint16_t ax;
  uint8_t ah,al;
  
  eax = addr;
  ax = eax & 0xFFFF;
  ah = ax >> 0x8;
  al = ax & 0xFF;
  mem->page = ah;
  mem->offset = al;
}

static void display(const char* func, const char *fmt, ...) {
#ifndef DEBUG
  return;
#endif
#ifdef linux
  // yellow (33) and bold (1)
  fprintf(stderr, "\033[33;1m");
  fprintf(stderr, "DEBUG (%s): ", func);
  // reset
  fprintf(stderr, "\033[00m");
#else
  fprintf(stderr, "DEBUG (%s): ", func);
#endif
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
}












