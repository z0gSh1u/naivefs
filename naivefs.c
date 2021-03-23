// =================
// naivefs.c
// 主体文件、超级块相关
// =================

#include "naivefs.h"

// ============ _naivefs.h ============

#include <linux/buffer_head.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/types.h>
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
void my_inode_init_owner(struct inode *inode, const struct inode *dir,
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

// ============ bitmap.c ============

// 获取一个可用的空data_block编号
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
  // 取超级块
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
  // FIXME: 有没有必要一次写整块？
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

// ============ dir.c ============

// 这个函数说明了如何遍历一个目录，获取其中的文件信息，实现它，文件系统就可以支持ls命令
// 注意，这里filp指向的是目录，而不是一个文件（这也是为什么它归在dops中）
// 但在linux里万物皆文件，所以这里用的仍是file*，这略有歧义，在此说明
// 另外，filldir_t是一个函数指针类型，也就是说filldir是一个回调函数
static int naive_readdir(struct file *filp, void *dirent, filldir_t filldir) {
  // 先把超级块拿到
  struct super_block *sb = filp->f_dentry->d_inode->i_sb;
  // 这样就可以拿到目录的inode了
  int ino_of_file = filp->f_dentry->d_inode->i_ino;
  struct naive_inode *ninode = naive_get_inode(sb, ino_of_file);

  // 先考虑没有下属文件（请树立目录也是文件的概念）的情况
  if (ninode->dir_children_count == 0 || ninode->block_count == 0)
    return 0;

  // 现在进入正题，读出所有文件
  struct naive_dir_record *dir_records =
      kmalloc(ninode->dir_children_count * NAIVE_DIR_RECORD_SIZE, GFP_KERNEL);
  // 开始向缓冲区填信息
  struct buffer_head *bh;
  int i, unread_children = ninode->dir_children_count;
  // 由于可能出现一个文件（child）占多个块的情况，因此这里的循环条件要做双重判断
  for (i = 0; i < ninode->block_count && unread_children > 0; i++) {
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
  loff_t pos = filp->f_pos;
  for (i = 0; i < ninode->dir_children_count; i++) {
    filldir(dirent, dir_records[i].filename, strlen(dir_records[i].filename),
            pos++, dir_records[i].i_ino, DT_REG); // FIXME: DT_DIR / DT_REG？
  }

  // 释放资源，搞定
  brelse(bh);
  kfree(dir_records);
  return 0;
}

// ============ file.c ============

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

  // 为新文件分配一个inode号
  // FIXME: 系统是否保证create的不可重入性？
  int inode_no_to_use = naive_new_inode_no(sb);

  // 拼装这个inode
  struct inode *inode;
  struct naive_inode ninode;
  inode = new_inode(sb);
  inode->i_ino = inode_no_to_use;
  ninode.i_ino = inode->i_ino;
  // 这里的dir可不能给NULL了，因为已经不是根目录了
  my_inode_init_owner(inode, dir, mode);
  // 其余属性也填上去
  inode->i_op = &naive_iops;
  inode->i_atime = inode->i_ctime = inode->i_mtime = CURRENT_TIME;
  ninode.i_atime = ninode.i_ctime = ninode.i_mtime = (inode->i_atime.tv_sec);
  // inode的uid、gid等属性不用手动填，在init_owner时会继承dir的
  ninode.i_uid = inode->i_uid;
  ninode.i_gid = inode->i_gid;
  ninode.i_nlink = inode->i_nlink;
  ninode.mode = mode;

  // 再来分类讨论一下类型
  if (S_ISDIR(mode)) {
    inode->i_size = 1; // FIXME: 正确吗？
    inode->i_blocks = 1;
    inode->i_fop = &naive_dops;
    ninode.block_count = 1;
    ninode.dir_children_count = 2; // .和..
    // 把.和..加进去
    // 先处理.，分配这个inode管辖的第一个块
    int block_no_to_use = naive_new_block_no(sb);
    ninode.block[0] = block_no_to_use;
    // 然后把这条记录准备一下
    struct naive_dir_record dir_dots[2];
    strcpy(dir_dots[0].filename, ".");
    dir_dots[0].i_ino = inode_no_to_use;
    // 再处理..
    strcpy(dir_dots[1].filename, "..");
    // 注意，它归属的inode是上级目录的inode，不是该目录的inode
    dir_dots[1].i_ino = dir->i_ino;
    // 把自定义inode信息和目录记录都写进磁盘
    write_back_ninode(sb, &ninode);
    write_back_block(sb, block_no_to_use, dir_dots, 2 * NAIVE_DIR_RECORD_SIZE);
    // 更新block的位图
    set_bmap_bit(sb, block_no_to_use, true);

    // FIXME: 应把整个文件系统的空闲块减1
  } else if (S_ISREG(mode)) {
    // 建立新的文件（空文件）并不占据数据块，这样就简单多了，不需要和块打交道
    inode->i_size = 0;
    inode->i_blocks = 0;
    inode->i_fop = &naive_fops;
    inode->i_mapping->a_ops = &naive_aops;
    ninode.block_count = 0;
    ninode.file_size = 0;
    write_back_ninode(sb, &ninode);
  } else {
    make_bad_inode(inode);
    return 0;
  }

  // 处理完新文件自身，我们还得处理它所在的目录
  // 先给所在目录加一条dir_record，关联到这个新文件
  struct naive_dir_record new_dir_record;
  strcpy(new_dir_record.filename, dentry->d_name.name);
  new_dir_record.i_ino = inode_no_to_use;
  // 写回
  struct naive_inode *dir_ninode = naive_get_inode(sb, dir->i_ino);
  struct buffer_head *bh = sb_bread(sb, dir_ninode->block[0]);
  // 追加在原有的children记录末尾
  memcpy(bh->b_data + dir_ninode->dir_children_count * NAIVE_DIR_RECORD_SIZE,
         &new_dir_record, NAIVE_DIR_RECORD_SIZE);
  map_bh(bh, sb, ninode.block[0]);
  brelse(bh);
  // 再给目录的自定义inode增加一个children
  dir_ninode->dir_children_count++;
  write_back_ninode(sb, dir_ninode);

  // 告诉系统这些inode是脏的
  mark_inode_dirty(inode);
  mark_inode_dirty(dir);

  // 更新inode的位图，因为我们新拿了一个inode且分配了
  set_imap_bit(sb, inode_no_to_use, true);

  // 把新文件的inode关联到dentry上
  d_instantiate(dentry, inode);
  return 0;
}

// 把变化的数据块写回盘
static void write_back_block(struct super_block *sb, int block_no, void *data,
                             int size) {
  struct naive_super_block *nsb = NAIVE_SB(sb);
  struct buffer_head *bh = sb_bread(sb, nsb->data_block_no + block_no);
  memcpy(bh->b_data, data, size);
  map_bh(bh, sb, nsb->data_block_no + block_no);
  brelse(bh);
}

// 把自定义inode写回盘
static void write_back_ninode(struct super_block *sb,
                              struct naive_inode *ninode) {
  struct naive_super_block *nsb = NAIVE_SB(sb);

  // 应该放到inode表的哪个块
  int block_no = nsb->inode_table_block_no + ninode->i_ino;

  // 现在开始上盘
  struct buffer_head *bh = sb_bread(sb, block_no);
  // 块首指针
  struct naive_inode *block_head = (struct naive_inode *)bh->b_data;
  memcpy(block_head, ninode, NAIVE_INODE_SIZE);
  // sb_bread只是读出来放到内存，前面更改的也是内存，并没有写回盘
  // 建立映射
  map_bh(bh, sb, block_no);

  // 完事
  brelse(bh);
}

// ============ inode.c ============

// 这个函数用来初始化inode很好用，但在2.6.21.7内核下还未提供，我们做个polyfill
// inode_init_owner的第二个参数是归属目录，根inode没有归属目录，给NULL
// inode_init_owner的第三个参数是inode的访问控制属性
void my_inode_init_owner(struct inode *inode, const struct inode *dir,
                         umode_t mode) {
  if (dir == NULL) {
    inode->i_uid = 0; // 这样做不一定准确
    inode->i_gid = 0; // 这样做不一定准确
  } else {
    inode->i_uid = dir->i_uid;
    inode->i_gid = dir->i_gid;
  }
  inode->i_mode = mode;
}

// 相当于naive_write_inode的实现
static struct buffer_head *naive_update_inode(struct inode *inode) {
  struct buffer_head *bh;
  struct naive_inode *ninode = naive_get_inode(inode->i_sb, inode->i_ino);

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
  ninode->i_atime = inode->i_atime.tv_sec;
  ninode->i_ctime = inode->i_ctime.tv_sec;
  ninode->i_mtime = inode->i_mtime.tv_sec;

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
  inode->i_mode = ninode->mode;
  inode->i_uid = ninode->i_uid;
  inode->i_gid = ninode->i_gid;
  inode->i_nlink = ninode->i_nlink;
  // ops和size对于不同类型的文件略有差异，在这里分类处理
  if (S_ISREG(ninode->mode)) {
    // 是一个文件
    inode->i_op = &naive_iops;
    inode->i_fop = &naive_fops;
    inode->i_mapping->a_ops = &naive_aops;
    inode->i_size = ninode->dir_children_count; // TODO: 合适吗？
  } else if (S_ISDIR(ninode->mode)) {
    // 是一个目录
    inode->i_op = &naive_iops;
    inode->i_fop = &naive_dops;
    inode->i_mapping->a_ops = &naive_aops;
    inode->i_size = ninode->file_size;
  } else {
    // lnk、tty等类型不支持
    make_bad_inode(inode);
  }
}

// 根据inode编号在指定文件系统实例（一个超级块对应一个文件系统实例）的inode表中取出对应的自定义inode
static struct naive_inode *naive_get_inode(struct super_block *sb, int ino) {
  // 先取出自定义超级块信息
  struct naive_super_block *nsb = NAIVE_SB(sb);

  // naive只有一个超级块、只有一个BlockGroup
  // 而且为了简单，我们的inode每个占一块
  // 故下面这个简单的算式就可以算得ino对应inode在inode表中所在的块号
  int block_no_of_ino = nsb->inode_table_block_no + ino; // FIXME
  // 找一个bh，读出整个块
  struct buffer_head *bh = sb_bread(sb, block_no_of_ino);
  struct naive_inode *ninode = (struct naive_inode *)bh->b_data;

  return ninode;
}

// 用于支持在目录中找文件（根据文件名锁定文件），将结果填充给dentry
struct dentry *naive_lookup(struct inode *dir, struct dentry *dentry,
                            struct nameidata *nd) {
  // 先把dir_record在的块读出来
  struct super_block *sb = dir->i_sb;
  struct naive_inode *ninode = naive_get_inode(sb, dir->i_ino);
  struct buffer_head *bh =
      sb_bread(sb, ninode->block[0]); // FIXME: 一定是第0块吗？

  // 现在开始遍历dir_record，直到找到文件对应的inode
  struct naive_dir_record *record_ptr = (struct naive_dir_record *)bh->b_data;
  struct inode *inode = NULL;

  int i;
  for (i = 0; i < ninode->dir_children_count; i++) {
    if (strcmp(dentry->d_name.name, record_ptr->filename) == 0) {
      // 文件名相同，就是找到了
      // iget的作用是从盘上读取指定的inode，这里需要原生inode，所以用不了naive_get_inode
      inode = iget(sb, record_ptr->i_ino);
      break;
    }
    record_ptr++;
  }
  // 结果写到dentry，返给系统，没找到就填充NULL
  d_add(dentry, inode);
  brelse(bh);
  return NULL;
}

// ===============================================================================

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
  // FIXME: 报错，暂时注释
  // 释放超级块即可
  // struct naive_super_block *nfs = NAIVE_SB(sb);
  // if (nfs == NULL)
  //   return;
  // kfree(nfs);
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
  my_inode_init_owner(root_inode, NULL, 0755 | S_IFDIR);

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
static void __exit exit_naivefs(void) { unregister_filesystem(&naive_fs_type); }
// 声明插拔函数
module_init(init_naivefs) module_exit(exit_naivefs) MODULE_AUTHOR("Z0GSH1U");
