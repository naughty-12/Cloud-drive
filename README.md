# 0323 Cloud Disk (云盘系统)

> C++11 云盘系统 | IOCP 异步 I/O | AI 智能预览/搜索/标签 | 分布式存储原型

[![C++](https://img.shields.io/badge/C%2B%2B-11-blue)](https://isocpp.org/)
[![Qt](https://img.shields.io/badge/Qt-5.15.2%20%7C%206.2.4-green)](https://www.qt.io/)
[![MySQL](https://img.shields.io/badge/MySQL-8.0.40-orange)](https://www.mysql.com/)
[![AI](https://img.shields.io/badge/AI-MultiProvider-purple)](https://siliconflow.cn/)

## 项目亮点

- **IOCP 异步 I/O**: Windows IOCP 事件驱动架构，1 accept 线程 + 4 worker 线程 + 独立 DB 线程（附压测工具，验证 100 并发）
- **自研二进制读写器**: BinaryStream + ProtocolFactory，37 个协议类型号（2~38），网络字节序统一
- **三层秒传漏斗**: L1 SQLite 缓存 → L2 稀疏指纹 → L3 Bloom Filter
- **SHA-256 安全基线**: 自实现 RFC 6234，替换 MD5；Prepared Statement 防 SQL 注入
- **AI 可插拔增强**: 多 Provider（SiliconFlow / OpenAI / Ollama / 自定义）智能预览 + Embedding 语义搜索 + 自动标签，API 不可用时静默降级
- **断点续传**: SQLite 持久化上传状态，服务重启自动恢复
- **分布式存储原型**: 哈希取模分片（FNV-1a，无虚拟节点）+ 节点镜像 + 重定向协议，双服务器可演示

## 架构

```
┌──────────────────────────────────────────────────────────┐
│                      CLIENT (Qt Widgets)                   │
│  login1 | Widget | TagCloud | tcpkernel | TCPClient       │
└─────────────────────── TCP/8899 ─────────────────────────┘
┌──────────────────────────────────────────────────────────┐
│                   SERVER (IOCP + Qt Core)                  │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌─────────┐ │
│  │ IocpServer│  │ tcpkernel│  │ DbWorker │  │ AI Layer│ │
│  │ 1 Accept  │  │ Protocol │  │ Async DB │  │ Multi-  │ │
│  │ N Workers │  │ Dispatch │  │ Task Q   │  │ Provider│ │
│  └──────────┘  └──────────┘  └──────────┘  └─────────┘ │
│  ┌──────────────────────────────────────────────────┐   │
│  │  FileStorage (blocks.dat + blocks.idx)            │   │
│  │  MySQL (metadata) + SQLite (upload_state)         │   │
│  │  BloomFilter (1.7MB / 100万 files)               │   │
│  └──────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────┘
```

## 环境要求

- Windows 10/11
- Qt 5.15.2 MinGW 8.1.0 64-bit（代码兼容 Qt 6.2.4 MinGW 11.2.0）
- MySQL 8.0
- MinGW 工具链（g++ 8.1.0 或 11.2.0，需与 Qt 版本匹配）

## 编译

```bash
# 1. libprotocol 静态库
cd shared/libprotocol
qmake && mingw32-make

# 2. 服务端
cd ../../0323server
qmake && mingw32-make

# 3. 客户端
cd ../0323client
qmake && mingw32-make
```

## 运行

```bash
# 1. 初始化数据库（默认 root/20041130，可改 scripts/init-db.sql 与 server.conf）
mysql -u root -p20041130 < scripts/init-db.sql

# 2. 启动服务端
cd 0323server/release
0323server.exe

# 3. 启动客户端 (另一个终端)
cd 0323client/release
0323client.exe

# 或一键启动
scripts/start-all.bat
```

> OpenSSL 依赖：Qt 5.15.2 运行时需 `libssl-1_1-x64.dll` + `libcrypto-1_1-x64.dll`，
> 已随仓库提供于 `0323server/third_party/openssl/`，拷贝到 exe 同目录即可。

## AI 功能（可选，未配置不影响基础功能）

```bash
# 方式一：环境变量（推荐）
set SILICONFLOW_API_KEY=sk-your-siliconflow-key   # 硅基流动（国内直连）
# 或 set OPENAI_API_KEY=sk-your-openai-key         # OpenAI
# 或 set OLLAMA_HOST=http://127.0.0.1:11434        # Ollama 本地（无需 Key）

# 方式二：server.conf 的 ai 段配置（可配 api_key 字段，环境变量优先）
# 优先级：server.conf ai 段 > SILICONFLOW_API_KEY > OPENAI_API_KEY > OLLAMA_HOST > 禁用
```

## 项目结构

```
0323/
├── shared/libprotocol/      # 协议静态库 (Packdef.h + BinaryStream + ProtocolFactory)
├── shared/log/              # 共享日志模块 (LogManager)
├── shared/crypto/           # 共享安全模块 (CryptoUtil — SHA-256，两端同一份源码)
├── 0323server/              # 服务端
│   ├── iocp/                # IOCP 异步 I/O 网络层
│   ├── db/                  # MySQL 封装 + 异步任务队列 + 断点续传状态
│   ├── ai/                  # AI 桥接层 + 智能预览 + 语义搜索 + 自动标签
│   ├── storage/             # 追加写存储引擎 + Bloom Filter
│   ├── cluster/             # 分布式节点管理 (哈希取模分片 + 镜像复制)
│   ├── kernel/              # 业务内核 (协议分发 + 所有 Handler)
│   └── third_party/openssl/ # OpenSSL 1.1.1 运行时 DLL
├── 0323client/              # 客户端
│   ├── kernel/              # 客户端内核 (信号发射)
│   ├── tcpclient/           # TCP 网络层 (WinSock2)
│   ├── cache/               # SQLite 秒传缓存
│   └── login1.* / widget.* / TagCloud.*  # 登录 + 主窗口 + AI 标签云
└── scripts/                 # 部署脚本 + 数据库初始化
```

## 技术栈

| 层级 | 技术 |
|------|------|
| 语言 | C++11 |
| 框架 | Qt 5.15.2（Core + GUI + Widgets + Network，兼容 Qt 6.2.4） |
| 网络 | WinSock2 + IOCP (Windows I/O Completion Port) |
| 协议 | 自定义二进制协议 (BinaryStream 序列化，类型号 2~38) |
| 数据库 | MySQL 8.0.40 + SQLite3 |
| AI | SiliconFlow / OpenAI / Ollama 多 Provider（DeepSeek-V3 / gpt-4o-mini / qwen2.5:7b） |
| 构建 | qmake + MinGW 64-bit |

## 功能矩阵

| 功能 | 服务端 | 客户端 | 技术亮点 |
|------|--------|--------|---------|
| 用户注册/登录 | ✅ SHA-256 | ✅ | Prepared Statement |
| 文件列表 | ✅ JOIN 查询 | ✅ QTableWidget | 分页支持 |
| 上传 (正常) | ✅ FileStorage | ✅ 分块发送 | 固定 4KB 块 |
| 上传 (秒传) | ✅ Bloom Filter | ✅ SQLite 缓存 | 三层漏斗 |
| 上传 (断点续传) | ✅ SqliteState | ✅ fseek | 重启恢复 |
| 下载 | ✅ 流式读取 | ✅ 循环收块 | SHA-256 校验 |
| 删除 | ✅ fcount-- | ✅ 确认对话框 | 引用计数 |
| 分享 | ✅ share_code | ✅ 显示 + 复制 | 8 位 hex |
| 提取 | ✅ code 验证 | ✅ QInputDialog | 跨用户映射 |
| AI 预览 | ✅ 多Provider | ✅ 摘要弹窗 | 降级到原文 |
| AI 搜索 | ✅ Embedding | ✅ 结果展示 | LIKE fallback |
| AI 标签 | ✅ 多Provider | ✅ 标签显示 | 降级到扩展名 |
| 分布式 | ✅ 哈希取模分片 | ✅ 重定向提示 | 节点镜像 |
| 视频播放 | ✅ HTTP Range 流媒体 | ✅ 唤起播放器 | 206 + 临时 Token |

## 安全设计

- 密码: SHA-256 + 固定盐值哈希，传输存储均为哈希值
- SQL: MySqlWrapper 参数化查询，所有 SQL 通过 Prepared Statement
- API Key: 环境变量 SILICONFLOW_API_KEY / OPENAI_API_KEY 或 server.conf ai.api_key（环境变量优先），Ollama 走 OLLAMA_HOST 无需 Key，全无则 AI 静默禁用
- 线程安全: std::mutex + std::atomic 保护所有共享状态

## License

MIT
