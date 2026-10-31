#ifndef VTFS_H
#define VTFS_H

#include <linux/fs.h>

#define VTFS_FILE 1
#define VTFS_DIRECTORY 2

#define VTFS_DEFAULT_UID 1000
#define VTFS_DEFAULT_GID 1000

struct dentry *vtfs_mount(struct file_system_type *fs_type, int flags,
                         const char *dev_name, void *data);

void vtfs_kill_sb(struct super_block *sb);

struct inode *vtfs_get_inode(struct super_block *sb, u64 id, u8 type, u32 permissions, u64 size,u32 link_count);
extern const struct inode_operations vtfs_dir_inode_ops;
extern const struct file_operations vtfs_dir_file_ops;
extern const struct file_operations vtfs_file_ops;

#endif