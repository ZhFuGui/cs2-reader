# cs2-reader

![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)
![Windows x64](https://img.shields.io/badge/Windows-x64-0078D4)
![VS2022](https://img.shields.io/badge/Visual_Studio-2022-5C2D91)

将 `readers-shared` 源码放在本仓库同级目录，或通过 MSBuild 的 `ReadersSharedDirectory` 指定其路径。用 Visual Studio 2022 打开 `cs2-reader.sln`，选择 **Release / x64**，构建解决方案。需要 C++ 桌面开发工具和 Windows SDK。Hyper-V 产物运行时需要已加载的 Hyper-mem，R3 产物通过共享层的 ntdll 后端读取目标。

默认关闭预编译头，Release 使用 `/MT`，Debug 使用 `/MTd`。启动项目为 `cs2-reader`，一次构建在同一输出目录生成四份文件，中间文件按后端和形态隔离。

| 后端 | EXE | DLL |
|---|---|---|
| Hyper-V | `cs2-reader.exe` | `cs2-reader.dll` |
| R3 | `cs2-reader-r3.exe` | `cs2-reader-r3.dll` |

Release 无控制台、不生成 PDB，关闭未使用的 RTTI，并启用可复现链接；Debug 保留控制台和调试符号。运行日志默认只进 1MiB 环形缓冲，`--log-file` 才在 `%TEMP%` 下创建随机名日志。后台运行可用 `--stop-event NAME` 停止。

```powershell
msbuild cs2-reader.sln /m /p:Configuration=Release /p:Platform=x64
.\bin\x64\Release\cs2-reader.exe
.\bin\x64\Release\cs2-reader.exe --bind 0.0.0.0 --port 13202
.\bin\x64\Release\cs2-reader-r3.exe --bind 127.0.0.1 --port 13202
```

DLL 经 Windows 正常加载后自动启动，共用 EXE 的业务逻辑；使用默认配置（2 个采集 worker、90Hz、只读、监听 `127.0.0.1:13202`），不读取宿主命令行、不接管宿主控制台。模块固定到宿主进程生命周期；游戏退出或消费者断线仍继续等待，可恢复错误沿用现有重试，致命异常仅收尾并停止 Reader。无启动、停止导出接口，不支持运行中卸载；宿主结束时由系统回收。手动映射不在支持范围内，EXE 和 DLL 不应同时占用相同监听地址及端口。

后端在编译时固定：Hyper-V 导入 `readers-shared.props`，R3 导入 `readers-shared-r3.props`，分别静态编入所选共享实现。产品只保留薄适配器，不维护另一套原生绑定和通道。两种产物都自动寻找目标进程，仅支持只读，运行时不接受 `--backend` 或 `--read-write` 选项。默认使用 2 个采集 worker，可用 `--workers N` 覆盖。偏移直接使用 `output`，目标 build 不匹配时发布不可用状态并保持订阅服务运行。

进程发现、PID＋创建时间身份、同实例重绑、模块目录、独立通道和内存读取由 `readers-shared` 负责。产品不访问共享内部头，也不接触 CR3、PEB 或 Native 句柄。共享层不创建采集线程或调度任务；游戏字段、对象关闭证明、版本校验、epoch 和订阅仍由本项目负责。

发现结果直接交接为首次会话，不为同一候选重复打开。确认地址空间变化后，先结束旧采集器和 worker，再调用共享会话的 `try_rebind`；新绑定必须保留同一个 PID 和创建时间，旧通道保持原绑定。普通字段缺页仅影响本次结果，不触发重新枚举或重绑。

共享源码固定使用 readers-shared 已发布提交 `47655f96288f0d67d8bdcad2d30bdd69c5f82618`，也是本轮验证基线。构建直接使用配置的私有源码目录，不会自动下载或更新依赖。ABI 来源与许可证由共享库的 `third_party` 目录维护。

网络额外导入 `readers-shared-tcp.props`。共享层独占监听、连接、四字节长度头、有界发送队列及短写续传；产品保留一个编码线程和 Reader v2 的握手、调频、状态及跨机时间规则。完整快照正文在编码线程封装，连接侧只复制发送副本；控制使用 Reliable，独立完整快照使用同键 Latest。每连接事件序号与发送 QPC 在首次发送尝试前原位写入，序号严格递增、允许撤销记录留下空号，短写不重新盖戳。心跳与发布机会统一安排最近定时点，不设置帧有效期或记录年龄淘汰，实际无进展超时保持默认关闭。

底层内存与进程实现由私有 `readers-shared` 维护，本仓库只使用其公开接口。构建需要具备该私有依赖的访问权限；后端能力、资源边界和安装要求以私有依赖的接入约定为准。

默认监听 `127.0.0.1`。`--bind` 可指定本机局域网 IPv4，或用 `0.0.0.0` 监听所有 IPv4 网卡。客户端使用同一 TCP 端口和 [Reader v2 协议](protocol/reader/v2/reader.proto)，跨机时间通过时钟往返映射。

在 [JSON 注册表](protocol/reader/diagnostic-codes.json) 中维护诊断编号，按 [编号对照表](protocol/reader/diagnostic-codes.md#维护编号) 中的命令刷新查阅表。

可通过 `/p:BuildArtifactsRoot=U:\cs2\build\cs2-reader` 将构建产物放到项目外。只改变中间目录时使用 `/p:BuildIntermediatesRoot=...`。
