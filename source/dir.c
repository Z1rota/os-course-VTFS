#include <linux/fs.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/limits.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <asm/unaligned.h>

#include "vtfs.h"
#include "http.h"

#define VTFS_LIST_BUFFER_SIZE 8192

static char *vtfs_encode_name(const struct dentry *dentry);

static struct dentry *vtfs_lookup(struct inode *parent, struct dentry *dentry, unsigned int flags) {
    char response[25];
    char parent_id[24];
    char *encoded_name;
    size_t received;
    int64_t status;
    struct inode *inode;
    const char *token = parent->i_sb->s_fs_info;

    if (dentry->d_name.len > NAME_MAX) {
        return ERR_PTR(-ENAMETOOLONG);
    }

    
    encoded_name = vtfs_encode_name(dentry);
    if (IS_ERR(encoded_name)) {
        return ERR_CAST(encoded_name);
    }

    snprintf(parent_id, sizeof(parent_id),"%lu",parent->i_ino);
    status = vtfs_http_call(token,"lookup",response,sizeof(response),&received,2,"parent",parent_id,"name",encoded_name);
    kfree(encoded_name);
    if (status == ENOENT) {
        d_add(dentry,NULL);
        return NULL;
    }

    if (status != 0) {
        return ERR_PTR(status > 0 ? -(int)status: -EIO);
    }
    if (received != sizeof(response)) {
        return ERR_PTR(-EIO);
    }

    inode = vtfs_get_inode(parent->i_sb,get_unaligned_le64(response),response[8],get_unaligned_le32(response + 9),get_unaligned_le64(response+13),get_unaligned_le32(response + 21));

    if (IS_ERR(inode)) {
        return ERR_PTR(PTR_ERR(inode));
    }
    return d_splice_alias(inode,dentry);

}

static int vtfs_iterate(struct file *file, struct dir_context *ctx) {
    struct inode *inode = file_inode(file);
    const char *token = inode->i_sb->s_fs_info;
    char parent_id[24];
    char *response;
    size_t received, offset;
    u32 count, i;
    int64_t status;
    int error = 0;

    if (!dir_emit_dots(file,ctx)) {
        return 0;
    }
    response =kmalloc(VTFS_LIST_BUFFER_SIZE,GFP_KERNEL);
    
    if (!response) {
        return -ENOMEM;
    }

    snprintf(parent_id,sizeof(parent_id),"%lu",inode->i_ino);

    status = vtfs_http_call(token,"list",response,VTFS_LIST_BUFFER_SIZE,&received,1,"parent",parent_id);
    
    if (status != 0) {
        error = status > 0 ? -(int)status : -EIO;
        goto out;
    }

    if (received < sizeof(u32)) {
        error = -EIO;
        goto out;
    }

    count = get_unaligned_le32(response);
    offset = sizeof(u32);

    for (i=0;i<count; i++) {
        u16 name_len;
        u64 entry_id;
        u8 type;
        const char *name;

        if (received - offset < sizeof(u16)) {
            error = -EIO;
            break;
        }

        name_len = get_unaligned_le16(response+offset);
        offset+=sizeof(u16);

        if (!name_len || name_len > NAME_MAX || received - offset < (size_t)name_len+sizeof(u64)+1) {
            error = -EIO;
            break;
        }
        name = response + offset;
        offset += name_len;

        entry_id = get_unaligned_le64(response+offset);
        offset+=sizeof(u64);

        type = response[offset++];

        if (type != VTFS_FILE && type != VTFS_DIRECTORY) {
            error = -EIO;
            break;
        }

        if (ctx->pos > (loff_t)i+2) {
            continue;
        }

        if (!dir_emit(ctx,name,name_len,entry_id,type == VTFS_DIRECTORY ? DT_DIR : DT_REG)) {
            break;
        }
        ctx->pos = (loff_t)i+3;
    }

    out:
        kfree(response);
        return error;
}

static int vtfs_create_node(struct inode *parent, struct dentry *dentry, bool directory) {
    const char *token = parent->i_sb->s_fs_info;
    const char *method = directory ? "mkdir" : "create";
    char response[25];
    char parent_id[24];
    char *name;
    struct inode *inode;
    struct timespec64 now;
    size_t received = 0;
    int64_t status;

    name = vtfs_encode_name(dentry);
    if (IS_ERR(name)) {
        return PTR_ERR(name);
    }
    snprintf(parent_id,sizeof(parent_id),"%lu",parent->i_ino);

    status = vtfs_http_call(token,method,response,sizeof(response),&received,2,"parent",parent_id,"name",name);
    kfree(name);
    
    if (status != 0) {
        return status > 0 ? -(int)status : -EIO;
    }

    if (received != sizeof(response)) {
        return -EIO;
    }
    
    if (response[8] != (directory ? VTFS_DIRECTORY : VTFS_FILE)) {
        return -EIO;
    }
    inode = vtfs_get_inode(parent->i_sb,get_unaligned_le64(response),response[8], get_unaligned_le32(response+9),get_unaligned_le64(response+13),get_unaligned_le32(response + 21));

if (IS_ERR(inode)) {
    return PTR_ERR(inode);
}
d_instantiate(dentry,inode);

if (directory) {
    inc_nlink(parent);
}
now = inode_set_ctime_current(parent);
    inode_set_mtime_to_ts(parent,now);
    return 0;
}

static int vtfs_delete_node(struct inode *parent,struct dentry *dentry, bool directory) {
    const char *token = parent->i_sb->s_fs_info;
    const char *method = directory ? "rmdir" : "unlink";
    struct inode *inode = d_inode(dentry);
    char parent_id[24];
    char *name;
    char response[1];
    size_t received = 0;
    int64_t status;
    struct timespec64 now;

    name = vtfs_encode_name(dentry);
    if (IS_ERR(name)) {
        return PTR_ERR(name);
    }
    snprintf(parent_id,sizeof(parent_id),"%lu",parent->i_ino);

    status = vtfs_http_call(token,method,response,sizeof(response),&received,2,"parent",parent_id,"name",name);
    kfree(name);

    if (status != 0) {
        return status > 0 ? -(int)status : -EIO;
    }

    if (received != 0) {
        return -EIO;
    }

    if (directory) {
        clear_nlink(inode);
        drop_nlink(parent);
    } else {
        drop_nlink(inode);
    }

    now = inode_set_ctime_current(parent);
    inode_set_mtime_to_ts(parent,now);
    inode_set_ctime_current(inode);

    return 0;
}

static int vtfs_create(struct mnt_idmap *idmap,struct inode *parent, struct dentry *dentry, umode_t mode, bool excl) {
    return vtfs_create_node(parent,dentry,false);
}

static int vtfs_mkdir(struct mnt_idmap *idmap,struct inode *parent, struct dentry *dentry, umode_t mode) {
    return vtfs_create_node(parent,dentry,true);
}

static int vtfs_unlink(struct inode *parent, struct dentry *dentry) {
    return vtfs_delete_node(parent,dentry,false);
}
static int vtfs_rmdir(struct inode *parent, struct dentry *dentry) {
    return vtfs_delete_node(parent,dentry,true);
}

static int vtfs_link(struct dentry *old_dentry, struct inode *parent, struct dentry *new_dentry) {
    struct inode *inode = d_inode(old_dentry);
    const char *token = parent->i_sb->s_fs_info;
    char inode_id[24];
    char parent_id[24];
    char response[25];
    char *name;
    size_t received = 0;
    int64_t status;
    struct timespec64 now;

    if (!S_ISREG(inode->i_mode)) {
        return -EPERM;
    }
    name = vtfs_encode_name(new_dentry);

    if (IS_ERR(name)) {
        return PTR_ERR(name);
    }
    snprintf(inode_id,sizeof(inode_id),"%lu",inode->i_ino);
    snprintf(parent_id,sizeof(parent_id),"%lu",parent->i_ino);

    status = vtfs_http_call(token,"link",response,sizeof(response),&received,3,"inode",inode_id,"parent",parent_id,"name",name);

    kfree(name);
    if (status != 0) {
        return status > 0 ? -(int)status : -EIO;
    }

    if (received != sizeof(response) || get_unaligned_le64(response) != inode->i_ino || response[8] != VTFS_FILE) {
        return -EIO;
    }
    ihold(inode);
    inc_nlink(inode);
    d_instantiate(new_dentry,inode);
    inode_set_ctime_current(inode);
    now = inode_set_ctime_current(parent);
    inode_set_mtime_to_ts(parent,now);
    return 0;
}

const struct inode_operations vtfs_dir_inode_ops = {
    .lookup = vtfs_lookup,
    .create = vtfs_create,
    .mkdir = vtfs_mkdir,
    .rmdir = vtfs_rmdir,
    .unlink = vtfs_unlink,
    .link = vtfs_link,
};
const struct file_operations vtfs_dir_file_ops = {
    .owner = THIS_MODULE,
    .llseek = generic_file_llseek,
    .read = generic_read_dir,
    .iterate_shared = vtfs_iterate,
};

static char *vtfs_encode_name(const struct dentry *dentry)
{
    char name[NAME_MAX + 1];
    char *encoded;
    size_t length = dentry->d_name.len;

    if (length > NAME_MAX)
        return ERR_PTR(-ENAMETOOLONG);

    encoded = kmalloc(length * 3 + 1, GFP_KERNEL);
    if (!encoded)
        return ERR_PTR(-ENOMEM);

    memcpy(name, dentry->d_name.name, length);
    name[length] = '\0';
    encode(name, encoded);

    return encoded;
}