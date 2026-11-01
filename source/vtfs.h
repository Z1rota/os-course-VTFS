#ifndef VTFS_H
#define VTFS_H

#include <linux/fs.h>
#include <linux/workqueue.h>

#define VTFS_FILE 1
#define VTFS_DIRECTORY 2

#define VTFS_DEFAULT_UID 1000
#define VTFS_DEFAULT_GID 1000


struct vtfs_sb_info {
    char *token;
    u64 session_id;
    struct delayed_work heartbeat_work;
    bool stopping;
};

static inline const char *vtfs_token(struct super_block *sb) {
    struct vtfs_sb_info *info = sb->s_fs_info;
    return info->token;
}

static inline u64 vtfs_session_id(struct super_block *sb) {
    struct vtfs_sb_info *info = sb->s_fs_info;
    return info->session_id;
}

struct dentry *vtfs_mount(struct file_system_type *fs_type, int flags,
                         const char *dev_name, void *data);

void vtfs_kill_sb(struct super_block *sb);

struct inode *vtfs_get_inode(struct super_block *sb, u64 id, u8 type, u32 permissions, u64 size,u32 link_count);
extern const struct inode_operations vtfs_dir_inode_ops;
extern const struct file_operations vtfs_dir_file_ops;
extern const struct file_operations vtfs_file_ops;

#endif