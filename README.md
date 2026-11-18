# Linux Chat

Linux Chat 是一个使用 C 编写的多客户端 TCP 聊天程序，包含 GTK2 图形客户端和一
个基于 `poll` 的单线程服务端。项目主要演示 Linux socket 编程、非阻塞 I/O、
增量协议解析、简单登录状态机，以及 GTK 主线程与网络接收线程之间的协作。

> 本项目没有密码认证、访问控制和传输加密，只适合本地学习、课程实验或可信局域网
> 测试，不应直接暴露到公网。

## 当前功能

- 客户端启动后自动连接 `127.0.0.1:8000`，连接失败时可点击 `Connect` 重试。
- 首次连接时弹出名称对话框，用户使用英文界面设置自己的显示名称。
- 名称长度为 1 到 32 字节，禁止首尾空白和 ASCII 控制字符。
- 服务端只检查当前在线会话。名称比较不区分 ASCII 大小写，例如 `Alice` 和
  `alice` 不能同时在线。
- 名称冲突时 TCP 连接保持打开，客户端可以输入新名称并在原连接上重新登录。
- 登录后，聊天正文会转发给其他已登录用户，并显示为 `Alice: hello`。
- 用户加入、离开和断线会自动广播聊天状态提示。
- 服务端使用一个线程和一个 `poll` 事件循环，同时处理监听 socket、读取和写入。
- 每个客户端使用非阻塞 socket、增量输入缓冲和有容量上限的输出 FIFO。
- 单条聊天文本最多 4000 字节，单个协议帧最多 4096 字节。
- 客户端可以处理 TCP 分片、粘包和同一连接上的连续多个协议帧。
- 聊天记录以时间戳格式追加到客户端当前目录的 `chatlog.db`。
- 服务端只打印连接、登录和断线等状态日志，不打印转发的聊天正文。

## 系统架构

```text
Client A                                              Client B
GTK main thread                                      GTK main thread
  | send_packet()                                      ^
  v                                                    | g_idle_add()
receiver thread -- TCP --> +----------------------+ --> receiver thread
                           | server poll loop     |
                           | fixed session table  |
                           | input/output buffers |
                           +----------------------+
```

客户端保留一个后台接收线程，避免阻塞 GTK 主循环。接收线程负责解析协议、通过
`g_idle_add()` 提交 UI 回调并清理自己所属的连接，但不直接操作 GTK 控件；所有
窗口更新都在 GTK 主线程执行。连接代数 `generation` 用于识别并丢弃旧连接遗留的
回调，防止旧连接影响新连接。

服务端没有客户端子线程或共享会话状态。监听 socket 和最多 10 个客户端 socket
都由同一个 `poll` 循环驱动，因此消息转发、登录检查和断线清理不需要加锁。

## 目录结构

```text
.
├── client/
│   ├── client.c        # GTK2 窗口、对话框、聊天记录和界面事件
│   ├── cl_sock.c       # socket、连接状态、接收线程与 GTK 线程切换
│   ├── client.h
│   └── makefile
├── server/
│   ├── server.c        # poll 服务端、登录状态机和消息转发
│   ├── server.h
│   └── makefile
├── tests/
│   └── test_chat.py    # 端到端协议与多客户端测试
├── protocol.c          # 完整协议帧的阻塞式发送与接收
├── protocol.h          # 端口、限制和消息类型
└── README.md
```

关键实现位置：

| 功能 | 代码位置 |
| --- | --- |
| 协议帧编码、分片重组基础函数 | `protocol.c` / `protocol.h` |
| 服务端登录、名称检查和消息广播 | `server/server.c` |
| 服务端 `poll` 主循环和输出背压 | `server/server.c` |
| 客户端自动连接与登录对话框 | `client/client.c` |
| 客户端网络线程及 GTK idle 回调 | `client/cl_sock.c` |
| 名称冲突、粘包、断线测试 | `tests/test_chat.py` |

## 环境要求

- Linux 或 WSL 中的 Ubuntu
- GCC 和 GNU Make
- GTK2 开发库
- `pkg-config`
- Python 3，仅运行集成测试时需要

在 Ubuntu 或 WSL 中安装依赖：

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libgtk2.0-dev python3
```

如果系统没有可用的图形显示环境，可额外安装 Xvfb 做客户端启动检查：

```bash
sudo apt install -y xvfb
```

## 编译

进入项目根目录：

```bash
cd /mnt/f/Projects/linux-chat/chat-new
```

完整清理并重新编译：

```bash
make -C server clean
make -C server

make -C client clean
make -C client
```

生成文件：

```text
server/server
client/client
```

也可以只重新编译其中一个目标：

```bash
make -C server
make -C client
```

如果直接调用 GCC，编译命令如下：

```bash
gcc -Wall -Wextra -g -I. -o server/server server/server.c

gcc -Wall -Wextra -g -I. \
  -o client/client \
  client/client.c client/cl_sock.c protocol.c \
  $(pkg-config --cflags --libs gtk+-2.0) -lpthread
```

服务端不依赖 pthread。GTK2 和 GLib 在较新的系统上可能产生弃用警告，这些警告
来自系统开发库头文件，不影响当前程序构建。

## 运行

先启动服务端：

```bash
cd /mnt/f/Projects/linux-chat/chat-new/server
./server
```

正常启动时会输出：

```text
chat server listening on port 8000
```

保持服务端运行，在另一个终端启动客户端：

```bash
cd /mnt/f/Projects/linux-chat/chat-new/client
./client
```

客户端会自动连接 `127.0.0.1:8000`，随后弹出 `Set your name` 对话框：

1. 输入名称并点击 `Login`。
2. 如果名称已被其他在线用户占用，界面会显示英文错误并再次弹出名称对话框。
3. 登录成功后，在下方输入框输入消息并点击 `Send`。
4. 其他已登录客户端会收到带发送者名称的消息。
5. 点击 `Quit` 或关闭窗口会释放连接及名称，其他用户会看到离开提示。

可以启动多个客户端并使用不同名称，验证消息广播、名称冲突和断线释放名称。
客户端关闭连接后再重新登录时，服务端会再次要求设置名称。

### 客户端聊天记录

客户端从启动目录打开 `chatlog.db` 并追加记录，例如：

```text
[Wed Nov 18 12:00:00 2026] Me:hello
[Wed Nov 18 12:00:03 2026] Bob:hi
```

该文件是普通的追加式文本文件，不会自动同步给其他客户端，也不会在重新启动时
加载回聊天窗口。服务端不保存聊天历史。

### 服务端日志

服务端会显示连接来源、登录名称、断线、离开以及服务器满载等状态，但不会输出
转发的聊天正文。例如发送 `hello` 时，服务端控制台不会出现 `Alice: hello`。

## 协议

客户端和服务端通过 TCP 交换协议帧。每个帧由四部分组成：

```text
4 字节网络字节序长度 | 1 字节消息类型 | 0..4095 字节负载
```

长度字段等于 `1 + 负载长度`，包含消息类型字节，但不包含长度字段本身。合法总
长度范围为 1 到 4096。负载不能包含 NUL 字节，客户端和服务端会在分配或转发
数据前检查边界。

消息类型：

| 值 | 名称 | 方向 | 负载 |
| --- | --- | --- | --- |
| 1 | `CHAT_MSG_LOGIN` | 客户端 -> 服务端 | 用户名称 |
| 2 | `CHAT_MSG_TEXT` | 双向 | 聊天正文或服务端提示 |
| 3 | `CHAT_MSG_LOGIN_OK` | 服务端 -> 客户端 | 服务端接受的名称 |
| 4 | `CHAT_MSG_LOGIN_ERROR` | 服务端 -> 客户端 | 可显示的英文错误信息 |

协议相关约束：

- 未登录客户端只能发送登录请求；尝试发送聊天正文会收到登录错误提示。
- 已登录会话再次发送登录请求会被拒绝，但不会关闭连接。
- 服务端只向其他已登录会话广播消息，不发送给发送者本人。
- 客户端输入缓冲可以保留半个协议帧，等下一次可读事件到达后继续解析。
- 服务端输出队列上限为 64 KiB；读取过慢或发送失败的客户端会被断开，避免内存
  无限增长。
- 服务端最多保留 10 个客户端会话，包括尚未完成登录的连接。

## 测试

集成测试会自行启动 `server/server`，因此运行前应确保 8000 端口没有其他服务端
占用：

```bash
cd /mnt/f/Projects/linux-chat/chat-new
python3 tests/test_chat.py
```

成功时输出：

```text
all protocol integration tests passed
```

如果本地已经有聊天服务占用 8000 端口，可以在独立网络命名空间中运行测试，避免
影响现有进程：

```bash
sudo unshare -n -- bash -lc '
  ip link set lo up
  cd /mnt/f/Projects/linux-chat/chat-new
  python3 tests/test_chat.py
'
```

测试覆盖：

- 登录成功、名称格式和未登录消息限制
- 不区分大小写的在线名称冲突
- 登录失败后在原连接上改名重试
- 聊天消息双向转发和加入/离开提示
- TCP 单字节分片及连续多个协议帧
- 最大聊天正文和最大协议长度边界
- 断线后名称释放并可再次登录
- 超长协议帧被拒绝
- 服务端控制台不打印转发的聊天正文

修改服务端或协议后，建议至少执行：

```bash
make -C server
make -C client
python3 tests/test_chat.py
```

## WSL 运行说明

Windows 11 的 WSLg 通常可以直接显示 GTK 窗口。先检查显示变量：

```bash
echo "$DISPLAY"
```

如果没有可用的图形显示环境，可以使用 Xvfb 做无界面启动检查。以下命令让 Xvfb
监听 TCP，以适配部分 WSL 环境中 Unix socket 不可写的情况：

```bash
Xvfb :90 -screen 0 1024x768x24 -listen tcp -nolisten unix -ac &
cd /mnt/f/Projects/linux-chat/chat-new/client
DISPLAY=127.0.0.1:90 ./client
```

客户端固定连接 `127.0.0.1:8000`，因此客户端和服务端需要运行在同一个网络命名
空间或同一台 Linux/WSL 主机中。

## 常见问题

### `./server: cannot execute: required file not found`

通常是对非 Linux 可执行文件运行了 `./server`，或者二进制来自其他平台。请确认
当前位于 WSL 的 Ubuntu，并重新编译：

```bash
make -C server clean
make -C server
file server/server
```

`file` 输出应包含 `ELF 64-bit`。

### `bind: Address already in use`

先检查 8000 端口：

```bash
ss -ltnp 'sport = :8000'
```

停止旧服务端后重新启动。不要在未确认进程用途时直接结束其他进程。

### 客户端自动连接失败

确认服务端已经启动，并且客户端与其位于同一个 Linux/WSL 网络环境。客户端当前
固定连接本机 `127.0.0.1:8000`；服务端通过 `INADDR_ANY` 监听所有 IPv4 地址。

### 客户端无法打开窗口

确认 `DISPLAY` 已设置且当前环境可以访问图形显示服务。没有 WSLg 时，可以使用
上文中的 Xvfb 命令做启动检查。

## 当前限制

- 只支持明文 TCP，没有 TLS。
- 没有账号密码、身份持久化、私聊、文件传输或离线消息。
- 名称只在写入服务端会话表后生效，不写入磁盘。
- 服务端最多支持 10 个同时存在的连接。
- 客户端服务器地址和端口需要在源码中修改。
