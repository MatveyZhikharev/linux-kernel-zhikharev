// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/security.h>

#define MINIFS_MAGIC 0x6D696E69

struct minifs_file {
    char *data;
    size_t size;
    size_t capacity;
};

static const struct inode_operations minifs_file_inode_operations;
static const struct inode_operations minifs_dir_inode_operations;
static const struct file_operations minifs_file_operations;

static struct minifs_file *minifs_file_alloc(void)
{
    struct minifs_file *mf = kzalloc(sizeof(*mf), GFP_KERNEL);
    if (!mf)
        return NULL;

    mf->capacity = 0;
    mf->size = 0;
    mf->data = NULL;
    return mf;
}

static void minifs_file_free(struct minifs_file *mf)
{
    if (!mf)
        return;

    kfree(mf->data);
    kfree(mf);
}

static struct inode *minifs_get_inode(struct super_block *sb,
                                      const struct inode *dir,
                                      umode_t mode, dev_t dev)
{
    struct inode *inode = new_inode(sb);
    struct minifs_file *mf = NULL;

    if (!inode)
        return NULL;

    inode->i_ino = get_next_ino();
    inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
    simple_inode_init_ts(inode);

    switch (mode & S_IFMT) {
    case S_IFDIR:
        inode->i_op = &minifs_dir_inode_operations;
        inode->i_fop = &simple_dir_operations;
        inc_nlink(inode);
        break;
    case S_IFREG:
        mf = minifs_file_alloc();
        if (!mf) {
            iput(inode);
            return NULL;
        }
        inode->i_op = &minifs_file_inode_operations;
        inode->i_fop = &minifs_file_operations;
        inode->i_private = mf;
        break;
    default:
        init_special_inode(inode, mode, dev);
        break;
    }

    return inode;
}

static int minifs_mknod(struct mnt_idmap *idmap, struct inode *dir,
                        struct dentry *dentry, umode_t mode, dev_t dev)
{
    struct inode *inode = minifs_get_inode(dir->i_sb, dir, mode, dev);
    int error = -ENOSPC;

    if (!inode)
        return error;

    error = security_inode_init_security(inode, dir, &dentry->d_name,
                                         NULL, NULL);
    if (error) {
        iput(inode);
        return error;
    }

    d_instantiate(dentry, inode);
    dget(dentry);
    inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
    return 0;
}

static int minifs_create(struct mnt_idmap *idmap, struct inode *dir,
                         struct dentry *dentry, umode_t mode, bool excl)
{
    return minifs_mknod(&nop_mnt_idmap, dir, dentry, mode | S_IFREG, 0);
}

static int minifs_mkdir(struct mnt_idmap *idmap, struct inode *dir,
                        struct dentry *dentry, umode_t mode)
{
    int ret = minifs_mknod(&nop_mnt_idmap, dir, dentry, mode | S_IFDIR, 0);
    if (!ret)
        inc_nlink(dir);
    return ret;
}

static ssize_t minifs_read(struct file *file, char __user *buf,
                           size_t len, loff_t *ppos)
{
    struct inode *inode = file_inode(file);
    struct minifs_file *mf = inode->i_private;

    if (!mf || !mf->data || mf->size == 0)
        return 0;

    return simple_read_from_buffer(buf, len, ppos, mf->data, mf->size);
}

static int minifs_grow_file(struct minifs_file *mf, size_t new_size)
{
    size_t new_capacity;
    char *new_data;

    if (new_size <= mf->capacity)
        return 0;

    new_capacity = max(new_size, mf->capacity ? mf->capacity * 2 : 64UL);
    new_data = krealloc(mf->data, new_capacity, GFP_KERNEL);
    if (!new_data)
        return -ENOMEM;

    mf->data = new_data;
    mf->capacity = new_capacity;
    return 0;
}

static ssize_t minifs_write(struct file *file, const char __user *buf,
                            size_t len, loff_t *ppos)
{
    struct inode *inode = file_inode(file);
    struct minifs_file *mf = inode->i_private;
    ssize_t written;
    size_t needed;
    int ret;

    if (*ppos < 0)
        return -EINVAL;

    if (!mf) {
        mf = minifs_file_alloc();
        if (!mf)
            return -ENOMEM;
        inode->i_private = mf;
    }

    inode_lock(inode);

    if ((size_t)*ppos > mf->size) {
        ret = minifs_grow_file(mf, (size_t)*ppos);
        if (ret)
            goto out_unlock;
        memset(mf->data + mf->size, 0, (size_t)*ppos - mf->size);
        mf->size = (size_t)*ppos;
    }

    needed = (size_t)(*ppos + len);
    ret = minifs_grow_file(mf, needed);
    if (ret)
        goto out_unlock;

    written = simple_write_to_buffer(mf->data, mf->capacity, ppos, buf, len);
    if (written > 0) {
        if ((size_t)*ppos > mf->size)
            mf->size = (size_t)*ppos;
        i_size_write(inode, mf->size);
        inode_set_mtime_to_ts(inode, inode_set_ctime_current(inode));
    }

    inode_unlock(inode);
    return written;

out_unlock:
    inode_unlock(inode);
    return ret;
}

static const struct file_operations minifs_file_operations = {
    .read = minifs_read,
    .write = minifs_write,
    .llseek = default_llseek,
};

static const struct inode_operations minifs_file_inode_operations = {
    .getattr = simple_getattr,
    .setattr = simple_setattr,
};

static const struct inode_operations minifs_dir_inode_operations = {
    .lookup = simple_lookup,
    .create = minifs_create,
    .mkdir = minifs_mkdir,
    .unlink = simple_unlink,
    .rmdir = simple_rmdir,
    .rename = simple_rename,
};

static void minifs_evict_inode(struct inode *inode)
{
    struct minifs_file *mf = inode->i_private;

    truncate_inode_pages_final(&inode->i_data);
    clear_inode(inode);

    if (mf) {
        inode->i_private = NULL;
        minifs_file_free(mf);
    }
}

static const struct super_operations minifs_super_ops = {
    .statfs = simple_statfs,
    .drop_inode = generic_delete_inode,
    .evict_inode = minifs_evict_inode,
};

static int minifs_fill_super(struct super_block *sb, void *data, int silent)
{
    struct inode *inode;
    struct dentry *root;

    sb->s_magic = MINIFS_MAGIC;
    sb->s_op = &minifs_super_ops;
    sb->s_time_gran = 1;

    inode = minifs_get_inode(sb, NULL, S_IFDIR | 0755, 0);
    if (!inode)
        return -ENOMEM;

    root = d_make_root(inode);
    if (!root)
        return -ENOMEM;

    sb->s_root = root;
    return 0;
}

static struct dentry *minifs_mount(struct file_system_type *fs_type,
                                  int flags, const char *dev_name,
                                  void *data)
{
    return mount_nodev(fs_type, flags, data, minifs_fill_super);
}

static struct file_system_type minifs_type = {
    .owner = THIS_MODULE,
    .name = "minifs",
    .mount = minifs_mount,
    .kill_sb = kill_litter_super,
};

static int __init minifs_init(void)
{
    return register_filesystem(&minifs_type);
}

static void __exit minifs_exit(void)
{
    unregister_filesystem(&minifs_type);
}

module_init(minifs_init);
module_exit(minifs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("student");
MODULE_DESCRIPTION("Mini in-memory filesystem for lab");
