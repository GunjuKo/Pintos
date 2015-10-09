#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <threads/malloc.h>
#include <threads/palloc.h>
#include "filesys/file.h"
#include "vm/page.h"
#include "vm/file.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "lib/kernel/list.h"

static bool vm_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);
static unsigned vm_hash_func(const struct hash_elem *e, void *aux UNUSED);
static void vm_destroy_func(struct hash_elem *e, void *aux UNUSED);

/* if a's vm_entry adress is less than b's vm_entry address return true */
static bool vm_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
	struct vm_entry *vme_a = hash_entry(a, struct vm_entry, elem);
	struct vm_entry *vme_b = hash_entry(b, struct vm_entry, elem);

	if(vme_a->vaddr < vme_b->vaddr)
		return true;
	else 
		return false;
}

/* define hash function */
static unsigned vm_hash_func(const struct hash_elem *e, void *aux UNUSED)
{
	struct vm_entry *vme = hash_entry(e, struct vm_entry, elem);
	return hash_int((int)vme->vaddr);
}

static void vm_destroy_func(struct hash_elem *e, void *aux UNUSED)
{
	struct vm_entry *vme = hash_entry(e, struct vm_entry, elem);
	void *physical_address;
	/* if virtual address is loaded on physical memory */
	if(vme->is_loaded == true)
	{
		/*get physical_address and free page */
		physical_address = pagedir_get_page(thread_current()->pagedir, vme->vaddr);
		free_page(physical_address);
		/* clear page table */
		pagedir_clear_page(thread_current()->pagedir, vme->vaddr);
	}
	/* free vm_entry */
	free(vme);
}

void vm_init(struct hash *vm)
{
	hash_init(vm, vm_hash_func, vm_less_func, NULL);
}

void vm_destroy(struct hash *vm)
{
	hash_destroy(vm, vm_destroy_func);
}
/* find vm_entry using virtual address */
struct vm_entry *find_vme(void *vaddr)
{
	struct vm_entry vme;
	struct hash_elem *element;
	/* try to find vm_entry by hash_find*/
	vme.vaddr = pg_round_down(vaddr);
	element = hash_find(&thread_current()->vm, &vme.elem);
	/* if get a element return vm_entry */
	if(element != NULL)
	{
		return hash_entry(element, struct vm_entry, elem);
	}
	return NULL;
}

bool insert_vme(struct hash *vm, struct vm_entry *vme)
{
	bool result = false;
	/* if hash_insert is success, return true */
	if(hash_insert(vm, &vme->elem) == NULL)
		result = true;
	return result;
}

bool delete_vme(struct hash *vm, struct vm_entry *vme)
{
	bool result = false;
	/* if hash_delete is success, return true */
	if(hash_delete(vm, &vme->elem) != NULL)
		result = true;
	free(vme);
	return result;   
}

bool load_file(void *kaddr, struct vm_entry *vme)
{
	bool result = false;   
	/* file read and if success, return true */
	/* read vm_entry's file to physical memory.*/
	if((int)vme->read_bytes == file_read_at(vme->file, kaddr, vme->read_bytes, vme->offset))
	{
		result = true;
		memset(kaddr + vme->read_bytes, 0, vme->zero_bytes);
	} 
	return result;
}

int file_mmap(int fd, void *addr)
{
	struct thread *cur = thread_current();
	struct mmap_file *mmap_file_entry;
	struct vm_entry *vme;
	struct file *mmap_file;
	uint32_t file_len;
	int32_t offset = 0;
	void *virtual_address = addr;
	size_t page_read_bytes;
	size_t page_zero_bytes;

	/* check addr is valid */
	if((uint32_t)addr%PGSIZE != 0 || addr == NULL)
	{
		return -1;
	}
	mmap_file_entry = malloc(sizeof(struct mmap_file));
	if(mmap_file_entry == NULL)
		return -1;
	/* reopen the file. if fail to reopen, return false */
	mmap_file = file_reopen(process_get_file(fd));
	if(mmap_file == NULL)
	{
		printf("File reopen fail!\n");
		return -1;
	}
	/* init mapid and increase thread_current's mapid */
	cur->mapid += 1;
	mmap_file_entry->mapid = cur->mapid;

	/* initialize mmap_file's vme_list */
	list_init(&(mmap_file_entry->vme_list));

	mmap_file_entry->file = mmap_file;
	file_len = file_length(mmap_file);

	while(file_len > 0)
	{
		vme = malloc(sizeof(struct vm_entry));
		if(vme == NULL)
			return -1;
		/* calculate how to fill vm_entry 
		   we will read page_read_bytes from file
		   and zero the final page_zero_bytes bytes.*/
		page_read_bytes = file_len < PGSIZE ? file_len : PGSIZE;
		page_zero_bytes = PGSIZE - page_read_bytes;
		/* initialize vm_entry */
		vme->type      = VM_FILE;
		vme->vaddr     = virtual_address;
		vme->writable  = true;
		vme->is_loaded = false;
		vme->pinned    = false;
		vme->file      = mmap_file;
		vme->offset    = offset;
		vme->read_bytes= page_read_bytes;
		vme->zero_bytes= page_zero_bytes;
		/* insert vm_entry to hash. if fail, return false */
		if(insert_vme(&cur->vm, vme) == false)
		{
			return -1;
		}
		/* insert vm_entry to mmap_file */
		list_push_back(&(mmap_file_entry->vme_list), &(vme->mmap_elem));
		/* advance */
		file_len -= page_read_bytes;
		offset += page_read_bytes;
		virtual_address += PGSIZE;
	}
	/* insert mmap_file to thread_current()'s mmap_list */
	list_push_back(&cur->mmap_list,&mmap_file_entry->elem);
	return cur->mapid;
}

void file_munmap(int mapping)
{
	struct mmap_file *map_file;
	struct thread *cur = thread_current();
	struct list_elem *element;
	struct list_elem *tmp;
	/* find mmap_file which mapid is equal to mapping */
	for(element = list_begin(&cur->mmap_list) ; element != list_end(&cur->mmap_list) ; element = list_next(element))
	{
		map_file = list_entry(element, struct mmap_file, elem);
		/* if mapping is CLOSE_ALL, close all map_file.
		   find mmap_file's mapid is equal to mapping and remove mmap_file from mmap_list. */
		if(mapping == CLOSE_ALL || map_file->mapid == mapping)
		{
			do_munmap(map_file);
			/* close file */
			file_close(map_file->file);
			/* remove from mmap_list */
			tmp = list_prev(element);
			list_remove(element);
			element = tmp;
			/* free the mmap_file */
			free(map_file);
			if(mapping != CLOSE_ALL)
				break;
		}
	}
}

void do_munmap(struct mmap_file *mmap_file)
{
	struct thread *cur = thread_current();
	struct list_elem *element;
	struct list_elem *tmp;
	struct list *vm_list = &(mmap_file->vme_list);
	struct vm_entry *vme;
	void *physical_address;
	/* remove all vm_entry */
	for(element = list_begin(vm_list); element != list_end(vm_list); element = list_next(element))
	{
		vme = list_entry(element, struct vm_entry, mmap_elem);
		/* if vm_entry is loaded to physical memory */
		if(vme->is_loaded == true)
		{
			physical_address = pagedir_get_page(cur->pagedir, vme->vaddr);
			/* if vm_entry's physical memory is dirty, write to disk */
			if(pagedir_is_dirty(cur->pagedir, vme->vaddr) == true)
			{
				lock_acquire(&file_lock);
				file_write_at(vme->file, vme->vaddr, vme->read_bytes, vme->offset);
				lock_release(&file_lock);
			}
			/* clear page table */
			pagedir_clear_page(cur->pagedir, vme->vaddr);
			/* free physical memory */
			free_page(physical_address);
		}
		/* remove from vme_list*/
		tmp = list_prev(element);
		list_remove(element);
		element = tmp;
		/* delete vm_entry from hash and free */
		delete_vme(&cur->vm, vme);
	}
}
