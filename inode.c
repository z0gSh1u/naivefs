// =================
// inode.c
// inode操作相关的函数
// =================

#include "_naivefs.h"
#include "naivefs.h"

// 相当于naive_write_inode的实现
static struct buffer_head *naive_update_inode(struct inode *inode) {
  struct buffer_head *bh;
  struct naive_inode *ninode = naive_get_inode(inode->sb, inode->i_ino);

  // 就是反过来填信息，不加注释了
  ninode->mode = inode->i_mode;
  ninode->i_uid = inode->i_uid;
  ninode->i_gid = inode->i_gid;
  ninode->i_nlink = inode->i_nlink;
  // 考虑到union的东西是同一个类型，其实不管直接赋值给谁都是对的，写上判断语义更清楚些
  if (S_ISDIR(inode->i_mode)) {
    ninode->dir_children_count = inode->i_size;
  } else if (S_ISREG(inode->i_mode)) {
    ninode->file_size = inode->i_size;
  } else {
    // 不支持
  }
  ninode->i_atime = inode->i_atime;
  ninode->i_ctime = inode->i_ctime;
  ninode->i_mtime = inode->i_mtime;

  // 然后把这个块加标脏标记，告知系统已经修改
  mark_buffer_dirty(bh);

  return bh;
}

// 往磁盘中写入inode，即原生inode转自定义inode
// 这里借鉴minix的写法，代理个update_inode，看起来比较专业
static int naive_write_inode(struct inode *inode, int wait) {
  // 跟进去看看，说白了就是read_inode的逆方法
  brelse(naive_update_inode(inode));
  return 0;
}

// 从磁盘中读出指定inode，即自定义inode转原生inode
// 作为参数传入的inode，需要的i_ino、i_sb属性被设置好，该函数需要填充其他属性
static void naive_read_inode(struct inode *inode) {
  // 先取到自定义inode再说
  struct naive_inode *ninode = naive_get_inode(inode->i_sb, inode->i_ino);
  // 注意，存在盘上的都是自定义inode
  // 也就是说，只有ninode上才有有效信息，inode->i_mode等其他各项属性都是不可靠、需要填充的
  // 开填吧！
  inode->i_mode = ninode->mode;
  inode->i_uid = ninode->i_uid;
  inode->i_gid = ninode->i_gid;
  inode->i_nlink = ninode->i_nlink;
  inode->i_atime = ninode->i_atime;
  inode->i_ctime = ninode->i_ctime;
  inode->i_mtime = ninode->i_mtime;
  // ops和size对于不同类型的文件略有差异，在这里分类处理
  if (S_ISREG(ninode->mode)) {
    // 是一个文件
    inode->i_op = &naive_iops;
    inode->i_fop = &naive_fops;
    inode->i_aop = &naive_aops;
    inode->i_size = ninode->dir_children_count; // TODO: 合适吗？
  } else if (S_ISDIR(ninode->mode)) {
    // 是一个目录
    inode->i_op = &naive_iops;
    inode->i_fop = &naive_dops;
    inode->i_aop = &naive_aops;
    inode->i_size = ninode->file_size;
  } else {
    // lnk、tty等类型不支持，打扰了
    make_bad_inode(inode);
  }
}

// 根据inode编号在指定文件系统实例（一个超级块对应一个文件系统实例）的inode表中取出对应的自定义inode
static struct naive_inode *
naive_get_inode(struct super_block *sb,
                int ino /* struct buffer_head **p (ext2有，但naive没用) */) {
  // 先取出自定义超级块信息
  struct naive_super_block *nsb = sb->s_fs_info;

  // naive只有一个超级块、只有一个BlockGroup
  // 故下面这个简单的算式就可以算得ino对应inode在inode表中所在的块号
  // 注意这里显然要向下取整
  int block_no_of_ino =
      nsb->inode_table_block + (int)(ino * NAIVE_INODE_SIZE / NAIVE_BLOCK_SIZE);
  // 找一个bh，先读出整个块
  struct buffer_head *bh = sb_bread(sb, block_no_of_ino);

  // bh->b_data是整个块，其中有多个inode信息
  // 我们再算一下偏移量，取出所需inode
  // 编号 mod 每个块的inode数 = 所在块的块内偏移
  int offset = ino % (NAIVE_BLOCK_SIZE / NAIVE_INODE_SIZE);

  // 搞定
  struct naive_inode *ninode_head = (struct naive_inode *)bh->b_data;
  return ninode_head + offset;
}

// 用于支持在目录中找文件（根据文件名锁定文件）
struct dentry *naive_lookup(struct inode *dir, struct dentry *dentry,
                            struct nameidata *nd) {
  struct super_block *sb = dir->i_sb;

  struct naive_inode *ninode = naive_get_inode(sb, dir->i_ino);
  int data_block_no = ninode->block[0];
  struct buffer_head *bh;
  bh = sb_bread(sb, data_block_no);

  struct naive_dir_record *record_ptr = (struct naive_dir_record *)bh->b_data;
  struct inode *inode;

  int i;
  for (i = 0; i < ninode->dir_children_count; i++) {
    if (strcmp(dentry->d_name.name, record_ptr->filename) == 0) {
      // 文件名相同，找到了
      // TODO: iget?
      inode = iget(sb, record_ptr->inode_no);
      d_add(dentry, inode);
      brelse(bh);
      return NULL;
    }
    record_ptr++;
  }
  d_add(dentry, NULL);
  brelse(bh);
  return NULL;
}