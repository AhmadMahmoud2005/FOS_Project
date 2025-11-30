/*
 * fault_handler.c
 *
 *  Created on: Oct 12, 2022
 *      Author: HP
 */

#include "trap.h"
#include <kern/proc/user_environment.h>
#include <kern/cpu/sched.h>
#include <kern/cpu/cpu.h>
#include <kern/disk/pagefile_manager.h>
#include <kern/mem/memory_manager.h>
#include <kern/mem/kheap.h>

//2014 Test Free(): Set it to bypass the PAGE FAULT on an instruction with this length and continue executing the next one
// 0 means don't bypass the PAGE FAULT
uint8 bypassInstrLength = 0;

//===============================
// REPLACEMENT STRATEGIES
//===============================
//2020
void setPageReplacmentAlgorithmLRU(int LRU_TYPE)
{
	assert(LRU_TYPE == PG_REP_LRU_TIME_APPROX || LRU_TYPE == PG_REP_LRU_LISTS_APPROX);
	_PageRepAlgoType = LRU_TYPE ;
}
void setPageReplacmentAlgorithmCLOCK(){_PageRepAlgoType = PG_REP_CLOCK;}
void setPageReplacmentAlgorithmFIFO(){_PageRepAlgoType = PG_REP_FIFO;}
void setPageReplacmentAlgorithmModifiedCLOCK(){_PageRepAlgoType = PG_REP_MODIFIEDCLOCK;}
/*2018*/ void setPageReplacmentAlgorithmDynamicLocal(){_PageRepAlgoType = PG_REP_DYNAMIC_LOCAL;}
/*2021*/ void setPageReplacmentAlgorithmNchanceCLOCK(int PageWSMaxSweeps){_PageRepAlgoType = PG_REP_NchanceCLOCK;  page_WS_max_sweeps = PageWSMaxSweeps;}
/*2024*/ void setFASTNchanceCLOCK(bool fast){ FASTNchanceCLOCK = fast; };
/*2025*/ void setPageReplacmentAlgorithmOPTIMAL(){ _PageRepAlgoType = PG_REP_OPTIMAL; };

//2020
uint32 isPageReplacmentAlgorithmLRU(int LRU_TYPE){return _PageRepAlgoType == LRU_TYPE ? 1 : 0;}
uint32 isPageReplacmentAlgorithmCLOCK(){if(_PageRepAlgoType == PG_REP_CLOCK) return 1; return 0;}
uint32 isPageReplacmentAlgorithmFIFO(){if(_PageRepAlgoType == PG_REP_FIFO) return 1; return 0;}
uint32 isPageReplacmentAlgorithmModifiedCLOCK(){if(_PageRepAlgoType == PG_REP_MODIFIEDCLOCK) return 1; return 0;}
/*2018*/ uint32 isPageReplacmentAlgorithmDynamicLocal(){if(_PageRepAlgoType == PG_REP_DYNAMIC_LOCAL) return 1; return 0;}
/*2021*/ uint32 isPageReplacmentAlgorithmNchanceCLOCK(){if(_PageRepAlgoType == PG_REP_NchanceCLOCK) return 1; return 0;}
/*2021*/ uint32 isPageReplacmentAlgorithmOPTIMAL(){if(_PageRepAlgoType == PG_REP_OPTIMAL) return 1; return 0;}

//===============================
// PAGE BUFFERING
//===============================
void enableModifiedBuffer(uint32 enableIt){_EnableModifiedBuffer = enableIt;}
uint8 isModifiedBufferEnabled(){  return _EnableModifiedBuffer ; }

void enableBuffering(uint32 enableIt){_EnableBuffering = enableIt;}
uint8 isBufferingEnabled(){  return _EnableBuffering ; }

void setModifiedBufferLength(uint32 length) { _ModifiedBufferLength = length;}
uint32 getModifiedBufferLength() { return _ModifiedBufferLength;}

//===============================
// FAULT HANDLERS
//===============================

//==================
// [0] INIT HANDLER:
//==================
void fault_handler_init()
{
	//setPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX);
	//setPageReplacmentAlgorithmOPTIMAL();
	setPageReplacmentAlgorithmCLOCK();
	//setPageReplacmentAlgorithmModifiedCLOCK();
	enableBuffering(0);
	enableModifiedBuffer(0) ;
	setModifiedBufferLength(1000);
}
//==================
// [1] MAIN HANDLER:
//==================
/*2022*/
uint32 last_eip = 0;
uint32 before_last_eip = 0;
uint32 last_fault_va = 0;
uint32 before_last_fault_va = 0;
int8 num_repeated_fault  = 0;
extern uint32 sys_calculate_free_frames() ;

struct Env* last_faulted_env = NULL;
void fault_handler(struct Trapframe *tf)
{
	/******************************************************/
	// Read processor's CR2 register to find the faulting address
	uint32 fault_va = rcr2();
	//cprintf("************Faulted VA = %x************\n", fault_va);
	//	print_trapframe(tf);
	/******************************************************/

	//If same fault va for 3 times, then panic
	//UPDATE: 3 FAULTS MUST come from the same environment (or the kernel)
	struct Env* cur_env = get_cpu_proc();
	if (last_fault_va == fault_va && last_faulted_env == cur_env)
	{
		num_repeated_fault++ ;
		if (num_repeated_fault == 3)
		{
			print_trapframe(tf);
			panic("Failed to handle fault! fault @ at va = %x from eip = %x causes va (%x) to be faulted for 3 successive times\n", before_last_fault_va, before_last_eip, fault_va);
		}
	}
	else
	{
		before_last_fault_va = last_fault_va;
		before_last_eip = last_eip;
		num_repeated_fault = 0;
	}
	last_eip = (uint32)tf->tf_eip;
	last_fault_va = fault_va ;
	last_faulted_env = cur_env;
	/******************************************************/
	//2017: Check stack overflow for Kernel
	int userTrap = 0;
	if ((tf->tf_cs & 3) == 3) {
		userTrap = 1;
	}
	if (!userTrap)
	{
		struct cpu* c = mycpu();
		//cprintf("trap from KERNEL\n");
		if (cur_env && fault_va >= (uint32)cur_env->kstack && fault_va < (uint32)cur_env->kstack + PAGE_SIZE)
			panic("User Kernel Stack: overflow exception!");
		else if (fault_va >= (uint32)c->stack && fault_va < (uint32)c->stack + PAGE_SIZE)
			panic("Sched Kernel Stack of CPU #%d: overflow exception!", c - CPUS);
#if USE_KHEAP
		if (fault_va >= KERNEL_HEAP_MAX)
			panic("Kernel: heap overflow exception!");
#endif
	}
	//2017: Check stack underflow for User
	else
	{
		//cprintf("trap from USER\n");
		if (fault_va >= USTACKTOP && fault_va < USER_TOP)
			panic("User: stack underflow exception!");
	}

	//get a pointer to the environment that caused the fault at runtime
	//cprintf("curenv = %x\n", curenv);
	struct Env* faulted_env = cur_env;
	if (faulted_env == NULL)
	{
		cprintf("\nFaulted VA = %x\n", fault_va);
		print_trapframe(tf);
		panic("faulted env == NULL!");
	}
	//check the faulted address, is it a table or not ?
	//If the directory entry of the faulted address is NOT PRESENT then
	if ( (faulted_env->env_page_directory[PDX(fault_va)] & PERM_PRESENT) != PERM_PRESENT)
	{
		faulted_env->tableFaultsCounter ++ ;
		table_fault_handler(faulted_env, fault_va);
	}
	else
	{
		if (userTrap)
		{
			/*============================================================================================*/
			//TODO: [PROJECT'25.GM#3] FAULT HANDLER I - #2 Check for invalid pointers
			//(e.g. pointing to unmarked user heap page, kernel or wrong access rights),
			//your code is here
			int perms = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);
			bool page_does_not_exist = (perms == -1);
			bool page_is_not_user = ((perms & PERM_USER) == 0);
			bool page_is_not_writeable = ((perms & PERM_PRESENT) == 1 && (perms & PERM_WRITEABLE) == 0);

			bool is_page_within_user_heap = (fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX);
			bool page_is_not_UHpage = is_page_within_user_heap && ((perms & PERM_UHPAGE) == 0);
			bool illegal_address = fault_va >= USER_LIMIT;
			
			if (page_does_not_exist || illegal_address || page_is_not_user || page_is_not_writeable || page_is_not_UHpage)
				env_exit();
			/*============================================================================================*/
		}

		/*2022: Check if fault due to Access Rights */
		int perms = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);
		if (perms & PERM_PRESENT)
			panic("Page @va=%x is exist! page fault due to violation of ACCESS RIGHTS\n", fault_va) ;
		/*============================================================================================*/


		// we have normal page fault =============================================================
		faulted_env->pageFaultsCounter ++ ;

//				cprintf("[%08s] user PAGE fault va %08x\n", faulted_env->prog_name, fault_va);
//				cprintf("\nPage working set BEFORE fault handler...\n");
//				env_page_ws_print(faulted_env);
		//int ffb = sys_calculate_free_frames();

		if(isBufferingEnabled())
		{
			__page_fault_handler_with_buffering(faulted_env, fault_va);
		}
		else
		{
			page_fault_handler(faulted_env, fault_va);
		}

		//		cprintf("\nPage working set AFTER fault handler...\n");
		//		env_page_ws_print(faulted_env);
		//		int ffa = sys_calculate_free_frames();
		//		cprintf("fault handling @%x: difference in free frames (after - before = %d)\n", fault_va, ffa - ffb);
	}

	/*************************************************************/
	//Refresh the TLB cache
	tlbflush();
	/*************************************************************/
}


//=========================
// [2] TABLE FAULT HANDLER:
//=========================
void table_fault_handler(struct Env * curenv, uint32 fault_va)
{
	//panic("table_fault_handler() is not implemented yet...!!");
	//Check if it's a stack page
	uint32* ptr_table;
#if USE_KHEAP
	{
		ptr_table = create_page_table(curenv->env_page_directory, (uint32)fault_va);
	}
#else
	{
		__static_cpt(curenv->env_page_directory, (uint32)fault_va, &ptr_table);
	}
#endif
}

//=========================
// [3] PAGE FAULT HANDLER:
//=========================
/* Calculate the number of page faults according th the OPTIMAL replacement strategy
 * Given:
 * 	1. Initial Working Set List (that the process started with)
 * 	2. Max Working Set Size
 * 	3. Page References List (contains the stream of referenced VAs till the process finished)
 *
 * 	IMPORTANT: This function SHOULD NOT change any of the given lists
 */
int get_optimal_num_faults(struct WS_List *initWorkingSet, int maxWSSize, struct PageRef_List *pageReferences)
{
	//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #2 get_optimal_num_faults
	//Your code is here
	//Comment the following line
	panic("get_optimal_num_faults() is not implemented yet...!!");
}

void page_fault_handler(struct Env * faulted_env, uint32 fault_va)
{
#if USE_KHEAP
	if (isPageReplacmentAlgorithmOPTIMAL())
	{
		//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #1 Optimal Reference Stream
		fault_va = ROUNDDOWN(fault_va, PAGE_SIZE);

		// check if faulted page is in the Active WS
		bool pageInActiveWS = 0;
		struct WorkingSetElement *wsElem;
		// used LIST_FOREACH to iterate through the linked list
		LIST_FOREACH(wsElem, &(faulted_env->page_WS_list))
		{
			if (wsElem->virtual_address == fault_va)
			{
				pageInActiveWS = 1;
				break;
			}
		}

		// if faulted page not in memory, read it from disk; else just set its present bit
		uint32 *ptr_table = NULL;
		struct FrameInfo *frame = get_frame_info(faulted_env->env_page_directory, fault_va, &ptr_table);
		if (frame == NULL)
		{
			// page not in memory - get from disk
			int perms = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);
			if (perms == -1) perms = 0;
			perms |= PERM_PRESENT | PERM_USER;

			int ret = alloc_page(faulted_env->env_page_directory, fault_va, perms, 0);
			if (ret == E_NO_MEM)
				panic("OPTIMAL: No memory for allocating a new frame");

			ret = pf_read_env_page(faulted_env, (void*)fault_va);
			if (ret == E_PAGE_NOT_EXIST_IN_PF)
			{
				bool within_UStack = fault_va < USTACKTOP && fault_va >= USTACKBOTTOM;
				bool within_UHeap = fault_va < USER_HEAP_MAX && fault_va >= USER_HEAP_START;
				if (within_UHeap || within_UStack)
					pf_add_empty_env_page(faulted_env, fault_va, 0);
				else
				{
					unmap_frame(faulted_env->env_page_directory, fault_va);
					env_exit();
				}
			}
		}
		else
		{
			// page in memory but present bit is 0 - just set present bit
			pt_set_page_permissions(faulted_env->env_page_directory, fault_va, PERM_PRESENT, 0);
		}

		// if the faulted page is in the active WS do nothing
		// else, if active WS is full, reset present & delete all its pages
		if (!pageInActiveWS)
		{
			uint32 wsSize = LIST_SIZE(&(faulted_env->page_WS_list));
			if (wsSize >= faulted_env->page_WS_max_size)
			{
				// if true then WS is full so reset present bit for all pages and clear the list
				struct WorkingSetElement *tempElem;
				LIST_FOREACH(tempElem, &(faulted_env->page_WS_list))
				{
					pt_set_page_permissions(faulted_env->env_page_directory, tempElem->virtual_address, 0, PERM_PRESENT);
				}
				// remove all elements from WS
				struct WorkingSetElement *toRemove;
				while ((toRemove = LIST_FIRST(&(faulted_env->page_WS_list))) != NULL)
				{
					LIST_REMOVE(&(faulted_env->page_WS_list), toRemove);
					kfree(toRemove); //used the implemented function kfree
				}
				faulted_env->page_last_WS_element = NULL;
			}

			// add the faulted page to the active WS
			struct WorkingSetElement *newElem = env_page_ws_list_create_element(faulted_env, fault_va);
			LIST_INSERT_TAIL(&(faulted_env->page_WS_list), newElem);
			if (faulted_env->page_last_WS_element == NULL)
				faulted_env->page_last_WS_element = newElem;
		}

		// add faulted page to the end of the reference stream list
		struct PageRefElement *refElem = kmalloc(sizeof(struct PageRefElement));
		if (refElem == NULL) // if null it means that we failed to allocate
			panic("OPTIMAL: Failed to allocate PageRefElement");
		refElem->virtual_address = fault_va;
		LIST_INSERT_TAIL(&(faulted_env->referenceStreamList), refElem);
	}
	else
	{
		struct WorkingSetElement *victimWSElement = NULL;
		uint32 wsSize = LIST_SIZE(&(faulted_env->page_WS_list));
		if(wsSize < (faulted_env->page_WS_max_size))
		{
			//TODO: [PROJECT'25.GM#3] FAULT HANDLER I - #3 placement
			//Your code is here
			//Comment the following line
			// panic("page_fault_handler().PLACEMENT is not implemented yet...!!");
			int perms = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);
			perms |= PERM_PRESENT | PERM_USER;
			int ret = alloc_page(faulted_env->env_page_directory, fault_va, perms, 0);
			if (ret == E_NO_MEM) {
				panic("No enough memory for allocating a new frame");
				return;
			}

			// Read the faulted page from page file to memory
			ret = pf_read_env_page(faulted_env, (void*)fault_va);

			// If the page does not exist on page file, then
			if (ret == E_PAGE_NOT_EXIST_IN_PF) {
				bool within_UStack = fault_va < USTACKTOP && fault_va >= USTACKBOTTOM;
				bool within_UHeap = fault_va < USER_HEAP_MAX && fault_va >= USER_HEAP_START;

				// If it is a stack or a heap page, then, it’s OK.
				if (within_UHeap || within_UStack)
					pf_add_empty_env_page(faulted_env, fault_va, 0);
				
				// Else, it must be rejected without harm to the kernel or other running processes, by exiting the process.
				else {
					unmap_frame(faulted_env->env_page_directory, fault_va);
					env_exit();
				}
			}
			
			// Reflect the changes in the page working set list (i.e. add new element to list & update its last one)
			struct WorkingSetElement *wsElement = env_page_ws_list_create_element(faulted_env, fault_va);
			LIST_INSERT_TAIL(&(faulted_env->page_WS_list), wsElement);
			faulted_env->page_last_WS_element = wsElement;
		}
		else
		{
			if (isPageReplacmentAlgorithmCLOCK())
			{
				//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #3 Clock Replacement
				// start from page_last_WS_element (clock hand)
				struct WorkingSetElement *current = faulted_env->page_last_WS_element;
				if (current == NULL)
					current = LIST_FIRST(&(faulted_env->page_WS_list));

				// find victim using CLOCK algorithm
				while (1)
				{
					uint32 perms = pt_get_page_permissions(faulted_env->env_page_directory, current->virtual_address);

					if (perms & PERM_USED)
					{
						// used bit is 1 now clear it and move to next
						pt_set_page_permissions(faulted_env->env_page_directory, current->virtual_address, 0, PERM_USED);

						// move to next element (circular)
						current = LIST_NEXT(current);
						if (current == NULL)
							current = LIST_FIRST(&(faulted_env->page_WS_list));
					}
					else
					{
						// found victim (use bit = 0)
						victimWSElement = current;
						break; // then stop the loop
					}
				}

				// get victim's virtual address
				uint32 victimVA = victimWSElement->virtual_address;

				// check if victim is modified if modified write back to disk
				// this is for better performance (if not modified it won't do useless writing to disk)
				uint32 victimPerms = pt_get_page_permissions(faulted_env->env_page_directory, victimVA);
				if (victimPerms & PERM_MODIFIED)
				{
					uint32 *ptr_table = NULL;
					struct FrameInfo *victimFrame = get_frame_info(faulted_env->env_page_directory, victimVA, &ptr_table);
					pf_update_env_page(faulted_env, victimVA, victimFrame);
				}

				// unmap the victim frame
				unmap_frame(faulted_env->env_page_directory, victimVA);

				// allocate new frame for faulted page
				int perms = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);
				if (perms == -1) perms = 0;
				perms |= PERM_PRESENT | PERM_USER;

				int ret = alloc_page(faulted_env->env_page_directory, fault_va, perms, 0);
				if (ret == E_NO_MEM)
					panic("CLOCK: No memory for allocating a new frame");

				// aead faulted page from disk
				ret = pf_read_env_page(faulted_env, (void*)fault_va);
				if (ret == E_PAGE_NOT_EXIST_IN_PF)
				{
					bool within_UStack = fault_va < USTACKTOP && fault_va >= USTACKBOTTOM;
					bool within_UHeap = fault_va < USER_HEAP_MAX && fault_va >= USER_HEAP_START;
					if (within_UHeap || within_UStack)
						pf_add_empty_env_page(faulted_env, fault_va, 0);
					else
					{
						unmap_frame(faulted_env->env_page_directory, fault_va);
						env_exit();
					}
				}

				// update the WS element with new VA (reuse the element)
				victimWSElement->virtual_address = ROUNDDOWN(fault_va, PAGE_SIZE);
				victimWSElement->sweeps_counter = 0;

				// update page_last_WS_element to next element (circular)
				struct WorkingSetElement *nextElem = LIST_NEXT(victimWSElement);
				if (nextElem == NULL)
					nextElem = LIST_FIRST(&(faulted_env->page_WS_list));
				faulted_env->page_last_WS_element = nextElem;
			}
			else if (isPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX))
			{
				//TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #2 LRU Aging Replacement
				//Your code is here
				//Comment the following line
				panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
			}
			else if (isPageReplacmentAlgorithmModifiedCLOCK())
			{
				//TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #3 Modified Clock Replacement
				//Your code is here
				//Comment the following line
				panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
			}
		}
	}
#endif
}

void __page_fault_handler_with_buffering(struct Env * curenv, uint32 fault_va)
{
	panic("this function is not required...!!");
}

