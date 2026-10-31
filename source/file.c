#include <linux/fs.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <asm/unaligned.h>
#include "vtfs.h"
#include "http.h"

#define VTFS_READ_CHUNK 4096
#define VTFS_WRITE_CHUNK 4096

static ssize_t vtfs_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    struct inode *inode = file_inode(file);
    const char *token = inode->i_sb->s_fs_info;
    char inode_id[24],offset[24],length[24];
    char *response;
    size_t received = 0;
    int64_t status;

    if (*ppos < 0) {
        return -EINVAL;
    }

    if (count ==0) {
        return 0;
    }

    if (count > VTFS_READ_CHUNK) {
        count = VTFS_READ_CHUNK;
    }

    response = kmalloc(count, GFP_KERNEL);
    if (!response) {
        return -ENOMEM;
    }
    
    snprintf(inode_id,sizeof(inode_id),"%lu",inode->i_ino);
    snprintf(offset,sizeof(offset),"%lld",(long long)*ppos);
    snprintf(length, sizeof(length), "%zu",count);

    status  = vtfs_http_call(token,"read",response,count,&received,3,"inode",inode_id,"offset",offset,"length",length);
    

    if (status != 0) {
        kfree(response);
        return status > 0 ? -(int)status : -EIO;
    }

    if (copy_to_user(buf,response,received)) {
        kfree(response);
        return -EFAULT;
    }

    *ppos +=received;
    kfree(response);
    return received;
    
}

static ssize_t vtfs_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {

    struct inode *inode = file_inode(file);
    const char *token = inode->i_sb->s_fs_info;
    struct timespec64 now;
    char *data;
    size_t size;
    loff_t offset;
    int64_t status;
    char path[256];
    char response[4];
    size_t received = 0;

    if (*ppos < 0) {
        return -EINVAL;
    }

    if (count == 0) {
        return 0;
    }

    size = min_t(size_t, count, VTFS_WRITE_CHUNK);

    data = kmalloc(size,GFP_KERNEL);
    if (!data) {
        return -ENOMEM;
    }

    if (copy_from_user(data,buf,size)) {
        kfree(data);
        return -EFAULT;
    }

    inode_lock(inode);

    offset = (file->f_flags & O_APPEND) ? i_size_read(inode) : *ppos;

    if (offset < 0 || offset > S64_MAX - size) {
        inode_unlock(inode);
        kfree(data);
        return -EFBIG;
    }

    snprintf(path, sizeof(path),
         "/api/write?token=%s&inode=%lu&offset=%lld",
         token, inode->i_ino, (long long)offset);

    status = vtfs_http_post(path, data, size, response, sizeof(response), &received);

    if (status == 0) {
    if (received != sizeof(u32) || get_unaligned_le32(response) != size)
        status = -EIO;
    }


    if (status != 0) {
        inode_unlock(inode);
        kfree(data);
        return status > 0 ? -(int)status : (ssize_t)status;
    }

    *ppos = offset + size;

    if (*ppos > i_size_read(inode)) {
        i_size_write(inode, *ppos);
    }
    now = inode_set_ctime_current(inode);
    inode_set_mtime_to_ts(inode,now);
    inode_unlock(inode);
    kfree(data);
    return size;
}

const struct file_operations vtfs_file_ops = {
    .owner = THIS_MODULE,
    .llseek = generic_file_llseek,
    .read = vtfs_read,
    .write = vtfs_write,
};