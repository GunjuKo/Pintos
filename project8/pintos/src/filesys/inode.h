#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"
#include "lib/kernel/list.h"

#define IS_DIR 1
#define IS_FILE 0

struct bitmap;
/* IN MEMORY I-NODE */
struct inode
{
	struct list_elem elem;
	block_sector_t sector;
	int open_cnt;
	bool removed;
	int deny_write_cnt;
	struct lock extend_lock;
};
/* ON DISK I NODE */
struct inode_disk
{
	off_t length;
	unsigned magic;
	uint32_t is_dir;
	block_sector_t direct_map_table[123];
	block_sector_t indirect_block_sec;
	block_sector_t double_indirect_block_sec;
};

void inode_init (void);
bool inode_create (block_sector_t, off_t, uint32_t is_dir);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
bool inode_is_dir(const struct inode *inode);
#endif /* filesys/inode.h */
