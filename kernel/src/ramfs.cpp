#include "ramfs.h"
#include "heap.h"
#include "kprint.h"

namespace {
    constexpr size_t MAX_CHILDREN = 64;
    uint32_t g_ramfs_next_inode = 1;

    struct ramfs_entry {
        char         name[128];
        uint8_t     *data;
        size_t       capacity;
        size_t       size;
        ramfs_entry *children[MAX_CHILDREN];
        size_t       child_count;
        ramfs_entry *parent;
        vfs_node     node;
    };

    void str_copy(char *dst, const char *src) {
        size_t i = 0;
        while (src && src[i] != '\0') {
            dst[i] = src[i];
            ++i;
        }
        dst[i] = '\0';
    }

    bool str_equal(const char *a, const char *b) {
        if (!a || !b) return false;
        size_t i = 0;
        while (a[i] && b[i]) {
            if (a[i] != b[i]) return false;
            ++i;
        }
        return a[i] == b[i];
    }

    int64_t ramfs_read(vfs_node *node, uint64_t offset, size_t size, void *buffer) {
        if (!node || !buffer || !node->private_data) {
            return -1;
        }

        ramfs_entry *entry = static_cast<ramfs_entry *>(node->private_data);
        if (offset >= entry->size) {
            return 0; // EOF
        }

        size_t available = entry->size - offset;
        size_t to_read = (size < available) ? size : available;

        uint8_t *dest = static_cast<uint8_t *>(buffer);
        for (size_t i = 0; i < to_read; ++i) {
            dest[i] = entry->data[offset + i];
        }

        return static_cast<int64_t>(to_read);
    }

    int64_t ramfs_write(vfs_node *node, uint64_t offset, size_t size, const void *buffer) {
        if (!node || !buffer || !node->private_data) {
            return -1;
        }

        ramfs_entry *entry = static_cast<ramfs_entry *>(node->private_data);
        size_t needed_capacity = offset + size;

        if (needed_capacity > entry->capacity) {
            size_t new_cap = (entry->capacity == 0) ? 64 : entry->capacity * 2;
            while (new_cap < needed_capacity) {
                new_cap *= 2;
            }

            uint8_t *new_data = static_cast<uint8_t *>(kmalloc(new_cap));
            if (!new_data) {
                return -1; // Out of memory
            }

            // Copy old data
            if (entry->data) {
                for (size_t i = 0; i < entry->size; ++i) {
                    new_data[i] = entry->data[i];
                }
                kfree(entry->data);
            }

            // Zero remaining area
            for (size_t i = entry->size; i < new_cap; ++i) {
                new_data[i] = 0;
            }

            entry->data = new_data;
            entry->capacity = new_cap;
        }

        const uint8_t *src = static_cast<const uint8_t *>(buffer);
        for (size_t i = 0; i < size; ++i) {
            entry->data[offset + i] = src[i];
        }

        if (needed_capacity > entry->size) {
            entry->size = needed_capacity;
            node->length = needed_capacity;
        }

        return static_cast<int64_t>(size);
    }

    int ramfs_open(vfs_node *node, uint32_t flags) {
        (void)node;
        (void)flags;
        return 0;
    }

    int ramfs_close(vfs_node *node) {
        (void)node;
        return 0;
    }

    int ramfs_truncate(vfs_node *node, uint64_t new_size) {
        if (!node || !node->private_data) {
            return -1;
        }

        ramfs_entry *entry = static_cast<ramfs_entry *>(node->private_data);
        if (new_size < entry->size) {
            entry->size = static_cast<size_t>(new_size);
            node->length = new_size;
        }
        return 0;
    }

    int ramfs_readdir(vfs_node *node, uint32_t index, vfs_dirent *out_dirent) {
        if (!node || !out_dirent || !node->private_data) {
            return -1;
        }

        ramfs_entry *entry = static_cast<ramfs_entry *>(node->private_data);
        if (index >= entry->child_count) {
            return 0; // No more entries
        }

        ramfs_entry *child = entry->children[index];
        out_dirent->inode = child->node.inode;
        out_dirent->type = child->node.type;
        str_copy(out_dirent->name, child->name);
        return 1;
    }

    vfs_node *ramfs_finddir(vfs_node *node, const char *name) {
        if (!node || !name || !node->private_data) {
            return nullptr;
        }

        ramfs_entry *entry = static_cast<ramfs_entry *>(node->private_data);
        for (size_t i = 0; i < entry->child_count; ++i) {
            if (str_equal(entry->children[i]->name, name)) {
                return &entry->children[i]->node;
            }
        }
        return nullptr;
    }

    vfs_node *ramfs_create(vfs_node *parent_node, const char *name, uint16_t mode);
    vfs_node *ramfs_mkdir(vfs_node *parent_node, const char *name, uint16_t mode);

    vfs_operations g_ramfs_ops = {
        ramfs_read,
        ramfs_write,
        ramfs_open,
        ramfs_close,
        ramfs_readdir,
        ramfs_finddir,
        ramfs_create,
        ramfs_mkdir,
        ramfs_truncate
    };

    ramfs_entry *create_entry(const char *name, vfs_node_type type, uint16_t mode, ramfs_entry *parent) {
        ramfs_entry *entry = static_cast<ramfs_entry *>(kmalloc(sizeof(ramfs_entry)));
        if (!entry) {
            return nullptr;
        }

        str_copy(entry->name, name);
        entry->data = nullptr;
        entry->capacity = 0;
        entry->size = 0;
        entry->child_count = 0;
        entry->parent = parent;

        for (size_t i = 0; i < MAX_CHILDREN; ++i) {
            entry->children[i] = nullptr;
        }

        str_copy(entry->node.name, name);
        entry->node.type = type;
        entry->node.mode = mode;
        entry->node.uid = 0;
        entry->node.gid = 0;
        entry->node.inode = g_ramfs_next_inode++;
        entry->node.length = 0;
        entry->node.ref_count = 0;
        entry->node.ops = &g_ramfs_ops;
        entry->node.private_data = entry;
        entry->node.ptr_mount = nullptr;

        return entry;
    }

    vfs_node *ramfs_create(vfs_node *parent_node, const char *name, uint16_t mode) {
        if (!parent_node || !name || !parent_node->private_data) {
            return nullptr;
        }

        ramfs_entry *parent = static_cast<ramfs_entry *>(parent_node->private_data);
        if (parent->child_count >= MAX_CHILDREN) {
            return nullptr; // Directory full
        }

        ramfs_entry *child = create_entry(name, vfs_node_type::FILE, mode, parent);
        if (!child) {
            return nullptr;
        }

        parent->children[parent->child_count++] = child;
        return &child->node;
    }

    vfs_node *ramfs_mkdir(vfs_node *parent_node, const char *name, uint16_t mode) {
        if (!parent_node || !name || !parent_node->private_data) {
            return nullptr;
        }

        ramfs_entry *parent = static_cast<ramfs_entry *>(parent_node->private_data);
        if (parent->child_count >= MAX_CHILDREN) {
            return nullptr;
        }

        ramfs_entry *child = create_entry(name, vfs_node_type::DIRECTORY, mode, parent);
        if (!child) {
            return nullptr;
        }

        parent->children[parent->child_count++] = child;
        return &child->node;
    }
}

vfs_node *ramfs_create_root(const char *name) {
    ramfs_entry *root = create_entry(name, vfs_node_type::DIRECTORY, 0755, nullptr);
    return root ? &root->node : nullptr;
}
