#ifndef BUFFER_CACHE_H
#define BUFFER_CACHE_H

#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"
#include "filesys/inode.h"


#define BUFFER_CACHE_ENTRY_NB 64
#define SECTOR_SIZE 512
#define INDIRECT_BLOCK_ENTRIES 128
#define DIRECT_BLOCK_ENTRIES 123

/* structure for buffer_cache */
struct buffer_head
{
	bool dirty;
	bool is_used; 
	bool clock_bit;
	block_sector_t sector;
	void* data;
	struct lock buffer_lock;
};
/* */
enum direct_t {
	NORMAL_DIRECT,
	INDIRECT,
	DOUBLE_INDIRECT,
	OUT_LIMIT
};

struct sector_location
{
	enum direct_t directness;
	off_t index1;
	off_t index2;
};

struct inode_indirect_block
{
	block_sector_t map_table[INDIRECT_BLOCK_ENTRIES];
};

void free_inode_sectors(struct inode_disk *inode_disk);
block_sector_t byte_to_sector(const struct inode_disk *inode_disk, off_t pos);
bool get_disk_inode(const struct inode *inode, struct inode_disk *inode_disk);
bool inode_update_file_length(struct inode_disk *disk, off_t start_pos, off_t end_pos);
void bc_init (void);
void bc_flush_entry (struct buffer_head *p_flush_entry);
void bc_flush_all_entries(void);
struct buffer_head* bc_lookup(block_sector_t sector);
struct buffer_head* bc_select_victim(void);
void bc_term(void);
bool bc_read(block_sector_t sector_idx, void *buffer, off_t bytes_read, int chunck_size, int sector_ofs);
bool bc_write(block_sector_t sector_idx, void *buffer, off_t bytes_written, int chunck_size, int sector_ofs);
#endif
