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
int initialarray = 0;
uint8 pages [NUM_OF_KHEAP_PAGES];
uint32 sizes[NUM_OF_KHEAP_PAGES];

void *kmalloc(unsigned int size) {
#if USE_KHEAP
  static struct kspinlock kmalloc_lock;
  static int Lock = 0;
  uint32 size1;
  uint32 pages_need;
  uint32 uspages;
  uint32 res_Virtual_Address;
  uint32 pagmapp = 0;
  uint32 nowva;
  uint32 virtual_address_of_map;
  void *res = NULL;
  int Break = 0;
  if (size == 0)
      return NULL;

  if (!Lock) {
    init_kspinlock(&kmalloc_lock, "kheap");
    Lock = 1;
  }
  acquire_kspinlock(&kmalloc_lock);
  if (size <= DYN_ALLOC_MAX_BLOCK_SIZE) {
    res = alloc_block(size);
    release_kspinlock(&kmalloc_lock);
    return res;
  }
  if (!initialarray) {
    for (uint32 i = 0; i < NUM_OF_KHEAP_PAGES; i++) {
      pages[i] = 0;
      sizes[i] = 0;
    }
    initialarray = 1;
  }
  
  size1 = ROUNDUP(size, PAGE_SIZE);
  pages_need = size1 >> PGSHIFT;

  if (pages_need == 0 || pages_need > ((KERNEL_HEAP_MAX - kheapPageAllocStart) >> PGSHIFT)) {
    release_kspinlock(&kmalloc_lock);
    return NULL;
  }

  uspages = 0;
  if (kheapPageAllocBreak > kheapPageAllocStart){
    uspages = (kheapPageAllocBreak - kheapPageAllocStart) >> PGSHIFT;
  }
  int max_worst = 0;
  unsigned int va_worst = 0;
  unsigned int va_exact = 0;
  unsigned int va_break = kheapPageAllocBreak;
  int count = 0;
  
  for(uint32 i = 0; i < uspages; i++){
    unsigned int page_begin = kheapPageAllocStart + (i * PAGE_SIZE);
    if(pages[i] == 0){
      count++;
      if(count == 1){
        va_exact = page_begin;
      }
    }else{
      if(count == pages_need){
        break;
      }
      else if(count < pages_need){
        count = 0;
        va_exact = 0;
      }else{
        if(max_worst < count){
          max_worst = count;
          va_worst = va_exact;
        }
        count = 0;
        va_exact = 0;
      }
    }
  }
  if(((va_exact != 0) && (count == pages_need)) || ((va_exact != 0) && (count > pages_need) && (count > max_worst))){
    res_Virtual_Address = va_exact;
  }
  else if(va_worst != 0){
    res_Virtual_Address = va_worst;
  }
  else if((va_exact == 0) && (va_worst == 0)){
    unsigned int free_before_break = 0;
    for(int i = uspages - 1; i >= 0; i--){
      if(pages[i] == 1){
        break;
      }
      free_before_break++;
      va_break = va_break - PAGE_SIZE;
    }
    uint32 free_before_unused = pages_need - free_before_break;
    if((((KERNEL_HEAP_MAX - kheapPageAllocStart) >> PGSHIFT) - uspages) >= free_before_unused){
      if(kheapPageAllocBreak + (free_before_unused * PAGE_SIZE) > KERNEL_HEAP_MAX){
        release_kspinlock(&kmalloc_lock);
        return NULL;
      }
      Break = 1;
      res_Virtual_Address = va_break;
    }else{
      release_kspinlock(&kmalloc_lock);
      return NULL;
    }
  }else{
    release_kspinlock(&kmalloc_lock);
    return NULL;
  }

  while (pagmapp < pages_need) {
    struct FrameInfo *ptr_frame_info;
    allocate_frame(&ptr_frame_info);
    nowva = res_Virtual_Address + (pagmapp * PAGE_SIZE);
      if (map_frame(ptr_page_directory, ptr_frame_info, nowva, PERM_WRITEABLE) != 0) {
      free_frame(ptr_frame_info);
      break;
    }
    pagmapp++;
  }

  if (pagmapp < pages_need) {
    while (pagmapp > 0) {
      pagmapp--;
      virtual_address_of_map = res_Virtual_Address + (pagmapp * PAGE_SIZE);
      unmap_frame(ptr_page_directory, virtual_address_of_map);
    }
    release_kspinlock(&kmalloc_lock);
    return NULL;
  }

  
  uint32 rindex = (res_Virtual_Address - kheapPageAllocStart) >> PGSHIFT;
  for (uint32 i = 0; i < pages_need; ++i){
    pages[rindex + i] = 1;
    if(i==0){
			sizes[rindex] = pages_need;
    }
    else{
      sizes[rindex + i] = 0;
  }
}

  if (Break){
    uint32 free_before_break = 0;
    for(int i = (((kheapPageAllocBreak - kheapPageAllocStart) >> PGSHIFT) - 1); i >= 0; i--){
      if(pages[i] == 1){  
        break;
      }
      free_before_break++;
    }
    uint32 free_before_unused = pages_need - free_before_break;
    kheapPageAllocBreak += (free_before_unused * PAGE_SIZE);
  }

  res = (void *)res_Virtual_Address;
  release_kspinlock(&kmalloc_lock);

  return res;
#else
	panic("not handled when KERN HEAP is disabled");
#endif
}
/* ======================== KFREE ======================== */
void kfree(void *virtual_address) {
#if USE_KHEAP
  static struct kspinlock kfree_lock;
  static int Lock = 0;
  uint32 va = (uint32)virtual_address;

  if (virtual_address == NULL)
    return;

  if (!Lock) {
    init_kspinlock(&kfree_lock, "kheap");
    Lock = 1;
  }

  acquire_kspinlock(&kfree_lock);
  if (va >= dynAllocStart && va < dynAllocEnd) {
    uint32 size = get_block_size(virtual_address);
    if (size == 0)
      panic("kfree:invalid size");;
    free_block(virtual_address);
    release_kspinlock(&kfree_lock);
    return;
  }
  if (va < kheapPageAllocStart || va >= kheapPageAllocBreak || va % PAGE_SIZE != 0) {
    release_kspinlock(&kfree_lock);
    panic("kfree:invalid address");
  } 
  if (virtual_address >= kheapPageAllocStart && virtual_address < kheapPageAllocBreak){
    uint32 indexofpage = (va >> PGSHIFT) - (kheapPageAllocStart >> PGSHIFT);
    if (indexofpage >= NUM_OF_KHEAP_PAGES) {
      release_kspinlock(&kfree_lock);
      panic("kfree: bad page index");
    }
    uint32 pages_need = sizes[indexofpage];
    if (pages_need == 0) {
      release_kspinlock(&kfree_lock);
      panic("kfree: no required pages");
    }
  uint32 loop_va = va;
  uint32 *ptr_page_table;
  struct FrameInfo *frame_info;
  for (int i = 0; i < pages_need; i++) {
    ptr_page_table = NULL;
    frame_info = get_frame_info(ptr_page_directory, loop_va, &ptr_page_table);
    if (frame_info){
      free_frame(frame_info);
    }
    unmap_frame(ptr_page_directory, loop_va);
    pages[indexofpage + i] = 0;
    sizes[indexofpage + i] = 0;
    loop_va = loop_va + PAGE_SIZE;
  }
  while (kheapPageAllocBreak > kheapPageAllocStart) {
    uint32 indexLastBreak = (kheapPageAllocBreak >> PGSHIFT) - (kheapPageAllocStart >> PGSHIFT);
    if (indexLastBreak == 0){
      break;
    }
    if (pages[indexLastBreak - 1] == 0){
      kheapPageAllocBreak = kheapPageAllocBreak - PAGE_SIZE;
    }else{
      break;
    }
  }
  }
  release_kspinlock(&kfree_lock);
#else
	panic("not handled when KERN HEAP is disabled");
#endif
}

unsigned int kheap_physical_address(unsigned int virtual_address) {
  #if USE_KHEAP
  unsigned int *ppt = NULL;
  struct FrameInfo* frame = get_frame_info(ptr_page_directory, virtual_address, &ppt);
  if (frame == NULL)
    return 0;

  unsigned int pa = to_physical_address(frame);
  return pa + PGOFF(virtual_address);
  #else
	panic("not handled when KERN HEAP is disabled");
  #endif
}

unsigned int kheap_virtual_address(unsigned int physical_address) {
  #if USE_KHEAP
  struct FrameInfo *frame = to_frame_info(physical_address);
  unsigned int va = frame->mapped_page;
  if (va == 0)
    return 0;
  else {
    unsigned int offset = PGOFF(physical_address);
    return ((va << 12) | offset);
  }
  #else
	panic("not handled when KERN HEAP is disabled");
  #endif
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
  //panic("krealloc() is not implemented yet...!!");
}
