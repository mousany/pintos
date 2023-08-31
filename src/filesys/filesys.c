#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/directory.h"
#include "threads/malloc.h"
#include "threads/thread.h"

#ifdef FS
#include "filesys/cache.h"
#endif

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
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
#ifdef FS
  cache_write_behind(true);
#endif

  free_map_close ();
}

struct path_elem
{
  char name[NAME_MAX + 1];
  struct list_elem elem;
};

static void
cleanup_path (struct list* path_list)
{
  while (!list_empty (path_list))
  {
    struct list_elem* e = list_pop_front (path_list);
    struct path_elem* pe = list_entry (e, struct path_elem, elem);
    free (pe);
  }
  free (path_list);
}

static struct list*
parse_path (const char* path)
{
  struct list* path_list = malloc (sizeof (struct list));
  if (path_list == NULL)
    return NULL;
  list_init (path_list);

  char* path_copy = malloc (strlen (path) + 1);
  if (path_copy == NULL)
  {
    free (path_list);
    return NULL;
  }
  strlcpy (path_copy, path, strlen (path) + 1);

  char* token, *save_ptr;
  for (token = strtok_r (path_copy, "/", &save_ptr); token != NULL;
      token = strtok_r (NULL, "/", &save_ptr))
  {
    if (strlen (token) > NAME_MAX)
    {
      cleanup_path (path_list);
      return NULL;
    }

    struct path_elem* elem = malloc (sizeof (struct path_elem));
    if (elem == NULL)
    {
      cleanup_path (path_list);
      return NULL;
    }
    
    strlcpy (elem->name, token, strlen (token) + 1);
    list_push_back (path_list, &elem->elem);
  }

  free (path_copy);
  return path_list;
}

static struct dir*
open_path (const char* path, struct path_elem** retained)
{
  if (path == NULL || strlen (path) == 0)
    return NULL;

  struct list* path_list;
  struct dir* root;
  if (path[0] == '/')
    {
      root = dir_open_root ();
      path_list = parse_path (path + 1);
    }
  else
    {
      if (thread_current ()->current_dir == NULL)
        root = dir_open_root ();
      else
        root = dir_reopen (thread_current ()->current_dir);
      path_list = parse_path (path);
    }

  if (path_list == NULL || root == NULL)
    {
      dir_close (root);
      return NULL;
    }

  if (retained != NULL)
  {
    struct list_elem* e = list_pop_back (path_list);
    *retained = list_entry (e, struct path_elem, elem);
  }

  struct list_elem* e;
  for (e = list_begin (path_list); e != list_end (path_list);
      e = list_next (e))
  {
    struct path_elem* pe = list_entry (e, struct path_elem, elem);
    struct inode* inode;
    if (!dir_lookup (root, pe->name, &inode) ||
        inode->data.type != INODE_DIR)
    {
      cleanup_path (path_list);
      dir_close (root);
      return NULL;
    }
    dir_close (root);
    root = dir_open (inode);
    if (root == NULL)
    {
      cleanup_path (path_list);
      return NULL;
    }
  }
  
  cleanup_path (path_list);
  return root;
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
#ifdef FS  
  if (name[0] == '/' && strlen (name) == 1)
    return false;

  struct path_elem* pe;
  struct dir *dir = open_path (name, &pe);
  if (dir == NULL)
    return false;
#else
  struct dir *dir = dir_open_root ();
#endif
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (
#ifdef FS                    
                        INODE_FILE,
#endif                  

                        inode_sector, initial_size)

#ifdef FS                        
                  && dir_add (dir, pe->name, inode_sector));
#else
                  && dir_add (dir, name, inode_sector));
#endif
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
#ifdef FS  
  free (pe);
#endif
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
#ifdef FS
void*
filesys_open (const char* name, enum inode_type* type)
#else
struct file *
filesys_open (const char *name)
#endif
{
#ifdef FS
  if (name[0] == '/' && strlen (name) == 1)
    {
      if (type != NULL) *type = INODE_DIR;
      return dir_open_root ();
    }

  struct path_elem* pe;
  struct dir *dir = open_path (name, &pe);
  if (dir == NULL)
    return NULL;
#else
  struct dir *dir = dir_open_root ();
#endif  

  struct inode *inode = NULL;

#ifdef FS
  dir_lookup (dir, pe->name, &inode);
  free (pe);
#else
  if (dir != NULL)
    dir_lookup (dir, name, &inode);
#endif

  dir_close (dir);

#ifdef FS
  if (inode == NULL)
    return NULL;

  if (type != NULL) *type = inode->data.type;
  if (inode->data.type == INODE_FILE)
    return file_open (inode);
  if (inode->data.type == INODE_DIR)
    return dir_open (inode);
  
  NOT_REACHED ();
#else
  return file_open (inode);
#endif
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
#ifdef FS
  if (name[0] == '/' && strlen (name) == 1)
    return false;

  struct path_elem* pe;
  struct dir *dir = open_path (name, &pe);
  if (dir == NULL)
    return false;
#else
  struct dir *dir = dir_open_root ();
#endif

#ifdef FS
  bool success = dir != NULL && dir_remove (dir, pe->name);
  free (pe);
#else
  bool success = dir != NULL && dir_remove (dir, name);
#endif
  dir_close (dir); 

  return success;
}

#ifdef FS
bool 
filesys_chdir (const char *name)
{
  if (name[0] == '/' && strlen (name) == 1)
    {
      if (thread_current ()->current_dir != NULL)
        dir_close (thread_current ()->current_dir);
      thread_current ()->current_dir = dir_open_root ();
      return true;
    }

  struct dir *dir = open_path (name, NULL);
  if (dir == NULL)
    return false;

  if (thread_current ()->current_dir != NULL)
    dir_close (thread_current ()->current_dir);
  thread_current ()->current_dir = dir;
  return true;
}

bool
filesys_mkdir (const char *name)
{
  if (name[0] == '/' && strlen (name) == 1)
    return false;

  struct path_elem* pe;
  struct dir *dir = open_path (name, &pe);
  if (dir == NULL)
    return false;

  block_sector_t inode_sector = 0;
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && dir_create (inode_sector, 16, inode_get_inumber (dir_get_inode (dir)))
                  && dir_add (dir, pe->name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  free (pe);
  dir_close (dir);

  return success;
}
#endif

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16
#ifdef FS  
      , ROOT_DIR_SECTOR
#endif
    ))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
