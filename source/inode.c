#include <linux/fs.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/uidgid.h>

#include "vtfs.h"

static const struct file_operations vtfs_file_ops = {
    .owner = THIS_MODULE,
    .llseek = generic_file_llseek,
};

struct inode *vtfs_get_inode(struct super_block *sb, u64 id, u8 type, u32 permissions, u64 size) {
    struct inode *inode;
    struct timespec64 now;
    if (type != VTFS_FILE && type != VTFS_DIRECTORY) {
        return ERR_PTR(-EIO);
    }
    
    inode = iget_locked(sb,id);
    if (!inode) {
        return ERR_PTR(-ENOMEM);
    }
    if (!(inode->i_state & I_NEW)) {
        return inode;
    }
    inode_init_owner(&nop_mnt_idmap,inode,NULL, (type == VTFS_DIRECTORY ? S_IFDIR : S_IFREG) | (permissions & 07777));
    
    inode->i_uid = KUIDT_INIT(VTFS_DEFAULT_UID);
    inode->i_gid = KGIDT_INIT(VTFS_DEFAULT_GID);
    inode->i_ino = id;
    i_size_write(inode, size);

    if (type == VTFS_DIRECTORY) {
        inode->i_op = &vtfs_dir_inode_ops;
        inode->i_fop = &vtfs_dir_file_ops;
        set_nlink(inode,2);
    } else {
        inode->i_fop = &vtfs_file_ops;
        set_nlink(inode,1);
    }

    now = inode_set_ctime_current(inode);
    inode_set_atime_to_ts(inode,now);
    inode_set_mtime_to_ts(inode,now);

    unlock_new_inode(inode);
    return inode;
}