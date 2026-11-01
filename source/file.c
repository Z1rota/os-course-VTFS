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

static int vtfs_open(struct inode *inode, struct file *file) {
    const char *token = vtfs_token(inode->i_sb);
    char session_id[24];
    char inode_id[24];
    char response[8];
    size_t received = 0;
    int64_t status;
    u64 *handle;

    handle = kmalloc(sizeof(*handle),GFP_KERNEL);
    if (!handle) {
        return -ENOMEM;
    }

    snprintf(session_id,sizeof(session_id),"%llu",(unsigned long long)vtfs_session_id(inode->i_sb));
    snprintf(inode_id,sizeof(inode_id),"%llu",(unsigned long long)inode->i_ino);
    status = vtfs_http_call(token,"open",response,sizeof(response),&received,2,"session",session_id,"inode",inode_id);
    if (status != 0) {
        kfree(handle);
        return status > 0 ? -(int)status : -EIO;
    }

    if (received != sizeof(response)) {
        kfree(handle);
        return -EIO;
    }
    file->private_data = handle;
    return 0;
}

static int vtfs_release(struct inode *inode, struct file *file) {
    const char *token = vtfs_token(inode->i_sb);
    u64 *handle = file->private_data;
    char session_id[24];
    char handle_id[24];
    char response[1];
    size_t received = 0;
    int64_t status;

    if (!handle) {
        return 0;
    }
    snprintf(session_id, sizeof(session_id), "%llu",(unsigned long long)vtfs_session_id(inode->i_sb));
    snprintf(handle_id, sizeof(handle_id), "%llu", (unsigned long long)*handle);

     status = vtfs_http_call(token, "close",response, sizeof(response), &received,2, "session", session_id,"handle", handle_id);
    if (status != 0 || received != 0) {
        pr_err("vtfs: failed to close handle %llu, status=%lld\n",(unsigned long long)*handle, (long long)status);
    }

    kfree(handle);
    file->private_data = NULL;

    return 0;
}

static ssize_t vtfs_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    struct inode *inode = file_inode(file);
    const char *token = vtfs_token(inode->i_sb);
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
    const char *token = vtfs_token(inode->i_sb);
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
    .open = vtfs_open,
    .release = vtfs_release,
};