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

static struct naive_super_block super_block;

static _Byte *bmap;
static _Byte *imap;
static int disk_size;
static int bmap_size;
static int imap_size;
static int inode_table_size;

static int format_disk(int fd, const char *path) {
  // 先获取设备（磁盘）的总大小
  struct stat stat_;
  stat(path, &stat_);
  disk_size = stat_.st_size;
  printf("[mkfs] size of disk: %lu bytes.\n", disk_size);
  super_block.version = 1;
  super_block.magic = NAIVE_MAGIC;
  super_block.blocks_count = (int)(disk_size / NAIVE_BLOCK_SIZE);
  super_block.inodes_count = super_block.blocks_count; // 按照图10-4，均为n
  super_block.free_blocks = 0;
  // 构建数据块位图
  bmap_size = super_block.blocks_count /
              (NAIVE_BLOCK_SIZE * 8); // 一个字节相当于8bit的bitmap
  if (super_block.blocks_count % (NAIVE_BLOCK_SIZE * 8) != 0)
    bmap_size += 1;
  super_block.bmap_block = NAIVE_BMAP_BLOCK;
  bmap = (_Byte *)malloc(bmap_size * NAIVE_BLOCK_SIZE);
  memset(bmap, 0, bmap_size * NAIVE_BLOCK_SIZE);
  // 构建inode位图
  imap_size = super_block.blocks_count /
              (NAIVE_BLOCK_SIZE * 8); // 一个字节相当于8bit的bitmap
  if (super_block.blocks_count % (NAIVE_BLOCK_SIZE * 8) != 0)
    imap_size += 1;
  super_block.imap_block = NAIVE_IMAP_BLOCK;
  imap = (_Byte *)malloc(imap_size * NAIVE_BLOCK_SIZE);
  memset(imap, 0, imap_size * NAIVE_BLOCK_SIZE);
  // 构建inode表
  inode_table_size =
      super_block.inodes_count * NAIVE_INODE_SIZE / NAIVE_BLOCK_SIZE;
  super_block.inode_table_block = super_block.imap_block + imap_size;
  super_block.data_block_number =
      super_block.inode_table_block + inode_table_size;
  super_block.free_blocks =
      super_block.blocks_count - super_block.data_block_number - 1; // why -1

  int i;
  for (i = 0; i < super_block.data_block_number + 1; i++) {
    int index = i / (8 * 8);
    int offset = i % (8 * 8);
    bmap[index] |= (1 << offset);
  }

  // bootblock
  _Byte _boot_padding[NAIVE_BLOCK_SIZE];
  write(fd, _boot_padding, NAIVE_BLOCK_SIZE);
  // superblock
  write(fd, &super_block, NAIVE_SUPER_BLOCK_SIZE);
  // bmap
  write(fd, bmap, bmap_size * NAIVE_BLOCK_SIZE);
  // imap
  memset(imap, 0, imap_size * NAIVE_BLOCK_SIZE);
  imap[0] |= 3; // ., .., welcome
  write(fd, imap, imap_size * NAIVE_BLOCK_SIZE);
  // itable
  struct naive_inode root_inode;
  root_inode.mode = S_IFDIR;
  root_inode.inode_no = NAIVE_ROOT_INODE_NO;
  root_inode.blocks = 1;
  root_inode.block[0] = super_block.data_block_number;
  root_inode.dir_children_count = 3;
  root_inode.i_gid = getgid();
  root_inode.i_uid = getuid();
  root_inode.i_nlink = 2; // ., ..
  root_inode.i_atime = root_inode.i_mtime = root_inode.i_ctime =
      (int64_t)time(NULL);
  write(fd, &root_inode, NAIVE_INODE_SIZE);

  struct naive_inode welcome_inode;
  welcome_inode.mode = S_IFREG;
  welcome_inode.inode_no = NAIVE_ROOT_INODE_NO + 1;
  welcome_inode.blocks = 0;
  welcome_inode.block[0] = 0;
  welcome_inode.file_size = 0;
  welcome_inode.i_gid = getgid();
  welcome_inode.i_uid = getuid();
  welcome_inode.i_nlink = 1;
  welcome_inode.i_atime = welcome_inode.i_mtime = welcome_inode.i_ctime =
      (int64_t)time(NULL);
  write(fd, &welcome_inode, NAIVE_INODE_SIZE);

  struct naive_dir_record dir_dot;
  strcpy(dir_dot.filename, ".");
  dir_dot.inode_no = NAIVE_ROOT_INODE_NO;
  struct naive_dir_record dir_dotdot;
  strcpy(dir_dotdot.filename, "..");
  dir_dotdot.inode_no = NAIVE_ROOT_INODE_NO;
  struct naive_dir_record welcome_file_record;
  strcpy(welcome_file_record.filename, "welcome");
  welcome_file_record.inode_no = NAIVE_ROOT_INODE_NO + 1;
  write(fd, &dir_dot, NAIVE_DIR_RECORD_SIZE);
  write(fd, &dir_dotdot, NAIVE_DIR_RECORD_SIZE);
  write(fd, &welcome_file_record, NAIVE_DIR_RECORD_SIZE);

  return 0;
}

int main(int argc, char const *argv[]) {
  int fd;
  if (argc != 2)
    printf("[mkfs] no device specified.\n");
  fd = open(argv[1], O_RDWR);
  format_disk(fd, argv[1]);
  close(fd);
}
