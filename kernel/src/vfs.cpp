#include "vfs.h"
#include "heap.h"
#include "kprint.h"

namespace {
    constexpr size_t MAX_FDS = 128;
    constexpr size_t MAX_MOUNTS = 16;

    struct mount_entry {
        char      path[128];
        vfs_node *target_node;
        vfs_node *fs_root;
        bool      active;
    };

    vfs_node *g_vfs_root = nullptr;
    file_descriptor g_fd_table[MAX_FDS];
    mount_entry g_mount_table[MAX_MOUNTS];

    size_t k_strlen(const char *s) {
        size_t len = 0;
        while (s && s[len] != '\0') ++len;
        return len;
    }

    void k_strcpy(char *dst, const char *src) {
        size_t i = 0;
        while (src && src[i] != '\0') {
            dst[i] = src[i];
            ++i;
        }
        dst[i] = '\0';
    }

    int k_strcmp(const char *s1, const char *s2) {
        while (*s1 && (*s1 == *s2)) {
            ++s1;
            ++s2;
        }
        return *(const unsigned char *)s1 - *(const unsigned char *)s2;
    }

    int k_strncmp(const char *s1, const char *s2, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            if (s1[i] != s2[i] || s1[i] == '\0') {
                return (unsigned char)s1[i] - (unsigned char)s2[i];
            }
        }
        return 0;
    }
}

void vfs_init(void) {
    g_vfs_root = nullptr;

    for (size_t i = 0; i < MAX_FDS; ++i) {
        g_fd_table[i].node = nullptr;
        g_fd_table[i].offset = 0;
        g_fd_table[i].flags = 0;
        g_fd_table[i].in_use = false;
    }

    for (size_t i = 0; i < MAX_MOUNTS; ++i) {
        g_mount_table[i].active = false;
        g_mount_table[i].fs_root = nullptr;
        g_mount_table[i].target_node = nullptr;
        g_mount_table[i].path[0] = '\0';
    }
}

vfs_node *vfs_get_root(void) {
    return g_vfs_root;
}

int vfs_mount(const char *mount_point, vfs_node *fs_root) {
    if (!mount_point || !fs_root) {
        return -1;
    }

    // Root mount
    if (k_strcmp(mount_point, "/") == 0) {
        g_vfs_root = fs_root;
        return 0;
    }

    // Lookup mount point target directory
    vfs_node *target = vfs_lookup(mount_point);
    if (!target) {
        return -1; // Mount point path not found
    }

    if (target->type != vfs_node_type::DIRECTORY && target->type != vfs_node_type::MOUNTPOINT) {
        return -1; // Mount point must be a directory
    }

    for (size_t i = 0; i < MAX_MOUNTS; ++i) {
        if (!g_mount_table[i].active) {
            k_strcpy(g_mount_table[i].path, mount_point);
            g_mount_table[i].target_node = target;
            g_mount_table[i].fs_root = fs_root;
            g_mount_table[i].active = true;

            target->type = vfs_node_type::MOUNTPOINT;
            target->ptr_mount = fs_root;
            return 0;
        }
    }

    return -1; // Mount table full
}

int vfs_unmount(const char *mount_point) {
    if (!mount_point) {
        return -1;
    }

    if (k_strcmp(mount_point, "/") == 0) {
        g_vfs_root = nullptr;
        return 0;
    }

    for (size_t i = 0; i < MAX_MOUNTS; ++i) {
        if (g_mount_table[i].active && k_strcmp(g_mount_table[i].path, mount_point) == 0) {
            if (g_mount_table[i].target_node) {
                g_mount_table[i].target_node->ptr_mount = nullptr;
                g_mount_table[i].target_node->type = vfs_node_type::DIRECTORY;
            }
            g_mount_table[i].active = false;
            g_mount_table[i].fs_root = nullptr;
            g_mount_table[i].target_node = nullptr;
            return 0;
        }
    }

    return -1; // Not found
}

vfs_node *vfs_lookup(const char *path) {
    if (!path || !g_vfs_root) {
        return nullptr;
    }

    if (k_strcmp(path, "/") == 0) {
        return g_vfs_root;
    }

    vfs_node *current = g_vfs_root;
    size_t i = 0;

    while (path[i] == '/') ++i; // Skip leading slashes

    while (path[i] != '\0') {
        char component[128];
        size_t c_idx = 0;

        while (path[i] != '/' && path[i] != '\0' && c_idx < 127) {
            component[c_idx++] = path[i++];
        }
        component[c_idx] = '\0';

        while (path[i] == '/') ++i; // Skip multiple slashes

        if (c_idx == 0) continue;

        if (k_strcmp(component, ".") == 0) {
            continue;
        }

        // Check if current node is a mountpoint
        if (current->ptr_mount != nullptr) {
            current = current->ptr_mount;
        }

        if (!current->ops || !current->ops->finddir) {
            return nullptr;
        }

        vfs_node *next = current->ops->finddir(current, component);
        if (!next) {
            return nullptr; // Component not found
        }

        current = next;
    }

    if (current && current->ptr_mount != nullptr) {
        current = current->ptr_mount;
    }

    return current;
}

int vfs_open(const char *path, uint32_t flags, uint16_t mode) {
    if (!path) {
        return -1;
    }

    vfs_node *node = vfs_lookup(path);

    // If node doesn't exist and O_CREAT is set, create it
    if (!node) {
        if (!(flags & O_CREAT)) {
            return -1; // File not found
        }

        // Find parent directory path and file basename
        size_t len = k_strlen(path);
        size_t last_slash = len;

        for (size_t i = len; i > 0; --i) {
            if (path[i - 1] == '/') {
                last_slash = i - 1;
                break;
            }
        }

        char parent_path[128];
        const char *filename = nullptr;

        if (last_slash == 0) {
            parent_path[0] = '/';
            parent_path[1] = '\0';
            filename = path + 1;
        } else if (last_slash == len) {
            parent_path[0] = '/';
            parent_path[1] = '\0';
            filename = path;
        } else {
            for (size_t i = 0; i < last_slash; ++i) {
                parent_path[i] = path[i];
            }
            parent_path[last_slash] = '\0';
            filename = path + last_slash + 1;
        }

        vfs_node *parent = vfs_lookup(parent_path);
        if (!parent || !parent->ops || !parent->ops->create) {
            return -1;
        }

        node = parent->ops->create(parent, filename, mode);
        if (!node) {
            return -1;
        }
    }

    // Handle O_TRUNC
    if ((flags & O_TRUNC) && node->ops && node->ops->truncate) {
        node->ops->truncate(node, 0);
    }

    // Call open operation if provided by filesystem
    if (node->ops && node->ops->open) {
        int res = node->ops->open(node, flags);
        if (res < 0) {
            return res;
        }
    }

    // Allocate File Descriptor
    for (size_t i = 0; i < MAX_FDS; ++i) {
        if (!g_fd_table[i].in_use) {
            g_fd_table[i].node = node;
            g_fd_table[i].flags = flags;
            g_fd_table[i].offset = (flags & O_APPEND) ? node->length : 0;
            g_fd_table[i].in_use = true;
            node->ref_count++;
            return static_cast<int>(i);
        }
    }

    return -1; // Out of file descriptors
}

int64_t vfs_read(int fd, void *buffer, size_t count) {
    if (fd < 0 || fd >= static_cast<int>(MAX_FDS) || !buffer) {
        return -1;
    }

    file_descriptor &desc = g_fd_table[fd];
    if (!desc.in_use || !desc.node) {
        return -1;
    }

    vfs_node *node = desc.node;
    if (!node->ops || !node->ops->read) {
        return -1;
    }

    int64_t bytes = node->ops->read(node, desc.offset, count, buffer);
    if (bytes > 0) {
        desc.offset += static_cast<uint64_t>(bytes);
    }
    return bytes;
}

int64_t vfs_write(int fd, const void *buffer, size_t count) {
    if (fd < 0 || fd >= static_cast<int>(MAX_FDS) || !buffer) {
        return -1;
    }

    file_descriptor &desc = g_fd_table[fd];
    if (!desc.in_use || !desc.node) {
        return -1;
    }

    vfs_node *node = desc.node;
    if (!node->ops || !node->ops->write) {
        return -1;
    }

    if (desc.flags & O_APPEND) {
        desc.offset = node->length;
    }

    int64_t bytes = node->ops->write(node, desc.offset, count, buffer);
    if (bytes > 0) {
        desc.offset += static_cast<uint64_t>(bytes);
    }
    return bytes;
}

int64_t vfs_lseek(int fd, int64_t offset, int whence) {
    if (fd < 0 || fd >= static_cast<int>(MAX_FDS)) {
        return -1;
    }

    file_descriptor &desc = g_fd_table[fd];
    if (!desc.in_use || !desc.node) {
        return -1;
    }

    int64_t new_offset = 0;
    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
        case SEEK_CUR:
            new_offset = static_cast<int64_t>(desc.offset) + offset;
            break;
        case SEEK_END:
            new_offset = static_cast<int64_t>(desc.node->length) + offset;
            break;
        default:
            return -1;
    }

    if (new_offset < 0) {
        return -1;
    }

    desc.offset = static_cast<uint64_t>(new_offset);
    return new_offset;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= static_cast<int>(MAX_FDS)) {
        return -1;
    }

    file_descriptor &desc = g_fd_table[fd];
    if (!desc.in_use || !desc.node) {
        return -1;
    }

    vfs_node *node = desc.node;
    if (node->ops && node->ops->close) {
        node->ops->close(node);
    }

    if (node->ref_count > 0) {
        node->ref_count--;
    }

    desc.node = nullptr;
    desc.offset = 0;
    desc.flags = 0;
    desc.in_use = false;
    return 0;
}

int vfs_readdir(int fd, uint32_t index, vfs_dirent *out_dirent) {
    if (fd < 0 || fd >= static_cast<int>(MAX_FDS) || !out_dirent) {
        return -1;
    }

    file_descriptor &desc = g_fd_table[fd];
    if (!desc.in_use || !desc.node) {
        return -1;
    }

    vfs_node *node = desc.node;
    if (node->type != vfs_node_type::DIRECTORY && node->type != vfs_node_type::MOUNTPOINT) {
        return -1;
    }

    if (!node->ops || !node->ops->readdir) {
        return -1;
    }

    return node->ops->readdir(node, index, out_dirent);
}

int vfs_stat(const char *path, vfs_stat_t *st) {
    if (!path || !st) {
        return -1;
    }

    vfs_node *node = vfs_lookup(path);
    if (!node) {
        return -1;
    }

    st->inode = node->inode;
    st->type = node->type;
    st->size = node->length;
    st->mode = node->mode;
    st->uid = node->uid;
    st->gid = node->gid;
    st->atime = 0;
    st->mtime = 0;
    st->ctime = 0;
    return 0;
}

int vfs_mkdir(const char *path, uint16_t mode) {
    if (!path) {
        return -1;
    }

    // Check if already exists
    if (vfs_lookup(path) != nullptr) {
        return -1;
    }

    size_t len = k_strlen(path);
    size_t last_slash = len;

    for (size_t i = len; i > 0; --i) {
        if (path[i - 1] == '/') {
            last_slash = i - 1;
            break;
        }
    }

    char parent_path[128];
    const char *dirname = nullptr;

    if (last_slash == 0) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
        dirname = path + 1;
    } else if (last_slash == len) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
        dirname = path;
    } else {
        for (size_t i = 0; i < last_slash; ++i) {
            parent_path[i] = path[i];
        }
        parent_path[last_slash] = '\0';
        dirname = path + last_slash + 1;
    }

    vfs_node *parent = vfs_lookup(parent_path);
    if (!parent || !parent->ops || !parent->ops->mkdir) {
        return -1;
    }

    vfs_node *new_dir = parent->ops->mkdir(parent, dirname, mode);
    return new_dir ? 0 : -1;
}

int vfs_allocate_fd(vfs_node *node, uint32_t flags) {
    if (!node) return -1;
    for (size_t i = 0; i < MAX_FDS; ++i) {
        if (!g_fd_table[i].in_use) {
            g_fd_table[i].node = node;
            g_fd_table[i].flags = flags;
            g_fd_table[i].offset = 0;
            g_fd_table[i].in_use = true;
            node->ref_count++;
            return static_cast<int>(i);
        }
    }
    return -1;
}

vfs_node *vfs_get_node(int fd) {
    if (fd < 0 || fd >= static_cast<int>(MAX_FDS)) return nullptr;
    if (!g_fd_table[fd].in_use) return nullptr;
    return g_fd_table[fd].node;
}
