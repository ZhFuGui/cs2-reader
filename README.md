# cs2-reader

![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)
![Windows x64](https://img.shields.io/badge/Windows-x64-0078D4)
![VS2022](https://img.shields.io/badge/Visual_Studio-2022-5C2D91)

用 Visual Studio 2022 打开 `cs2-reader.sln`，选择 **Release / x64**，构建解决方案。需要 C++ 桌面开发工具、Windows SDK 和已加载的 hyperv-guest-memory（不兼容旧 hyper-reV ABI）。首次绑定时通过现有内存超调用定位内核映像、推断布局，并用普通进程目录核对身份；不查询系统模块表或要求 SeDebugPrivilege。

默认关闭预编译头，Release 使用 `/MT`，Debug 使用 `/MTd`。启动项目为 `cs2-reader`。

Release 无控制台、不生成 PDB，关闭未使用的 RTTI，并启用可复现链接；Debug 保留控制台和调试符号。运行日志默认只进 1MiB 环形缓冲，`--log-file` 才在 `%TEMP%` 下创建随机名日志。后台运行可用 `--stop-event NAME` 停止。

```powershell
msbuild cs2-reader.sln /m /p:Configuration=Release /p:Platform=x64
.\bin\x64\Release\cs2-reader.exe
.\bin\x64\Release\cs2-reader.exe --bind 0.0.0.0 --port 13202
```

hyper-mem 是唯一后端，自动寻找目标进程，默认只读；不接受 `--backend` 选项。偏移直接使用 `output`，目标 build 不匹配时拒绝采样。

原生进程快照首次探测长度后预留余量并复用缓冲，只在首次发现、候选出现或绑定失效时刷新。已绑定身份由 hypervisor 读取 PID、CreateTime 和退出标志核验；等待游戏时通过操作 12 查找候选，无需 Windows 进程通知 API。首次正常需要一次长度查询加一次快照查询，稳态不再定时枚举进程。

Hyper-V 模式要求上游支持批量读取操作 11 和有界链表查找操作 12，绑定时进行自读验证。进程查找默认每次遍历最多 1024 个节点，必要时有界续查；命中窗口直接提供 CR3、PEB 和创建时间。批量读取每次最多 64 项、合计 64KiB，超过时继续分批；长连续范围沿用单范围复制。日志的 `hc_single`、`hc_batch` 统计采集批次实际读取超调用次数，`read_calls` 仍为逻辑读取数量。

默认监听 `127.0.0.1`；`--bind` 可指定本机局域网 IPv4，或用 `0.0.0.0` 监听所有 IPv4 网卡。客户端填写 Reader 主机的局域网地址，使用相同的 TCP 端口和 v1 协议；跨机时间通过既有时钟往返映射。

可通过 `/p:BuildArtifactsRoot=U:\cs2\build\cs2-reader` 将构建产物放到项目外。只改变中间目录时使用 `/p:BuildIntermediatesRoot=...`。
