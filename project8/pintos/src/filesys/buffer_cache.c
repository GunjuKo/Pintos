#include <threads/malloc.h>
#include <stdio.h>
#include <string.h>
#include "filesys/buffer_cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"

/* This value is address of buffer cache */
void *p_buffer_cache;   
/* Array of struct buffer_head.*/
struct buffer_head head_buffer[BUFFER_CACHE_ENTRY_NB];
/* This value is made for clock Algorithm */
int clock_hand;


static void locate_byte(off_t pos, struct sector_location *sec_loc);
static inline off_t map_table_offset(int index);
static bool register_sector(struct inode_disk *inode_disk, block_sector_t new_sector, struct sector_location sec_loc);

/* get disk from inode */
bool get_disk_inode(const struct inode *inode, struct inode_disk *inode_disk)
{
	bc_read(inode->sector,(void *)inode_disk, 0, SECTOR_SIZE, 0);
	return true;
}
/* */
static void locate_byte(off_t pos, struct sector_location *sec_loc)
{
	off_t pos_sector = pos / SECTOR_SIZE;

	/* if direct */
	if(pos_sector < DIRECT_BLOCK_ENTRIES)
	{
		/* update sec_loc value */
		sec_loc->directness = NORMAL_DIRECT;
		/* index1 is used for index of direct_map_table and index2 is not used */
		sec_loc->index1    = pos_sector;
		sec_loc->index2    = -1;
	}
	/* if Indirect */
	else if(pos_sector < (off_t)(DIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES) )
	{
		/* update sec_loc value */
		sec_loc->directness = INDIRECT;
		sec_loc->index1     = pos_sector - DIRECT_BLOCK_ENTRIES;
		/* index2 is not used */
		sec_loc->index2     = -1;
	}
	/* if double indirect */
	else if(pos_sector < (off_t)( DIRECT_BLOCK_ENTRIES + ( INDIRECT_BLOCK_ENTRIES * (INDIRECT_BLOCK_ENTRIES + 1) ) ) )
	{
		/* update sec_loc value */
		sec_loc->directness = DOUBLE_INDIRECT;
		sec_loc->index1     = (pos_sector - DIRECT_BLOCK_ENTRIES - INDIRECT_BLOCK_ENTRIES)/128;
		sec_loc->index2     = (pos_sector - DIRECT_BLOCK_ENTRIES - INDIRECT_BLOCK_ENTRIES)%128;
	}
	/* wrong offset */
	else
	{
		printf("wrong offset!\n");
		sec_loc->directness = OUT_LIMIT;
	}
}
/* change index to byte offset */
static inline off_t map_table_offset(int index)
{
	return index*4;
}
/* update new disk block number to inode_disk */
static bool register_sector(struct inode_disk *inode_disk, block_sector_t new_sector, struct sector_location sec_loc)
{
	switch(sec_loc.directness)
	{
		case NORMAL_DIRECT:
			/* update new disk block */
			inode_disk->direct_map_table[sec_loc.index1] = new_sector;
			break;
		case INDIRECT:
			/* if use indirect first, allocate disk for indirect index block */
			if(sec_loc.index1 == 0)
			{
				block_sector_t sector_idx;
				if(free_map_allocate(1, &sector_idx))
					inode_disk->indirect_block_sec = sector_idx;
			}
			/* allocate new_block and write new_sector to new_block */
			block_sector_t *new_block = (block_sector_t *)malloc(SECTOR_SIZE);
			if(new_block == NULL)
				return false;
			new_block[sec_loc.index1] = new_sector;
			/* write new_block to index block */
	   bc_write(inode_disk->indirect_block_sec, (void *)new_block,map_table_offset(sec_loc.index1) ,4 ,map_table_offset(sec_loc.index1));
            free(new_block);
			break;
		case DOUBLE_INDIRECT:
			/* if index1 and index2 is 0, allocate disk for index block 1 */
			if(sec_loc.index1 == 0 && sec_loc.index2 ==0)
			{
				block_sector_t sector_idx;
				if(free_map_allocate(1, &sector_idx))
					inode_disk->double_indirect_block_sec = sector_idx;
			}
			/* if index2 is 0, allocate disk for index block 2 and update index block 1 */
			if(sec_loc.index2 == 0 )
			{
				/* allocate disk for index block 2 */
				block_sector_t sector_idx;
				if(free_map_allocate(1, &sector_idx) == false)
					return false;
				/* update index block 1*/
				block_sector_t *new_block = (block_sector_t *)malloc(SECTOR_SIZE);
				if(new_block == NULL)
					return false;
				new_block[sec_loc.index1] = sector_idx;
				bc_write(inode_disk->double_indirect_block_sec, (void *)new_block, sec_loc.index1 * 4, 4, sec_loc.index1 * 4);
				free(new_block);
			}
			/* update index block 2 */
			/* Value for read index_block 1 */
			block_sector_t *index_block1 = (block_sector_t *)malloc(SECTOR_SIZE);
			if(index_block1 == NULL)
				return false;
			/* Value for update index_block 2 */
			block_sector_t *new_block2    = (block_sector_t *)malloc(SECTOR_SIZE);
			if(new_block2 == NULL)
				return false;
			/* read index block 1 */
			bc_read(inode_disk->double_indirect_block_sec, (void *)index_block1, 0, SECTOR_SIZE, 0);
			block_sector_t block_sector = index_block1[sec_loc.index1];
			/* update index block 2*/
			new_block2[sec_loc.index2] = new_sector;
			bc_write(block_sector, (void *)new_block2, sec_loc.index2 * 4, 4, sec_loc.index2 * 4);
			free(new_block2);
			free(index_block1);
			break;
		default:
			return false;
	}
	return true;
}
/* chage byte offset to disk sector */
block_sector_t byte_to_sector(const struct inode_disk *inode_disk, off_t pos)
{
	block_sector_t result_sec;
	if(pos < inode_disk->length)
	{
		struct inode_indirect_block *ind_block;
		struct inode_indirect_block *second_ind_block;
		struct sector_location sec_loc;
		/* get index1,index2 and directness */
		locate_byte(pos,&sec_loc);

		switch(sec_loc.directness)
		{
			/* direct */
			case NORMAL_DIRECT:
				    result_sec = inode_disk->direct_map_table[sec_loc.index1];
					break;
			/* indirect */
			case INDIRECT:
					ind_block = (struct inode_indirect_block *)malloc(SECTOR_SIZE);
					/* read index1 block from disk */
					if(ind_block != NULL)
					{
						/* read index block1 */
						bc_read(inode_disk->indirect_block_sec, (void *)ind_block, 0, SECTOR_SIZE, 0);
						/* get the disk sector_number, which located index1 */
						result_sec = ind_block->map_table[sec_loc.index1];
						free(ind_block);
					}
					else
						result_sec = 0;
					break;
			case DOUBLE_INDIRECT:
					ind_block = (struct inode_indirect_block *)malloc(SECTOR_SIZE);
					second_ind_block = (struct inode_indirect_block *)malloc(SECTOR_SIZE);
					/* read index block1 and index block2 from disk. and get disk block number */
					if(ind_block != NULL && second_ind_block != NULL)
					{
						/* read index block1 */
						bc_read(inode_disk->double_indirect_block_sec, (void *)ind_block, 0, SECTOR_SIZE, 0);
						/* read_index block2 */
						bc_read(ind_block->map_table[sec_loc.index1] ,(void *)second_ind_block, 0, SECTOR_SIZE, 0);
						result_sec = second_ind_block->map_table[sec_loc.index2];
						free(ind_block);
						free(second_ind_block);
					}
					else
						result_sec = 0;
					break;
			case OUT_LIMIT:
					printf("OUT LIMIT!\n");
					result_sec = 0;
					break;
		}
	}
	else
		result_sec = 0;
	return result_sec;
}
/* if offset is bigger than file_length, allocate new disk and update inode */
bool inode_update_file_length(struct inode_disk *inode_disk, off_t start_pos, off_t end_pos)
{
	off_t size = end_pos - start_pos;
	off_t offset = start_pos;
	void *zeros = malloc(SECTOR_SIZE);
	int chunck_size;
	/* make zeros */
	if(zeros == NULL)
		return false;
	memset(zeros, 0, SECTOR_SIZE);
	while( size > 0 )
	{
		/* offset in the disk block */
		int sector_ofs = offset % SECTOR_SIZE;
		/* caculate chunck size */
		if( size >= SECTOR_SIZE)
			chunck_size = SECTOR_SIZE - sector_ofs;
		else
		{
			if(sector_ofs + size > SECTOR_SIZE)
				chunck_size = SECTOR_SIZE - sector_ofs;
			else
				chunck_size = size;
		}
		/* if sector_ofs is bigger than zero, already allocate disk block */
		if(sector_ofs > 0 )
		{
		}
		/* allocate new disk block */
		else
		{
			struct sector_location sec_loc;
			block_sector_t sector_idx;
			/* allocate new disk block */
			if(free_map_allocate(1, &sector_idx) == true)
			{
				/* update disk block number */
				locate_byte(offset, &sec_loc);
				register_sector(inode_disk, sector_idx, sec_loc);
			}
			else
			{
				free(zeros);
				return false;
			}
			/* init new disk block to 0 */
			bc_write(sector_idx, zeros, 0, SECTOR_SIZE, 0);
		}
		/* advance */
		size   -= chunck_size;
		offset += chunck_size;
	}
	free(zeros);
	return true;
}
void free_inode_sectors(struct inode_disk *inode_disk)
{
	/* double indirect block release */
	if(inode_disk->double_indirect_block_sec > 0)
	{ 
		/* read index block 1*/
		int i = 0;
		struct inode_indirect_block *index_block1;
		index_block1 = (struct inode_indirect_block *)malloc(SECTOR_SIZE);
		if(index_block1 == NULL)
			return;
		bc_read(inode_disk->double_indirect_block_sec, (void *)index_block1, 0, SECTOR_SIZE, 0);
		/* approach index block 2(index i) by index block 1 */
		while(index_block1->map_table[i] > 0)
		{
			int j = 0;
			struct inode_indirect_block *index_block2;
			/* read index block 2 */
			index_block2 = (struct inode_indirect_block *)malloc(SECTOR_SIZE);
			if(index_block2 == NULL)
				return;
			bc_read(index_block1->map_table[i], (void *)index_block2, 0, SECTOR_SIZE, 0);
			/* free disk block which stored in index block 2*/
			while(index_block2->map_table[j] > 0)
			{
				free_map_release(index_block2->map_table[j], 1);
				j++;
			}
			/* free index block 2*/
			free_map_release(index_block1->map_table[i], 1);
			i++;
		}
		/* free index block 1 */
		free_map_release(inode_disk->double_indirect_block_sec, 1);
	}
	/* indirect block release */
	if(inode_disk->indirect_block_sec > 0 )
	{
		int i = 0;
		struct inode_indirect_block *index_block1;
		index_block1 = (struct inode_indirect_block *)malloc(SECTOR_SIZE);
		if(index_block1 == NULL)
			return;
		bc_read(inode_disk->indirect_block_sec, (void *)index_block1, 0, SECTOR_SIZE, 0);
		/* free disk block which stored in index block 1 */
		while(index_block1->map_table[i] > 0)
		{
			free_map_release(index_block1->map_table[i], 1);
			i++;
		}
		/* free index block 1 */
		free_map_release(inode_disk->indirect_block_sec, 1);
	}
	int i = 0;
	/* free direct block release */
	while(inode_disk->direct_map_table[i] > 0 )
	{
		free_map_release(inode_disk->direct_map_table[i], 1);
		i++;
	}
}
/* allocating the buffer_cache memory and init the head_buffer */
void bc_init(void)
{
	unsigned i;
	/* allocating the buffer_cache memory */
	p_buffer_cache = malloc(SECTOR_SIZE * BUFFER_CACHE_ENTRY_NB);
	if(p_buffer_cache == NULL)
	{
		printf("allocating the buffer_cache is failed\n");
		return;
	}
	/* initialize the head_buffer */
	clock_hand = 0;
	for(i=0; i<BUFFER_CACHE_ENTRY_NB; i++)
	{
		head_buffer[i].dirty     = false;
		head_buffer[i].is_used   = false;
		head_buffer[i].clock_bit = false;
		head_buffer[i].data      = p_buffer_cache + SECTOR_SIZE * i;
		/* init lock valuable */
		lock_init(&head_buffer[i].buffer_lock);
	}
}

/* flush data from buffer cache to disk */
void bc_flush_entry(struct buffer_head *p_flush_entry)
{
	/* write data of buffer cache to disk */
	block_write(fs_device, p_flush_entry->sector, p_flush_entry->data);
	/* updata dirty value */
	p_flush_entry->dirty = false;
}

/* flush all entries of */
void bc_flush_all_entries(void)
{
	unsigned i;
	for(i=0; i<BUFFER_CACHE_ENTRY_NB; i++)
	{
		if(head_buffer[i].is_used == true && head_buffer[i].dirty == true)
			bc_flush_entry(&head_buffer[i]);
	}
}

struct buffer_head* bc_lookup(block_sector_t sector)
{
	unsigned i;
	for(i=0; i<BUFFER_CACHE_ENTRY_NB; i++)
	{
		/* if find buffer_cache entry */
		if(head_buffer[i].is_used == true && head_buffer[i].sector == sector){
			return &head_buffer[i];
		}
	}
	/* can't find buffer_cache entry */
	return NULL;
}

struct buffer_head* bc_select_victim(void)
{
	unsigned i;
	/* find buffer_cache entry which is not used */
	for(i=0; i<BUFFER_CACHE_ENTRY_NB; i++)
	{
		if(head_buffer[i].is_used == false){
			return &head_buffer[i];
		}
	}
	/* if buffer_cache is fulled. select the victim */
	while(true)
	{
		if(clock_hand == BUFFER_CACHE_ENTRY_NB -1)
			clock_hand = 0;
		else
			clock_hand++;
		/* select the victim */
		if(head_buffer[clock_hand].clock_bit == false)
		{
			/* if dirty, flush to disk */
			if(head_buffer[clock_hand].dirty == true)
			{
				bc_flush_entry(&head_buffer[clock_hand]);
			}
			/* update buffer head */
			head_buffer[clock_hand].sector    = -1;
			head_buffer[clock_hand].is_used   = false;
			head_buffer[clock_hand].clock_bit = false;
			/* return entry */
			return &head_buffer[clock_hand];
		}
		else
		{
			head_buffer[clock_hand].clock_bit = false;
		}
	}
}

void bc_term(void)
{
	/* flush all entries to disk */
	bc_flush_all_entries();
	/* free the buffer cache */	
	free(p_buffer_cache);
}
bool bc_read(block_sector_t sector_idx, void *buffer, off_t bytes_read, int chunck_size, int sector_ofs)
{
	/* find the buffer cache entry of which block_sector_t is equal to sector_idx */
	struct buffer_head *sector_buffer = bc_lookup(sector_idx);
	/* if can't find the buffer cache entry */
	if(sector_buffer == NULL)
	{
		sector_buffer = bc_select_victim();
		/* update buffer_head and read the data from disk */
		sector_buffer->sector    = sector_idx;
		sector_buffer->is_used   = true;
		block_read(fs_device, sector_idx, sector_buffer->data);
	}
	/* updata the clock bit */
	sector_buffer->clock_bit = true;
	/* read data from buffer cache */
	memcpy(buffer + bytes_read, sector_buffer->data + sector_ofs, chunck_size);
	return true;
}
bool bc_write(block_sector_t sector_idx, void *buffer, off_t bytes_written, int chunck_size, int sector_ofs)
{
	/* find the buffer cache entry of which block_sector_t is equal to sector_idx */
	struct buffer_head *sector_buffer = bc_lookup(sector_idx);
	/* if can't find the buffer cache entry */
	if(sector_buffer == NULL)
	{
		sector_buffer = bc_select_victim();
		/* update buffer_head and read the data from disk */
		sector_buffer->sector  = sector_idx;
		sector_buffer->is_used = true;
		block_read(fs_device, sector_idx, sector_buffer->data);
	}
	/* write the data to buffer_cache */
	memcpy(sector_buffer->data + sector_ofs, buffer + bytes_written, chunck_size);
	/* update the */
	lock_acquire(&sector_buffer->buffer_lock);
	sector_buffer->clock_bit = true;
	sector_buffer->dirty     = true;
	lock_release(&sector_buffer->buffer_lock);
	return true;  
}
