// =================
// dir.c
// 目录操作相关的函数
// =================

#include "_naivefs.h"
#include "naivefs.h"

// 这个函数说明了如何遍历一个目录，获取其中的文件信息，实现它，文件系统就可以支持ls命令
// 注意，这里filp指向的是目录，而不是一个文件（这也是为什么它归在dops中）
// 但在linux里万物皆文件，所以这里用的仍是file*，这略有歧义，在此说明
// 另外，filldir_t是一个函数指针类型，也就是说filldir是一个回调函数
static int naive_readdir(struct file *filp, void *dirent, filldir_t filldir) {
  // 先把超级块拿到再说
  struct super_block *sb = filp->f_inode->i_sb;
  // 这样就可以拿到目录的inode了
  int ino_of_file = filp->f_inode->i_ino;
  struct naive_inode *ninode = naive_get_inode(sb, ino_of_file);

  // 先考虑没有下属文件（请树立目录也是文件的概念）的情况
  if (ninode->dir_children_count == 0 || ninode->blocks == 0)
    return 0;

  // 现在进入正题，读出所有文件
  // 先开个缓冲区
  struct naive_dir_record *dir_records =
      kzmalloc(ninode->dir_children_count * NAIVE_DIR_RECORD_SIZE, GFP_KERNEL);
  // 开始向缓冲区填信息
  struct buffer_head *bh;
  int i, unread_children = ninode->dir_children_count;
  // 请集中注意力，下面这个循环的处理非常精妙而严谨，请仔细体会
  // 由于可能出现一个文件（child）占多个块的情况，因此这里的循环条件要做双重判断
  for (i = 0; i < ninode->blocks && unread_children > 0; i++) {
    // 读出数据块
    bh = sb_bread(sb, ninode->block[i]);
    // 剩下没读的record占多少字节
    int tail_rest = unread_children * NAIVE_DIR_RECORD_SIZE;
    // 已经读的record数
    int head_done_records = ninode->dir_children_count - unread_children;
    // 已经读的占多少字节
    int head_done_bytes =
        (ninode->dir_children_count - unread_children) * NAIVE_DIR_RECORD_SIZE;
    if (tail_rest < NAIVE_BLOCK_SIZE) {
      // 剩下没读的已经不足一个块，这说明sb_bread发生了超读，要进行仔细的收尾处理
      // FIXME
      memcpy(dir_records + head_done_records, bh->b_data, tail_rest);
      break;
    } else {
      // 其他情况直接读即可，不用特别控制长度
      memcpy(dir_records + head_done_records, bh->b_data, NAIVE_BLOCK_SIZE);
      unread_children -= NAIVE_BLOCK_SIZE / NAIVE_DIR_RECORD_SIZE;
    }
  }

  // 之后，调用filldir来告知系统目录下有哪些文件
  // 可以看到，由于我们利用dir_record来接管目录下的文件记录，此时取文件名和inode号变得无比简单
  // 全程不需要与系统原生的dentry互动
  for (i = 0; i < ninode->dir_children_count; i++) {
    filldir(dirent, dir_records[i].filename, strlen(dir_records[i].filename),
            dir_records[i].inode_no, DT_REG); // FIXME: DT_DIR / DT_REG？
  }

  // 释放资源，搞定
  brelse(bh);
  kfree(dir_records);
  return 0;
}