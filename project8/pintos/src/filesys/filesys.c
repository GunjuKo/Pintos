#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <threads/malloc.h>
#include "threads/thread.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/buffer_cache.h"
/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  bc_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
  thread_current()->thread_dir = dir_open_root();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  bc_term ();	
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  char *cp_name   = malloc( sizeof(char) * (strlen(name) + 1) );
  char *file_name = malloc( sizeof(char) * (strlen(name) + 1) ); 
  if( cp_name == NULL || file_name == NULL)
	  return false;
  strlcpy(cp_name, name, strlen(name)+1);
  block_sector_t inode_sector = 0;
  struct dir *dir = parse_path (cp_name, file_name);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, IS_FILE)
                  && dir_add (dir, file_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);
  free(cp_name);
  free(file_name);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  char *cp_name;
  char *file_name;
  struct dir *dir;
  struct inode *inode = NULL;

  cp_name = malloc( sizeof(char) * (strlen(name)+1) );
  if(cp_name == NULL)
	  return NULL;
  file_name = malloc( sizeof(char) * (strlen(name)+1) );
  if(file_name == NULL){
	  free(cp_name);
	  return NULL;
  }
  /* copy name to cp_name */
  strlcpy(cp_name, name, strlen(name)+1 );
  dir = parse_path (cp_name, file_name);
  if (dir != NULL)
    dir_lookup (dir, file_name, &inode);
  dir_close (dir);

  free(file_name);
  free(cp_name);
  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  char *cp_name;
  char *file_name;

  if(strcmp(name,"/")==0)
	  return false;
  cp_name = malloc( sizeof(char) * (strlen(name)+1) );
  file_name = malloc( sizeof(char) * (strlen(name)+1) );

  if(cp_name == NULL || file_name == NULL)
	  return false;
  strlcpy(cp_name, name, strlen(name)+1);

  struct dir *dir = parse_path (cp_name, file_name);
  bool success = dir != NULL && dir_remove (dir, file_name);
  dir_close (dir); 

  free(file_name);
  free(cp_name);

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  struct dir *root_dir = dir_open_root();
  struct inode *root_inode = dir_get_inode(root_dir); 
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  /* add . and .. entry to root directory */
  dir_add(root_dir, ".", inode_get_inumber(root_inode));
  dir_add(root_dir,"..", inode_get_inumber(root_inode));
  free_map_close ();
  printf ("done.\n");
}
/* parsing the path, and return working directory */
struct dir* parse_path(char *path_name, char *file_name)
{
	struct dir *dir;
	char *token, *next_token, *save_ptr;
	struct inode *inode;
	if(path_name == NULL || file_name == NULL)
		return NULL;
	if(strlen(path_name) == 0)
		return NULL;
	/* if path name is absulute path */
	if(path_name[0] == '/')
		dir = dir_open_root();
	else /* path name is relative path */
		dir = dir_reopen(thread_current()->thread_dir);
	token = strtok_r(path_name, "/", &save_ptr);
	next_token = strtok_r(NULL, "/", &save_ptr);
	while(token != NULL && next_token != NULL)
	{
		/* find file named token in dir directory */
		if(dir_lookup(dir, token, &inode) == false)
		{
			return NULL;
		}
		/* if token is not a directory */
		if(inode_is_dir(inode) == false){
			dir_close(dir);
			return NULL;
		}
		/* close dir */
		dir_close(dir);
		/* set dir */
		dir = dir_open(inode);
		/* advance */
		token = next_token;
		next_token = strtok_r(NULL, "/", &save_ptr);
	}
	/* copy token to file_name */
	strlcpy(file_name, token, strlen(token)+1);
	return dir;
}
/* create directory file */
bool filesys_create_dir(const char *name)
{
	struct dir *parent_dir;
	struct inode *parent_inode;
	struct inode *tmp;
	struct dir *new_dir;
	bool result = false;
	/* if dir name is NULL, return false*/
	if(name == NULL)
		return result;
	/* copy name to cp_name */
	char *cp_name = malloc( sizeof(char) * (strlen(name)+1) );
	strlcpy(cp_name, name, strlen(name)+1 );
	char *file_name;
	file_name = malloc( sizeof(char) * (strlen(name)+1) );
	if(file_name == NULL)
	{
		free(cp_name);
		return result;
	}
	parent_dir = parse_path(cp_name, file_name);
	/* if already same name file exist in directory, return false*/
	if(dir_lookup(parent_dir, file_name, &tmp) == true)
		return result;
	/* allocate bitmap */
	block_sector_t sector_idx;
	free_map_allocate(1, &sector_idx);
	/* create directory */
	dir_create(sector_idx, 16);
	/* add new entry to parent directory */
	dir_add(parent_dir, file_name, sector_idx);
	/* add entry '.' and '..' to directory */
	new_dir = dir_open( inode_open(sector_idx) ); 
	dir_add(new_dir,".",sector_idx);
	parent_inode = dir_get_inode(parent_dir);
	dir_add(new_dir,"..", inode_get_inumber(parent_inode));

	free(cp_name);
	free(file_name);
	result = true;
	return result;
}
