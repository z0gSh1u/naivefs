// =================
// file.c
// 文件操作相关的函数
// =================

#include "_naivefs.h"
#include "naivefs.h"

// 创建文件
static int naive_create(struct inode *dir, struct dentry *dentry, int mode,
                        struct nameidata *nd) {
  return naive_mknod(dir, dentry, mode);
}

// 创建目录
static int naive_mkdir(struct inode *dir, struct dentry *dentry, int mode,
                       struct nameidata *nd) {
  // 注意！根据资料，虽然naive_mkdir会被系统在创建文件夹时调用，
  // 但是调用时系统给的mode并不激活IFDIR位！这大概是一个bug。
  // 不管怎样，手动与上IFDIR。
  return naive_mknod(dir, dentry, mode | S_IFDIR);
}

// 该函数将帮助我们创建目录和文件，这里考虑更普遍的情况——创建结点（mknod）
// dir：要创建的文件所在的目录；dentry：新建的文件的目录项
// 请建立“目录也是文件”的概念，所以dentry的信息可能是新目录的，也可能是新文件的
// 这也是仿照minix写的
static int naive_mknod(struct inode *dir, struct dentry *dentry, int mode) {
  // 按照惯例，先拿超级块
  struct super_block *sb = dir->i_sb;
  struct naive_super_block *nsb = NAIVE_SB(sb);

  // 要创建的文件名
  const char *filename = dentry->d_name.name;
  // 拿到目录的自定义inode
  struct naive_inode *dir_ninode = naive_get_inode(sb, dir->i_ino);

  // 为新文件分配一个inode号
  // 思考：系统是否保证create的不可重入性？
  int inode_no_to_use = naive_new_inode_no(sb);

  // 拼装这个inode，还是那个套路
  // 顺手把对应的自定义inode也拼了吧
  struct inode *inode;
  struct naive_inode ninode;
  inode = new_inode(sb);
  inode->i_ino = inode_no_to_use;
  ninode.inode_no = inode->i_ino;
  // 这里的dir可不能给NULL了，因为已经不是根目录了
  inode_init_owner(inode, dir, mode);
  // 其余属性也填上去
  inode->i_op = &naive_iops;
  inode->i_atime = inode->i_ctime = inode->i_mtime = CURRENT_TIME;
  ninode.i_atime = ninode.i_ctime = ninode.i_mtime = inode->i_atime;
  // inode的uid、gid等属性不用手动填，在init_owner时会继承dir的
  ninode.i_uid = inode->i_uid;
  ninode.i_gid = inode->i_gid;
  ninode.i_nlink = inode->i_nlink;
  ninode.mode = mode;
  // 再来分类讨论一下类型
  if (S_ISDIR(mode)) {
    inode->i_size = 1; // TODO: 正确吗？
    inode->i_blocks = 1;
    inode->i_fop = &naive_dops;
    ninode.blocks = 1;
    ninode.dir_children_count = 2; // .和..
    // 把我们的钉子户.和..加进去
    // 先处理.，分配这个inode管辖的第一个块
    ninode.block[0] = naive_new_block_no(sb);
    // 然后把这条记录准备一下
    struct naive_dir_record dir_dot;
    strcpy(dir_dot.filename, ".");
    dir_dot.inode_no = inode_no_to_use;
    // 再处理..
    struct naive_dir_record dir_dotdot;
    strcpy(dir_dotdot.filename, "..");
    // 注意注意，它归属的inode是上级目录的inode，不是该目录的inode
    dir_dotdot.inode_no = dir->i_ino;
    // 好了，现在把inode信息和目录记录都写进磁盘！
    save_inode(sb, ninode);

    // TODO: naivefs不考虑复杂的异常情况，其实这里应该把整个文件系统的空闲块减1
  } else if (S_ISREG(mode)) {
    inode->i_size = 0;
    inode->i_blocks = 0;
    inode->i_fop = &naive_fops;
    inode->i_mapping->a_ops = &naive_aops;
    ninode.blocks = 0;
    ninode.file_size = 0;
    // 好了，现在把inode信息和目录记录都写进磁盘！

  } else {
    make_bad_inode(inode);
    return 0;
  }
  // 还没完呢

  // 别忘了告诉系统这些地方已经脏了，有空更新一下
  mark_inode_dirty(inode);
  mark_inode_dirty(dir);
  d_instantiate(dentry, inode);
  return 0;
}

static int save_block(struct super_block *sb, int block_no, void *data,
                      int size) {
  struct naive_super_block *nsb = NAIVE_SB(sb);
  struct buffer_head *bh = sb_bread(sb, )
}

static int save_inode(struct super_block *sb, struct naive_inode *ninode) {
  // 拿自定义超级块，不说了
  struct naive_super_block *nsb = NAIVE_SB(sb);

  // 让我们再做点数学
  // 应该放到inode表的哪个块
  int block_no = nsb->inode_table_block +
                 ninode->inode_no * NAIVE_INODE_SIZE / NAIVE_BLOCK_SIZE;
  // 在这个块中的偏移量是多少
  int block_offset = ninode->inode_no % (NAIVE_BLOCK_SIZE / NAIVE_INODE_SIZE);

  // 现在开始上盘
  struct buffer_head *bh = sb_bread(sb, block_no);
  // 块首指针
  struct naive_inode *block_head = (struct naive_inode *)bh->b_data;
  memcpy(block_head + block_offset, ninode, NAIVE_INODE_SIZE);
  // sb_bread只是读出来放到内存，前面更改的也是内存，并没有写回盘
  // 为了上盘，要建立映射，这是从ext2学的
  map_bh(bh, sb, block_no);

  // 完事
  brelse(bh);
  return 0;
}