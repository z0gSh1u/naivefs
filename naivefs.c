// =================
// super.c
// 主体文件、超级块相关
// =================

#include "naivefs.h"
#include "_naivefs.h"

// sops实现了inode的读写和naive的卸载
static struct super_operations naive_sops = {
    .read_inode = naive_read_inode,
    .write_inode = naive_write_inode,
    .put_super = naive_put_super,
    .statfs = simple_statfs,
};

// iops实现了常用的三个
static struct inode_operations naive_iops = {
    .lookup = naive_lookup,
    .create = naive_create,
    .mkdir = naive_mkdir,
};

// fops基本不需要特殊处理，全部靠系统自带完成即可
static struct file_operations naive_fops = {
    .llseek = generic_file_llseek,
    .read = do_sync_read,
    .aio_read = generic_file_aio_read,
    .write = do_sync_write,
    .aio_write = generic_file_aio_write,
    .mmap = generic_file_mmap,
    .sendfile = generic_file_sendfile,
};

static struct file_operations naive_dops = {
    .read = generic_read_dir,
    .readdir = naive_readdir,
};

static struct address_space_operations naive_aops = {
    .readpage = simple_readpage,
    .sync_page = block_sync_page,
    .commit_write = generic_commit_write,
};

// 该函数说明了如何卸载文件系统，主要是做一些清理善后工作
static void naive_put_super(struct super_block *sb) {
  // 释放超级块即可
  struct naive_super_block *nfs = NAIVE_SB(sb);
  if (nfs == NULL)
    return;
  kfree(nfs);
  return;
}

// 该函数说明了如何从磁盘读出超级块，读出的结果填充到第一个参数sb
static int naive_fill_super(struct super_block *sb, void *data, int silent) {
  // 从磁盘上读块，主要借助buffer_head指针和sb_bread来完成
  struct buffer_head *bh = sb_bread(sb, NAIVE_SUPER_BLOCK_BLOCK);
  // 我们在超级块的b_data中放的是自定义超级块信息
  struct naive_super_block *nsb = (struct naive_super_block *)bh->b_data;

  // 现在，填充这些系统侧需要的基本信息
  sb->s_magic = nsb->magic; // 魔数
  sb->s_op = &naive_sops;   // sops
  sb->s_maxbytes =
      NAIVE_BLOCK_SIZE * NAIVE_BLOCK_PER_FILE; // 声明每个文件的最大大小
  sb->s_fs_info = nsb; // 将自定义super_block结构放到私有域

  // 我们还需要拼装一个根目录的inode，也叫根inode，这个inode要关联到超级块
  // 利用new_inode方法可以取到一个可用的空inode
  struct inode *root_inode = new_inode(sb);
  // 填一些信息
  inode_init_owner(root_inode, NULL, 0755 | S_IFDIR);
  // 继续填一些信息
  struct naive_inode *root_ninode = naive_get_inode(sb, NAIVE_ROOT_INODE_NO);
  root_inode->i_ino = NAIVE_ROOT_INODE_NO;
  root_inode->i_sb = sb;
  root_inode->i_mode = root_ninode->mode;
  // FIXME: 把目录下文件数作为i_size是否合适？
  root_inode->i_size = root_ninode->dir_children_count;
  // add，modify，create
  root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime =
      CURRENT_TIME;
  // 增加一个链接数，避免被回收
  root_inode->i_nlink++;
  // i_op是inode操作集，f_op是文件对象操作集，由于root_inode对应一个目录，所以这里给fop赋dops
  root_inode->i_op = &naive_iops;
  root_inode->i_fop = &naive_dops;
  // 和super_block类似，原生inode也提供了一个私有域来放自定义inode，但是很多文件系统都没用到
  // 先写着再说吧，因为实际上只要有i_ino，调用naive_get_inode去找也可以
  root_inode->i_private = root_ninode;

  // 最后关联根inode和超级块即可
  sb->s_root = d_alloc_root(root_inode);

  // buffer_header用完应当释放（不放也行）
  brelse(bh);
  return 0;
}

// 该函数声明了如何获取一个超级块
// 获取超级块借助自带的get_sb_bdev即可，注意naive_fill_super说明了如何填充超级块
// 这里和指导书不一样，跟进代码后发现2.6.21.7下get_sb_nodev原型返回值为int，且增加mnt参数
static int naive_get_sb(struct file_system_type *fs_type, int flags,
                        const char *dev_name, void *data,
                        struct vfsmount *mnt) {
  return get_sb_bdev(fs_type, flags, dev_name, data, naive_fill_super, mnt);
}

// 文件系统类型定义
static struct file_system_type naive_fs_type = {
    .owner = THIS_MODULE,
    .name = "naivefs",
    .fs_flags = FS_REQUIRES_DEV,
    .get_sb = naive_get_sb,
    .kill_sb = kill_block_super, // 销毁超级块用自带的方法即可
};

// 将文件系统作为可插拔模块注册到系统
static int __init init_naivefs(void) {
  return register_filesystem(&naive_fs_type);
}
// 拔出文件系统模块
static void __exit exit_naivefs(void) {
  unregister_filesystem(&naive_fs_type);
}
// 声明插拔函数
module_init(init_naivefs)
module_exit(exit_naivefs)
MODULE_AUTHOR("ZHUOXU");
