# 三路资源部署与调用方收口

本方案生成可审阅的部署配置和声明，复用[上线证据门禁](resource-release-readiness.md)，不替代该门禁，不自动授权上线、激活或删列。当前正式库尚未迁移，真实旧 writer 尚未取得停写证明；已有真实三路和本地双实例恢复结果不是本方案已在生产执行的证明。历史本地文件按用户范围不验收，只保留数据库原路径，不扫描旧文件、不补下载、不清空路径。

## 配置与检查工具

模板位于 [resource-production.env.example](../../config/resource-production.env.example) 和 [resource-deployment-plan.example.json](../../config/resource-deployment-plan.example.json)。复制到仓库外受保护目录后填写；仓库模板中的空值和 null 保持未知。工具仅需 Python 3.10+ 标准库，所有输出独占创建，不覆盖先前审阅或失败现场：

```powershell
python scripts/resource-deployment-tests.py
python scripts/resource-deployment-readiness.py template --output '<新的部署声明.json>'
python scripts/resource-deployment-readiness.py config --env-file '<受保护的显式env>' --output '<新的离线配置检查.json>'
python scripts/resource-deployment-readiness.py inventory --output '<新的本机脱敏库存.json>'
```

`config` 仅在指定 `--probe-rc`、`--probe-targets`、`--mysql` 时访问所选依赖，不启动 RC、crawler、worker 或迁移。离线检查即使配置完整也返回 2，因为运行依赖仍为 unknown；0 只表示本工具请求的配置/身份/版本/维护核对一致，仍输出 `production_authorized=false/activation_performed=false`。1 表示输入、依赖或独占输出错误。`template/inventory` 的 0 只表示该动作完成，必须另外读库存的截断与未知字段。

未来由操作者显式选择已经审阅的发布范围和客户端，执行只读依赖检查：

```powershell
python scripts/resource-deployment-readiness.py config --env-file '<受保护的显式env>' --release-plan '<已审阅release-plan.json>' --cli '<明确的crawler_cloud_receipts.exe>' --mysql '<明确的mysql.exe>' --probe-rc --probe-targets --output '<新的依赖检查.json>'
```

工具直接调用现有 `resource-release-check.py::validate_plan` 计算 scope 摘要，包含未提交源码清单和实际 CLI 摘要的原要求继续生效。只读 MySQL 子进程采用 `--no-defaults` 配合独立不存在的 `MYSQL_TEST_LOGIN_FILE`（兼容 MySQL 8.0；不加载默认 login 文件），密码只放本次 `MYSQL_PWD` 环境，不进入 argv、仓库或日志；原始 stdout/stderr 不打印。查询在只读事务中核对实例+库身份摘要、当前/已应用 schema 版本，以及三端真实指纹、修订和 `dispatch_state=maintenance`。这不代替正式 schema/checksum 和 SQL 兼容证据。未指定数据库检查、连接失败或范围不一致均无法判就绪。

本轮发布范围显式采用 `scope.history_local_files=preserve_paths_only`，并填写完整 `scope.local_path_manifest_sha256`。该字段进入原 scope 摘要；未填写策略的旧计划仍按 `verify`，不会被本工具静默豁免。只将 `file_restore` 标为 `excluded_not_tested`，不算通过；`clone_upgrade` 仍须提供 `historical_local_paths_preserved=true`、`local_path_manifest_sha256_before/after` 均等于 scope 摘要，以及非负整数且相等的 `local_paths_count_before/after`。四 owner 的路径元数据可用 [resource-local-path-manifest.py](../../scripts/resource-local-path-manifest.py) 在显式隔离库只读生成，仅读 owner/id/local_path 的数据库摘要，不打开历史文件。部署声明的 `history_local_files` 必须与这一发布范围一致，不能免除其他证据。

两端 RC 先用匿名 `core/version` 确认 401，再用本端 Basic Auth 只读获取版本；没有认证保护的 RC 拒绝通过。HTTP 只允许 loopback，其他地址要求 HTTPS，禁止 URL 中携带认证/query/fragment，不跟随重定向、不使用系统代理。真实目标身份仍由同一发布 CLI 的 `target-probe` 核对，云端指纹必须匹配发布范围。百度的 `webdav_mount` 证明网关挂载身份，不证明底层百度账号；Google 为已登录的原生交互 OAuth remote，核对实际账号/根，而非 remote 名称。只读可达和身份一致不证明写权限；本次不重新上传来补证。

本地只做配置根及 staging 的目录存在/冲突检查，不遍历历史文件，不运行 local target-probe。新 local 是否启用与历史文件免验是两个选择；模板保持 `LOCAL_ENABLED=true`，使新任务可按三路执行；只有明确选择云端任务时才关闭它。历史文件免验不关闭新任务 local，也不清理旧路径。路径预算要求：三端 staging 为绝对现有目录，彼此不重合或包含，云端 staging 不与正式存储根互相包含，每端至少容纳一个 `MAX_BYTES`，全局来源/发布槽不少于启用端数。工具不会替操作者创建数据目录或证明 ACL、剩余空间和硬盘性能；须在部署宿主核对这些条件。

## 已有 rclone 登录与常驻进程边界

沿用已授权 WebDAV 和原生 Google OAuth remote，不重新登录、不修改原 `rclone.conf`。部署宿主从受保护 secret store 或受限文件提供运行环境；dotenv 不展开 `${...}`，也不识别 `secret://...`。secret 引用必须由宿主先解析，不能把引用字符串当密码传给进程。两端配置和 RC 密码分别供应；不要把完整环境、remote 配置或 RC 请求头写入日志。

部署为两个独立认证 RC 和一个 parallel crawler。每个 RC 使用仓库外专属受保护配置副本，以避免两个常驻进程共享可写 OAuth 配置；副本复用已有授权，仅由部署宿主管理刷新和轮换，原登录文件保持只读。不得把配置副本加入源码/发布证据。crawler 使用逐端 `CRAWLER_RESOURCE_<BAIDU|GDRIVE>_RCLONE_*`，兼容既有全局 RC 默认值，但不要用两个 remote 别名伪装不同实际目标。

下面是供审阅的手动启动骨架，**本轮未执行**。`$rclone` 必须由已校验的可执行文件路径供应。函数仅让本轮新进程继承 secret 环境，argv 中没有账号密码或配置正文；它没有注册系统服务：

```powershell
function Start-ResourceRc([string]$target, [string]$address) {
    $keys = @('RCLONE_CONFIG', 'RCLONE_RC_USER', 'RCLONE_RC_PASS')
    $before = @{}
    foreach ($key in $keys) { $before[$key] = [Environment]::GetEnvironmentVariable($key, 'Process') }
    try {
        $env:RCLONE_CONFIG = [Environment]::GetEnvironmentVariable("CRAWLER_DEPLOYMENT_${target}_RCLONE_CONFIG", 'Process')
        $env:RCLONE_RC_USER = [Environment]::GetEnvironmentVariable("CRAWLER_RESOURCE_${target}_RCLONE_RC_USER", 'Process')
        $env:RCLONE_RC_PASS = [Environment]::GetEnvironmentVariable("CRAWLER_RESOURCE_${target}_RCLONE_RC_PASSWORD", 'Process')
        if (-not $env:RCLONE_CONFIG -or -not $env:RCLONE_RC_USER -or -not $env:RCLONE_RC_PASS) { throw 'Explicit protected RC configuration required' }
        if (-not [IO.Path]::IsPathRooted($env:RCLONE_CONFIG) -or -not (Test-Path -LiteralPath $env:RCLONE_CONFIG -PathType Leaf)) { throw 'RC configuration file unavailable' }
        Start-Process -FilePath $rclone -ArgumentList @('rcd', '--rc-addr', $address, '--log-level', 'ERROR') -WindowStyle Hidden -PassThru
    } finally {
        foreach ($key in $keys) { [Environment]::SetEnvironmentVariable($key, $before[$key], 'Process') }
    }
}
$baiduRc = Start-ResourceRc 'BAIDU' '127.0.0.1:5572'
$gdriveRc = Start-ResourceRc 'GDRIVE' '127.0.0.1:5573'
```

正式常驻使用部署环境已有的服务监管器，记录服务标识、运行账号、可执行文件摘要、工作目录、受保护 secret 引用、认证监听、退出/重启策略和监控负责人。将上述环境分配到各自子进程，不把用户名/密码改成 `--rc-user/--rc-pass`。本仓库不假定 NSSM、WinSW 或其他服务包装器已经安装，不自动注册计划任务/服务，也不把手工 Popen 句柄当成生产监管。crawler 依赖两个 RC 的就绪检查；故障重启不能悄悄回退到旧模式或切换 remote。

crawler 的宿主必须在启动前设置 `CRAWLER_ENV_FILE` 的实际存在绝对路径并注入所需 secrets，确保工作目录与发布源码一致。保持 `CRAWLER_MYSQL_INIT_SCHEMA=false`：正式升级只能在批准维护窗口单独执行，绝不能靠首次常驻启动迁移正式库。保持三端持久 maintenance，逐项核对固定 legacy view 和已持久 profile；固定 view 不一致需要显式治理，不能用改 env 或重启覆盖。producer 默认关闭；需开启时单独安排有效 MRC key 的 secret 供应和预算，不把“缺 key 等待”当成全部任务完成。

## 本机库存与调用方逐项处理

`inventory` 只读本机进程、服务、计划任务和 Git 已跟踪/未忽略的相关源码文件。宿主匹配在 PowerShell 内部执行，不输出命令行、服务路径、计划动作或凭据；只输出受控镜像类别、PID、身份 SHA-256、状态和计数。每类最多 64 项，30 秒超时，原生输出最多 64 KiB。匹配项是候选，可能含维护/构建辅助进程，不能直接认定为 writer。无法枚举或超限必须保留 unavailable/truncated。

仓库最多检查 3000 个相关文件，每文件 2 MiB，输出至多 256 个相对文件/行号/固定路由引用；优先 scripts/config，然后实现、测试和说明。定义、fixture 和文档引用不是已经部署的调用方。上限触发后 `repository_scan_complete=false`，不能当成全量清单。无匹配也不能证明独立机器、容器、外部脚本、旧二进制或人工 SQL 已停止；输出始终保留 `external_writers=null/writers_stopped_proven=false`。

部署声明按每个实际调用方填写 owner、kind、稳定 caller_id，以及所有写路由的 `upgraded/disabled/unknown` 决定和对应原始证据文件摘要。盘点范围必须包含其他宿主；`unknown_callers` 由操作者审阅后明确填写，工具不会自动补 0。原始记录只放受保护目录，用相对路径、bytes、SHA-256 引用；文件范围/摘要校验复用发布工具。检查命令：

```powershell
python scripts/resource-deployment-readiness.py declarations --plan '<部署声明.json>' --release-plan '<同范围release-plan.json>' --output '<新的声明检查.json>'
```

| 旧写入口 | 调用方可以审阅的决定 |
| --- | --- |
| resources/process、covers/process | 明确迁到 v2 `resources/process`；或保留 `fixed_legacy_process`，并核对已持久固定 view、维护保护和批次进度/停止 |
| resources/retry、resources/reset | 旧入口继续 503；若调用方改为 `v2_resources_process_action_retry`，必须明确 `starts_transfer_accepted=true/equivalent=false` 并提供测试证据。它开始受理传输，与原重置状态不同；不改成普通 process、不解除历史 skip |
| resources/recover-stale、covers/recover-stale | 明确改用 `maintenance_cli_recover_tasks`，显式逐端目标、停止 writer 和有限 stale 阈值；不能继承旧过滤器为全端恢复 |
| maintenance/recover-stale | 没有单一等价替代，先明确停用旧调用方；元数据恢复和逐端任务恢复分别审批执行 |
| metadata/backfill/其他未接线 writer | 继续 503，不能填写不存在的 v2 等价入口；先明确停用，再独立实现并验证迁接 |

上表中的“升级”指调用方动作已改造，**不表示服务器旧 retry/reset 或所有旧 API 已经迁接**。503、migration 对象和指导文本本身不是部署升级证据，详见[旧调用方契约](resource-compatibility-cleanup.md)。

## 停写窗口、启动与停止顺序

1. 先冻结入口和新任务提交，逐个升级/停用声明中的旧二进制、计划任务、服务及外部脚本；保留 owner 确认和实际执行记录。
2. 按[服务生命周期](service-lifecycle.md)查询并协作取消批次，观察 worker、producer、认领和 I/O 排空。`tasks/stop` 的 200 不是网络/回写全部结束证明，health 也不是准入就绪证明。
3. 向每个已确认 crawler 进程发送其支持的 Ctrl+C/SIGINT，或通过能够转发该信号的既有监管器停止；等待实际退出、PID/监听和认领核对。没有 HTTP 全服务 shutdown 路由；强杀仅是中断恢复路径，不能写成正常排空。
4. 由操作者填写带时区的停写起止时刻、全实例停止及 running_claims/uncertain_tasks/external_writers 的明确观察值。窗口达到原 release 阈值，结束至检查不超过 5 分钟；任何 unknown 或非零均不通过。`basis=operator_attestation` 仍是操作者声明，附件真实性不由工具独立认证。
5. 保存本轮批准范围内的备份及未决清单，按原发布门禁完成正式迁移、历史采信和基线治理；工具不执行这些操作。历史本地免验只按发布计划原规则处理，不能扩展为免除数据库、调用方或停写证据。
6. 先启动认证 RC 并核对版本/身份，再以关闭自动初始化的 crawler 在持久 maintenance 下启动；检查 health、schema/status、settings、只读 catalog、队列和目标。保持原路径；准入打开前提交的处理请求应被维护闸门拒绝。
7. 完成统一 release 证据审阅后，另行批准基线激活和放量。先小范围明确资源 ID/目标/幂等键，核对未决与取消，再恢复已验证调用方；不得以配置检查的 0 自动激活。

正常关停顺序为冻结调用方、三端维护、协作取消/排空 crawler、确认 crawler 退出，最后通过各自监管器停止本轮明确身份的两个 RC。停 RC 前确认相关任务已结束；重启顺序反向。记录未决意图供显式 recover/repair，不能删除 staging/journal 或自动重传来“清零”。不按进程名称批量 Stop-Process，不停止原 MySQL 服务。

## 删除前 capture 与兼容期收缩

在部署流程中禁止绕过 capture 的删除脚本/人工 SQL。每个删除入口和负责 owner 都应提供程序化或受控人工的“删除前捕获→保存审阅记录→执行已经单独批准的资源删除→只读核对”流程。涉及父实体、MRC 及级联时，先枚举并分别捕获 song/singer/album/song_artifact 四类受影响 ID；一次 capture 至多 200 个 ID，必须三端 maintenance、所有 writer 已停止排空。示例和状态含义复用[清理说明](resource-compatibility-cleanup.md#删除前捕获)。

声明中的 `capture_before_every_delete` 和 `parent_cascade_four_owners_reviewed` 需要真实部署证据，不能因为 CLI 存在而填 true。`cleanup-preview/status` 只读候选不是线上对象存在/无共享引用的永久证明，也不授权远端删除。本工具不删除远端对象，不生成删列 migration；兼容期与旧字段退出仍按[退役要求](resource-schema-retirement.md)独立审阅。

声明检查仅提示材料可交给原发布 kind `writer_compatibility/writers_stopped` 复核，始终 `release_evidence_generated=false`。原发布 JSON 仍须逐项提供正确 measurements、scope_sha256 和实际执行附件；不要直接把声明检查 JSON 改名为发布证据。任何工具通过都不自动授权正式发布或删列。
