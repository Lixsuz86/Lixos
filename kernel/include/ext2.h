#ifndef LIXOS_EXT2_H
#define LIXOS_EXT2_H

#include <stdint.h>
#include <stddef.h>
#include "vfs.h"

constexpr uint16_t EXT2_SUPER_MAGIC = 0xEF53;

// Inode File Types
constexpr uint16_t EXT2_S_IFREG  = 0x8000;
constexpr uint16_t EXT2_S_IFDIR  = 0x4000;
constexpr uint16_t EXT2_S_IFCHR  = 0x2000;
constexpr uint16_t EXT2_S_IFBLK  = 0x6000;
constexpr uint16_t EXT2_S_IFIFO  = 0x1000;
constexpr uint16_t EXT2_S_IFLNK  = 0xA000;
constexpr uint16_t EXT2_S_IFSOCK = 0xC000;

// Directory Entry File Types
constexpr uint8_t EXT2_FT_UNKNOWN  = 0;
constexpr uint8_t EXT2_FT_REG_FILE = 1;
constexpr uint8_t EXT2_FT_DIR      = 2;
constexpr uint8_t EXT2_FT_CHRDEV   = 3;
constexpr uint8_t EXT2_FT_BLKDEV   = 4;
constexpr uint8_t EXT2_FT_FIFO     = 5;
constexpr uint8_t EXT2_FT_SOCK     = 6;
constexpr uint8_t EXT2_FT_SYMLINK  = 7;

// ext2 Superblock
struct ext2_superblock {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    // Extended fields (rev >= 1)
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algo_bitmap;
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_padding1;
    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;
    uint32_t s_hash_seed[4];
    uint8_t  s_def_hash_version;
    uint8_t  s_reserved_char_pad;
    uint16_t s_reserved_word_pad;
    uint32_t s_default_mount_opts;
    uint32_t s_first_meta_bg;
    uint32_t s_reserved[190];
} __attribute__((packed));

// ext2 Block Group Descriptor
struct ext2_bg_descriptor {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
} __attribute__((packed));

// ext2 Inode Structure
struct ext2_inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint32_t i_osd2[3];
} __attribute__((packed));

// ext2 Directory Entry Structure
struct ext2_dirent {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
} __attribute__((packed));

// Mount ext2 filesystem on the default AHCI disk starting at given LBA
vfs_node *ext2_mount(uint64_t start_lba = 0);

#endif // LIXOS_EXT2_H
