/**
 * Kongmeng Lor
 * ICS 462 Operating System
 * Professor: Robin Ehrlich
 * Project 4 - Virtual Memory Manager
 * 11/29/2019
 *
 * COMPILE: gcc main.c vmm.c
 * RUN: ./a.out addresses.txt
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <stddef.h>

#include "vmm.h"

// specifications
const size_t PAGE_N = 256; // size of entries in page table
const size_t PAGE_SIZE = 256; // size of the page table
const size_t FRAME_N = 256; // number of frames
const size_t FRAME_SIZE = 256; // size of each frame
const size_t TLB_SIZE = 16;
const size_t LOGICAL_ADDRESS_N = 1000; // number of logical address in file

// 
const char* BACKING_STORE = "BACKING_STORE.bin";

// prototype
void pre_init(uint32_t[], char*, size_t);

// entry point
int main(int argc, char** arg) {
  int i;
  uint32_t vmem_arr[LOGICAL_ADDRESS_N];
  uint32_t logical, physical;
  int8_t value;

  if (argc < 2) {
    printf("Please input a file! Usage: ./a.out <filename>\n");
    return 1;
  }

  // initialize virtual memory from txt file
  pre_init(vmem_arr, arg[1], LOGICAL_ADDRESS_N);

  // initialize memory management unit
  init_mmu(BACKING_STORE, PAGE_N, PAGE_SIZE, FRAME_N, FRAME_SIZE, TLB_SIZE);

  // feed virtual addresses to the memory management unit */
  for (i = 0; i < LOGICAL_ADDRESS_N; i++) {
    logical = vmem_arr[i];
    physical = mmu_getphysical(logical);
    value = mmu_getvalue(physical);
    printf("logical: %-4d \t physical: %-4d \t value: %-4d\n\n", logical, physical, value);
  }

  shutdown_mmu(); // print statistic here
  return (0);
}

inline void pre_init(uint32_t arr[], char* file, size_t size) {
  FILE* fp;
  int i;
  uint32_t eax;

  fp = fopen(file, "r");
  if (fp == NULL) {
    fprintf(stderr, "fopen() failed, errno = %d\n", errno);
    exit(EXIT_FAILURE);
  }
  // load addresses from txt file to memory
  for (i = 0; i < size && !feof(fp); i++) {
    fscanf(fp, "%d", &eax);
    arr[i] = eax;
  }
  fclose(fp);
}

