# naivefs

一个**未能完全实现的**简单的 Linux 下的磁盘上文件系统。

## 编译

- 准备合适版本的 Linux 内核源码

  ```shell
  KERNEL_DIR := /lib/modules/$(shell uname -r)/build
  ```

  实验使用的是 *2.6.21.7* 版本。

- 清理

  ```shell
  make clean
  ```

- 编译格式化工具

  ```shell
  make mkfs
  ```

- 编译文件系统

  ```shell
  make default
  ```

## 实验报告

希望可以帮助你少走弯路：[实验报告](./report.pdf)。

