// =================
// bitmap.c
// bitmap操作相关函数
// =================

// bit=1为已使用，0为未使用

#include "_naivefs.h"
#include "naivefs.h"

// 获取一个可用的空data_block编号
// 跟下面那个基本一样，不写注释了
static int naive_new_block_no(struct super_block *sb) {
  struct naive_super_block *nsb = NAIVE_SB(sb);
  int bmap_size = nsb->block_total / 8;
  _Byte *bmap = kmalloc(bmap_size, GFP_KERNEL);
  struct buffer_head *bh = sb_bread(sb, NAIVE_BMAP_BLOCK);
  memcpy(bmap, (_Byte *)(bh->b_data), bmap_size);

  int res = nsb->data_block_no;
  _Byte *bmap_ptr = bmap;
  while (*(bmap_ptr++) == 0x00ff)
    res += 8;
  _Byte detector = 1;
  while ((*bmap_ptr) & detector == 1) {
    detector <<= 1;
    res += 1;
  }

  brelse(bh);
  kfree(bmap);
  return res;
}

// 获取一个可用的空inode编号，与自带的new_inode不同的是，该方法采用bitmap确定空闲inode编号
static int naive_new_inode_no(struct super_block *sb) {
  // 先取了超级块再说
  struct naive_super_block *nsb = NAIVE_SB(sb);

  // 先把imap读进内存
  int imap_size = nsb->block_total / 8; // 1bit每块，故除以8为字节数
  _Byte *imap = kmalloc(imap_size, GFP_KERNEL);
  // 根据布局图，inode_table_block之前为imap_block，且只有一块
  // naivefs不考虑磁盘空间非常大，使得一块不足以支持imap存放的情况
  struct buffer_head *bh = sb_bread(sb, NAIVE_IMAP_BLOCK);
  memcpy(imap, (_Byte *)(bh->b_data), imap_size);

  // 现在，只需要在imap找到首个为0（未使用）的位即可
  // 简单的二进制
  int res = NAIVE_ROOT_INODE_NO + 1;
  _Byte *imap_ptr = imap;
  while (*(imap_ptr++) == 0x00ff)
    res += 8;
  _Byte detector = 1;
  while ((*imap_ptr) & detector == 1) {
    detector <<= 1;
    res += 1;
  }

  // 搞定
  brelse(bh);
  kfree(imap);
  return res;
}

// 把某块的bmap对应bit置值
static void set_bmap_bit(struct super_block *sb, int block_no, bool to) {
  // 取自定义超级块
  struct naive_super_block *nsb = NAIVE_SB(sb);
  // 读出整个bmap
  // FIXME: 在不对naivefs数据块的数量进行限制的前提下，并不一定保证bmap只占一块
  int bmap_size = nsb->block_total / 8;
  _Byte *bmap = kmalloc(bmap_size, GFP_KERNEL);
  struct buffer_head *bh = sb_bread(sb, NAIVE_BMAP_BLOCK);
  memcpy(bmap, (_Byte *)(bh->b_data), bmap_size);

  // 做点数学
  int line = block_no / 8;   // 在第几个Byte
  int offset = block_no % 8; // 在第line个Byte的第几bit
  int bit = to == true ? 1 : 0;
  *(bmap + line) |= (bit << offset);

  // 改完拷回去
  // TODO: 有没有必要一次写整块？
  memcpy(bh->b_data, bmap, NAIVE_BLOCK_SIZE);
  map_bh(bh, sb, NAIVE_BMAP_BLOCK);

  brelse(bh);
}

// 把某块的imap对应bit置值
static void set_imap_bit(struct super_block *sb, int block_no, bool to) {
  struct naive_super_block *nsb = NAIVE_SB(sb);
  int imap_size = nsb->block_total / 8;
  _Byte *imap = kmalloc(imap_size, GFP_KERNEL);
  struct buffer_head *bh = sb_bread(sb, NAIVE_IMAP_BLOCK);
  memcpy(imap, (_Byte *)(bh->b_data), imap_size);

  int line = block_no / 8;
  int offset = block_no % 8;
  int bit = to == true ? 1 : 0;
  *(imap + line) |= (bit << offset);

  memcpy(bh->b_data, imap, NAIVE_BLOCK_SIZE);
  map_bh(bh, sb, NAIVE_BMAP_BLOCK);

  brelse(bh);
}
