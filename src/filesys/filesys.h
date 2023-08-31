#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/inode.h"
#include "filesys/off_t.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */

/* Block device that contains the file system. */
extern struct block *fs_device;

void filesys_init (bool format);
void filesys_done (void);

bool filesys_create (const char *name, off_t initial_size);
#ifdef FS
void* filesys_open (const char *name, enum inode_type* type);
#else
struct file *filesys_open (const char *name);
#endif
bool filesys_remove (const char *name);

#ifdef FS
bool filesys_chdir (const char *name);
bool filesys_mkdir (const char *name);
#endif

#endif /* filesys/filesys.h */
