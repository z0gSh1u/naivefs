// =================
// mkfs.naive.c
// naivefs格式化工具
// =================

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "naivefs.h"

static struct naive_super_block nsb;
static _Byte *bmap;
static _Byte *imap;
static int disk_size;
static int inode_table_size;

// 按排布图来布局分区
static void format_disk(int fd, const char *path) {
  // 先获取设备（磁盘）的总大小
  struct stat stat_;
  stat(path, &stat_);
  disk_size = stat_.st_size;
  printf("[mkfs_naive] Capacity of disk: %lu bytes.\n", disk_size);

  // 构建超级块
  nsb.magic = NAIVE_MAGIC;
  nsb.block_total = (int)(disk_size / NAIVE_BLOCK_SIZE);
  nsb.inode_total = 128; // FIXME

  // 构建数据块位图
  bmap = (_Byte *)malloc(NAIVE_BLOCK_SIZE);
  memset(bmap, 0, NAIVE_BLOCK_SIZE);

  // 构建inode位图
  imap = (_Byte *)malloc(NAIVE_BLOCK_SIZE);
  memset(imap, 0, NAIVE_BLOCK_SIZE);

  // 构建inode表
  inode_table_size = nsb.inode_total * NAIVE_INODE_SIZE / NAIVE_BLOCK_SIZE;
  nsb.inode_table_block_no = NAIVE_IMAP_BLOCK + 1;
  // FIXME: 这样似乎不能保证图10-4末尾两个组成部分均为n块
  nsb.data_block_no = nsb.inode_table_block_no + inode_table_size;

  // 引导块，如图所示，放空
  _Byte _boot_padding[NAIVE_BLOCK_SIZE];
  write(fd, _boot_padding, NAIVE_BLOCK_SIZE);
  // 超级块
  write(fd, &nsb, NAIVE_SUPER_BLOCK_SIZE);
  // bmap
  // 先把数据块以前的位图标记成已使用
  // 用户只允许存放到后续的数据块内，不允许触碰其他类型的块
  int i;
  for (i = 0; i < nsb.data_block_no; i++) {
    int line = i / 8;
    int offset = i % 8;
    bmap[line] |= (1 << offset);
  }
  write(fd, bmap,  NAIVE_BLOCK_SIZE);
  // imap
  imap[0] |= 2; // ., ..
  write(fd, imap,  NAIVE_BLOCK_SIZE);

  // 准备基本的inode
  struct naive_inode root_inode;
  root_inode.mode = S_IFDIR;
  root_inode.i_ino = NAIVE_ROOT_INODE_NO;
  root_inode.block_count = 1;
  root_inode.block[0] = nsb.data_block_no;
  root_inode.dir_children_count = 3;
  root_inode.i_gid = getgid();
  root_inode.i_uid = getuid();
  root_inode.i_nlink = 2; // ., ..
  root_inode.i_atime = root_inode.i_mtime = root_inode.i_ctime = time(NULL);
  write(fd, &root_inode, NAIVE_INODE_SIZE);

  // 准备几条目录记录写到数据块
  struct naive_dir_record dir_dot;
  strcpy(dir_dot.filename, ".");
  dir_dot.i_ino = NAIVE_ROOT_INODE_NO;
  struct naive_dir_record dir_dotdot;
  strcpy(dir_dotdot.filename, "..");
  dir_dotdot.i_ino = NAIVE_ROOT_INODE_NO;
  // 挪指针
  lseek(fd, nsb.data_block_no * NAIVE_BLOCK_SIZE, SEEK_SET);
  write(fd, &dir_dot, NAIVE_DIR_RECORD_SIZE);
  write(fd, &dir_dotdot, NAIVE_DIR_RECORD_SIZE);
}

int main(int argc, char const *argv[]) {
  int fd;
  if (argc != 2)
    printf("[mkfs_naive] No device specified.\n");
  fd = open(argv[1], O_RDWR);
  format_disk(fd, argv[1]);
  close(fd);
}
