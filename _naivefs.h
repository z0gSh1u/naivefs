// =================
// _naivefs.h
// 文件系统实现使用
// =================

#ifndef _NAIVEFS_H_
#define _NAIVEFS_H_

#include "naivefs.h"
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <stdbool.h>

// 全部ops的预定义
static struct super_operations naive_sops;
static struct inode_operations naive_iops;
static struct file_operations naive_fops;
static struct file_operations naive_dops;
static struct address_space_operations naive_aops;

// 全部相关函数的预定义
// ================= dir.c =================
static int naive_readdir(struct file *filp, void *dirent, filldir_t filldir);
// ================= file.c =================
static int naive_create(struct inode *dir, struct dentry *dentry, int mode,
                        struct nameidata *nd);
static int naive_mkdir(struct inode *dir, struct dentry *dentry, int mode,
                       struct nameidata *nd);
static int naive_mknod(struct inode *dir, struct dentry *dentry, int mode);
static int save_block(struct super_block *sb, int block_no, void *data,
                      int size);
static int save_inode(struct super_block *sb, struct naive_inode *ninode);
static void write_back_block(struct super_block *sb, int block_no, void *data,
                             int size);
static void write_back_ninode(struct super_block *sb,
                              struct naive_inode *ninode);
// ================= inode.c =================
static struct buffer_head *naive_update_inode(struct inode *inode);
static int naive_write_inode(struct inode *inode, int wait);
static void naive_read_inode(struct inode *inode);
static struct naive_inode *naive_get_inode(struct super_block *sb, int ino);
struct dentry *naive_lookup(struct inode *dir, struct dentry *dentry,
                            struct nameidata *nd);
void inode_init_owner(struct inode *inode, const struct inode *dir,
                      umode_t mode);
// ================= bitmap.c =================
static int naive_new_block_no(struct super_block *sb);
static int naive_new_inode_no(struct super_block *sb);
static void set_bmap_bit(struct super_block *sb, int block_no, bool to);
static void set_imap_bit(struct super_block *sb, int block_no, bool to);
// ================= naivefs.c =================
static void naive_put_super(struct super_block *sb);
static int naive_fill_super(struct super_block *sb, void *data, int silent);
static int naive_get_sb(struct file_system_type *fs_type, int flags,
                        const char *dev_name, void *data, struct vfsmount *mnt);
static struct file_system_type naive_fs_type;
static int __init init_naivefs(void);
static void __exit exit_naivefs(void);

// 用于取super_block上的私有域
static struct naive_super_block *NAIVE_SB(struct super_block *sb) {
  return sb->s_fs_info;
}

#endif
