# Migu 资源批次事件规范

本文约束 `/api/migu/resources/process` 产生的批次事件。事件用于回答“批次现在执行到哪一步”，属于可查询的业务进度和审计记录，落库到 `crawl_batch_events`，由 `/api/migu/tasks/{batch_id}/events` 返回。

## 分层边界

- 批次事件用于前端展示、结果追踪和审计查询，必须通过 `SongResourceProcessListener` 写入，application workflow 不直接依赖具体 logger。
- 运行日志用于实时排障和线下追踪，资源批次事件会通过 application 的 `DiagnosticsSink` 脱敏镜像到 console/file logger；日志不是前端任务状态的唯一来源。
- 同一业务阶段只定义一个稳定事件名，同步和异步 workflow 必须保持一致。
- payload 必须是 JSON object 字符串；没有扩展字段时写 `{}`。
- recorder 写入事件和运行日志前必须统一补齐 trace 字段：批次事件包含 `trace`、`batch_id`、`source`、`resource_kind`、`format_type`、`content_id`、`limit`、`offset`、`statuses`；item 事件包含 `trace`、`batch_id`、`item_index`、`item_key`、`resource_id`、`song_source_ref_id`、`content_id`、`resource_kind`、`format_type`、`related`、`synthetic`。
- 结束类事件必须记录耗时：`itemCompleted` / `itemFailed` 写入 item 级 `duration_ms`，`batchFinished` / `batchFailed` 写入批次级 `duration_ms`。

## 脱敏规则

- 禁止记录 Cookie、Authorization、pacmtoken 明文、完整 listen URL、完整临时下载 URL、签名参数、`Key`、`Tim`、`playSessionId`。
- 允许记录 `resource_id`、`song_source_ref_id`、`content_id`、`resource_kind`、`format_type`、状态码、字节数、相对存储路径、checksum、错误分类和稳定结果码。
- 错误文本必须截断到可诊断的短文本，不保存外部平台完整响应体。
- pacmtoken 事件只能记录是否刷新、token 长度和更新时间。

## 事件阶段

批次级事件：

- `batchStarted`：批次已创建。
- `resourceClaimStarted`：开始按请求条件认领 `song_resources`。
- `resourceClaimCompleted`：认领完成，payload 记录 `claimed_count`、`concurrency`、`retries`、`timeout_ms`。
- `resourceClaimEmpty`：认领结果为空，表示本批次没有可处理目标。
- `resourceClaimFailed`：认领失败，payload 记录脱敏错误。
- `pacmtokenRefreshed`：运行期 token 已检查或刷新，不记录明文。
- `batchFinished` / `batchFailed`：批次结束或提交失败。

资源 item 事件：

- `itemStarted`：资源 item 进入 Running。
- `resourceAttemptStarted`：一次资源处理尝试开始，payload 记录 `attempt` 和 `max_attempts`。
- `resourceUrlRequestStarted` / `resourceUrlFetched` / `resourceUrlRequestFailed`：listen URL 请求阶段。
- `resourceUrlRestricted` / `resourceUrlRejected` / `resourceUrlParseFailed`：平台业务码或解析失败阶段。
- `resourceContentDownloadStarted` / `resourceContentDownloaded` / `resourceContentDownloadFailed`：内容下载阶段。
- `resourcePathPlanned` / `resourcePathPlanFailed`：相对路径规划阶段。
- `resourceBlobSaveStarted`：blob staging 写入开始。
- `resourceBlobCommitStarted` / `resourceBlobSaved` / `resourceBlobSaveFailed` / `resourceBlobValidationFailed`：blob commit 与校验阶段。
- `resourceDbWritebackCompleted` / `resourceDbWritebackFailed`：成功资源回写完成或失败。
- `resourceFailureWritebackCompleted` / `resourceFailureWritebackFailed`：失败资源状态回写完成或失败。
- `resourceSkipWritebackCompleted` / `resourceSkipWritebackFailed`：永久跳过资源状态回写完成或失败。
- `resourceRetryScheduled`：可恢复失败将进入下一次尝试。
- `resourceAttemptFinished`：一次尝试已有最终结果或准备返回。
- `itemCompleted` / `itemFailed`：资源 item 结束。

关联歌词资源事件：

- `relatedResourceClaimStarted`：audio 响应带出歌词 URL 后开始认领关联资源。
- `relatedResourceClaimed`：关联资源认领成功，后续按普通直链资源下载。
- `relatedResourceNotClaimed`：无可认领关联资源，生成 synthetic skipped item。
- `relatedResourceDeduplicated`：同一首歌同一歌词类型已处理，当前音质不再重复下载。

## 实施约束

- 新增资源处理阶段时，优先扩展上述事件；只有出现新的独立业务阶段才新增事件名。
- 事件 payload 字段必须稳定、小而明确，不把数据库行、外部响应体或本地绝对路径直接塞入 payload。
- 单次下载进度事件必须限频；默认只记录开始、完成和失败，避免大批次写爆事件表。
- 修改事件名或 payload 语义时，需要同步更新本文件、`sql/mysql/25_create_crawl_batch_events.sql` 注释和相关测试。
- 运行日志控制台只输出文本格式；文本文件和 JSONL 文件独立配置，可同时保存。
- `INFO` 只保留批次开始/结束、认领开始/完成/空结果、item 完成/失败、资源回写完成、失败/跳过回写完成和 pacmtoken 检查刷新；尝试开始、内容下载开始、路径规划、blob 保存/提交等细粒度成功阶段默认降为 `DEBUG`。
- 运行日志默认写入 `logs/crawler.log` 文本文件，可通过 `CRAWLER_LOG_TEXT_FILE_ENABLED=true|false`、`CRAWLER_LOG_TEXT_FILE`、`CRAWLER_LOG_JSON_FILE_ENABLED=true|false`、`CRAWLER_LOG_JSON_FILE` 调整。
