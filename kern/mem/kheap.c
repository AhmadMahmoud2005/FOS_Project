#include "kheap.h"

#include "../conc/kspinlock.h"
#include <inc/dynamic_allocator.h>
#include <inc/memlayout.h>
#include <kern/conc/sleeplock.h>
#include <kern/mem/memory_manager.h>
#include <kern/proc/user_environment.h>


#define KERNEL_HEAP_MAX2 0xA0000000
#define PAGE_END KERNEL_HEAP_MAX2
#define NUM_FRAMES ((KERNEL_HEAP_MAX2 - KERNEL_HEAP_START) / PAGE_SIZE)
//==================================================================================//
//============================== GIVEN FUNCTIONS
//===================================//
//==================================================================================//

//==============================================
// [1] INITIALIZE KERNEL HEAP:
//==============================================
// TODO: [PROJECT'25.GM#2] KERNEL HEAP - #0 kheap_init [GIVEN]
// Remember to initialize locks (if any)
void kheap_init() {
  //==================================================================================
  // DON'T CHANGE THESE
  // LINES==========================================================
  //==================================================================================
  {
    initialize_dynamic_allocator(KERNEL_HEAP_START,
                                 KERNEL_HEAP_START + DYN_ALLOC_MAX_SIZE);
    set_kheap_strategy(KHP_PLACE_CUSTOMFIT);
    kheapPageAllocStart = dynAllocEnd + PAGE_SIZE;
    kheapPageAllocBreak = kheapPageAllocStart;
  }
  //==================================================================================
  //==================================================================================
}

//==============================================
// [2] GET A PAGE FROM THE KERNEL FOR DA:
//==============================================
int get_page(void *va) {
  int ret = alloc_page(ptr_page_directory, ROUNDDOWN((uint32)va, PAGE_SIZE),
                       PERM_WRITEABLE, 1);
  if (ret < 0)
    panic("get_page() in kern: failed to allocate page from the kernel");
  return 0;
}

//==============================================
// [3] RETURN A PAGE FROM THE DA TO KERNEL:
//==============================================
void return_page(void *va) {
  unmap_frame(ptr_page_directory, ROUNDDOWN((uint32)va, PAGE_SIZE));
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS
//==================================//
//==================================================================================//
//===================================
// [1] ALLOCATE SPACE IN KERNEL HEAP:
//===================================
static enum { KHEAP_PAGE_FREE = 0, KHEAP_PAGE_ALLOCATED = 1 };

static unsigned char metadata_initialized = 0;
static unsigned char page_state[NUM_OF_KHEAP_PAGES];
static unsigned int page_alloc_sizes[NUM_OF_KHEAP_PAGES];
void *kmalloc(unsigned int size) {

  // SINGLE GLOBAL HEAP LOCK
  static struct kspinlock kheap_lock;
  static int lock_initialized = 0;

  unsigned int aligned_size, required_pages, heap_capacity;
  unsigned int used_pages, run_idx, idx, start, len;
  unsigned int best_start, best_len, base_va;
  unsigned int mapped_pages, i, current_va, mapped_va;
  int extend_break;
  unsigned char *zero_ptr;
  unsigned int zero_bytes;
  void *result;

  if (size == 0)
      return NULL;

  if (!lock_initialized) {
    init_kspinlock(&kheap_lock, "kheap");
    lock_initialized = 1;
  }

  result = NULL;
  zero_ptr = NULL;
  zero_bytes = 0;

  acquire_kspinlock(&kheap_lock);

  /* ---------- 1) Dynamic-block allocator ---------- */
  if (size <= DYN_ALLOC_MAX_BLOCK_SIZE) {
    result = alloc_block(size);
    release_kspinlock(&kheap_lock);
    return result;
  }

  /* ---------- 2) Page allocator ---------- */
  if (!metadata_initialized) {
    for (i = 0; i < NUM_OF_KHEAP_PAGES; ++i) {
      page_state[i] = KHEAP_PAGE_FREE;
      page_alloc_sizes[i] = 0;
    }
    metadata_initialized = 1;
  }

  if (kheapPageAllocStart == 0)
    panic("kmalloc(): kernel heap is not initialized");

  aligned_size = ROUNDUP(size, PAGE_SIZE);
  required_pages = aligned_size >> PGSHIFT;
  heap_capacity = (KERNEL_HEAP_MAX - kheapPageAllocStart) >> PGSHIFT;

  if (required_pages == 0 || required_pages > heap_capacity) {
    release_kspinlock(&kheap_lock);
    return NULL;
  }

  used_pages = 0;
  if (kheapPageAllocBreak > kheapPageAllocStart)
    used_pages = (kheapPageAllocBreak - kheapPageAllocStart) >> PGSHIFT;

  /* Find free run */
  run_idx = used_pages;
  idx = 0;
  while (idx < used_pages && run_idx == used_pages) {
    while (idx < used_pages && page_state[idx] != KHEAP_PAGE_FREE)
      idx++;
    if (idx >= used_pages) break;

    start = idx;
    len = 0;
    while (idx < used_pages && page_state[idx] == KHEAP_PAGE_FREE) {
      len++;
      idx++;
    }
    if (len == required_pages) {
      run_idx = start;
      break;
    }
  }

  /* Best fit if exact run not found */
  if (run_idx == used_pages) {
    best_start = used_pages;
    best_len = 0;
    idx = 0;
    while (idx < used_pages) {
      while (idx < used_pages && page_state[idx] != KHEAP_PAGE_FREE)
        idx++;
      if (idx >= used_pages) break;

      start = idx;
      len = 0;
      while (idx < used_pages && page_state[idx] == KHEAP_PAGE_FREE) {
        len++;
        idx++;
      }
      if (len >= required_pages && len > best_len) {
        best_len = len;
        best_start = start;
      }
  }
  if (best_len >= required_pages)
    run_idx = best_start;
}

  /* Extend break if needed */
  extend_break = 0;
  if (run_idx == used_pages) {
    if (used_pages + required_pages > heap_capacity) {
      release_kspinlock(&kheap_lock);
      return NULL;
    }
    run_idx = used_pages;
    extend_break = 1;
  }

  /* Map pages */
  base_va = kheapPageAllocStart + (run_idx << PGSHIFT);
  mapped_pages = 0;
  while (mapped_pages < required_pages) {
    struct FrameInfo *frame = NULL;
    current_va = base_va + (mapped_pages << PGSHIFT);
    if (allocate_frame(&frame) != 0)
      break;
    if (map_frame(ptr_page_directory, frame, current_va, PERM_WRITEABLE) != 0) {
      free_frame(frame);
      break;
    }
    mapped_pages++;
  }

  if (mapped_pages < required_pages) {
    while (mapped_pages > 0) {
      mapped_pages--;
      mapped_va = base_va + (mapped_pages << PGSHIFT);
      unmap_frame(ptr_page_directory, mapped_va);
    }
    release_kspinlock(&kheap_lock);
    return NULL;
  }

  /* Mark allocated pages */
  for (i = 0; i < required_pages; ++i)
    page_state[run_idx + i] = KHEAP_PAGE_ALLOCATED;

  /* Record allocation size */
  page_alloc_sizes[run_idx] = required_pages;
  for (i = 1; i < required_pages; ++i)
    page_alloc_sizes[run_idx + i] = 0;

  if (extend_break)
    kheapPageAllocBreak += (required_pages << PGSHIFT);

  result = (void *)base_va;
  zero_ptr = (unsigned char *)base_va;
  zero_bytes = aligned_size;

  release_kspinlock(&kheap_lock);

  /* Zero memory */
  if (zero_ptr) {
    while (zero_bytes--) {
      *zero_ptr++ = 0;
    }
  }

  return result;
}

/* ======================== KFREE ======================== */
void kfree(void *virtual_address) {
  static struct kspinlock kheap_lock;
  static int lock_initialized = 0;

  uint32 va = (uint32)virtual_address;
  uint32 page_idx, required_pages;
  uint32 cur_va, i;
  uint32 *ptr_table;
  struct FrameInfo *frame_info;

  if (virtual_address == NULL)
    return;

  if (!lock_initialized) {
    init_kspinlock(&kheap_lock, "kheap");
    lock_initialized = 1;
  }

  acquire_kspinlock(&kheap_lock);

  /* Block allocator */
  if (va >= KERNEL_HEAP_START && va < KERNEL_HEAP_START + DYN_ALLOC_MAX_SIZE) {
    uint32 size = get_block_size(virtual_address);
    if (size == 0)
      panic("kfree: block allocator returned size 0");
    free_block(virtual_address);
    release_kspinlock(&kheap_lock);
    return;
  }

  /* Page allocator */
  if (va < kheapPageAllocStart || va >= KERNEL_HEAP_MAX) {
    release_kspinlock(&kheap_lock);
    panic("kfree: invalid VA %p", virtual_address);
  }

  /* Must be page-aligned */
  if (va & (PAGE_SIZE - 1)) {
    release_kspinlock(&kheap_lock);
    panic("kfree: VA %p not page-aligned", virtual_address);
  }

  /* Get page index */
  page_idx = (va - kheapPageAllocStart) >> PGSHIFT;

  if (page_idx >= NUM_OF_KHEAP_PAGES) {
    release_kspinlock(&kheap_lock);
    panic("kfree: bad page_idx");
  }

  required_pages = page_alloc_sizes[page_idx];
  if (required_pages == 0) {
    release_kspinlock(&kheap_lock);
    panic("kfree: no metadata for VA %p", virtual_address);
  }

  /* Free pages */
  cur_va = va;
  for (i = 0; i < required_pages; ++i) {
    ptr_table = NULL;
    frame_info = get_frame_info(ptr_page_directory, cur_va, &ptr_table);
    if (frame_info)
      free_frame(frame_info);
    unmap_frame(ptr_page_directory, cur_va);

    page_state[page_idx + i] = KHEAP_PAGE_FREE;
    page_alloc_sizes[page_idx + i] = 0;

    cur_va += PAGE_SIZE;
  }

  /* Shrink break if top pages are free */
  while (kheapPageAllocBreak > kheapPageAllocStart) {
    uint32 last_page_idx = (kheapPageAllocBreak - kheapPageAllocStart) >> PGSHIFT;
    if (last_page_idx == 0)
      break;
    if (page_state[last_page_idx - 1] == KHEAP_PAGE_FREE)
      kheapPageAllocBreak -= PAGE_SIZE;
    else
      break;
  }

  release_kspinlock(&kheap_lock);
}

unsigned int kheap_physical_address(unsigned int virtual_address) {
  unsigned int *ppt = NULL;
  struct FrameInfo* frame = get_frame_info(ptr_page_directory, virtual_address, &ppt);
  if (frame == NULL)
    return 0;

  unsigned int pa = to_physical_address(frame);
  return pa + PGOFF(virtual_address);
}

unsigned int kheap_virtual_address(unsigned int physical_address) {
  struct FrameInfo *frame = to_frame_info(physical_address);
  unsigned int va = frame->mapped_page;
  if (va == 0)
    return 0;
  else {
    unsigned int offset = PGOFF(physical_address);
    return ((va << 12) | offset);
  }
}

//=================================================================================//
//============================== BONUS FUNCTION
//===================================//
//=================================================================================//
// krealloc():

//	Attempts to resize the allocated space at "virtual_address" to
//"new_size" bytes, 	possibly moving it in the heap. 	If successful, returns the
//new virtual_address, in which case the old virtual_address must no longer be
//accessed. 	On failure, returns a null pointer, and the old virtual_address
//remains valid.

//	A call with virtual_address = null is equivalent to kmalloc().
//	A call with new_size = zero is equivalent to kfree().

extern __inline__ uint32 get_block_size(void *va);

void *krealloc(void *virtual_address, uint32 new_size) {
  // TODO: [PROJECT'25.BONUS#2] KERNEL REALLOC - krealloc
  // Your code is here
  // Comment the following line
  panic("krealloc() is not implemented yet...!!");
}
