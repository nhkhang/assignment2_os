
#include "mem.h"
#include "stdlib.h"
#include "string.h"
#include <pthread.h>
#include <stdio.h>

static BYTE _ram[RAM_SIZE];

static struct
{
	uint32_t proc; // ID of process currently uses this page
	int index;	   // Index of the page in the list of pages allocated
				   // to the process.
	int next;	   // The next page in the list. -1 if it is the last
				   // page.
} _mem_stat[NUM_PAGES];

static pthread_mutex_t mem_lock;

void init_mem(void)
{
	memset(_mem_stat, 0, sizeof(*_mem_stat) * NUM_PAGES);
	memset(_ram, 0, sizeof(BYTE) * RAM_SIZE);
	pthread_mutex_init(&mem_lock, NULL);
}

/* get offset of the virtual address */
static addr_t get_offset(addr_t addr)
{
	return addr & ~((~0U) << OFFSET_LEN);
}

/* get the first layer index */
static addr_t get_first_lv(addr_t addr)
{
	return addr >> (OFFSET_LEN + PAGE_LEN);
}

/* get the second layer index */
static addr_t get_second_lv(addr_t addr)
{
	return (addr >> OFFSET_LEN) - (get_first_lv(addr) << PAGE_LEN);
}

/* Search for page table table from the a segment table */
static struct page_table_t *get_page_table(
	addr_t index, // Segment level index
	struct seg_table_t *seg_table)
{ // first level table

	/*
	 * TODO: Given the Segment index [index], you must go through each
	 * row of the segment table [seg_table] and check if the v_index
	 * field of the row is equal to the index
	 *
	 * */
	if (seg_table == NULL)
	{
		return NULL;
	}
	int i;
	for (i = 0; i < seg_table->size; i++)
	{
		// Enter your code here
		if ((seg_table->table[i].v_index) == index)
		{
			return seg_table->table[i].pages;
		}
	}
	return NULL;
}

/* Translate virtual address to physical address. If [virtual_addr] is valid,
 * return 1 and write its physical counterpart to [physical_addr].
 * Otherwise, return 0 */
static int translate(
	addr_t virtual_addr,   // Given virtual address
	addr_t *physical_addr, // Physical address to be returned
	struct pcb_t *proc)
{ // Process uses given virtual address

	/* Offset of the virtual address */
	addr_t offset = get_offset(virtual_addr);
	/* The first layer index */
	addr_t first_lv = get_first_lv(virtual_addr);
	/* The second layer index */
	addr_t second_lv = get_second_lv(virtual_addr);
	/* Search in the first level */
	struct page_table_t *page_table = get_page_table(first_lv, proc->seg_table);
	if (page_table == NULL)
	{
		return 0;
	}

	int i;
	for (i = 0; i < page_table->size; i++)
	{
		if (page_table->table[i].v_index == second_lv)
		{
			/* TODO: Concatenate the offset of the virtual addess
			 * to [p_index] field of page_table->table[i] to 
			 * produce the correct physical address and save it to
			 * [*physical_addr]  */
			addr_t p_index = page_table->table[i].p_index; // physical page index
			* physical_addr = (p_index << OFFSET_LEN) | (offset);
			return 1;
		}
	}
	return 0;
}
addr_t alloc_mem(uint32_t size, struct pcb_t *proc)
{
	pthread_mutex_lock(&mem_lock);
	addr_t ret_mem = 0;
	/* 
	 * TODO: Allocate [size] byte in the memory for the process [proc] and 
	 * save the address of the first byte in the allocated memory region to [ret_mem].
	 */

	// Number of pages we will use for this process
	uint32_t num_pages = size / PAGE_SIZE + (size % PAGE_SIZE ? 1 : 0);
	
	// memory available? We could allocate new memory region or not?
	//
	int i = 0;
	int mem_avail = 0;
	int num_free_pages = 0; // count free pages
	for (i = 0; i < NUM_PAGES; i++) {
		if (_mem_stat[i].proc == 0) {
			num_free_pages += 1;
			if (num_free_pages == num_pages) break;
		}
	}
	if (num_free_pages < num_pages) {
		mem_avail = 0;
	}
	else if (proc->bp + num_pages*PAGE_SIZE >= RAM_SIZE) {
		mem_avail = 0;
	}
	else {
		mem_avail = 1;
	}

	if (mem_avail) {
		/* We could allocate new memory region to the process */
		ret_mem = proc->bp;
		// first byte of new memory required for process
		proc->bp += num_pages * PAGE_SIZE;
		// update break pointer for heap segment process
		int count_alloc = 0; // count allocated pages
		int last_allocated_page_index = -1; // use for update field [next] of last allocated page
		int i;
		for (i = 0; i < NUM_PAGES; i++) {
			if (_mem_stat[i].proc) continue; // page is used

			_mem_stat[i].proc = proc->pid; // the page is used by process [proc]
			_mem_stat[i].index = count_alloc; // index in list of allocated pages
			
			if (last_allocated_page_index > -1) { // not initial page, update last page
				_mem_stat[last_allocated_page_index].next = i;
			}
			last_allocated_page_index = i; // update last page

			// Find or Create virtual page table
			addr_t v_address = ret_mem + count_alloc * PAGE_SIZE; // virtual address of this page
			addr_t v_segment = get_first_lv(v_address);
			// printf("+ pid = %d ::: v_segment = %d, v_page = %d\n", 
			// 	proc->pid, v_segment, get_second_lv(v_address));
			struct page_table_t * v_page_table = get_page_table(v_segment, proc->seg_table);
			if (v_page_table == NULL) {
				int idx = proc->seg_table->size;
				proc->seg_table->table[idx].v_index = v_segment;
				v_page_table
					= proc->seg_table->table[idx].pages
					= (struct page_table_t*) malloc(sizeof(struct page_table_t));
				proc->seg_table->size++;
			}
			int idx = v_page_table->size++;
			v_page_table->table[idx].v_index = get_second_lv(v_address);
			v_page_table->table[idx].p_index = i; // format of i is 10 bit segment and page in address

			if (++count_alloc == num_pages) {
				_mem_stat[i].next = -1; // last page in list
				break;
			}
		}	
	}
	// printf("--------------------------------------------\n");
	// printf("Aloccation\n");
	// printf("--------------------------------------------\n");
	// dump();
	// printf("vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv\n");

	pthread_mutex_unlock(&mem_lock);
	return ret_mem;
}
static int remove_page_table(addr_t v_segment, struct seg_table_t *seg_table)
{
	if (seg_table == NULL)
		return 0;
	int i;
	for (i = 0; i < seg_table->size; i++)
	{
		if (seg_table->table[i].v_index == v_segment)
		{
			int idx = seg_table->size - 1;
			seg_table->table[i] = seg_table->table[idx];
			seg_table->table[idx].v_index = 0;
			free(seg_table->table[idx].pages);
			seg_table->size--;
			return 1;
		}
	}
	return 0;
}

void free_break_point(struct pcb_t *proc)
{
	while (proc->bp >= PAGE_SIZE)
	{
		addr_t last_addr = proc->bp - PAGE_SIZE;
		addr_t last_segment = get_first_lv(last_addr);
		addr_t last_page = get_second_lv(last_addr);
		struct page_table_t *page_table = get_page_table(last_segment, proc->seg_table);
		if (page_table == NULL)
			return;
		while (last_page >= 0)
		{
			int i;
			for (i = 0; i < page_table->size; i++)
			{
				if (page_table->table[i].v_index == last_page)
				{
					proc->bp -= PAGE_SIZE;
					last_page--;
					break;
				}
			}
			if (i == page_table->size)
				break;
		}
		if (last_page >= 0)
			break;
	}
}

int free_mem(addr_t address, struct pcb_t *proc)
{
	/*TODO: Release memory region allocated by [proc]. The first byte of
	 * this region is indicated by [address]. Task to do:
	 * 	- Set flag [proc] of physical page use by the memory block
	 * 	  back to zero to indicate that it is free.
	 * 	- Remove unused entries in segment table and page tables of
	 * 	  the process [proc].
	 * 	- Remember to use lock to protect the memory from other
	 * 	  processes.  */
	pthread_mutex_lock(&mem_lock);

	addr_t virtual_addr = address;
	addr_t physical_addr = 0;

	if (!translate(virtual_addr, &physical_addr, proc))
		return 1;

	addr_t physical_segment_page_index = physical_addr >> OFFSET_LEN;
	int num_pages = 0;
	int i;
	for (i = physical_segment_page_index; i != -1; i = _mem_stat[i].next)
	{
		num_pages++;
		_mem_stat[i].proc = 0;
	}

	for (i = 0; i < num_pages; i++)
	{
		addr_t v_addr = virtual_addr + i * PAGE_SIZE;
		addr_t v_segment = get_first_lv(v_addr);
		addr_t v_page = get_second_lv(v_addr);
		struct page_table_t *page_table = get_page_table(v_segment, proc->seg_table);
		if (page_table == NULL)
		{
			continue;
		}
		int j;
		for (j = 0; j < page_table->size; j++)
		{
			if (page_table->table[j].v_index == v_page)
			{
				int last = --page_table->size;
				page_table->table[j] = page_table->table[last];
				break;
			}
		}
		if (page_table->size == 0)
		{
			remove_page_table(v_segment, proc->seg_table);
		}
	}

	if (virtual_addr + num_pages * PAGE_SIZE == proc->bp)
	{
		free_break_point(proc);
	}
	// printf("--------------------------------------------\n");
	// printf("DEALLOCATE\n");
	// printf("--------------------------------------------\n");
	// dump();
	// printf("vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv\n");

	pthread_mutex_unlock(&mem_lock);
	return 0;
}

int read_mem(addr_t address, struct pcb_t *proc, BYTE *data)
{
	addr_t physical_addr;
	if (translate(address, &physical_addr, proc))
	{
		*data = _ram[physical_addr];
		return 0;
	}
	else
	{
		return 1;
	}
}

int write_mem(addr_t address, struct pcb_t *proc, BYTE data)
{
	addr_t physical_addr;
	if (translate(address, &physical_addr, proc))
	{
		_ram[physical_addr] = data;
		return 0;
	}
	else
	{
		return 1;
	}
}

void dump(void)
{
	int i;
	for (i = 0; i < NUM_PAGES; i++)
	{
		if (_mem_stat[i].proc != 0)
		{
			printf("%03d: ", i);
			printf("%05x-%05x - PID: %02d (idx %03d, nxt: %03d)\n",
				   i << OFFSET_LEN,
				   ((i + 1) << OFFSET_LEN) - 1,
				   _mem_stat[i].proc,
				   _mem_stat[i].index,
				   _mem_stat[i].next);
			int j;
			for (j = i << OFFSET_LEN;
				 j < ((i + 1) << OFFSET_LEN) - 1;
				 j++)
			{

				if (_ram[j] != 0)
				{
					printf("\t%05x: %02x\n", j, _ram[j]);
				}
			}
		}
	}
}
