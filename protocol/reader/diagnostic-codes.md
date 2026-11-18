# Reader v2 状态与诊断编号

本页列出 Reader v2 的诊断编号，供维护者查阅和生成代码。

## 维护编号

在 [编号注册表](diagnostic-codes.json) 中维护原因和事件的整数、C++ 名称、中文含义和用法。状态原因可填写 `availability`，对照表会据此生成可用性与原因码的组合。构建根据注册表生成 C++ 枚举，并检查下方两张编号表是否一致。修改注册表后，在仓库根目录运行以下命令刷新表格。

```powershell
.\build\generate_diagnostic_codes.ps1 -ReaderRoot . -UpdateMarkdown
```

## 编号格式

Reader 内的 `reason_code` 和 `event_id` 各自使用显式分配的 `uint32` 正整数。对照表以 `R` 表示原因（Reason），以 `E` 表示事件（Event），后接至少三位十进制数字。`R001` 对应 `reason_code=1`，`E001` 对应 `event_id=1`。协议和结构化日志使用相同的原因整数。

每个编号在所属字段内唯一，`0` 保留给未指定。编号发布后固定，停用的编号继续留档。可用性、严重级别、协议版本、会话代次及原生错误码由各自的字段表达。代码重构后，同一故障继续使用已发布的原因编号。 [Windows 事件编号](https://learn.microsoft.com/en-us/windows/win32/wes/eventschema-eventid-systempropertiestype-element) [OpenTelemetry 日志字段](https://opentelemetry.io/docs/specs/otel/logs/data-model/#field-eventname)

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

编号表和 C++ 枚举由同一份注册表生成。 [生成脚本](../../build/generate_diagnostic_codes.ps1)
