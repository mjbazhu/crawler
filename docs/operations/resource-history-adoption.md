# 历史发现报告的单端采信

本流程连接[历史发现](resource-history-discovery.md)与历史跳过策略：操作者先审阅指定平台的对象证据，再以独立清单、完整 SHA-256 和明确平台声明导入。发现本身仍是只读；采信只改变选定端的当前内容/目标版本策略，不下载、上传、创建传输任务，也不替另一端或本地作保存声明。

## 准备与执行

先停止并排空新旧 writer、完成备份和克隆库验证，将三个目标退回 maintenance。`--writers-stopped` 是操作者声明，不是工具替你停止外部进程；目标/来源/占用仍会在事务内重核。正式库名必须与所选私有环境中的数据库一致。

```powershell
$tool = './build/msvc-debug/Debug/crawler_cloud_receipts.exe'
$taskDatabase = '<明确库名>'
& $tool history-preview --database $taskDatabase --report '<发现报告report.jsonl>' --report-sha256 '<报告完整SHA256>' --run-id 'baidu-reviewed-001' --output-dir '<尚不存在的清单目录>'

$manifest = '<清单目录>/manifest.jsonl'
# 审阅后若编辑action，以最终完整文件重新计算摘要。
$reviewedSha = (Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToLowerInvariant()
& $tool history-apply --database $taskDatabase --manifest $manifest --sha256 $reviewedSha --attest-destination baidu --writers-stopped
& $tool history-status --database $taskDatabase --manifest $manifest --sha256 $reviewedSha
```

preview 独占创建新目录，输出 `manifest.jsonl` 及其 SHA-256；失败只保留新目录中的 `.part`，不覆盖其他报告或清单。输入报告格式必须为 `crawler.resource-cloud-discovery.v1`，输出为独立的 `crawler.resource-history-adoption.v1`，不能把原报告直接 apply。原报告 SHA、发现 run/定义摘要、库、owner、目标修订/指纹、报告已提交高水位与观察数全部固定在清单头中。

每行保留原 observation，只允许编辑 action。不要改资源身份、来源、代次、路径、观察值或清单头来绕过冲突。修改后使用最终文件摘要；同 run ID 不能换另一份清单，重新审阅须新 run ID。两个平台使用不同 run ID。

## 决策与证据等级

| action | 允许的证据 | 效果 |
| --- | --- | --- |
| adopt | observed / hash_matched | 写入本端 skip，绑定当前内容代次和目标修订；保留人工采信性质 |
| allow_transfer | 明确 missing | 显式设为本端 normal，供后来普通 process 正常判断；不直接排队或触发内容请求 |
| review | 任意待审证据 | 不改变策略，记录 conflict，完成遍历仍不能封存为无冲突补充 |

preview 默认只给 observed/hash_matched 建议 adopt；missing、unknown、conflict、unresolved、source_changed 保持 review。明确缺失也必须由操作者审阅后把该行改为 allow_transfer，不能由工具自动把网络/认证未知当成缺失。错误的 action/证据组合同样记录 conflict。

库内本地状态不要求为 2；新平台已有对象即使本地从未下载也可采信。资源必须仍存在，未受限，且内容代次、来源身份和旧值摘要与报告一致。选定端有任务/producer占用、未决意图，或版本已经前进，均拒绝该行。导入只处理本端，不解除另一平台/local skip，也不解除基线的全局 excluded；后者仍走明确的 refresh_content 流程。

元数据 SHA-256/MD5/SHA-1 的比较结果只保存在证据中，新的采信回执是 operator_attested，不填造 uploaded_at/verified_at，不推进 confirmed_*，不把 observed 或旧 MD5 升级为 SHA-256 验证。已有成功确认、路径、摘要和时间保留。同内容代次/同目标版本的强确认与 missing 矛盾时保持 conflict，须先 repair/人工核对；不会为了放行悄悄抹掉确认。旧目标版本的成功事实可以保留，同时为新目标建立明确 normal 决策。

## 事务、重放与控制

apply 先复制并核对完整清单 SHA，再流式预检全部行，确认格式/顺序/数量没有截断后才连接写流程。每页最多 200 条；页内观察必须与数据库持久发现记录精确一致，不能删除不利观察或拼造正证据。资源、目标、维护闸门和本端占用重新核对，策略、逐行审计与检查点同事务提交，失败整页回滚。

同清单同页重跑不重复审计，也不把游标倒退。提交响应未知时先 history-status 核对，不假定回滚；以同清单续跑。结果中的 accepted 同时包含 adopt 和 allow_transfer，conflicts 必须为零才能作为完整基线补充；completed 只表示本范围遍历完成。

```powershell
& $tool history-control --database $taskDatabase --manifest $manifest --sha256 $reviewedSha --action pause --expected-generation 0
& $tool history-control --database $taskDatabase --manifest $manifest --sha256 $reviewedSha --action resume --expected-generation 1
& $tool history-control --database $taskDatabase --manifest $manifest --sha256 $reviewedSha --action cancel --expected-generation 2
```

每次控制以 status 返回的 control_generation 为准；示例数字不是固定值。暂停/取消不撤销已提交策略，不推进资源代次。控制状态 active/paused/cancelled 与每页自动 paused/completed 的进度状态分开。pause 幂等，resume 只接受显式 paused，completed/cancelled 不可恢复；取消后要用新 run 重新审阅剩余范围。

每次 apply 在启动时固定 execution_generation，旧进程不能在 pause→resume 后悄悄采用新代次继续执行。已持锁页可能先完成，控制命令随后原子生效；后续旧页会被拒绝。重新启动 apply 才能读取新代次并从持久检查点续跑。控制变更与治理审计同事务，审计写入失败不留下半份状态。

原双端声明清单仍用已有 preview/apply/status。显式控制命令为：

```powershell
& $tool import-control --database $taskDatabase --manifest '<原双端清单>' --sha256 '<该清单SHA256>' --action pause --expected-generation 0
```

其 resume/cancel 规则相同，双端控制状态和代次原子更新。旧客户端默认代次 0 在未进行显式控制时保持兼容；发生控制后旧页失败关闭，必须由支持控制代次的 CLI 新执行续跑。

## 基线与数据结构

完整补充清单可作为选定 owner/cloud 的基线 supplement；保留原四类双端基线作为本地和原历史范围锚点。review/seal/activate 会再次核对完整高水位、逐行审计、来源及当前端策略，不根据一次 apply 成功直接激活目标。部分发现报告可以导入，但部分范围不能替代完整基线覆盖。具体基线参数见[维护导入](resource-cloud-import.md)。

复用 resource_cloud_backfills：主键为 `<run_id>:<provider>`，strategy=import_discovery_skip；原双端声明仍为 import_attested_skip。resource_cloud_migration_audit 保存旧源事实和 imported/allow_transfer/conflict；receipt.evidence.history_adoption 保存报告/清单 SHA、发现定义及观察。V2026102912 增加 backfill 的 control_state/control_generation，旧 state 进度语义保留。已执行迁移不得重写。

## 验证

`ResourceHistoryManifestTest` 覆盖多页转换、明确决策、旧摘要、截断/路径预检和文件快照；`MySqlResourceHistoryAdoptionTest` 使用空的 crawler_ownership_test_history_adoption_*，覆盖真实发现记录、单端策略、来源/目标围栏、控制代次、SQL故障回滚及零内容副作用；`MySqlLegacyResourceImportTest` 补双端原子控制。基线 supplement 集成另测，不将隔离库或替身结果计为真实网盘及生产迁移验收。
