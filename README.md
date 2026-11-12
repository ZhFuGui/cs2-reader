# cs2-reader

![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)
![Windows x64](https://img.shields.io/badge/Windows-x64-0078D4)
![VS2022](https://img.shields.io/badge/Visual_Studio-2022-5C2D91)

用 Visual Studio 2022 打开 `cs2-reader.sln`，选择 **Release / x64**，构建解决方案。需要 C++ 桌面开发工具、Windows SDK 和已加载的 hyperv-guest-memory（不兼容旧 hyper-reV ABI）。

默认关闭预编译头，Release 使用 `/MT`，Debug 使用 `/MTd`。启动项目为 `cs2-reader`，一次构建在同一输出目录生成 `cs2-reader.exe` 和 `cs2-reader.dll`，中间文件按形态隔离。

Release 无控制台、不生成 PDB，关闭未使用的 RTTI，并启用可复现链接；Debug 保留控制台和调试符号。运行日志默认只进 1MiB 环形缓冲，`--log-file` 才在 `%TEMP%` 下创建随机名日志。后台运行可用 `--stop-event NAME` 停止。

```powershell
msbuild cs2-reader.sln /m /p:Configuration=Release /p:Platform=x64
.\bin\x64\Release\cs2-reader.exe
.\bin\x64\Release\cs2-reader.exe --bind 0.0.0.0 --port 13202
```

DLL 经 Windows 正常加载后自动启动，共用 EXE 的业务逻辑；使用默认配置（2 个采集 worker、90Hz、只读、监听 `127.0.0.1:13202`），不读取宿主命令行、不接管宿主控制台。模块固定到宿主进程生命周期；游戏退出或消费者断线仍继续等待，可恢复错误沿用现有重试，致命异常仅收尾并停止 Reader。无启动、停止导出接口，不支持运行中卸载；宿主结束时由系统回收。手动映射不在支持范围内，EXE 和 DLL 不应同时占用相同监听地址及端口。

hyper-mem 是唯一后端，自动寻找目标进程，默认只读；不接受 `--backend` 选项。默认使用 2 个采集 worker，可用 `--workers N` 覆盖。偏移直接使用 `output`，目标 build 不匹配时拒绝采样。



默认监听 `127.0.0.1`；`--bind` 可指定本机局域网 IPv4，或用 `0.0.0.0` 监听所有 IPv4 网卡。客户端填写 Reader 主机的局域网地址，使用相同的 TCP 端口和 v1 协议；跨机时间通过既有时钟往返映射。

可通过 `/p:BuildArtifactsRoot=U:\cs2\build\cs2-reader` 将构建产物放到项目外。只改变中间目录时使用 `/p:BuildIntermediatesRoot=...`。
