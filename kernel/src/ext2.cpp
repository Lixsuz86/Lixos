#include "ext2.h"
#include "ahci.h"
#include "heap.h"
#include "string.h"
#include "kprint.h"

namespace {
    struct ext2_fs_t {
        uint64_t            start_lba;
        uint32_t            block_size;
        uint32_t            sectors_per_block;
        uint32_t            inodes_per_group;
        uint32_t            blocks_per_group;
        uint32_t            inode_size;
        uint32_t            bg_count;
        ext2_superblock     sb;
        ext2_bg_descriptor *bgd_table;
        vfs_operations      ops;
    };

    struct ext2_node_data {
        ext2_fs_t *fs;
        uint32_t   inode_num;
        ext2_inode raw_inode;
    };

    bool ext2_read_block(ext2_fs_t *fs, uint32_t block_num, void *buf) {
        if (!fs || block_num == 0) return false;
        uint64_t lba = fs->start_lba + (static_cast<uint64_t>(block_num) * fs->sectors_per_block);
        return disk_read_sectors(lba, fs->sectors_per_block, buf);
    }

    bool ext2_write_block(ext2_fs_t *fs, uint32_t block_num, const void *buf) {
        if (!fs || block_num == 0) return false;
        uint64_t lba = fs->start_lba + (static_cast<uint64_t>(block_num) * fs->sectors_per_block);
        return disk_write_sectors(lba, fs->sectors_per_block, buf);
    }

    bool ext2_read_inode(ext2_fs_t *fs, uint32_t ino, ext2_inode *out_inode) {
        if (!fs || ino == 0 || !out_inode) return false;

        uint32_t bg = (ino - 1) / fs->inodes_per_group;
        uint32_t idx = (ino - 1) % fs->inodes_per_group;

        if (bg >= fs->bg_count) return false;

        uint32_t inode_table_block = fs->bgd_table[bg].bg_inode_table;
        uint32_t block_offset = (idx * fs->inode_size) / fs->block_size;
        uint32_t byte_offset = (idx * fs->inode_size) % fs->block_size;

        uint8_t *block_buf = static_cast<uint8_t *>(kmalloc(fs->block_size));
        if (!block_buf) return false;

        if (!ext2_read_block(fs, inode_table_block + block_offset, block_buf)) {
            kfree(block_buf);
            return false;
        }

        memcpy(out_inode, block_buf + byte_offset, sizeof(ext2_inode));
        kfree(block_buf);
        return true;
    }

    bool ext2_write_inode(ext2_fs_t *fs, uint32_t ino, const ext2_inode *in_inode) {
        if (!fs || ino == 0 || !in_inode) return false;

        uint32_t bg = (ino - 1) / fs->inodes_per_group;
        uint32_t idx = (ino - 1) % fs->inodes_per_group;

        if (bg >= fs->bg_count) return false;

        uint32_t inode_table_block = fs->bgd_table[bg].bg_inode_table;
        uint32_t block_offset = (idx * fs->inode_size) / fs->block_size;
        uint32_t byte_offset = (idx * fs->inode_size) % fs->block_size;

        uint8_t *block_buf = static_cast<uint8_t *>(kmalloc(fs->block_size));
        if (!block_buf) return false;

        if (!ext2_read_block(fs, inode_table_block + block_offset, block_buf)) {
            kfree(block_buf);
            return false;
        }

        memcpy(block_buf + byte_offset, in_inode, sizeof(ext2_inode));
        bool ok = ext2_write_block(fs, inode_table_block + block_offset, block_buf);
        kfree(block_buf);
        return ok;
    }

    uint32_t ext2_get_inode_block(ext2_fs_t *fs, const ext2_inode *inode, uint32_t block_index) {
        if (block_index < 12) {
            return inode->i_block[block_index];
        }

        uint32_t ptrs_per_block = fs->block_size / sizeof(uint32_t);
        block_index -= 12;

        // Singly-indirect block
        if (block_index < ptrs_per_block) {
            if (inode->i_block[12] == 0) return 0;
            uint32_t *indirect = static_cast<uint32_t *>(kmalloc(fs->block_size));
            if (!indirect) return 0;
            ext2_read_block(fs, inode->i_block[12], indirect);
            uint32_t blk = indirect[block_index];
            kfree(indirect);
            return blk;
        }

        block_index -= ptrs_per_block;

        // Doubly-indirect block
        if (block_index < ptrs_per_block * ptrs_per_block) {
            if (inode->i_block[13] == 0) return 0;
            uint32_t *d_indirect = static_cast<uint32_t *>(kmalloc(fs->block_size));
            if (!d_indirect) return 0;
            ext2_read_block(fs, inode->i_block[13], d_indirect);

            uint32_t first_idx = block_index / ptrs_per_block;
            uint32_t second_idx = block_index % ptrs_per_block;
            uint32_t s_block = d_indirect[first_idx];
            kfree(d_indirect);

            if (s_block == 0) return 0;
            uint32_t *s_indirect = static_cast<uint32_t *>(kmalloc(fs->block_size));
            if (!s_indirect) return 0;
            ext2_read_block(fs, s_block, s_indirect);
            uint32_t blk = s_indirect[second_idx];
            kfree(s_indirect);
            return blk;
        }

        return 0;
    }

    // Allocate a free data block
    uint32_t ext2_alloc_block(ext2_fs_t *fs) {
        uint8_t *bitmap = static_cast<uint8_t *>(kmalloc(fs->block_size));
        if (!bitmap) return 0;

        for (uint32_t bg = 0; bg < fs->bg_count; ++bg) {
            if (fs->bgd_table[bg].bg_free_blocks_count == 0) continue;

            ext2_read_block(fs, fs->bgd_table[bg].bg_block_bitmap, bitmap);

            for (uint32_t i = 0; i < fs->blocks_per_group; ++i) {
                uint32_t byte_idx = i / 8;
                uint8_t  bit_mask = 1 << (i % 8);

                if (!(bitmap[byte_idx] & bit_mask)) {
                    // Mark as used
                    bitmap[byte_idx] |= bit_mask;
                    ext2_write_block(fs, fs->bgd_table[bg].bg_block_bitmap, bitmap);

                    fs->bgd_table[bg].bg_free_blocks_count--;
                    fs->sb.s_free_blocks_count--;

                    // Update block group descriptors and superblock
                    uint32_t bgd_block = (fs->block_size == 1024) ? 2 : 1;
                    ext2_write_block(fs, bgd_block, fs->bgd_table);

                    uint8_t *sb_buf = static_cast<uint8_t *>(kmalloc(fs->block_size));
                    if (sb_buf) {
                        uint32_t sb_block = (fs->block_size == 1024) ? 1 : 0;
                        ext2_read_block(fs, sb_block, sb_buf);
                        uint32_t sb_off = (fs->block_size == 1024) ? 0 : 1024;
                        memcpy(sb_buf + sb_off, &fs->sb, sizeof(ext2_superblock));
                        ext2_write_block(fs, sb_block, sb_buf);
                        kfree(sb_buf);
                    }

                    kfree(bitmap);
                    uint32_t alloc_block = (bg * fs->blocks_per_group) + i + fs->sb.s_first_data_block;

                    // Zero newly allocated block
                    uint8_t *zero_buf = static_cast<uint8_t *>(kmalloc(fs->block_size));
                    if (zero_buf) {
                        memset(zero_buf, 0, fs->block_size);
                        ext2_write_block(fs, alloc_block, zero_buf);
                        kfree(zero_buf);
                    }
                    return alloc_block;
                }
            }
        }

        kfree(bitmap);
        return 0;
    }

    // Allocate a free inode
    uint32_t ext2_alloc_inode(ext2_fs_t *fs) {
        uint8_t *bitmap = static_cast<uint8_t *>(kmalloc(fs->block_size));
        if (!bitmap) return 0;

        for (uint32_t bg = 0; bg < fs->bg_count; ++bg) {
            if (fs->bgd_table[bg].bg_free_inodes_count == 0) continue;

            ext2_read_block(fs, fs->bgd_table[bg].bg_inode_bitmap, bitmap);

            for (uint32_t i = 0; i < fs->inodes_per_group; ++i) {
                uint32_t byte_idx = i / 8;
                uint8_t  bit_mask = 1 << (i % 8);

                if (!(bitmap[byte_idx] & bit_mask)) {
                    bitmap[byte_idx] |= bit_mask;
                    ext2_write_block(fs, fs->bgd_table[bg].bg_inode_bitmap, bitmap);

                    fs->bgd_table[bg].bg_free_inodes_count--;
                    fs->sb.s_free_inodes_count--;

                    uint32_t bgd_block = (fs->block_size == 1024) ? 2 : 1;
                    ext2_write_block(fs, bgd_block, fs->bgd_table);

                    uint8_t *sb_buf = static_cast<uint8_t *>(kmalloc(fs->block_size));
                    if (sb_buf) {
                        uint32_t sb_block = (fs->block_size == 1024) ? 1 : 0;
                        ext2_read_block(fs, sb_block, sb_buf);
                        uint32_t sb_off = (fs->block_size == 1024) ? 0 : 1024;
                        memcpy(sb_buf + sb_off, &fs->sb, sizeof(ext2_superblock));
                        ext2_write_block(fs, sb_block, sb_buf);
                        kfree(sb_buf);
                    }

                    kfree(bitmap);
                    return (bg * fs->inodes_per_group) + i + 1;
                }
            }
        }

        kfree(bitmap);
        return 0;
    }

    vfs_node *ext2_create_vfs_node(ext2_fs_t *fs, uint32_t ino, const ext2_inode *raw_inode, const char *name);

    int64_t ext2_op_read(vfs_node *node, uint64_t offset, size_t size, void *buffer) {
        if (!node || !buffer || !node->private_data) return -1;

        ext2_node_data *nd = static_cast<ext2_node_data *>(node->private_data);
        ext2_fs_t *fs = nd->fs;

        if (offset >= nd->raw_inode.i_size) {
            return 0; // EOF
        }

        size_t available = nd->raw_inode.i_size - offset;
        size_t to_read = (size < available) ? size : available;

        uint8_t *dest = static_cast<uint8_t *>(buffer);
        uint8_t *block_buf = static_cast<uint8_t *>(kmalloc(fs->block_size));
        if (!block_buf) return -1;

        size_t bytes_read = 0;
        while (bytes_read < to_read) {
            uint64_t cur_offset = offset + bytes_read;
            uint32_t block_idx = static_cast<uint32_t>(cur_offset / fs->block_size);
            uint32_t block_off = static_cast<uint32_t>(cur_offset % fs->block_size);
            size_t chunk = fs->block_size - block_off;
            if (chunk > (to_read - bytes_read)) {
                chunk = to_read - bytes_read;
            }

            uint32_t disk_block = ext2_get_inode_block(fs, &nd->raw_inode, block_idx);
            if (disk_block == 0) {
                // Sparse hole: return zeroes
                memset(dest + bytes_read, 0, chunk);
            } else {
                ext2_read_block(fs, disk_block, block_buf);
                memcpy(dest + bytes_read, block_buf + block_off, chunk);
            }

            bytes_read += chunk;
        }

        kfree(block_buf);
        return static_cast<int64_t>(bytes_read);
    }

    int64_t ext2_op_write(vfs_node *node, uint64_t offset, size_t size, const void *buffer) {
        if (!node || !buffer || !node->private_data) return -1;

        ext2_node_data *nd = static_cast<ext2_node_data *>(node->private_data);
        ext2_fs_t *fs = nd->fs;

        const uint8_t *src = static_cast<const uint8_t *>(buffer);
        uint8_t *block_buf = static_cast<uint8_t *>(kmalloc(fs->block_size));
        if (!block_buf) return -1;

        size_t bytes_written = 0;
        while (bytes_written < size) {
            uint64_t cur_offset = offset + bytes_written;
            uint32_t block_idx = static_cast<uint32_t>(cur_offset / fs->block_size);
            uint32_t block_off = static_cast<uint32_t>(cur_offset % fs->block_size);
            size_t chunk = fs->block_size - block_off;
            if (chunk > (size - bytes_written)) {
                chunk = size - bytes_written;
            }

            // Direct block allocation if not yet present
            if (block_idx < 12) {
                if (nd->raw_inode.i_block[block_idx] == 0) {
                    uint32_t new_blk = ext2_alloc_block(fs);
                    if (!new_blk) {
                        kfree(block_buf);
                        return bytes_written > 0 ? static_cast<int64_t>(bytes_written) : -1;
                    }
                    nd->raw_inode.i_block[block_idx] = new_blk;
                    nd->raw_inode.i_blocks += fs->sectors_per_block;
                }

                uint32_t disk_block = nd->raw_inode.i_block[block_idx];
                if (block_off > 0 || chunk < fs->block_size) {
                    ext2_read_block(fs, disk_block, block_buf);
                }
                memcpy(block_buf + block_off, src + bytes_written, chunk);
                ext2_write_block(fs, disk_block, block_buf);
            }

            bytes_written += chunk;
        }

        if (offset + size > nd->raw_inode.i_size) {
            nd->raw_inode.i_size = static_cast<uint32_t>(offset + size);
            node->length = nd->raw_inode.i_size;
        }

        ext2_write_inode(fs, nd->inode_num, &nd->raw_inode);
        kfree(block_buf);
        return static_cast<int64_t>(bytes_written);
    }

    int ext2_op_open(vfs_node *node, uint32_t flags) {
        (void)node;
        (void)flags;
        return 0;
    }

    int ext2_op_close(vfs_node *node) {
        (void)node;
        return 0;
    }

    int ext2_op_readdir(vfs_node *node, uint32_t index, vfs_dirent *out_dirent) {
        if (!node || !out_dirent || !node->private_data) return -1;

        ext2_node_data *nd = static_cast<ext2_node_data *>(node->private_data);
        ext2_fs_t *fs = nd->fs;

        uint8_t *block_buf = static_cast<uint8_t *>(kmalloc(fs->block_size));
        if (!block_buf) return -1;

        uint32_t entry_count = 0;
        uint32_t total_blocks = (nd->raw_inode.i_size + fs->block_size - 1) / fs->block_size;

        for (uint32_t b = 0; b < total_blocks; ++b) {
            uint32_t blk = ext2_get_inode_block(fs, &nd->raw_inode, b);
            if (blk == 0) continue;

            ext2_read_block(fs, blk, block_buf);

            uint32_t off = 0;
            while (off < fs->block_size) {
                ext2_dirent *de = reinterpret_cast<ext2_dirent *>(block_buf + off);
                if (de->rec_len == 0) break;

                if (de->inode != 0) {
                    if (entry_count == index) {
                        out_dirent->inode = de->inode;
                        out_dirent->type = (de->file_type == EXT2_FT_DIR) ? vfs_node_type::DIRECTORY : vfs_node_type::FILE;
                        size_t n_len = de->name_len;
                        if (n_len > 127) n_len = 127;
                        memcpy(out_dirent->name, de->name, n_len);
                        out_dirent->name[n_len] = '\0';

                        kfree(block_buf);
                        return 1;
                    }
                    entry_count++;
                }
                off += de->rec_len;
            }
        }

        kfree(block_buf);
        return 0;
    }

    vfs_node *ext2_op_finddir(vfs_node *node, const char *name) {
        if (!node || !name || !node->private_data) return nullptr;

        ext2_node_data *nd = static_cast<ext2_node_data *>(node->private_data);
        ext2_fs_t *fs = nd->fs;

        uint8_t *block_buf = static_cast<uint8_t *>(kmalloc(fs->block_size));
        if (!block_buf) return nullptr;

        uint32_t total_blocks = (nd->raw_inode.i_size + fs->block_size - 1) / fs->block_size;
        size_t name_len = strlen(name);

        for (uint32_t b = 0; b < total_blocks; ++b) {
            uint32_t blk = ext2_get_inode_block(fs, &nd->raw_inode, b);
            if (blk == 0) continue;

            ext2_read_block(fs, blk, block_buf);

            uint32_t off = 0;
            while (off < fs->block_size) {
                ext2_dirent *de = reinterpret_cast<ext2_dirent *>(block_buf + off);
                if (de->rec_len == 0) break;

                if (de->inode != 0 && de->name_len == name_len) {
                    if (memcmp(de->name, name, name_len) == 0) {
                        uint32_t child_ino = de->inode;
                        kfree(block_buf);

                        ext2_inode child_inode{};
                        if (ext2_read_inode(fs, child_ino, &child_inode)) {
                            return ext2_create_vfs_node(fs, child_ino, &child_inode, name);
                        }
                        return nullptr;
                    }
                }
                off += de->rec_len;
            }
        }

        kfree(block_buf);
        return nullptr;
    }

    vfs_node *ext2_op_create(vfs_node *parent, const char *name, uint16_t mode) {
        if (!parent || !name || !parent->private_data) return nullptr;

        ext2_node_data *p_nd = static_cast<ext2_node_data *>(parent->private_data);
        ext2_fs_t *fs = p_nd->fs;

        // Allocate free inode
        uint32_t new_ino = ext2_alloc_inode(fs);
        if (new_ino == 0) return nullptr;

        ext2_inode new_raw{};
        new_raw.i_mode = EXT2_S_IFREG | (mode & 0777);
        new_raw.i_uid = 1000;
        new_raw.i_gid = 1000;
        new_raw.i_size = 0;
        new_raw.i_links_count = 1;
        new_raw.i_blocks = 0;

        ext2_write_inode(fs, new_ino, &new_raw);

        // Append to parent directory
        uint8_t *block_buf = static_cast<uint8_t *>(kmalloc(fs->block_size));
        if (!block_buf) return nullptr;

        size_t name_len = strlen(name);
        uint16_t needed_len = static_cast<uint16_t>(sizeof(ext2_dirent) + name_len);
        needed_len = (needed_len + 3) & ~3; // 4-byte align

        uint32_t blk = ext2_get_inode_block(fs, &p_nd->raw_inode, 0);
        if (blk != 0) {
            ext2_read_block(fs, blk, block_buf);

            uint32_t off = 0;
            ext2_dirent *last_de = nullptr;

            while (off < fs->block_size) {
                ext2_dirent *de = reinterpret_cast<ext2_dirent *>(block_buf + off);
                if (de->rec_len == 0) break;

                uint16_t actual_len = static_cast<uint16_t>(sizeof(ext2_dirent) + de->name_len);
                actual_len = (actual_len + 3) & ~3;

                if (off + de->rec_len >= fs->block_size) {
                    // Last entry in block
                    if (de->rec_len - actual_len >= needed_len) {
                        uint16_t old_rec = de->rec_len;
                        de->rec_len = actual_len;

                        ext2_dirent *new_de = reinterpret_cast<ext2_dirent *>(block_buf + off + actual_len);
                        new_de->inode = new_ino;
                        new_de->rec_len = old_rec - actual_len;
                        new_de->name_len = static_cast<uint8_t>(name_len);
                        new_de->file_type = EXT2_FT_REG_FILE;
                        memcpy(new_de->name, name, name_len);

                        ext2_write_block(fs, blk, block_buf);
                        break;
                    }
                }

                last_de = de;
                off += de->rec_len;
            }
        }

        kfree(block_buf);
        return ext2_create_vfs_node(fs, new_ino, &new_raw, name);
    }

    vfs_node *ext2_create_vfs_node(ext2_fs_t *fs, uint32_t ino, const ext2_inode *raw_inode, const char *name) {
        vfs_node *node = static_cast<vfs_node *>(kmalloc(sizeof(vfs_node)));
        ext2_node_data *nd = static_cast<ext2_node_data *>(kmalloc(sizeof(ext2_node_data)));

        if (!node || !nd) {
            return nullptr;
        }

        nd->fs = fs;
        nd->inode_num = ino;
        memcpy(&nd->raw_inode, raw_inode, sizeof(ext2_inode));

        strcpy(node->name, name);
        node->inode = ino;
        node->mode = raw_inode->i_mode;
        node->uid = raw_inode->i_uid;
        node->gid = raw_inode->i_gid;
        node->length = raw_inode->i_size;
        node->ref_count = 0;
        node->type = (raw_inode->i_mode & EXT2_S_IFDIR) ? vfs_node_type::DIRECTORY : vfs_node_type::FILE;
        node->ops = &fs->ops;
        node->private_data = nd;
        node->ptr_mount = nullptr;

        return node;
    }
}

vfs_node *ext2_mount(uint64_t start_lba) {
    if (!disk_is_available()) {
        kprintf("[FAIL] ext2_mount: No AHCI disk available!\n");
        return nullptr;
    }

    ext2_fs_t *fs = static_cast<ext2_fs_t *>(kmalloc(sizeof(ext2_fs_t)));
    if (!fs) return nullptr;

    fs->start_lba = start_lba;

    // 1. Read Superblock (offset 1024 bytes -> LBA 2 on 512B sectors)
    uint8_t sb_buf[1024];
    if (!disk_read_sectors(start_lba + 2, 2, sb_buf)) {
        kprintf("[FAIL] ext2_mount: Failed to read superblock LBA 2!\n");
        kfree(fs);
        return nullptr;
    }

    memcpy(&fs->sb, sb_buf, sizeof(ext2_superblock));

    if (fs->sb.s_magic != EXT2_SUPER_MAGIC) {
        kprintf("[FAIL] ext2_mount: Invalid ext2 magic 0x%04x (expected 0xEF53)!\n", fs->sb.s_magic);
        kfree(fs);
        return nullptr;
    }

    fs->block_size = 1024 << fs->sb.s_log_block_size;
    fs->sectors_per_block = fs->block_size / 512;
    fs->inodes_per_group = fs->sb.s_inodes_per_group;
    fs->blocks_per_group = fs->sb.s_blocks_per_group;
    fs->inode_size = (fs->sb.s_rev_level >= 1) ? fs->sb.s_inode_size : 128;

    fs->bg_count = (fs->sb.s_blocks_count + fs->blocks_per_group - 1) / fs->blocks_per_group;

    // 2. Read Block Group Descriptor Table
    uint32_t bgd_block = (fs->block_size == 1024) ? 2 : 1;
    size_t bgd_table_size = fs->bg_count * sizeof(ext2_bg_descriptor);
    size_t bgd_blocks = (bgd_table_size + fs->block_size - 1) / fs->block_size;

    fs->bgd_table = static_cast<ext2_bg_descriptor *>(kmalloc(bgd_blocks * fs->block_size));
    if (!fs->bgd_table) {
        kfree(fs);
        return nullptr;
    }

    for (size_t b = 0; b < bgd_blocks; ++b) {
        ext2_read_block(fs, bgd_block + b, reinterpret_cast<uint8_t *>(fs->bgd_table) + (b * fs->block_size));
    }

    // 3. Setup VFS Operations
    fs->ops.read = ext2_op_read;
    fs->ops.write = ext2_op_write;
    fs->ops.open = ext2_op_open;
    fs->ops.close = ext2_op_close;
    fs->ops.readdir = ext2_op_readdir;
    fs->ops.finddir = ext2_op_finddir;
    fs->ops.create = ext2_op_create;
    fs->ops.mkdir = nullptr;
    fs->ops.truncate = nullptr;

    // 4. Read Root Inode (Inode 2)
    ext2_inode root_inode{};
    if (!ext2_read_inode(fs, 2, &root_inode)) {
        kprintf("[FAIL] ext2_mount: Failed to read root inode 2!\n");
        kfree(fs->bgd_table);
        kfree(fs);
        return nullptr;
    }

    vfs_node *root_node = ext2_create_vfs_node(fs, 2, &root_inode, "/");
    return root_node;
}
