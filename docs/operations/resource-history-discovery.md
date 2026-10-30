# 历史对象自动发现

此工具按数据库中的已有路径，逐对象查询指定云端的元数据，产出可审阅报告；不下载媒体、上传、改变回执或历史 skip，也不自动开放基线。适用于首次核对已有双端数据，或新启用平台后检查其历史对象。正式库执行前仍按[维护导入](resource-cloud-import.md)完成备份、目标身份核对和部署准备。

## 范围与命令

先使用既有 `target-probe` 核对真实账号/挂载身份，再按维护流程 `configure-target` 注册指纹。`discover-run` 复用服务的 parallel 及逐端配置：百度既可选 baidupcs，也可选 rclone/WebDAV；沿用既有 RC 认证，不改 rclone 配置、不重新登录。启用平台与适配工具是两个概念。

以下参数中的库名必须与所选私有配置一致。start/status/control/export 只连接数据库；只有 run 执行只读网盘探测。

```powershell
$tool = './build/msvc-debug/Debug/crawler_cloud_receipts.exe'
$taskDatabase = '<明确的目标库>'
& $tool discover-start --database $taskDatabase --run-id 'baidu-history-001' --owner song --destination baidu --path-source cloud --strip-prefix '/旧归档根' --page-size 50 --concurrency 4 --timeout-ms 5000
& $tool discover-run --database $taskDatabase --run-id 'baidu-history-001' --pages 10
& $tool discover-status --database $taskDatabase --run-id 'baidu-history-001'
& $tool discover-pause --database $taskDatabase --run-id 'baidu-history-001'
& $tool discover-resume --database $taskDatabase --run-id 'baidu-history-001'
& $tool discover-cancel --database $taskDatabase --run-id 'baidu-history-001'
& $tool discover-export --database $taskDatabase --run-id 'baidu-history-001' --output-dir '<尚不存在的报告目录>'
```

- `owner` 是 song、singer、album 或 song_artifact，指文件行而非歌曲实体；两端和四类资源分别建 run。
- `path-source` 默认 cloud，也可 local；`strip-prefix` 只剥离明确的旧根，按完整路径组件精确匹配。路径改为 `/` 分隔后必须为安全相对路径。绝对路径须明确给根映射；URL、查询参数、父目录穿越、空路径、受限资源均不探测。不猜 basename、不根据旧 baidupcs/rclone 工具字段推断平台，不遍历未知网盘对象。
- 新平台可沿用明确相同的相对目录布局，但必须自行选择正确的映射。没有可映射旧路径的行报告 unresolved，不能由此推断对象缺失。
- page-size 为 1..200，concurrency 为 1..8，timeout-ms 为 100..10000；默认 50/4/5000。页预算为 `60000 + ceil(page_size/concurrency)*timeout_ms`，不得超过 600000ms。一次 run 命令处理 1..1000 页，默认一页；再次运行从已提交游标续跑，不需要重复 start。命令内部自动有界并发，尚未接服务常驻调度或 HTTP 管理接口。

## 结果与校验值

| grade | 含义与处理 |
| --- | --- |
| unresolved | 路径未定位或资源受限；不发对象请求，人工修正映射后新建 run |
| unknown | 网络/认证/响应/字段不可用；与明确不存在分开，重查需新 run |
| missing | 精确对象查询明确返回不存在；报告本身不授权补传 |
| conflict | 对象为目录，或已知大小/相同算法摘要不一致；人工复核 |
| observed | 精确文件存在及大小可读，但缺少可比较摘要；保留弱证据 |
| hash_matched | 库内合法摘要与元数据中的相同算法相符且无已知大小冲突；不自动提升回执 |
| source_changed | 查询期间原行消失、代次、来源身份或旧值摘要变化；保留原输入及观察，但不能采信为当前事实 |

支持合法 SHA-256、MD5、SHA-1 的同算法比较；旧未知/size 指纹不当作内容摘要，不重新请求源站或回读内容计算。rclone 只用 `operations/stat`，不回退 `hashsumfile(download=true)`；原生百度只用 `meta` 的精确路径/类型/大小，不采信分片 MD5，因此通常最多 observed。相同大小或旧 MD5 匹配均不表示实际 SHA-256 核验；严格要求仍须另行授权内容核验。

## 持久性、暂停与并发

start 固定库身份、目标修订/指纹、owner、高水位、路径映射与执行配置，并保存定义摘要；同 ID 同定义重放，异定义拒绝，不随后来新增资源扩大范围。对已经开始的 run，配置或目标变更应使用新 ID。每个 destination 最多一个 running run；暂停的 run 不占活动名额，恢复时重新检查目标及该名额。completed/cancelled 不可恢复。

每页持久租约覆盖页预算再加 60 秒，活租约不被其他进程接管；过期后新认领推进代次。暂停/取消推进代次并清除租约，迟到页无法提交，也不能暂停后来认领者。每一并发组之间检查控制状态；在途只读请求最多等自己的对象超时结束，命令返回时没有遗留线程。进程强杀后须等待租约过期再续跑。

网络请求始终在事务外；页前后核对真实目标身份，页提交再次核对持久目标与库身份，事务内一次写入观察、计数和游标。页失败整页回滚，游标不前进；提交响应未知时先 status 重读，保留原租约与检查点，禁止假定回滚。目标身份或来源读取不可用会暂停本 run。已提交的 unknown 等结果不在同 run 内重写，可在审阅后创建新范围任务。

报告是历史时点证据，不是远端持续可用证明。读取期间之外的来源变化仍须后续导入/基线审阅再次核对；删除或别名合并不会改写旧观察。库内有空洞时按 ID 高水位推进，不要求连续 ID。

## 导出与历史跳过

export 独占创建新目录，按已提交游标分页流式导出 `report.jsonl`，输出文件 SHA-256。文件头格式为 `crawler.resource-cloud-discovery.v1`，包含固定定义、状态、游标和各等级计数；未完成 run 可导出部分报告。失败只留下该新目录中的 `.part`，重试选择新的目录，不覆盖旧报告。已提交观察只追加，导出期间续跑不会混入快照游标之后的数据。

此报告与双端声明导入清单格式不同，不能直接 apply。先审阅逐端结果、解决冲突/未知、确认范围，再按[历史导入](resource-cloud-import.md)生成并审阅现有声明清单、固定 SHA-256、apply、review/seal/activate；双端已保存必须明确声明，不能由单端 observed 自动推导。现已提供[单端审阅采信](resource-history-adoption.md)：history-preview从报告生成独立清单，操作者审阅并固定最终SHA，再history-apply并明确平台/停写；完整范围可作为基线supplement。missing可明确选择本端normal而不入队，unknown等仍需处理，不自动放行。已有 crawler 历史 skip 保持原语义，发现不会撤销或提升它；普通 process 仍优先跳过。

## 数据库升级与验证

`V2026102911` 仅新增 `resource_discovery_runs`、`resource_discovery_observations`，以及目标状态的三项队列限额列；不改旧迁移、不删旧列、不触碰旧资源正文。观察表只外键关联 run，不关联可删除的资源行。先停写并备份，在克隆库升级/验证后部署新二进制，再按基线流程放行；旧二进制不得继续写同库。队列限额见[持久任务](resource-transfer-tasks.md#持久队列容量)。

离线 `ResourceDiscoveryProbeTest` 验证路径、旧摘要及只读 stat/meta；隔离库 `MySqlResourceDiscoveryTest` 验证租约、暂停/取消、目标/来源变化、回滚/恢复、四类资源及导出，无真实源站或云端请求。真实目标、千万级扫描和生产迁移仍需另行验收。
