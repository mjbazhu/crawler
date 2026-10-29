# 验证与代码审查

选择验证方案、执行测试、审查代码和交付前读取本文。命令在 crawler 模块根目录执行；根据实际改动选择检查，并区分离线回归、外部集成与历史记录。

## 测试与验证要求

代码改动后按下表选择覆盖实际改动的 target。首次配置或修改 CMake 时重新 configure；已配置环境按 target 构建。下面仅以歌曲来源 parser 为例，其他任务替换为受影响的 target 和实际实现文件：

```powershell
cmake --preset msvc-vcpkg
cmake --build --preset msvc-vcpkg-debug --target MiguSongSourceParserTest
clangd --check=src/infrastructure/music/migu/parser/MiguSongSourceParsing.cpp --compile-commands-dir=build/msvc-debug
```

补充要求：

- 修改头文件后，应至少对一个包含该头的 `.cpp` 运行 `clangd --check`。
- 修改 CMake 后必须重新 configure。
- 修改 SQLite schema、DAO、transaction 或并发逻辑后必须增加或更新测试；现有测试未全部接入 CMake 时，要同步接入或在最终回复说明手动验证方式。
- 修改 provider/parser 时应使用离线 fixture 覆盖解析逻辑，不让单元测试依赖实时网络。
- 无法运行验证时，最终回复必须说明原因、未覆盖风险和建议的下一步。

## 测试环境与执行条件

| 类型 | 执行内容 | 所需条件 |
| --- | --- | --- |
| 离线 | fixture、mock provider/repository 或内存中的协议映射 | 不调用真实音乐平台，不依赖 MySQL；仍按实际测试处理线程及临时文件 |
| 本机 I/O | 本地 HTTP、异步运行时、临时文件及事件日志检查 | 本机端口、线程和临时目录可用；不是外部平台集成测试 |
| 独立 MySQL | 建表、迁移、fixture 写入、认领、恢复及查询验证 | 已有授权和可用测试配置，确认有效配置指向独立测试库；不能使用运行音乐库 |
| 真实平台 | 真实请求、采集或资源下载，部分同时写 MySQL 和文件 | 在用户已授权范围内，具备相应测试库、平台访问条件和资源测试所需 pacmtoken；结果脱敏 |

SQL 测试配置从 [MySqlTestSupport.h](../../tests/migu/support/MySqlTestSupport.h) 的 `makeMySqlConfigFromEnv` 读取，可能加载 `.env.test` 与 `.env`。运行前核对生效来源，不输出密钥；`MySqlMetadataOwnershipTest` 另要求数据库名以 `crawler_ownership_test_` 开头。仅有环境文件不代表可以使用其中的运行数据库做集成测试。

`MySqlMetadataOwnershipTest --check-sql-only` 也会连接并初始化测试 schema，再 prepare 运行期 SQL，仍受独立测试库条件约束，不能当成纯文本检查。

## 按任务选择验证

下表是选择依据，不是每类任务一律全跑。先选覆盖实际改动的 target；接口、数据或状态跨类变更时取相关项，缺失覆盖再补有意义的测试。

| 任务或改动 | 已注册 target | 类型及选择条件 |
| --- | --- | --- |
| API/DTO 与 OpenAPI | `MiguCatalogApiSmokeTest`、`MiguApiDocumentTest`；真实路由另选 `MiguHttpRouteSmokeTest` | 前两者离线；路由测试使用本机 `127.0.0.1:19080` 和替身服务，端口需要可用 |
| 平台请求与解析 | `MiguProviderTest`，及 `MiguSongSourceParserTest` / `MiguSingerIndexParserTest` / `MiguAlbumSourceParserTest` / `MiguSongResourceParserTest` 中受影响者 | 离线；按解析能力选，不为 parser 变更运行真实采集 |
| 歌手目录和采集编排 | `MiguSingerIndexIngestionFlowTest`、`MiguSingerAlbumIngestionFlowTest` 中受影响者 | mock 数据源的本机流程；URL/搜索等没有对应 target 的分支需补覆盖，不能声称这些测试覆盖所有 workflow |
| 元数据合并、字段归属及本地封面 | `MetadataOwnershipTest`，加受影响 parser | mock HTTP 与临时文件；SQL/迁移行为另选下一行 |
| MySQL writer、migration、关系或 catalog | `MySqlMetadataOwnershipTest`、`MiguSongRelationIntegrationTest`、`MiguCatalogQueryIntegrationTest` 中受影响者 | 独立 MySQL；写入/升级选 ownership，关系选 relation，事实计数/查询/资源治理选 catalog |
| 资源 workflow 与资源解析 | `SongResourceDownloadWorkflowTest`、`AsyncSongResourceDownloadWorkflowTest`，解析变化加 `MiguSongResourceParserTest` | workflow 使用替身端口，不要求真实下载；同步与异步实现都受影响时都检查 |
| 歌手/专辑封面本地下载 | `MetadataCoverDownloadWorkflowTest`、`MetadataCoverStoreTest`、`MetadataCoverApiTest`；持久化另选 `MySqlMetadataCoverQueueTest` | 前三项分别覆盖文件回执编排、回环 HTTP/图片内容及接口/后台任务；MySQL 测试仅访问 `crawler_ownership_test_*` 独立库。API/共享文件设施变化继续选相应行 |
| 歌手/专辑封面云端存储 | `MetadataCoverCloudWorkflowTest`、`CloudFileStoresTest`、`ProcessRunnerTest`；持久化另选 `MySqlMetadataCoverCloudTest` | 共用流程、百度回读、Google RC multipart/摘要、真实本机进程超时/取消；MySQL 用独立库验证云端代次、快照和别名。真实云端另需有效登录态/RC，不能把替身结果计作真实上传 |
| 流式传输、完整文件提交或异步运行时 | `AsyncHttpStreamTest`、`AsyncFileBlobStoreTest`、`SongResourceDownloadStorageTest`、`AsyncTaskTest` 中受影响者 | 本机 I/O；分别覆盖回环 HTTP、文件存储或异步调度，不能互相替代 |
| 批次事件、认领与恢复 | `CrawlerServiceResourceBatchEventTest`；持久化认领/恢复选 `MiguBackfillClaimIntegrationTest` / `MiguMetadataRecoveryIntegrationTest` | 事件测试使用替身及本地日志；后两者使用独立 MySQL 并写 fixture |
| 配置、构建与运行设置 | 构建 `demo` 或受影响 target；设置/协议变化选 API 行及实际消费者测试 | 构建不等于启动；启动可能初始化数据库，需按任务范围判断是否执行 |
| 故障排查或代码审查 | 选上面与证据所指阶段相符的项 | 只读任务不默认运行会改库或采集的测试；准确报告实际执行范围 |

target 的来源是 [CMakeLists.txt](../../CMakeLists.txt)。`tests/legacy/` 中存在源码不代表当前已注册；新增或改名后先核对 CMake，不能用不存在的测试名交付。

已配置环境中，按 target 构建并精确选择测试，例如：

```powershell
cmake --build --preset msvc-vcpkg-debug --target MiguSongSourceParserTest
ctest --test-dir build/msvc-debug -R '^MiguSongSourceParserTest$' --output-on-failure
```

该例只演示离线 parser 测试的选择方式。不要为一次局部变更不加筛选地运行全部 CTest；当前注册项也包含真实平台和 MySQL 测试。CMake 变化先重新 configure，工具链未就绪时记录缺失条件。

结果至少记录命令、退出结果和验证范围；`SKIP`、环境不足、只构建成功、只读过测试源码都不能写成业务回归通过。

## 仅整理说明文档

- 仅调整根 `AGENTS.md`、文档导航、说明文档的组织结构和引用，且未改变 API 契约、SQL、构建配置或生产代码时，检查迁移内容覆盖、Markdown 链接、UTF-8 无 BOM、格式和 Git diff。
- 此类整理不要求编译整个 C++ 模块或运行依赖外部平台、MySQL 的测试；如果同时修改契约或代码，仍执行对应检查。
- 核对原规范中的禁止事项、状态不变量和操作前提是否保留，不把历史实测写成当前验证结果。

## 资源链路专项回归

- 修改资源下载链路后至少运行：

```powershell
.\.vscode\build-msvc-target-debug.cmd SongResourceDownloadWorkflowTest
.\build\msvc-debug\Debug\SongResourceDownloadWorkflowTest.exe
.\.vscode\build-msvc-target-debug.cmd AsyncSongResourceDownloadWorkflowTest
.\build\msvc-debug\Debug\AsyncSongResourceDownloadWorkflowTest.exe
```

- 修改异步 HTTP 流式下载、blob store 或 application async 基础设施后继续运行：

```powershell
.\.vscode\build-msvc-target-debug.cmd AsyncTaskTest
.\.vscode\build-msvc-target-debug.cmd AsyncHttpStreamTest
.\.vscode\build-msvc-target-debug.cmd AsyncFileBlobStoreTest
```

- 修改 API、DTO 或 API 契约文档后运行：

```powershell
.\.vscode\build-msvc-target-debug.cmd MiguCatalogApiSmokeTest
.\.vscode\build-msvc-target-debug.cmd MiguHttpRouteSmokeTest
.\.vscode\build-msvc-target-debug.cmd MiguApiDocumentTest
```

- 真实资源下载测试只在用户明确提供可用 MySQL 和 pacmtoken 后执行，且输出必须脱敏。推荐命令模式：

```powershell
$env:CRAWLER_OUTBOUND_VERIFY_TLS='false'
$env:CRAWLER_TEST_MIGU_ASYNC_RESOURCE_FORMAT='PQ'
$env:CRAWLER_TEST_MIGU_ASYNC_RESOURCE_LIMIT='30'
$env:CRAWLER_TEST_MIGU_ASYNC_RESOURCE_CONCURRENCY='5'
.\build\msvc-debug\Debug\MiguAsyncSongResourceBatchIngestionTest.exe
```

构建脚本位于本机 `.vscode/`，该目录由 `.gitignore` 忽略。新环境缺少脚本时，可使用 `cmake --build --preset msvc-vcpkg-debug --target <Target>` 构建对应 target，再运行 `build/msvc-debug/Debug/<Target>.exe`。涉及真实 MySQL 和平台的测试仍受上面的显式条件约束。

## 历史结果

原规范中的成功率、平台业务码样本与环境结论移至 [当前交接](current.md) 和 [运行操作](../operations/resource-storage.md)。验证命令存在不代表本次已经执行。

## 代码审查规范

审查或修改前先确认：

- 当前工作目录、已有规范文件、CMake、clang-format、clangd 配置。
- 如果当前目录不是 Git 仓库根目录，应向上查找；若仍不可用，在回复中说明无法获得 Git 状态。
- 用户已有改动不得回滚；无关改动忽略，相关改动先理解再协作。
- 本次改动是否仍然只服务 crawler 模块职责。
- include 方向是否违反分层依赖。
- 是否新增了无必要抽象、重复逻辑、跨层直接依赖、大而全工具类或无法测试的全局状态。

审查重点：

- 设计是否符合 api/application/domain/infrastructure 边界。
- 资源管理是否完整 RAII，是否存在泄漏、悬垂引用、共享所有权滥用。
- SQLite/MySQL/Asio/文件系统错误是否完整处理。
- 并发路径是否存在竞态、死锁、长时间持锁或线程不安全对象共享。
- 测试是否覆盖核心成功路径、失败路径、边界条件和回滚语义。

审查输出：

- 优先列问题和风险，带文件/行号。
- 没有问题时明确说明剩余风险或未验证项。
- 风格争议以 `.clang-format` 和 [开发规范](development.md) 为准；没有规则时跟随局部既有风格。
