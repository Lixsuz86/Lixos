#ifndef LIXOS_RAMFS_H
#define LIXOS_RAMFS_H

#include "vfs.h"

// Initialize and create a new in-memory RAMFS root node
vfs_node *ramfs_create_root(const char *name = "/");

#endif // LIXOS_RAMFS_H
