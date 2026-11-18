# Linux Chat

一个运行在 Linux 上的简易多客户端聊天程序，包含 TCP 服务端和 GTK2 图形客户端。

## 功能

- 服务端同时维护多个客户端连接，当前最多支持 10 个客户端。
- 客户端通过 GTK2 窗口连接 `127.0.0.1:8000` 并发送消息。
- 服务端将消息转发给其他客户端，消息带有 `fd_<发送端描述符>:` 前缀。
- 每条消息最大 4000 字节，通信帧最大 4096 字节。
- 支持分片到达的消息和同一连接连续发送的多个消息帧。
- 客户端在当前工作目录的 `chatlog.db` 中追加保存聊天记录。
- 客户端界面和提示信息使用英文。

> 该项目没有用户认证和传输加密，只适合本地学习、课程实验或可信局域网测试，不建议直接暴露到公网。

## 目录结构

```text
.
├── client/
│   ├── client.c        # GTK 窗口和界面事件
│   ├── cl_sock.c       # 客户端 socket 连接和接收线程
│   ├── client.h
│   └── makefile
├── server/
│   ├── server.c        # TCP 服务端和消息广播
│   ├── server.h
│   └── makefile
├── protocol.c          # 消息帧收发实现
├── protocol.h          # 端口、长度限制和协议接口
└── README.md
```

## 环境要求

- Linux，推荐 Ubuntu
- GCC、GNU Make 和 pthread
- GTK2 开发库
- pkg-config

在 Ubuntu 或 WSL 的 Ubuntu 中安装编译依赖：

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libgtk2.0-dev
```

仅需要无界面运行测试时，可以额外安装 Xvfb：

```bash
sudo apt install -y xvfb
```

## 编译

进入项目根目录：

```bash
cd /mnt/f/Projects/linux-chat/chat-new
```

编译服务端：

```bash
make -C server
```

编译客户端：

```bash
make -C client
```

修改源码后需要完整重新编译时，先清理再构建：

```bash
make -C server clean
make -C server

make -C client clean
make -C client
```

编译完成后会生成：

```text
server/server
client/client
```

也可以不使用 `make`，直接执行：

```bash
gcc -Wall -Wextra -g -I. \
  -o server/server \
  server/server.c protocol.c \
  -lpthread

gcc -Wall -Wextra -g -I. \
  -o client/client \
  client/client.c client/cl_sock.c protocol.c \
  $(pkg-config --cflags --libs gtk+-2.0) \
  -lpthread
```

编译时出现 GTK2 或 GLib 的弃用警告属于依赖库警告，不影响当前程序运行。

## 运行

先在一个终端启动服务端：

```bash
cd /mnt/f/Projects/linux-chat/chat-new/server
./server
```

启动成功后会显示：

```text
chat server listening on port 8000
```

保持该终端运行，另开一个终端启动客户端：

```bash
cd /mnt/f/Projects/linux-chat/chat-new/client
./client
```

客户端窗口打开后，点击 `Connect`。要测试消息转发，可以再启动一个客户端：

```bash
cd /mnt/f/Projects/linux-chat/chat-new/client
./client
```

两个客户端都连接后，在其中一个窗口输入消息并点击 `Send`，另一个客户端会收到消息。

## WSL 运行说明

Windows 11 的 WSLg 通常可以直接显示 GTK 窗口。可以用以下命令检查显示环境：

```bash
echo "$DISPLAY"
```

如果 `DISPLAY` 为空，需要启用 WSLg，或者在 WSL 中配置可用的 X Server。

没有图形显示环境时，可以使用 Xvfb 做启动检查：

```bash
Xvfb :90 -screen 0 1024x768x24 -listen tcp -nolisten unix -ac &
cd /mnt/f/Projects/linux-chat/chat-new/client
DISPLAY=127.0.0.1:90 ./client
```

## 通信协议

每条消息使用带长度前缀的 TCP 帧：

```text
4 字节网络字节序长度 + 对应长度的消息正文
```

服务端收到合法消息后会转发给其他客户端，并添加发送端文件描述符前缀。客户端和线程使用 `send_frame`、`recv_frame` 统一处理 TCP 分片。

## 常见问题

### `./server: cannot execute: required file not found`

通常是执行了仓库中的旧 32 位程序，而系统缺少 32 位动态加载器。重新执行：

```bash
make -C server clean
make -C server
file server/server
```

输出应包含 `ELF 64-bit`。

### 客户端无法连接

确认服务端正在运行，并检查端口是否被其他程序占用：

```bash
ss -ltnp 'sport = :8000'
```

客户端当前固定连接 `127.0.0.1:8000`，服务端和客户端需要运行在同一台机器上。

### GTK 窗口无法打开

确认 `DISPLAY` 已设置，并且当前环境能够访问图形显示服务。WSL 用户还需要确认 WSLg 或第三方 X Server 正常工作。
