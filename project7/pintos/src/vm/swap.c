#include "vm/file.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "devices/block.h"
#include "threads/synch.h"
#include "lib/kernel/bitmap.h"

struct lock swap_lock;
struct bitmap *swap_map;
struct block *swap_block;

void swap_init(void)
{
	/* get swap_block. */
	swap_block = block_get_role(BLOCK_SWAP);
	if(swap_block == NULL)
		return;
	/* create bitmap */
	swap_map = bitmap_create(block_size(swap_block) / SECTORS_PER_PAGE );
	if(swap_map == NULL)
		return;
	/* initialize bitmap */
	bitmap_set_all(swap_map, SWAP_FREE);
	/* initialize lock value */
	lock_init(&swap_lock);
}

void swap_in(size_t used_index, void* kaddr)
{
	int i;
	lock_acquire(&swap_lock);
	
	/* check if used_index is empty slot */
	if(bitmap_test(swap_map, used_index) == SWAP_FREE)
		return;
	/* read from swap disk to physical memory */
	for(i=0; i<SECTORS_PER_PAGE; i++)
	{
		block_read(swap_block, used_index * SECTORS_PER_PAGE + i, (uint8_t *)kaddr + i * BLOCK_SECTOR_SIZE);
	}
	/* change bitmap 1 to 0 */
	bitmap_flip(swap_map, used_index);
	lock_release(&swap_lock);
}

size_t swap_out(void *kaddr)
{
	int i;
	size_t free_index;
	lock_acquire(&swap_lock);

	/* find SWAP_FREE index. if there is no SWAP_FREE index, return*/
	free_index = bitmap_scan_and_flip(swap_map, 0, 1, SWAP_FREE);
	if(free_index == BITMAP_ERROR)
		return BITMAP_ERROR;
	/* write to swap disk */
	for(i=0; i<SECTORS_PER_PAGE; i++)
	{
		block_write(swap_block, free_index * SECTORS_PER_PAGE + i, (uint8_t *)kaddr + i * BLOCK_SECTOR_SIZE);
	}

	lock_release(&swap_lock);
	return free_index;
}
