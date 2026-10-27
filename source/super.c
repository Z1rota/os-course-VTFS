#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/printk.h>

#include "vtfs.h"
#include "http.h"

#define VTFS_MAGIC 0x56544653
#define VTFS_ROOT_RESPONSE_SIZE 21
#define VTFS_DIRECTORY 2

static int vtfs_fill_super(struct super_block *sb, void *data, int silent) {
  const char *token = data;
  char response[VTFS_ROOT_RESPONSE_SIZE] = {0};
  struct inode *root_inode;
  __le64 inode_id;
  __le32 permissions;
  struct timespec64 now;
  int64_t status;
  size_t received;

  if (!token || !*token) {
    return -EINVAL;
  }
  status = vtfs_http_call(token, "root", response, sizeof(response), &received, 0);
  if (status != 0) {
    return status > 0 ? -(int)status : -EIO;
  }

  if (received != sizeof(response) || response[8] != VTFS_DIRECTORY) {
    return -EIO;
  }

  memcpy(&inode_id, response, sizeof(inode_id));
  memcpy(&permissions,response+9,sizeof(permissions));
  sb->s_magic= VTFS_MAGIC;
  sb->s_fs_info = kstrdup(token, GFP_KERNEL);
  if (!sb->s_fs_info) {
    return -ENOMEM;
  }
  root_inode = new_inode(sb);
  if (!root_inode) goto no_memory;

  inode_init_owner(&nop_mnt_idmap,root_inode,NULL,S_IFDIR | (le32_to_cpu(permissions) & 07777));
  root_inode->i_uid = KUIDT_INIT(VTFS_DEFAULT_UID);
  root_inode->i_gid = KGIDT_INIT(VTFS_DEFAULT_GID);

  root_inode->i_ino = le64_to_cpu(inode_id);
  root_inode->i_op = &vtfs_dir_inode_ops;
  root_inode->i_fop = &vtfs_dir_file_ops;
  now = inode_set_ctime_current(root_inode);
  inode_set_atime_to_ts(root_inode, now);
  inode_set_mtime_to_ts(root_inode, now);
  set_nlink(root_inode, 2);

  sb->s_root = d_make_root(root_inode);
  if (!sb->s_root) {
    goto no_memory;
  }
  pr_info("vtfs mounted\n");
  return 0;

  no_memory:
    kfree(sb->s_fs_info);
    sb->s_fs_info = NULL;
    return -ENOMEM;
}

struct dentry *vtfs_mount(struct file_system_type *fs_type,int flags, const char *dev_name, void *data) {
  return mount_nodev(fs_type,flags,(void *)dev_name,vtfs_fill_super);
}

void vtfs_kill_sb(struct super_block *sb) {
  void *token = sb->s_fs_info;
  kill_anon_super(sb);
  kfree(token);
  pr_info("vtfs unmounted\n");
}

