// =================
// _naivefs.h
// 文件系统实现使用
// =================

#ifndef _NAIVEFS_H_
#define _NAIVEFS_H_

#include "naivefs.h"

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
// ================= inode.c =================
static struct buffer_head *naive_update_inode(struct inode *inode);
static int naive_write_inode(struct inode *inode, int wait);
static void naive_read_inode(struct inode *inode);
static struct naive_inode *naive_get_inode(struct super_block *sb, int ino);
struct dentry *naive_lookup(struct inode *dir, struct dentry *dentry,
                            struct nameidata *nd);
// ================= bitmap.c =================
static int naive_new_block_no(struct super_block *sb);
static int naive_new_inode_no(struct super_block *sb);

// 用于取super_block上的私有域
static struct naive_super_block *NAIVE_SB(struct super_block *sb) {
  return sb->s_fs_info;
}

#endif