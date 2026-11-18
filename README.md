# Linux Chat

一个运行在 Linux 上的多客户端 TCP 聊天程序，包含 GTK2 图形客户端和
`poll` 驱动的单线程服务端。项目适合学习 socket 编程、非阻塞 I/O、简单协议
设计和 GTK2 界面开发。

> 本项目没有密码验证和传输加密，仅适合本地学习、课程实验或可信局域网测试，
> 不建议直接暴露到公网。

## 功能

- 客户端启动后自动连接 `127.0.0.1:8000`。
- 首次连接时输入显示名称，服务端确认该名称是否已被当前在线会话使用。
- 名称长度为 1 到 32 字节，比较时不区分 ASCII 大小写，例如 `Alice` 和
  `alice` 不能同时登录。
- 名称冲突时连接保持打开，客户端会显示英文错误并允许重新输入名称。
- 登录成功后，聊天消息会转发给其他已登录客户端，并显示为 `Alice: hello`。
- 服务端使用单个线程和 `poll` 同时处理监听 socket、客户端读取与写入。
- 每个客户端使用非阻塞 socket、增量接收缓冲和独立的待发送缓冲。
- 单条聊天文本最多 4000 字节，单个协议包最多 4096 字节。
- 客户端支持 TCP 分片、连续多个消息包，并在当前目录的 `chatlog.db`
  中追加以时间戳开头的聊天记录。
- 客户端窗口、按钮和错误提示均使用英文。

## 目录结构

```text
.
├── client/
│   ├── client.c        # GTK2 窗口、登录对话框和界面事件
│   ├── cl_sock.c       # socket、登录状态和接收线程
│   ├── client.h
│   └── makefile
├── server/
│   ├── server.c        # poll 驱动的单线程聊天服务端
│   ├── server.h
│   └── makefile
├── tests/
│   └── test_chat.py    # 协议与多客户端集成测试
├── protocol.c          # 协议包的阻塞式收发辅助函数
├── protocol.h          # 端口、长度限制和消息类型
└── README.md
```

## 环境要求

- Linux 或 WSL 中的 Ubuntu
- GCC 和 GNU Make
- GTK2 开发库
- `pkg-config`
- Python 3（运行集成测试时需要）

在 Ubuntu 或 WSL 中安装依赖：

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libgtk2.0-dev python3
```

如果使用没有图形桌面的环境做客户端启动检查，可以额外安装 Xvfb：

```bash
sudo apt install -y xvfb
```

## 编译

在 Linux 或 WSL 中进入项目根目录：

```bash
cd /mnt/f/Projects/linux-chat/chat-new
```

完整重新编译：

```bash
make -C server clean
make -C server

make -C client clean
make -C client
```

生成的文件为：

```text
server/server
client/client
```

也可以直接使用 GCC：

```bash
gcc -Wall -Wextra -g -I. -o server/server server/server.c

gcc -Wall -Wextra -g -I. \
  -o client/client \
  client/client.c client/cl_sock.c protocol.c \
  $(pkg-config --cflags --libs gtk+-2.0) -lpthread
```

服务的构建目标不再需要 pthread。编译时出现的 GTK2/GLib 弃用警告来自
系统开发库头文件，不影响程序运行。

## 运行

先在一个终端启动服务端：

```bash
cd /mnt/f/Projects/linux-chat/chat-new/server
./server
```

成功启动后会显示：

```text
chat server listening on port 8000
```

保持服务端运行，在另一个终端启动客户端：

```bash
cd /mnt/f/Projects/linux-chat/chat-new/client
./client
```

客户端会自动连接服务器，然后弹出 `Set your name` 对话框。输入名称并点击
`Login`。如果名称已被其他在线用户使用，客户端会显示错误并再次要求输入名称。

要测试消息转发，可以再启动一个客户端，使用另一个名称登录。在任一客户端
输入消息并点击 `Send`，其他已登录客户端会收到带发送者名称的消息。

退出客户端会关闭连接，释放该名称。其他客户端会收到类似
`Alice left the chat` 的离开提示。

## 协议

客户端和服务端使用 TCP。每个包的结构如下：

```text
4 字节网络字节序长度 | 1 字节消息类型 | 负载
```

长度字段的值等于 `1 + 负载长度`，包含消息类型字节，不包含 4 字节长度字段。
负载中不能包含 NUL，长度字段的合法范围为 1 到 4096。

消息类型：

| 值 | 名称 | 方向 | 负载 |
| --- | --- | --- | --- |
| 1 | `CHAT_MSG_LOGIN` | 客户端 -> 服务端 | 用户名称 |
| 2 | `CHAT_MSG_TEXT` | 双向 | 聊天正文或服务端生成的提示 |
| 3 | `CHAT_MSG_LOGIN_OK` | 服务端 -> 客户端 | 服务端接受的名称 |
| 4 | `CHAT_MSG_LOGIN_ERROR` | 服务端 -> 客户端 | 可显示的英文错误信息 |

客户端登录成功前不能发送聊天正文。服务端只把聊天消息转发给已登录会话，
不会转发给尚未登录的客户端。

## 测试

运行协议和多客户端集成测试：

```bash
cd /mnt/f/Projects/linux-chat/chat-new
python3 tests/test_chat.py
```

测试覆盖：

- 登录成功和名称校验
- 不区分大小写的同名冲突
- 同名失败后在原连接上改用其他名称
- 聊天消息转发
- 分片包和同一连接上的连续多个包
- 最大聊天负载
- 未登录客户端发送聊天正文
- 客户端断开后名称再次可用
- 超过最大长度的协议包被拒绝

检查服务端是否仍有客户端线程相关代码：

```bash
grep -R -n -E 'pthread|client_thread' server
```

正常情况下上述命令没有输出，因为服务端的网络消息转发只由 `poll` 主循环完成。

## WSL 运行说明

Windows 11 的 WSLg 通常可以直接显示 GTK 窗口。可以先检查显示变量：

```bash
echo "$DISPLAY"
```

如果没有图形显示环境，可以使用 Xvfb 做无界面启动检查。此前在部分 WSL
环境中 Unix socket 不可写，因此这里让 Xvfb 监听 TCP：

```bash
Xvfb :90 -screen 0 1024x768x24 -listen tcp -nolisten unix -ac &
cd /mnt/f/Projects/linux-chat/chat-new/client
DISPLAY=127.0.0.1:90 ./client
```

## 常见问题

### `./server: cannot execute: required file not found`

通常是在 WSL 中执行了由错误平台生成的可执行文件，或旧二进制文件被删除后
仍保留在运行状态。应进入 WSL 的 Ubuntu，在项目目录重新编译：

```bash
make -C server clean
make -C server
file server/server
```

输出应包含 `ELF 64-bit`。

### 客户端无法连接

确认服务端正在运行，并且 8000 端口没有被旧进程占用：

```bash
ss -ltnp 'sport = :8000'
```

客户端固定连接 `127.0.0.1:8000`，因此当前服务端和客户端需要运行在同一台
机器或同一个 WSL 环境中。

### GTK 窗口无法打开

确认 `DISPLAY` 已设置，当前环境能够访问图形显示服务。没有 WSLg 时，可以使用
上文中的 Xvfb 命令做启动检查。
