// =================
// naivefs.h
// naive文件系统关键参数、数据结构的定义
// mkfs、文件系统实现共用
// =================

#ifndef NAIVEFS_H_
#define NAIVEFS_H_

#define NAIVE_BLOCK_SIZE 512       // 块大小512B
#define NAIVE_MAGIC 990717         // 魔数
#define NAIVE_BLOCK_PER_FILE 8     // 每个文件最多占多少块
#define NAIVE_MAX_FILENAME_LEN 128 // 文件名最大长度
#define NAIVE_BOOT_BLOCK 0         // 引导块块号
#define NAIVE_SUPER_BLOCK_BLOCK 1  // 超级块块号
#define NAIVE_BMAP_BLOCK 2         // 块位图块号
#define NAIVE_IMAP_BLOCK 3         // inode位图块号
#define NAIVE_ROOT_INODE_NO 0      // 根inode编号
#define NAIVE_SUPER_BLOCK_SIZE sizeof(struct naive_super_block)
#define NAIVE_INODE_SIZE sizeof(struct naive_inode)
#define NAIVE_DIR_RECORD_SIZE sizeof(struct naive_dir_record)

typedef unsigned char _Byte; // 字节定义

// 自定义超级块
// 这里参考了《鸟哥linux私房菜》和其他代码
// 考虑到naivefs基本不做异常处理，省略了很多没有用到的属性
struct naive_super_block {
  int magic;                // 魔数
  int inode_total;          // inode的总量
  int block_total;          // 块的总量
  int inode_table_block_no; // inode表块起始位置
  int data_block_no;        // 数据块起始位置
  // 补齐到一个块
  _Byte _padding[(NAIVE_BLOCK_SIZE - 5 * sizeof(int))];
};

// 自定义inode
struct naive_inode {
  int mode;                        // mode
  int i_ino;                       // ino
  int block_count;                 // 该inode负责几个块
  int block[NAIVE_BLOCK_PER_FILE]; // 负责哪几个块，用第0块首部存dir_record
  // 对于文件，记录文件大小；对于目录，记录目录下项目数
  union {
    int file_size;
    int dir_children_count;
  };
  // TODO: 下面这些属性原生inode上也有，先记着，没作用后续删掉
  int i_uid;
  int i_gid;
  int i_nlink;
  int i_atime;
  int i_ctime;
  int i_mtime;
  // FIXME: 补齐是方便naive_get_inode计算，但是没必要
  _Byte _padding[(NAIVE_BLOCK_SIZE - (10 + NAIVE_BLOCK_PER_FILE) * sizeof(int))];
};

// 目录下的项目的记录
struct naive_dir_record {
  int i_ino;                             // 所属目录的inode编号
  char filename[NAIVE_MAX_FILENAME_LEN]; // 文件名
};

#endif
