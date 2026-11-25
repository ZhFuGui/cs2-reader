# Reader v2 状态与诊断编号

本文维护 Reader v2 的状态、订阅回执和运行诊断约定。部署时以同一版本的协议描述符和 SDK 配对。

## 维护编号

在 [编号注册表](diagnostic-codes.json) 中维护原因和事件的整数、C++ 名称、中文含义和用法。状态原因可填写 `availability`，对照表会据此生成可用性与原因码的组合。构建根据注册表生成 C++ 枚举，并检查下方两张编号表是否一致。修改注册表后，在仓库根目录运行以下命令刷新表格。

```powershell
.\build\generate_diagnostic_codes.ps1 -ReaderRoot . -UpdateMarkdown
```

## 编号格式

Reader 内的 `reason_code` 和 `event_id` 各自使用显式分配的 `uint32` 正整数。对照表以 `R` 表示原因（Reason），以 `E` 表示事件（Event），后接至少三位十进制数字。`R001` 对应 `reason_code=1`，`E001` 对应 `event_id=1`。协议和结构化日志使用相同的原因整数。

每个编号在所属字段内唯一，`0` 保留给未指定。编号发布后固定，停用的编号继续留档。可用性、严重级别、协议版本、会话代次及原生错误码由各自的字段表达。代码重构后，同一故障继续使用已发布的原因编号。 [Windows 事件编号](https://learn.microsoft.com/en-us/windows/win32/wes/eventschema-eventid-systempropertiestype-element) [OpenTelemetry 日志字段](https://opentelemetry.io/docs/specs/otel/logs/data-model/#field-eventname)

## 协议 v2

`Envelope`、`Hello` 和 `Subscribe` 使用 SDK 中同一个协议版本常量 `2`。按现有流程冻结 v2 描述符、摘要和 SDK，Reader 与订阅端成对升级。版本和摘要都须通过握手校验。v2 作为唯一运行协议。

| 消息 | 字段 | 约定 |
|---|---|---|
| `Status` | `availability` | 表达数据可用性 |
| `Status` | `optional uint32 reason_code = 5` | 有细分原因时填写正整数，原字符串字段号 `4` 保留 |
| `Reject` | `request_id` | 关联被拒绝的请求 |
| `Reject` | `uint32 reason_code = 4` | 必须大于零，原字段号 `2` 和 `3` 保留 |

保留原字段号可避免同一字段号按不同类型解释。原因码在线上是开放的正整数，C++ 内部用有语义的枚举名。新增原因码通过注册表分配，沿用现有字段结构和协议摘要。 [Protobuf 字段演进](https://protobuf.dev/programming-guides/proto3/#updating)

| `availability` | 含义 | 订阅端行为 |
|---:|---|---|
| `1` | 等待目标进程 | 等待后续状态和快照 |
| `2` | 采集可用 | 等待或接收当前代次的快照 |
| `3` | 数据不可用 | 撤销旧数据的当前有效性 |
| `4` | 正在停止 | 结束当前会话 |

`Status` 的缺省原因或 `0` 表示没有细分说明，发送端使用缺省。未知的正整数原因码原样显示或记录，订阅端继续按 `availability` 判断。未知可用性按数据不可用处理。`Reject` 缺失原因或原因为 `0` 时按协议错误处理。未知的正整数原因仍表示拒绝，已确认的订阅设置继续生效。 [Google API 错误标识设计](https://google.aip.dev/193#statusdetails)

文档将 `availability=3` 与 `reason_code=1` 的组合简写为 `3/1`，表示数据不可用，原因为 `R001`。`Ready`、心跳和 `last_successful_capture_sequence` 都只提供状态或历史进展。数据恢复须有当前代次的新快照。

## 不兼容时的行为

确认当前源和配置不兼容时，初始化层抛出 `IncompatibleSource`，由运行循环发布 `3/1`。发布层撤销最新待编码输入及已编码快照，服务继续响应状态、订阅请求和心跳。同一源实例与配置保持不可用，源实例或已部署配置变化后重新校验。暂时不可读或校验结果不稳定时继续走等待重试。用户停止信号保持优先。 [初始化校验](../../src/game/cs2/session/context.cpp#L22) [运行循环](../../src/runtime/runner.cpp#L94)

沿用发布令牌和失效序号阻止旧结果晚到。失效时丢弃尚未发出的旧快照。半条 TCP 记录已发送时关闭受影响连接，由断线使旧数据失效。快速失效又恢复时，订阅端先收到失效，再收到就绪状态。 [发布失效](../../src/synchronization/v2/publication.cpp#L30) [订阅排序](../../src/synchronization/v2/subscribers.cpp#L89) [短写处理](../../src/transport/tcp_peer.cpp#L44)

## 原因编号

<!-- BEGIN GENERATED reasons -->

| 编号 | C++ 名称 | 含义 | 使用位置或行为 |
|---|---|---|---|
| `R001` | `SourceIncompatible` | 当前采集配置与已校验的源不兼容 | `3/1`。暂停采集，服务继续提供状态和心跳 |
| `R002` | `DiscoveryUnavailable` | 目标发现暂时不可用 | `3/2`。按已有等待间隔重试 |
| `R003` | `BindingUnavailable` | 目标绑定暂时不可用 | `3/3`。按已有恢复路径核对绑定 |
| `R004` | `ContextPending` | 采集所需上下文尚未就绪 | `3/4`。等待上下文可用后再校验 |
| `R005` | `WorkersUnavailable` | 采集工作线程暂时不可用 | `3/5`。按已有等待间隔重试 |
| `R006` | `SourceRetired` | 已确认目标实例退出或被替换 | `3/6`。撤销该实例的发布资格 |
| `R007` | `SourceContextChanged` | 已确认采集上下文变化 | `3/7`。撤销旧代次，按已有流程确认新代次 |
| `R008` | `CaptureUnavailable` | 当前采集暂时失败 | `3/8`。撤销当前发布结果，等待下一次有效采集 |
| `R009` | `EncodingFailed` | 当前数据编码失败 | `3/9`。撤销失败结果，继续处理后续有效输入 |
| `R010` | `SourceInvalidated` | 源已恢复，订阅端尚未收到此前的失效 | `3/10`。先通知失效，再通知当前状态 |
| `R011` | `StaleRequest` | 订阅请求序号陈旧 | `Reject`，关联原请求 ID |
| `R012` | `ProtocolVersionMismatch` | 订阅协议版本不匹配 | `Reject`，关联原请求 ID |
| `R013` | `SchemaMismatch` | 订阅协议描述摘要不匹配 | `Reject`，关联原请求 ID |
| `R014` | `UnsupportedRate` | 请求频率超出支持范围 | `Reject`，关联原请求 ID |
| `R015` | `InvalidOptions` | 启动参数无效 | 本地故障事件，按参数校验结果退出 |
| `R016` | `ApplicationFailure` | 应用入口收到未分类的致命异常 | 本地故障事件，保留明确的进程退出状态 |
| `R017` | `NetworkUnavailable` | TCP 服务初始化或运行失败 | 本地故障事件，附可取得的原生错误码 |
| `R018` | `LogFileUnavailable` | 文件日志打开或写入失败 | 本地故障事件，继续记录到内存日志 |
| `R019` | `SnapshotExportFailed` | 诊断快照导出失败 | 本地故障事件，记录导出状态 |
| `R024` | `WaitExpired` | 等待目标达到用户配置的时限 | 本地故障事件，按等待策略退出 |

<!-- END GENERATED reasons -->

## 日志事件编号

日志记录 UTC 时间、级别和数值 `event_id`，并按场景附 `availability`、`reason_code`、会话代次或原生错误码。事件编号表示发生了什么，原因码提供该事件的细分原因。

<!-- BEGIN GENERATED events -->

| 编号 | C++ 名称 | 事件 | 主要字段 |
|---|---|---|---|
| `E001` | `StatusChanged` | 状态切换 | `availability`、可选 `reason_code` 和会话代次 |
| `E002` | `Fault` | 本地故障 | `reason_code`、可选的 `os_code` |
| `E003` | `Started` | 服务启动 | 版本、频率等运行配置 |
| `E004` | `SourceOpened` | 采集会话建立 | 会话代次 |
| `E005` | `CaptureStats` | 采集统计 | 序号、计数、频率、耗时 |
| `E006` | `SubscriptionStats` | 订阅统计 | 连接、发送、合并计数 |
| `E007` | `Stopped` | 服务停止 | 最终计数 |

<!-- END GENERATED events -->

```text
2026-11-19T02:15:42Z INFO event_id=1 availability=1
2026-11-19T02:15:43Z INFO event_id=1 availability=2 epoch=6
2026-11-19T02:15:44Z WARN event_id=1 availability=3 reason_code=1
2026-11-19T02:15:45Z WARN event_id=2 reason_code=17 os_code=10048
```

同一状态、原因和代次重复出现时合并日志。按现有周期记录采集及发送摘要。在异常处理处生成编号，系统错误保留已取得的原生数值。CLI 帮助和诊断导出继续按各自的人机和数据接口维护。 [采集摘要](../../src/diagnostics/capture_report.cpp#L16) [发送摘要](../../src/app/outputs.cpp#L29)

编码与 TCP 分别保存各自的当前故障，复用现有锁。当前令牌的编码结果成功提交时清除编码故障，TCP 服务恢复监听时清除网络故障。周期诊断能同时记录两项故障。 [故障与恢复](../../src/synchronization/v2/publication.cpp#L55) [TCP 恢复入口](../../src/synchronization/v2/service.cpp#L92)

## 验收

1. 用离线输入覆盖稳定不兼容、临时失败、恢复后的新代次和用户停止。核对服务持续响应、同一状态只记录一次，以及旧数据撤销。
2. 用冻结描述符配对测试 v2 正常订阅、全部状态、四类拒绝、未知原因、摘要不符和跨版本拒绝。验证调频被拒后此前确认的订阅仍有效。 [冻结脚本](../../build/freeze_protocol.py#L14) [构建核验](../../build/verify_protocol.ps1#L9)
3. 核对编号各自唯一，同一原因在协议和日志中取值一致。验证编码与网络故障可以同时存在并独立恢复。以短写测试检查失效时的断线处理，再覆盖文件日志写入失败和诊断导出失败。

编号表和 C++ 枚举由同一份注册表生成。 [生成脚本](../../build/generate_diagnostic_codes.ps1) [v2 协议](v2/reader.proto#L40)
