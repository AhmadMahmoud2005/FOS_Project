/*
 * dynamic_allocator.c
 *
 *  Created on: Sep 21, 2023
 *      Author: HP
 */
#include <inc/assert.h>
#include <inc/string.h>
#include "../inc/dynamic_allocator.h"

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//
//==================================
// [1] GET PAGE VA:
//==================================
__inline__ uint32 to_page_va(struct PageInfoElement *ptrPageInfo)
{
	//Get start VA of the page from the corresponding Page Info pointer
	int idxInPageInfoArr = (ptrPageInfo - pageBlockInfoArr);
	return dynAllocStart + (idxInPageInfoArr << PGSHIFT);
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

//==================================
// [1] INITIALIZE DYNAMIC ALLOCATOR:
//==================================
bool is_initialized = 0;
void initialize_dynamic_allocator(uint32 daStart, uint32 daEnd)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert(daEnd <= daStart + DYN_ALLOC_MAX_SIZE);
		is_initialized = 1;
	}
	//==================================================================================
	//==================================================================================
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #1 initialize_dynamic_allocator
	//Your code is here
	//Comment the following line
	// panic("initialize_dynamic_allocator() Not implemented yet");
	dynAllocStart = daStart;
    dynAllocEnd = daEnd;
    LIST_INIT(&(freePagesList));
	for (int i = 0; i < (daEnd-daStart)/PAGE_SIZE; i++) {
		pageBlockInfoArr[i].block_size = 0;
		pageBlockInfoArr[i].num_of_free_blocks = 0;
		struct PageInfoElement *pagern = &pageBlockInfoArr[i];
		LIST_INSERT_TAIL(&(freePagesList), pagern);
	}
    for (int i = 0; i < 8; i++)
	{
		LIST_INIT(&freeBlockLists[i]);
	}
}
int64 Get_Page_Num(void *va){
	return ((uint32)va - dynAllocStart) / PAGE_SIZE;
}

//===========================
// [2] GET BLOCK SIZE:
//===========================
__inline__ uint32 get_block_size(void *va)
{
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #2 get_block_size
	//Your code is here
	int64 page_num = Get_Page_Num(va);
	if(page_num == -1){
		return 0;
	}
	uint32 block_size = ((uint32) pageBlockInfoArr[page_num].block_size);
	return (block_size);
	//Comment the following line
	//panic("get_block_size() Not implemented yet");

}
int32 Index_Of_Block_Free_List(void *va){
	uint32 index = 0; 
	uint32 size = DYN_ALLOC_MIN_BLOCK_SIZE;
    uint32 block_size = get_block_size(va);
	while(block_size != size){
		size = size * 2;
		if(size > DYN_ALLOC_MAX_BLOCK_SIZE){
			return -1;
		}
		index++;
	}
	return index;
}
int8 is_free(void *va){
	struct BlockElement *target = NULL;
	struct BlockElement *current = (struct BlockElement*) va;
	uint32 index = Index_Of_Block_Free_List(va);
	if(index == -1){
		return -1;
	}
	LIST_FOREACH(target, &freeBlockLists[index]){
		if(target == current){
			return 1;
		}
	}
    return 0; 
}


inline uint16 nearest_power2(uint32 x) {
	if (x <= 8) return 8;
	uint16 next = 8;
	while  (x > next) {
		next = next * 2;
	}
	return next;
}
int32 Index_Of_Block(uint32 size){
	int x = 8; 
	for(int i = 0; i < 9; i++){
		if(size == x){
			return i;
		}
		x = x * 2;
	}
    return -1 ;
}
void if_get(struct BlockElement *bl ,int32 ib){
LIST_REMOVE(&freeBlockLists[ib], bl);
int64 page_index =Get_Page_Num((void*)bl);
pageBlockInfoArr[page_index].num_of_free_blocks= pageBlockInfoArr[page_index].num_of_free_blocks - 1;
}


//===========================
// 3) ALLOCATE BLOCK:
//===========================
void *alloc_block(uint32 size)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert(size <= DYN_ALLOC_MAX_BLOCK_SIZE);
	}
	//==================================================================================
	//==================================================================================
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #3 alloc_block
	//Your code is here
	
    if (size == 0){
    	return NULL;
    }
    uint16 sz =nearest_power2(size);
    int32 ib = Index_Of_Block(sz);
    if (ib == -1){
		return NULL;
	}
	struct BlockElement *bl =LIST_FIRST(&freeBlockLists[ib]);
    if (bl==NULL){
    	  
    	struct PageInfoElement* freepage = LIST_FIRST(&freePagesList);
    	if (freepage==NULL){
    		while(bl==NULL){
    			ib=ib+1;
    			if (ib>=9){
    				panic("no free block");
    			}
    			bl = LIST_FIRST(&freeBlockLists[ib]);
    		}
			if_get(bl , ib);
			return (void*)bl;
    	}
    	else {
	    uint32 VAPAGE = to_page_va(freepage);
	    get_page((void*) VAPAGE);
    	uint16 numofBlocks=(4096/sz);	
		LIST_REMOVE(&freePagesList,freepage);
    	int64 PageIn = Get_Page_Num((void*)VAPAGE);
		if(PageIn == -1){
			return NULL;
		}
    	pageBlockInfoArr[PageIn].block_size=sz;
    	pageBlockInfoArr[PageIn].num_of_free_blocks=numofBlocks;
		for (int i = 0 ; i < numofBlocks ; i++){
			uint32 startOfBlock= VAPAGE + (i*sz);
			struct BlockElement* element = (struct BlockElement*) startOfBlock;
			LIST_INSERT_TAIL(&freeBlockLists[ib], element );
        }
		bl =LIST_FIRST(&freeBlockLists[ib]);
		if_get(bl , ib);
		return (void*)bl;
    	
    	}
    	
    }
    else{
		if_get(bl , ib);
		return (void*)bl;
	}
	//Comment the following line
	//panic("alloc_block() Not implemented yet");

	//TODO: [PROJECT'25.BONUS#1] DYNAMIC ALLOCATOR - block if no free block
}

//===========================
// [4] FREE BLOCK:
//===========================
void free_block(void *va)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert((uint32)va >= dynAllocStart && (uint32)va < dynAllocEnd);
	}
	//==================================================================================
	//==================================================================================

	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #4 free_block
	//Your code is here
    if (va == NULL || get_block_size(va) == 0) {
        return;
    }
	int8 is_free_var = is_free(va);
	if ( (is_free_var == 1) || (is_free_var == -1) ) {
        return;
    }
	int32 index_block = Index_Of_Block_Free_List(va);
	if ( index_block == -1 ) {
        return;
    }
	
	//insert block to free block list
	struct BlockElement *current = (struct BlockElement*) va;
	LIST_INSERT_TAIL(&freeBlockLists[index_block] , current);
	//update the num of free
	int64 index_page = Get_Page_Num(va);
	if(index_page == -1){
		return;
	}
	pageBlockInfoArr[index_page].num_of_free_blocks = pageBlockInfoArr[index_page].num_of_free_blocks + 1;
    //if the page totally free
	uint32 page_size = 4096;
	uint32 block_size_var = (uint32) pageBlockInfoArr[index_page].block_size;
	if (block_size_var == 0) {
    return;
    }
	uint32 total_free =((page_size) / block_size_var);
	if(total_free == pageBlockInfoArr[index_page].num_of_free_blocks)
	{
        pageBlockInfoArr[index_page].block_size = 0;
	    pageBlockInfoArr[index_page].num_of_free_blocks = 0;
		LIST_INSERT_TAIL(&freePagesList , &pageBlockInfoArr[index_page]);
		uint32 page_va_start = to_page_va(&pageBlockInfoArr[index_page]);
		uint32 page_va_end = page_va_start + page_size;
		struct BlockElement *target = NULL;
	    LIST_FOREACH(target , &freeBlockLists[index_block]){
		    if(((uint32)target >= page_va_start) && ((uint32)target < page_va_end))
			{
			    LIST_REMOVE(&(freeBlockLists[index_block]), target);
		    }

	    }
		return_page((void*) page_va_start);
	}
	
	//Comment the following line
	//panic("free_block() Not implemented yet");

}
//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//

//===========================
// [1] REALLOCATE BLOCK:
//===========================
void* realloc_block(void* va, uint32 new_size)
{
	//TODO: [PROJECT'25.BONUS#2] KERNEL REALLOC - realloc_block
	//Your code is here
	//Comment the following line
	panic("realloc_block() Not implemented yet");
}