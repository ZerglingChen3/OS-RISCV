# 用户进程和Shell

## 基本情况

为了对内核进行测试，我们编写了一部分用户进程，并实现了一个 Shell 界面，可以执行一些简单的命令。

## 用户进程地址空间

```c
TRAMPOLINE_TOP -----> +----------------------------+----0x0000 0040 0000 0000-------
                      |                            |                          /|\
                      |         Trampoline         |    8192 Byte         trampoline
                      |                            |                          \|/
   U_STACK_TOP -----> +----------------------------+----0x0000 003f ffff 7ed4-----
                      |                            |                          /|\
                      |         User Stack         |    1 GB                   |
                      |                            |                           |
    U_HEAP_TOP -----> +----------------------------+----0x0000 003e ffff 7ed4  |
                      |                            |                           |
                      |         User Heap          |    1 GB                   |
                      |                            |                           |
 U_HEAP_BOTTOM -----> +----------------------------+----0x0000 003d ffff 7ed4  |
                      |                            |                           |
                      |                            |                           |
                      |                            |                           |
                      +----------------------------+----0x0000 0000 0100 3000 user
                      |                            |                           |
                      |         User Buffer        |    12288 Byte             |
                      |                            |                           |
                      +----------------------------+----0x0000 0000 0100 0000  |
                      |                            |                           |
                      |                            |                           |
                      |                            |                           |
      U_DATA   -----> +----------------------------+                           |
                      |                            |                           |
                      |                            |                           |
                      |                            |                           |    
      U_TEXT   -----> +----------------------------+----0x0000 0000 00a0 0000  |
                      |                            |                          \|/
     0 ------------>  +----------------------------+ -----------------------------
```

* `U_TEXT` 是用户进程代码段

* `U_DATA` 是用户进程数据段

* `U_BUFFER` 是用户进程输出缓冲区

* `U_HEAP` 是用户堆区

* `U_STACK` 是用户栈区

* `Trampoline` 是用户内核切换代码段

## 用户进程组成

用户编写的程序的部分入口为 `userMain` ，还需要链接启动汇编和 lib 库才可以执行，包括：

* 用户程序入口 `Entry.S` : 用户加载到内存后 EPC 将设置到该文件开头，会跳转到 `LibMain` 函数
* 用户库接口 `LibMain.c` : 会进行参数传递，并在用户程序结束后返回后通过 `exit` 系统调用传递返回值
* 库函数 `LibMain.h` : 定义了一系列系统调用接口，用户只需要包含该头文件就可以完成相应功能
* 用户程序 `*.c` : 用户程序入口为 `userMain` ，在这里用户编写自己的应用程序


## 用户进程加载方式

我们的内核对于用户进程提供两种加载方式：**链接到内核**和 **Exec 加载**

### 链接到内核

* 首先所有用户程序链接到一起生成 `ELF` 文件
* 使用自行编写的读取文件 `BinaryToC.c` 将 `ELF` 转化成一个二进制数组，并记录文件大小，得到程序的组织格式为一个标准的c程序
* 编译该c程序并将其链接到内核中

所有用的测试程序均用此种方式进行链接到内核，如下图所示：

```c
     PROCESS_CREATE_PRIORITY(ForkTest, 5);
     PROCESS_CREATE_PRIORITY(ProcessIdTest, 4);
     PROCESS_CREATE_PRIORITY(SysfileTest, 1);
     PROCESS_CREATE_PRIORITY(PipeTest, 1);
     PROCESS_CREATE_PRIORITY(ExecTest,1);
     PROCESS_CREATE_PRIORITY(SyscallTest, 1);
     PROCESS_CREATE_PRIORITY(MountTest, 1);
     PROCESS_CREATE_PRIORITY(WaitTest, 1);
```

### Exec 加载

* 首先所有用户程序链接到一起生成 `ELF` 文件
* 将所有 `ELF` 文件全部放到 `mnt` 文件夹下
* 在 QEMU 上模拟则以 `mnt` 为根目录生成磁盘镜像文件 `fs.img`
* 在开发板上测试则直接将该文件夹所有文件拷贝到 SD 卡上
* 在我们的内核中中通过 `Exec` 系统调用直接访问程序

所有的 Shell 命令均通过此种方式进行调用。

## Shell

为了方便使用我们开发的操作系统，我们设计了一个 Shell 界面，可以方便用户进行操作。

### 启动初始化

在进入 Shell 之前，我们首先需要对标准输入、标准输出和标准异常的文件描述符进行申请。

首先通过 `dev` 系统调用申请将0号文件描述符申请为串口外设，接着通过 `dup` 系统调用保证1号和2号文件描述符均映射为串口外设，如下代码所示：

```c
    dev(1, O_RDWR);
    dup(0);
    dup(0);
```

其中0号为标准输入，1号为标准输出，2号为标准异常。

在初始化完毕后，就可以通过 `exec` 调用 `sh` 程序了，该程序需要提前放在 SD 卡上。

### Shell 界面

#### 显示工作目录

Shell 界面首先通过系统调用 `getcwd` 获得当前工作路径，输出等待用户输入指令：

```shell
[/home]:$
```

#### 获取用户指令

用户开始输入指令，当读取到 `\n` 和 `\r` 时命令解析完成，跳转到执行解析部分。如果用户输入了 `ASCII` 码为127的退格字符，`Shell` 将向控制台输入一个光标退格命令，同时缓冲区若非空则下标减1表明删除了最后的字符。

#### 解析用户指令

* 在所有命令中，`cd` 命令是内置在 `shell` 中的特殊命令，并不需要加载磁盘上的程序文件。不论跳转的目录为相对目录还是绝对目录，都可以根据当前目录得到需要跳转的绝对目录，进而调用 `chdir` 进行目录转换
* 对于其他命令，我们首先通过 `fork` 创建子进程，子进程通过 `exec` 执行命令，而父进程则等待子进程的返回值进行命令的执行
* 对于具体命令行的解析，这里采用了比较成熟的递归下降的解析方法，源自 `MIT xv6` 的 Shell 解析程序。语法成分可以分为执行部分 `execcmd` 、重定向部分 `redircmd` 、管道部分 `pipecmd` 、命令列表部分 `listcmd` 和后台命令部分 `backcmd`。
* 命令行解析之后，会调用 `runcmd` 函数对命令进行执行。不同的命令将采用不同的系统调用进行工作
  * 执行部分：`exec` 系统调用
  * 重定向部分：`close` 关闭之前的文件描述符，`open` 需要打开的文件描述符以实现重定向
  * 管道部分：`fork` 两次创建读进程和写进程，读进程 `close` 写端，写进程 `close` 读端
  * 命令链表部分：首先 `exec` 执行左边的命令，再 `exec` 执行右边的命令
  * 后台命令：`fork` 后子进程执行命令，主进程直接退出

#### 清空指令结构体

在指令结束之后，清空指令对应的结构体空间中的每一个域。

### 基本 Shell 命令

为了测试 `Shell` ，我们编写了若干 `Shell` 命令方便测试。

* `ls` : 显示一个目录下所有应用程序，该程序通过调用 `getdirent` 系统调用获得一个目录下的文件信息
* `echo` : 回显参数
* `xargs` : 将标准输入解析为参数，该程序通过 `read` 系统调用读取控制台输入，并使用 `exec` 执行命令
* `cat` : 显示文件内容或者从标准输入的内容，该程序通过 `open` 系统调用打开文件，并用 `read` 系统调用获取文件内容
* `mkdir` : 创建文件夹，该程序通过 `open` 系统调用创建文件夹
* `touch` : 创建文件，该程序通过 `open` 系统调用创建文件
* `rm` : 删除文件，该程序通过 `unlink` 系统调用删除文件

## 总结

* 通过 `Shell` 测试系统调用是一种比较高效的方式，也更容易找到系统调用的错误
* 由于初赛时间紧张，还未支持环境变量和历史，在之后会更加完善 `Shell`
