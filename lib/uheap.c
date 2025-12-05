#include <inc/lib.h>

uint8 pages[NUM_OF_UHEAP_PAGES]; 
uint32 sizes[NUM_OF_UHEAP_PAGES]; 

int initial_arrays = 0;
int __firstTimeFlag = 1;
void intializeArrayPages()
{
    if (initial_arrays == 0)
    {
        for (uint32 i = 0; i < NUM_OF_UHEAP_PAGES; i++)
        {
            pages[i] = 0;
            sizes[i] = 0;
        }
        initial_arrays = 1;
    }
}

void uheap_init()
{
	if (__firstTimeFlag)
	{
		initialize_dynamic_allocator(USER_HEAP_START, USER_HEAP_START + DYN_ALLOC_MAX_SIZE);
		uheapPlaceStrategy = sys_get_uheap_strategy();
		uheapPageAllocStart = dynAllocEnd + PAGE_SIZE;
		uheapPageAllocBreak = uheapPageAllocStart;
		__firstTimeFlag = 0;
	}
}

int get_page(void* va)
{
	int ret = __sys_allocate_page(ROUNDDOWN(va, PAGE_SIZE), PERM_USER | PERM_WRITEABLE | PERM_UHPAGE);
	if (ret < 0)
		panic("get_page() failed");
	return 0;
}

void return_page(void* va)
{
	int ret = __sys_unmap_frame(ROUNDDOWN((uint32)va, PAGE_SIZE));
	if (ret < 0)
		panic("return_page() failed");
}

void* malloc(uint32 size)
{
#if USE_KHEAP 
	uheap_init();

	if (size == 0)
		return NULL;

	if (size <= DYN_ALLOC_MAX_BLOCK_SIZE)
		return alloc_block(size);

	intializeArrayPages();
	uint32 num = ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE;
	uint32 total = (USER_HEAP_MAX - uheapPageAllocStart) / PAGE_SIZE;
	uint32 used = (uheapPageAllocBreak - uheapPageAllocStart) / PAGE_SIZE;

	if (num == 0 || num > total)
		return NULL;

	uint32 found = used;
	uint32 exact = used;
	uint32 worst = used;
	uint32 worst_sz = 0;
	uint32 i = 0;

	while (i < used)
	{
		while (i < used && pages[i] == 1)
			i++;
		if (i >= used)
			break;

		uint32 start = i;
		uint32 cnt = 0;
		while (i < used && pages[i] == 0)
		{
			cnt++;
			i++;
		}

		if (cnt == num)
		{
			exact = start;
			break;
		}

		if (cnt > num && cnt > worst_sz)
		{
			worst_sz = cnt;
			worst = start;
		}
	}
	
	if (exact != used)
		found = exact;
	else if (worst != used)
		found = worst;


	if (found == used)
	{
		if (used + num > total)
			return NULL;
		uheapPageAllocBreak += num * PAGE_SIZE;
	}

	uint32 va = uheapPageAllocStart + found * PAGE_SIZE;
	sys_allocate_user_mem(va, num * PAGE_SIZE);

	for (i = 0; i < num; i++)
		pages[found + i] = 1;
	sizes[found] = num;

	return (void*)va;
#else
	panic("not handled when KERN HEAP is disabled");
#endif
}

void free(void* virtual_address)
{
	#if USE_KHEAP 
	if (virtual_address == NULL)
		return;

	uheap_init();

	uint32 va = (uint32)virtual_address;

	if (va >= dynAllocStart && va < dynAllocEnd)
	{
		free_block(virtual_address);
		return;
	}

	if (va < uheapPageAllocStart || va >= USER_HEAP_MAX)
		panic("invalid address");

	uint32 idx = (va - uheapPageAllocStart) / PAGE_SIZE;
	uint32 num = sizes[idx];

	if (num == 0)
		panic("not allocated");

	sys_free_user_mem(va, num * PAGE_SIZE);

	uint32 i;
	for (i = 0; i < num; i++)
	{
		pages[idx + i] = 0;
		sizes[idx + i] = 0;
	}

	uint32 last = (uheapPageAllocBreak - uheapPageAllocStart) / PAGE_SIZE;
	while (last > 0 && pages[last - 1] == 0)
	{
		uheapPageAllocBreak -= PAGE_SIZE;
		last--; 
	}
#else
	panic("not handled when KERN HEAP is disabled");
#endif
}


  void* smalloc(char *sharedVarName, uint32 size, uint8 isWritable)
{
#if USE_KHEAP
	//==============================================================
	//DON'T CHANGE THIS CODE========================================
	uheap_init();
	if (size == 0) return NULL ;
	if ((isWritable != 0)&&(isWritable != 1)) return NULL ;
	if (sharedVarName == NULL) return NULL ;
	//==============================================================

	//TODO: [PROJECT'25.IM#3] SHARED MEMORY - #2 smalloc
	//Your code is here
	uint32 size2 = ROUNDUP(size, PAGE_SIZE);
	uint32 num_pages_need = size2 >> PGSHIFT;
	if (num_pages_need == 0){
		return NULL;
	}
	if (num_pages_need >= ((USER_HEAP_MAX - uheapPageAllocStart) / PAGE_SIZE)){
    return NULL;
    }
	intializeArrayPages();

	int max_worst = 0;
	uint32 va_worst = 0 ;
	uint32 va_exact = 0;
	uint32 va_break = uheapPageAllocBreak;
	uint32 pages_loop = ((uheapPageAllocBreak - uheapPageAllocStart) >>PGSHIFT);
	int count = 0;
	for(int i = 0; i < pages_loop ; i++){
		uint32 page_begin = uheapPageAllocStart + (i * PAGE_SIZE) ;
		if(pages[i] == 0){
			count++;
			if(count == 1){
				va_exact = page_begin;
			}
		}else{
				if(count == num_pages_need){
					break;
				}
				else if(count < num_pages_need) {
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
		if(((va_exact != 0) && (count == num_pages_need)) || ((va_exact != 0) && (count > num_pages_need) && (count > max_worst))){
            int ret = sys_create_shared_object(sharedVarName, size, isWritable, (void*)va_exact);
            if((ret == E_SHARED_MEM_EXISTS) || (ret == E_NO_SHARE)){
		    	return NULL;
	        }
	        else{
			    for(int i = 0 ; i < num_pages_need ; i++){
					uint32 index = ((va_exact + (i * PAGE_SIZE)) >> PGSHIFT) - (uheapPageAllocStart >> PGSHIFT);
                    pages[index] = 1;
					if(i==0){
					sizes[index] = num_pages_need;
					}else{
                    sizes[index] = 0;
					}
				}
		        return (void*)va_exact;
	        }
		}
		else if(va_worst != 0){
            int ret = sys_create_shared_object(sharedVarName, size, isWritable, (void*)va_worst);
            if((ret == E_SHARED_MEM_EXISTS) || (ret == E_NO_SHARE)){
		    	return NULL;
	        }
	        else{
				for(int i = 0 ; i < num_pages_need ; i++){
					uint32 index = ((va_worst + (i * PAGE_SIZE)) >> PGSHIFT) - (uheapPageAllocStart >> PGSHIFT);
                    pages[index] = 1;
					if(i==0){
					sizes[index] = num_pages_need;
					}else{
                    sizes[index] = 0;
					}
					
				}
		        return (void*)va_worst;
	        }
		}
		if((va_exact == 0) && (va_worst == 0) ){
			uint32 free_before_break = 0;
        for(int32 i = ((uheapPageAllocBreak >>PGSHIFT)-1); i >= (uheapPageAllocStart >>PGSHIFT) ; i--){
			if(pages[i - (uheapPageAllocStart >>PGSHIFT)] == 1){
				break;
			}
			free_before_break ++;
			va_break = va_break - PAGE_SIZE;
		}
		uint32 free_before_unused = num_pages_need - free_before_break;
		if(((USER_HEAP_MAX >>PGSHIFT) - (uheapPageAllocBreak >>PGSHIFT)) >= free_before_unused){
            if (uheapPageAllocBreak + (free_before_unused * PAGE_SIZE) > USER_HEAP_MAX) {
                return NULL;
            }
            uheapPageAllocBreak = uheapPageAllocBreak + (free_before_unused * PAGE_SIZE);
			int ret = sys_create_shared_object(sharedVarName, size, isWritable, (void*)va_break);
            if((ret == E_SHARED_MEM_EXISTS) || (ret == E_NO_SHARE)){
		        return NULL;
	        }
	        else{
			for(int i = 0 ; i < num_pages_need ; i++){
					uint32 index = ((va_break + (i * PAGE_SIZE)) >> PGSHIFT) - (uheapPageAllocStart >> PGSHIFT);
                    pages[index] = 1;
					if(i==0){
					sizes[index] = num_pages_need;
					}else{
                    sizes[index] = 0;
					}
			}
			return (void*)va_break;
	    }
		}else{
			return NULL;
		}
		
		}
#else
	panic("not handled when KERN HEAP is disabled");
#endif
	////////////////////////////////////////////////////////////////////////////////////////////////////
	//Comment the following line
	//panic("smalloc() is not implemented yet...!!");
}

//========================================
// [4] SHARE ON ALLOCATED SHARED VARIABLE:
//========================================
void* sget(int32 ownerEnvID, char *sharedVarName)
{  
#if USE_KHEAP 
	//==============================================================
	//DON'T CHANGE THIS CODE========================================
	uheap_init();
	if (sharedVarName == NULL) return NULL ;
	//==============================================================
    int size = sys_size_of_shared_object(ownerEnvID, sharedVarName);
	if(size == E_SHARED_MEM_NOT_EXISTS){
		return NULL;
	}
	uint32 size2 = ROUNDUP(size, PAGE_SIZE);
	uint32 num_pages_need = size2 >> PGSHIFT;
	if (num_pages_need == 0){
		return NULL;
	}
	if (num_pages_need >= ((USER_HEAP_MAX - uheapPageAllocStart) / PAGE_SIZE)){
    	return NULL;
    }
	intializeArrayPages();

	int max_worst = 0;
	uint32 va_worst = 0 ;
	uint32 va_exact = 0;
	uint32 va_break = uheapPageAllocBreak;
	uint32 pages_loop = ((uheapPageAllocBreak - uheapPageAllocStart) >>PGSHIFT);
	int count = 0;
	for(int i = 0; i < pages_loop ; i++){
		uint32 page_begin = uheapPageAllocStart + (i * PAGE_SIZE) ;
		if(pages[i] == 0){
			count++;
			if(count == 1){
				va_exact = page_begin;
			}
		}else{
				if(count == num_pages_need){
					break;
				}
				else if(count < num_pages_need) {
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
		if(((va_exact != 0) && (count == num_pages_need)) || ((va_exact != 0) && (count > num_pages_need) && (count > max_worst))){
            int ret = sys_get_shared_object(ownerEnvID, sharedVarName,(void*)va_exact);
            if(ret == E_SHARED_MEM_NOT_EXISTS){
		    	return NULL;
	        }
	        else{
			    for(int i = 0 ; i < num_pages_need ; i++){
					uint32 index = ((va_exact + (i * PAGE_SIZE)) >> PGSHIFT) - (uheapPageAllocStart >> PGSHIFT);
                    pages[index] = 1;
					if(i==0){
					sizes[index] = num_pages_need;
					}else{
                    sizes[index] = 0;
					}
				}
		        return (void*)va_exact;
	        }
		}
		else if(va_worst != 0){
            int ret = sys_get_shared_object(ownerEnvID, sharedVarName,(void*)va_worst);
            if(ret == E_SHARED_MEM_NOT_EXISTS){
		    	return NULL;
	        }
	        else{
				for(int i = 0 ; i < num_pages_need ; i++){
					uint32 index = ((va_worst + (i * PAGE_SIZE)) >> PGSHIFT) - (uheapPageAllocStart >> PGSHIFT);
                    pages[index] = 1;
					if(i==0){
					sizes[index] = num_pages_need;
					}else{
                    sizes[index] = 0;
					}
					
				}
		        return (void*)va_worst;
	        }
		}
		if((va_exact == 0) && (va_worst == 0) ){
			uint32 free_before_break = 0;
        for(int32 i = ((uheapPageAllocBreak >>PGSHIFT)-1); i >= (uheapPageAllocStart >>PGSHIFT) ; i--){
			if(pages[i - (uheapPageAllocStart >>PGSHIFT)] == 1){
				break;
			}
			free_before_break ++;
			va_break = va_break - PAGE_SIZE;
		}
		uint32 free_before_unused = num_pages_need - free_before_break;
		if(((USER_HEAP_MAX >>PGSHIFT) - (uheapPageAllocBreak >>PGSHIFT)) >= free_before_unused){
            if (uheapPageAllocBreak + (free_before_unused * PAGE_SIZE) > USER_HEAP_MAX) {
                return NULL;
            }
            uheapPageAllocBreak = uheapPageAllocBreak + (free_before_unused * PAGE_SIZE);
			int ret = sys_get_shared_object(ownerEnvID, sharedVarName,(void*)va_break);
            if(ret == E_SHARED_MEM_NOT_EXISTS){
		        return NULL;
	        }
	        else{
			for(int i = 0 ; i < num_pages_need ; i++){
					uint32 index = ((va_break + (i * PAGE_SIZE)) >> PGSHIFT) - (uheapPageAllocStart >> PGSHIFT);
                    pages[index] = 1;
					if(i==0){
					sizes[index] = num_pages_need;
					}else{
                    sizes[index] = 0;
					}
			}
			return (void*)va_break;
	    }
		}else{
			return NULL;
		}
		
	}
#else
	panic("not handled when KERN HEAP is disabled");
#endif
	////////////////////////////////////////////////////////////////////////////////////////////////////
	//Comment the following line
	//panic("smalloc() is not implemented yet...!!");
}


//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//


//=================================
// REALLOC USER SPACE:
//=================================
//	Attempts to resize the allocated space at "virtual_address" to "new_size" bytes,
//	possibly moving it in the heap.
//	If successful, returns the new virtual_address, in which case the old virtual_address must no longer be accessed.
//	On failure, returns a null pointer, and the old virtual_address remains valid.

//	A call with virtual_address = null is equivalent to malloc().
//	A call with new_size = zero is equivalent to free().

//  Hint: you may need to use the sys_move_user_mem(...)
//		which switches to the kernel mode, calls move_user_mem(...)
//		in "kern/mem/chunk_operations.c", then switch back to the user mode here
//	the move_user_mem() function is empty, make sure to implement it.
void *realloc(void *virtual_address, uint32 new_size)
{
	//==============================================================
	//DON'T CHANGE THIS CODE========================================
	uheap_init();
	//==============================================================
	panic("realloc() is not implemented yet...!!");
}


//=================================
// FREE SHARED VARIABLE:
//=================================
//	This function frees the shared variable at the given virtual_address
//	To do this, we need to switch to the kernel, free the pages AND "EMPTY" PAGE TABLES
//	from main memory then switch back to the user again.
//
//	use sys_delete_shared_object(...); which switches to the kernel mode,
//	calls delete_shared_object(...) in "shared_memory_manager.c", then switch back to the user mode here
//	the delete_shared_object() function is empty, make sure to implement it.
void sfree(void* virtual_address)
{
	//TODO: [PROJECT'25.BONUS#5] EXIT #2 - sfree
	//Your code is here
	//Comment the following line
	panic("sfree() is not implemented yet...!!");

	//	1) you should find the ID of the shared variable at the given address
	//	2) you need to call sys_freeSharedObject()
}


//==================================================================================//
//========================== MODIFICATION FUNCTIONS ================================//
//==================================================================================//
