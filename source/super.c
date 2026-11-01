#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/printk.h>

#include "vtfs.h"
#include "http.h"
#include <linux/jiffies.h>
#include <asm/unaligned.h>

#define VTFS_MAGIC 0x56544653
#define VTFS_ROOT_RESPONSE_SIZE 25
#define VTFS_DIRECTORY 2
#define VTFS_HEARTBEAT_INTERVAL (15 * HZ)

static int vtfs_session_request(struct vtfs_sb_info *info, const char *method) {
  char session_id[24];
    char response[1];
    size_t received = 0;
    int64_t status;

    snprintf(session_id, sizeof(session_id), "%llu", (unsigned long long)info->session_id);

    status = vtfs_http_call(info->token, method, response, sizeof(response), &received, 1, "session", session_id);

    if (status != 0) {
        return status > 0 ? -(int)status : -EIO;
    }

    return received == 0 ? 0 : -EIO;
}

static void vtfs_heartbeat_work(struct work_struct *work) {
  struct vtfs_sb_info *info = container_of(to_delayed_work(work), struct vtfs_sb_info, heartbeat_work);
  int error = vtfs_session_request(info, "session/heartbeat");
   if (error) {
        pr_warn("vtfs: heartbeat failed: %d\n", error);
    }

    if (!READ_ONCE(info->stopping)) {
        schedule_delayed_work(&info->heartbeat_work,VTFS_HEARTBEAT_INTERVAL);
    }
}

static int vtfs_fill_super(struct super_block *sb, void *data, int silent) {
  const char *token = data;
    char response[VTFS_ROOT_RESPONSE_SIZE] = {0};
    char session_response[8];

    struct vtfs_sb_info *info;
    struct inode *root_inode;
    struct timespec64 now;

    __le64 inode_id;
    __le32 permissions;

    int64_t status;
    size_t received = 0;
    size_t session_received = 0;
    int error;


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

  info = kzalloc(sizeof(*info), GFP_KERNEL);
  if (!info) {
     return -ENOMEM;
  }

  info->token = kstrdup(token, GFP_KERNEL);

  if (!info->token) {
      error = -ENOMEM;
      goto free_info;
  }
  status = vtfs_http_call(info->token, "session/start",session_response, sizeof(session_response),&session_received, 0);

  if (status != 0) {
      error = status > 0 ? -(int)status : -EIO;
      goto free_info;
  }

  if (session_received != sizeof(session_response)) {
      error = -EIO;
      goto free_info;
  }

  info->session_id = get_unaligned_le64(session_response);

  if (info->session_id == 0) {
      error = -EIO;
      goto free_info;
  }

  INIT_DELAYED_WORK(&info->heartbeat_work, vtfs_heartbeat_work);

  sb->s_magic = VTFS_MAGIC;
  sb->s_fs_info = info;

  root_inode = new_inode(sb);
  if (!root_inode) {
      error = -ENOMEM;
      goto end_session;
  }

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
      error = -ENOMEM;
      goto end_session;
  }
  schedule_delayed_work(&info->heartbeat_work,VTFS_HEARTBEAT_INTERVAL);

  pr_info("vtfs mounted, session=%llu\n",(unsigned long long)info->session_id);

  return 0;

  end_session:
    vtfs_session_request(info, "session/end");
    sb->s_fs_info = NULL;

  free_info:
    kfree(info->token);
    kfree(info);
    return error;
}

struct dentry *vtfs_mount(struct file_system_type *fs_type,int flags, const char *dev_name, void *data) {
  return mount_nodev(fs_type,flags,(void *)dev_name,vtfs_fill_super);
}

void vtfs_kill_sb(struct super_block *sb) {
    struct vtfs_sb_info *info = sb->s_fs_info;

    if (info) {
        WRITE_ONCE(info->stopping, true);
        cancel_delayed_work_sync(&info->heartbeat_work);
    }

    kill_anon_super(sb);

    if (info) {
        int error = vtfs_session_request(info, "session/end");

        if (error)
            pr_warn("vtfs: failed to end session: %d\n", error);

        kfree(info->token);
        kfree(info);
    }

    sb->s_fs_info = NULL;
    pr_info("vtfs unmounted\n");
}

