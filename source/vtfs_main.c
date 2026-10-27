#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>

#include "vtfs.h"


static struct file_system_type vtfs_fs_type = {
  .owner = THIS_MODULE,
  .name = "vtfs",
  .mount = vtfs_mount,
  .kill_sb = vtfs_kill_sb,
};

static int __init vtfs_init(void) {
  int error = register_filesystem(&vtfs_fs_type);
  if (error) {
    pr_err("vtfs: error code: %d\n",error);
  }
  return error;
}

static void __exit vtfs_exit(void) {
  unregister_filesystem(&vtfs_fs_type);
}

module_init(vtfs_init);
module_exit(vtfs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Z1rota");
MODULE_DESCRIPTION("virtual filesystem for Os course by Smirnov Andrey P3313");