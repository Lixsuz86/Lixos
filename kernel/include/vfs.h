#ifndef LIXOS_VFS_H
#define LIXOS_VFS_H

#include <stdint.h>
#include <stddef.h>

// File open flags (POSIX-compatible)
constexpr uint32_t O_RDONLY    = 0x0000;
constexpr uint32_t O_WRONLY    = 0x0001;
constexpr uint32_t O_RDWR      = 0x0002;
constexpr uint32_t O_CREAT     = 0x0040;
constexpr uint32_t O_TRUNC     = 0x0200;
constexpr uint32_t O_APPEND    = 0x0400;
constexpr uint32_t O_DIRECTORY = 0x10000;

// Seek whence
constexpr int SEEK_SET = 0;
constexpr int SEEK_CUR = 1;
constexpr int SEEK_END = 2;

// Node Types
enum class vfs_node_type : uint32_t {
    FILE        = 0x01,
    DIRECTORY   = 0x02,
    CHARDEVICE  = 0x03,
    BLOCKDEVICE = 0x04,
    PIPE        = 0x05,
    SYMLINK     = 0x06,
    SOCKET      = 0x07,
    MOUNTPOINT  = 0x08
};

// Directory entry structure (for readdir)
struct vfs_dirent {
    uint32_t      inode;
    vfs_node_type type;
    char          name[128];
};

// File stat structure
struct vfs_stat_t {
    uint32_t      inode;
    vfs_node_type type;
    uint64_t      size;
    uint16_t      mode;
    uint32_t      uid;
    uint32_t      gid;
    uint64_t      atime;
    uint64_t      mtime;
    uint64_t      ctime;
};

// Forward declaration
struct vfs_node;

// VFS Operations structure (like Linux file_operations / inode_operations)
struct vfs_operations {
    int64_t     (*read)(vfs_node *node, uint64_t offset, size_t size, void *buffer);
    int64_t     (*write)(vfs_node *node, uint64_t offset, size_t size, const void *buffer);
    int         (*open)(vfs_node *node, uint32_t flags);
    int         (*close)(vfs_node *node);
    int         (*readdir)(vfs_node *node, uint32_t index, vfs_dirent *out_dirent);
    vfs_node *  (*finddir)(vfs_node *node, const char *name);
    vfs_node *  (*create)(vfs_node *parent, const char *name, uint16_t mode);
    vfs_node *  (*mkdir)(vfs_node *parent, const char *name, uint16_t mode);
    int         (*truncate)(vfs_node *node, uint64_t new_size);
};

// Generic VFS Inode / Node representation
struct vfs_node {
    char            name[128];
    vfs_node_type   type;
    uint16_t        mode;
    uint32_t        uid;
    uint32_t        gid;
    uint32_t        inode;
    uint64_t        length;
    uint32_t        ref_count;
    
    // File system driver operations
    vfs_operations *ops;
    
    // Pointer to private filesystem-specific inode data (ext2_inode, ramfs_entry, btrfs_item, etc.)
    void           *private_data;
    
    // Pointer to mounted filesystem root (if this node is a mountpoint)
    vfs_node       *ptr_mount;
};

// File Descriptor representation
struct file_descriptor {
    vfs_node *node;
    uint64_t  offset;
    uint32_t  flags;
    bool      in_use;
};

// Core VFS Management APIs
void vfs_init(void);

// Mount management
int vfs_mount(const char *mount_point, vfs_node *fs_root);
int vfs_unmount(const char *mount_point);
vfs_node *vfs_get_root(void);

// Path Lookup
vfs_node *vfs_lookup(const char *path);

// File Descriptor based high-level I/O APIs (used by Kernel & Syscalls)
int      vfs_open(const char *path, uint32_t flags, uint16_t mode = 0644);
int64_t  vfs_read(int fd, void *buffer, size_t count);
int64_t  vfs_write(int fd, const void *buffer, size_t count);
int64_t  vfs_lseek(int fd, int64_t offset, int whence);
int      vfs_close(int fd);
int      vfs_readdir(int fd, uint32_t index, vfs_dirent *out_dirent);
int      vfs_allocate_fd(vfs_node *node, uint32_t flags);
vfs_node *vfs_get_node(int fd);
int      vfs_stat(const char *path, vfs_stat_t *st);
int      vfs_mkdir(const char *path, uint16_t mode = 0755);

#endif // LIXOS_VFS_H
