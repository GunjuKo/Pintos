#include "vm/file.h"
#include "vm/swap.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include <threads/malloc.h>
#include <stdio.h>
void lru_list_init(void)
{
	/* initialize */
	list_init(&lru_list);
	lock_init(&lru_list_lock);
	lru_clock = NULL;
}

/* add page to lru list */
void add_page_to_lru_list(struct page *page)
{
	if(page != NULL)
	{
     	lock_acquire(&lru_list_lock);
		list_push_back(&lru_list, &page->lru);
		lock_release(&lru_list_lock);
	}
}

/* delete page from lru list */
void del_page_from_lru_list(struct page* page)
{
	if(page != NULL)
	{
		if(lru_clock == page)
		{
			lru_clock = list_entry(list_remove(&page->lru), struct page, lru);
		}
		else
			list_remove(&page->lru);
	}
}

struct page *alloc_page(enum palloc_flags flags)
{
	struct page *new_page;
	void *kaddr;
	if((flags & PAL_USER) == 0)
		return NULL;
	/* allocate physical memory */
	kaddr = palloc_get_page(flags);
	/* if fail, free physical memory and retry physical memory allocate*/
	while(kaddr == NULL)
	{
		try_to_free_pages();
		kaddr = palloc_get_page(flags);
	}
	new_page = malloc(sizeof(struct page));
	if(new_page == NULL)
	{
		palloc_free_page(kaddr);
		return NULL;
	}
	/* initialize page */
	new_page->kaddr  = kaddr;
	new_page->pg_thread = thread_current();
	/* insert page to lru list */
	add_page_to_lru_list(new_page);
	return new_page;
}

void free_page(void *kaddr)
{
	struct list_elem *element;
	struct page *lru_page;
	lock_acquire(&lru_list_lock);
	/* find page */
	for(element = list_begin(&lru_list); element != list_end(&lru_list); element = list_next(element))
	{
		lru_page = list_entry(element, struct page, lru);
		/* if find page, call the __free_page */
		if(lru_page->kaddr == kaddr)
		{
			__free_page(lru_page);
			break;
		}
	}
	lock_release(&lru_list_lock);
}

void __free_page(struct page *page)
{
	/* free physical memory */
	palloc_free_page(page->kaddr);
	/* delete page from lru_list */
	del_page_from_lru_list(page);
	free(page);
}

struct list_elem* get_next_lru_clock(void)
{
	struct list_elem *element;
	/* if lru_clock is NULL */
	if(lru_clock == NULL)
	{
		element = list_begin(&lru_list);
		/* if lru_list is not empty list, return the first of list */
		if(element != list_end(&lru_list))
		{
			lru_clock = list_entry(element, struct page, lru);
			return element;
		}
		else
		{
			return NULL;
		}
	}
	element = list_next(&lru_clock->lru);
	/* if lru_clock page is final page of lru_list */
	if(element == list_end(&lru_list))
	{
		/* if lru_list has only one page */
		if(&lru_clock->lru == list_begin(&lru_list))
		{
			return NULL;
		}
		else
		{
			/* lru_list has more than one page, lru_clock points list begin page */
			element = list_begin(&lru_list);
		}
	}
	lru_clock = list_entry(element, struct page, lru);
	return element;
}

void try_to_free_pages(void)
{
	struct thread *page_thread;
	struct list_elem *element;
	struct page *lru_page;
	lock_acquire(&lru_list_lock);
	if(list_empty(&lru_list) == true)
	{
		lock_release(&lru_list_lock);
		return;
	}
	while(true)
	{
		/* get next element */
		element = get_next_lru_clock();
		if(element == NULL){
			lock_release(&lru_list_lock);
			return;
		}
		lru_page = list_entry(element, struct page, lru);
		if(lru_page->vme->pinned == true)
			continue;
		page_thread = lru_page->pg_thread;
		/* if page address is accessed, set accessed bit 0 and continue(it's not victim) */
		if(pagedir_is_accessed(page_thread->pagedir, lru_page->vme->vaddr))
		{
			pagedir_set_accessed(page_thread->pagedir, lru_page->vme->vaddr, false);
			continue;
		}
		/* if not accessed, it's victim */
		/* if page is dirty */
		if(pagedir_is_dirty(page_thread->pagedir, lru_page->vme->vaddr) || lru_page->vme->type == VM_ANON)
		{
			/* if vm_entry is mmap file, don't call swap out.*/
			if(lru_page->vme->type == VM_FILE)
			{
				lock_acquire(&file_lock);
				file_write_at(lru_page->vme->file, lru_page->kaddr ,lru_page->vme->read_bytes, lru_page->vme->offset);
				lock_release(&file_lock);
			}
			/* if not mmap_file, change type to ANON and call swap_out function */
			else
			{
				lru_page->vme->type = VM_ANON;
				lru_page->vme->swap_slot = swap_out(lru_page->kaddr);
 			}
		}
		lru_page->vme->is_loaded = false;
		pagedir_clear_page(page_thread->pagedir, lru_page->vme->vaddr);
		__free_page(lru_page);
		break;
	}
    lock_release(&lru_list_lock);
	return;
}
