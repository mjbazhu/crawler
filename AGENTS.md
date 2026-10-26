# Crawler 模块开发规范

本文件约束 `backend-cpp/modules/crawler` 后续所有代码、文档、脚本和数据结构改动。进入本模块工作的 agent 必须先阅读本文件，再阅读 `CmakeLists.txt`、`CMakePresets.json`、`.clang-format`、`.clangd` 和本次任务相关源码。

## 模块定位

`modules/crawler` 只负责音乐数据采集、音乐元数据解析、音乐参数信息整理，以及把这些数据可靠地写入本地存储。

允许的职责：

- 从受支持的音乐数据源获取歌曲、专辑、歌手、榜单或搜索结果等音乐数据。
- 解析外部响应为本模块领域模型，例如 `Song`、`Album`、`Singer` 和明确命名的参数对象。
- 保存音乐元数据到 SQLite，保存必要的二进制资源到本地文件系统。
- 提供最小 HTTP/CLI 入口，用于触发爬取、查询任务进度和返回执行结果。
- 管理爬虫任务的并发、失败回滚、重试、超时和进度快照。

禁止越界：

- 不实现音乐播放器、流媒体播放、转码、推荐系统、歌单社交、用户账号、会员、支付、评论、歌词编辑、前端 UI、管理后台或通用下载器。
- 不实现代理池、代理检测、任意 URL 转发、通用爬虫平台、反爬绕过平台或浏览器自动化采集系统。
- 不把外部音乐平台的账号、Cookie、Token、签名密钥或内部调试数据硬编码进代码或文档。
- 不把 `song.db`、`song copy.db`、`*.db-wal`、`*.db-shm` 这类本地运行数据当作业务代码修改对象；需要 schema 变更时只改 schema 管理代码和测试数据。
- 任何新增能力如果不能直接服务于“获取音乐数据并可靠存储”这个目标，应拒绝或先拆到其他模块。

## 架构原则

本模块默认采用 KISS、YAGNI、SOLID、DRY：

- KISS：优先直接、清晰、局部化的设计；不为少量逻辑引入大框架。
- YAGNI：不预建空层、空接口、空工厂；只有真实需求出现时才新增抽象。
- SRP：控制器只做协议适配，用例只编排爬取流程，DAO 只做持久化，provider 只适配外部音乐源。
- OCP：新增音乐源、存储实现或 HTTP 客户端时，通过新增实现扩展，不修改高层用例策略。
- LSP/ISP：接口必须小而稳定；不要把搜索、下载、解析、存储、进度查询塞进一个大接口。
- DIP：高层业务依赖抽象，基础设施实现抽象；具体类装配只出现在组合根。
- DRY：重复的 URL 参数构造、错误映射、事务回滚、响应序列化、日志脱敏必须提取到明确职责组件。

### SRP/ISP 防回归约束

- `IMusicProvider`、`MetadataParser`、`AlbumSourceRepository` 只作为兼容聚合接口保留；新增 use case、workflow 或 application service 不得直接依赖这些聚合接口，必须依赖 `SongSearchProvider`、`AlbumSearchProvider`、`RawPageFetcher`、`ParseResultParser`、`AlbumSourceWriter`、`AlbumSourceSyncQueue` 等窄 port。旧聚合构造函数只能服务 legacy 测试或兼容装配，新增生产装配必须走 `CrawlerServiceDependencies`。
- `CrawlerService` 只能作为 application 门面和运行设置协调者；不得继续承载分页采集、解析、持久化、HTTP/数据库适配或批量任务细节。新增依赖应通过明确的依赖结构分组注入，避免继续扩展超长构造函数；已标记 deprecated 的聚合构造函数不得继续增加参数或业务逻辑。
- application 层不得直接使用 `std::cout`、`std::cerr`、具体日志库或基础设施 logger；诊断输出必须通过 application 定义的诊断 port、结构化结果或由调用层记录。
- application 运行设置模型只表达可热更新业务策略，例如批量并发、分页并发、重试次数和任务超时；API 响应中为了兼容保留的 MySQL、SQLite、Drogon、Asio 或线程池只读字段必须由 API/infrastructure 快照提供，不得回流到 application model。
- 单个 workflow、parser、client、repository 实现文件超过约 300 行，或同时承载两个以上独立流程时，必须优先拆分；如暂不拆分，代码审查说明必须写清楚保留理由和后续拆分点。
- workflow 只编排一个业务流程族；URL 采集、关键词搜索、歌手目录采集、专辑详情补齐应由独立 workflow 承载，共享分页执行、解析调度和持久化 helper，禁止把所有流程重新堆回一个 `MusicCollectionWorkflow`。`MusicCollectionWorkflow` 只允许作为兼容门面委托既有 workflow，新增采集流程不得继续给它添加成员状态、构造参数或分页细节。
- 平台 parser 应按解析能力拆分；通用解析结果、歌曲来源、歌手来源、专辑来源和分页检查是不同职责。兼容门面可以委托多个小 parser，但不得继续新增大段 JSON 字段提取逻辑。
- HTTP client 中 URL 解析、请求构造、响应解析、chunk 解码、Asio 会话生命周期和 I/O 线程管理应分开；禁止把新的协议解析或连接状态逻辑继续堆到单个 client `.cpp`、`AsioHttpSession.h` 或 `AsioHttpCoroutineRequest.h`。新增 proxy、TLS、响应大小限制、状态码判断等协议 helper 必须放入明确的 detail/helper 组件，并尽量让 callback session 和 coroutine request 复用同一实现。
- MySQL 专辑来源保存和同步队列状态管理是两个 adapter 职责；新增能力必须分别落到 writer 或 sync queue，兼容 repository 只做委托。`AlbumSourceSyncQueue` 当前同时覆盖详情同步与歌曲同步状态，如果歌曲补齐演进为独立用例，应继续拆成 `AlbumDetailSyncQueue` 和 `AlbumSongSyncQueue` 这类更窄 port。

当前已知需要持续关注的残余风险：`CrawlerService.h` 仍暴露 deprecated 聚合构造函数；`MusicCollectionWorkflow` 仍是兼容门面；`AlbumSourceSyncQueue` 同时覆盖详情与歌曲同步状态；`AsioHttpSession.h` 和 `AsioHttpCoroutineRequest.h` 仍是接近 300 行的模板状态机。`MiguParser`、`AsioHttpClient.cpp`、`MySqlAlbumSourceRepository` 已完成第一轮职责拆分，后续只按新增职责继续下沉到细分 parser、HTTP helper 或 MySQL adapter。

## Git 提交规范

- 提交信息统一使用 Conventional Commits 形式：`type(scope): 简体中文摘要`。
- `type` 和 `scope` 使用英文小写，常用类型包括 `feat`、`fix`、`test`、`docs`、`refactor`、`chore`；冒号后的摘要必须使用简体中文。
- 提交正文如需说明背景、风险或验证方式，也使用简体中文；代码标识符、命令、路径和错误码保持原文。
- 同一提交只包含一个明确职责范围内的改动；配置、领域/application、API、基础设施、SQL/migration、测试和文档应按实际边界分批提交。
- 提交前必须分别检查 `git diff --cached --name-only`、`git diff --name-only` 和 `git ls-files --others --exclude-standard`，确认暂存区只包含用户本次要求的范围。
- 在存在用户历史改动或原暂存区内容时，禁止使用 `git add -A`、全量 `git add .` 或大范围 `git add <dir>` 来构建提交；必须按文件或 hunk 精确暂存。
- 不要为了构建当前提交而把用户已有暂存区挪进 stash，除非用户明确要求；如确实需要临时保护，必须先列出暂存文件清单，恢复后再次核对数量和文件名。
- 发生误提交时优先用 `git reset --mixed HEAD~1` 撤销提交并保留工作区内容；撤销后不得继续提交，先向用户说明当前 `HEAD`、暂存区、未暂存和未跟踪文件状态。
- 分批提交前应先向用户展示准备提交的精确文件清单；用户确认后再执行提交。提交后用 `git log -1 --oneline` 和 `git status --short` 复核。
- 默认交付流程为：从最新 `main` 创建 `codex/<功能名称>` 分支 → 按功能和依赖分批提交并验证 → 推送功能分支 → 创建目标为 `main` 的 PR → 检查通过后标准合并 → 同步本地 `main`。
- 日常改动在功能分支提交和推送，`main` 通过 PR 接收改动。SQL 删列、C++ 参数绑定和依赖查询必须保持在同一完整提交中，不能为了分批而拆断依赖。
- PR 标题和说明使用简体中文，说明具体问题、最终行为、主要改动和验证结果；区分本地验证与 GitHub CI，原有失败和未执行的检查必须如实记录。
- 标准合并使用 GitHub 的 `Create a merge commit`，通过 CLI 时使用 `gh pr merge --merge`，保留原有历史和分批提交记录；除非用户另有要求，不使用 squash、rebase 或强制推送改写历史。
- 合并前确认 PR 的目标分支、最新提交、验证结果和冲突状态；合并后同步本地 `main`，核对远端提交及工作区状态，并向用户报告 PR 链接和合并结果。

## 目标分层

当前代码必须按 `api`、`application`、`domain`、`infrastructure` 四层组织。公共头文件根目录固定为 `include/crawler/**`，实现根目录固定为 `src/**`。旧的 `service`、`core`、`db`、`http`、`http_server`、`provider`、`parser`、`storage`、`utils` 顶层目录已迁移完毕，后续不得恢复或新增这些旧目录。

```text
domain <- application <- api
domain <- application <- infrastructure
src/main.cpp / infrastructure::app::ServerApplication 负责装配具体实现
```

目录边界：

- `modules/crawler` 本身就是音乐数据采集、整理和存储的业务模块。模块内部先按 `api`、`application`、`domain`、`infrastructure` 分层；当某一层只有一个主要业务能力时，可以直接按职责目录组织，不强制再套 `ingestion` 这类重复业务目录。
- 只有当本模块内部出现多个清晰、可独立演进的高层业务能力时，才在对应层下面增加业务切片目录；业务切片目录应来自领域语言，而不是为了目录对称而创建。
- `api/**`：音乐数据采集入站 HTTP API、路由、DTO、协议模型和响应映射。当前可直接使用 `controllers`、`dto`、`http`、`routing` 等职责目录。
- `application/**`：音乐数据采集与入库用例、任务模型、结果模型和 port。当前使用 `use_cases`、`model`、`result`、`ports` 等职责目录；`ports` 下必须继续按适配方向细分，例如 `provider`、`persistence`、`storage`、`parsing`、`execution`，禁止把所有 port 平铺到一个目录。
- `domain/music/**`：音乐领域模型、值对象和纯规则，例如歌曲、专辑、歌手、音乐元数据解析结果。
- `infrastructure/app/**`：组合根，只负责启动配置、依赖装配和进程生命周期。
- `infrastructure/http/server/**`：Drogon 入站 HTTP server adapter，只处理框架适配。
- `infrastructure/http/client/**`：出站 HTTP client 抽象与 Asio 实现；必须有超时、错误码、响应大小上限、代理参数和线程安全说明。
- `infrastructure/music/migu/**`：Migu 平台 provider、parser、参数构造等平台适配。
- `infrastructure/music/persistence/sqlite/**`：音乐元数据 SQLite repository/DAO、schema、statement、transaction。
- `infrastructure/storage/filesystem/**`：本地文件系统存储 adapter。
- `infrastructure/json/**`、`infrastructure/observability/logging/**`、`infrastructure/platform/**`、`infrastructure/execution/threading/**`：明确跨业务基础设施能力。禁止恢复 `shared`、`utils`、`common` 这类杂物目录；能归业务切片或具体 adapter 的代码必须归到明确职责目录。
- `composition root`：`include/crawler/infrastructure/app/**`、`src/infrastructure/app/**`、`src/main.cpp`，只做启动和依赖装配。

## include 与 namespace 规则

- 项目内公共头文件 include 必须从模块名开始：`#include "crawler/api/..."`、`#include "crawler/application/..."`、`#include "crawler/domain/..."`、`#include "crawler/infrastructure/..."`。
- 禁止在生产代码中使用迁移前路径：`#include "api/..."`、`#include "application/..."`、`#include "domain/..."`、`#include "infrastructure/..."`，也不要用同目录相对 include 绕过 `crawler/` 前缀。
- 生产代码命名空间根必须与上层模块保持一致：`orchestrator::modules::crawler`。
- 允许的层级 namespace 示例：`orchestrator::modules::crawler::api::controllers`、`orchestrator::modules::crawler::api::routing`、`orchestrator::modules::crawler::application::use_cases`、`orchestrator::modules::crawler::application::ports`、`orchestrator::modules::crawler::domain`、`orchestrator::modules::crawler::infrastructure::music::migu::provider`。
- 禁止新增或恢复独立顶层 namespace：`api`、`application`、`domain`、`http`、`http_server`、`parser`、`provider`、`storage`、`utils`、`app`。
- 禁止在全局命名空间为领域类型建立兼容别名，例如 `using Song = ...`、`namespace result = ...`；测试和 `.cpp` 可在局部作用域使用清晰的类型别名，但不能伪装成旧顶层 namespace。
- 第三方 C/C++ 类型必须保持在其原始命名空间或全局命名空间内；例如 `sqlite3`、`sqlite3_stmt` 不得在 `orchestrator::modules::crawler` 下重新前置声明。

## `api` 边界

位置：`include/crawler/api/**`、`src/api/**`

职责：

- HTTP 请求入口、路由处理、DTO、请求参数校验、HTTP 状态码映射、响应序列化。
- 把 HTTP/JSON/Query 参数转换为 application 输入模型。
- 把 application 返回结果转换为 HTTP 响应。

允许依赖：

- `application` 的用例接口或服务。
- `api/dto`、`api/http` 这类协议模型。
- HTTP server 抽象或现有轻量 HTTP 类型。

禁止：

- 不直接 include 或调用 `infrastructure/music/persistence/**`、`infrastructure/storage/filesystem/**`、`infrastructure/music/migu/provider/**`、`infrastructure/music/migu/parser/**`、`infrastructure/http/client/AsioHttpClient.h`、SQLite、MySQL、Boost.Asio、Drogon。
- API 层可以在 DTO/序列化边界使用 JSON 库，但不得把 JSON 类型泄漏到 application/domain 接口。
- 不在 controller 中写爬取流程、事务、文件保存、外部 URL 拼接、平台签名、重试或解析逻辑。
- 不把 HTTP 类型传入 `domain`。
- 不返回异常栈、数据库路径、本地文件路径、外部平台 token、Cookie 或完整请求头。

## `application` 边界

位置：`include/crawler/application/**`、`src/application/**`

职责：

- 编排用例：搜索并保存、爬取并保存、批量任务、进度快照、失败回滚。
- 定义高层所需 port，例如 `MusicProvider`、`SongRepository`、`AlbumRepository`、`SingerRepository`、`FileStore`、`TaskExecutor`、`Clock`。port 必须按业务和适配方向归类，不能平铺堆放。
- 维护事务边界、幂等策略、重试策略、错误映射和任务状态。
- 只表达“本模块要完成什么”，不关心“外部技术怎么做”。

允许依赖：

- `domain` 中的模型、值对象和纯规则。
- application 自己定义的 port。
- C++ 标准库。

禁止：

- 不 include `api/**`、`infrastructure/http/server/**`、`infrastructure/music/persistence/**`、`infrastructure/http/client/AsioHttpClient.h`、`infrastructure/music/migu/provider/**`、SQLite、MySQL、Boost.Asio、nlohmann_json 具体解析细节。
- 不直接读写文件系统，不直接执行 SQL，不直接拼外部平台 API URL。
- 不创建具体基础设施对象；构造函数接收抽象或已存在的兼容接口。
- 不依赖具体 DAO、`FileStorage`、`ThreadPool`；新增用例先引入小型 port，再由 infrastructure 实现。

## `domain` 边界

位置：`include/crawler/domain/music/**`，必要时才新增 `src/domain/music/**`

职责：

- 表达与框架无关的音乐领域概念：歌曲、专辑、歌手、资源标识、音乐参数、抓取状态、错误码、值对象和不变量。
- 保存纯规则：字段合法性、去重 key、文件名规范化策略、数据归一化规则等。
- 保持可单元测试、无 I/O、无线程、无数据库、无网络。

允许依赖：

- C++ 标准库。
- domain 同层文件。

禁止：

- 不依赖 `api`、`application`、`infrastructure`。
- 不 include SQLite、MySQL、HTTP server、JSON 库、文件系统、线程池、日志实现。
- 不知道数据来自哪个音乐平台，不知道配置从哪里来，不知道数据如何保存。
- 平台专属签名、URL 参数加密、HTTP header、分页协议属于 `infrastructure/music/migu/provider/**` 或其他具体平台 provider，除非抽象成稳定的音乐领域参数。

## `infrastructure` 边界

位置：`include/crawler/infrastructure/**`、`src/infrastructure/**`；跨业务基础能力也必须放入明确职责目录，例如 `http/client`、`json`、`observability/logging`、`platform`、`execution/threading`，不得使用 `shared`、`utils`、`common` 兜底。

职责：

- 适配外部细节：SQLite、MySQL、Asio HTTP client、HTTP server、文件系统、JSON 解析、具体音乐平台 provider、日志、线程池。
- 实现 application 定义的 port 或当前兼容接口。
- 把外部错误转换为 application/domain 可理解的结果。

允许依赖：

- application/domain 暴露的抽象和模型。
- 第三方库和操作系统 API。

禁止：

- 不决定业务用例流程，不绕过 application 直接从 controller 写数据库或文件。
- 不把 SQLite 连接、`sqlite3_stmt*`、MySQL 连接、Boost.MySQL 行类型、HTTP 框架类型泄漏到 application/domain 接口。
- 不在多个 adapter 中复制同一套重试、去重、回滚或错误映射规则。

基础设施细分要求：

- `infrastructure/music/persistence/sqlite`：只管理音乐元数据 SQLite 连接、statement、transaction、schema、DAO/Repository 实现；SQL 不散落到 application/api。
- `infrastructure/http/client`：只提供跨业务出站 HTTP client 抽象与 Asio 实现；必须有超时、错误码、响应大小上限、代理参数和线程安全说明。
- `infrastructure/music/persistence/mysql`：只提供 MySQL 连接池、基础 CRUD 仓储辅助、事务和后续 MySQL repository 实现；不得把 MySQL 类型泄漏到 application/domain。
- `infrastructure/http/server`：只提供入站 HTTP 服务器适配；Drogon 类型不得泄漏到 `api/application/domain`。
- `infrastructure/music/migu/provider`：只封装 Migu API、签名、分页、请求参数和响应调用；不保存文件或写数据库。
- `infrastructure/music/migu/parser`：只把 Migu 响应解析为领域模型或解析结果；不发网络请求，不写数据库。
- `infrastructure/storage/filesystem`：只处理文件路径、原子写入、删除、存在性检查和回滚辅助；不决定业务流程。
- `infrastructure/json`、`infrastructure/observability/logging`、`infrastructure/platform`、`infrastructure/execution/threading`：只放明确跨业务基础设施能力。禁止恢复 `shared`、`utils`、`common` 杂物目录；能归具体 adapter 或业务切片的代码必须归到更明确的位置。

## 组合根

位置：`include/crawler/infrastructure/app/**`、`src/infrastructure/app/**`、`src/main.cpp`

职责：

- 读取启动配置，创建具体基础设施对象，装配 controller、use case、provider、repository、storage、thread pool。
- 管理进程级生命周期，例如 HTTP server 启停、数据库连接池和 schema 初始化。

禁止：

- 不写爬取业务规则、SQL 细节、HTTP 响应业务语义或平台解析逻辑。
- `ServerApplication::Impl` 可以持有具体类，但不得成为业务代码堆放处。

## 依赖方向硬性检查

- `domain/music/**` 不得 include `api/**`、`application/**`、`infrastructure/**`。
- `application/**` 不得 include `api/**`、`infrastructure/**`、具体 SQLite/MySQL/Boost.Asio/nlohmann_json 头。
- `api/**` 不得 include `infrastructure/music/persistence/**`、`infrastructure/music/migu/provider/**`、`infrastructure/storage/filesystem/**`、`infrastructure/http/client/**`、SQLite、MySQL、Boost.Asio、Drogon。API 层 DTO/序列化代码可以使用 JSON 库，但不得泄漏到 application/domain。
- `infrastructure/**` 可以依赖 application/domain 抽象，但不能让低层类型反向出现在高层接口。
- 具体实现的 `new`、`make_shared`、`make_unique` 优先出现在组合根或明确 factory。
- 新增接口前先问：是否已有真实替换点、测试替身或跨实现需求。没有就不要抽象。
- 允许 `include/crawler/api/controllers/*`、`include/crawler/api/dto/*`、`include/crawler/api/http/*`、`include/crawler/api/routing/*`、`include/crawler/application/use_cases/*`、`include/crawler/application/model/*`、`include/crawler/application/result/*`、`include/crawler/application/ports/*` 这类层内职责目录；禁止新增 `include/crawler/domain/model/*`、`include/crawler/infrastructure/provider/*`、`include/crawler/infrastructure/parser/*`、`include/crawler/infrastructure/shared/*`、`include/crawler/infrastructure/utils/*`、`include/crawler/infrastructure/common/*` 这类模糊或反向聚合目录。

## 文件放置规则

- 公共头文件放 `include/crawler/<layer>/<responsibility-or-business-slice>/...`，实现放 `src/<layer>/<responsibility-or-business-slice>/...`，路径必须与职责一致。
- 生产代码不得放入 `tests/`、`md/`、`others/`、`build/`、`data/`、`.vscode/` 或项目根目录。
- `others/` 只视为历史参考，不作为新代码落点；需要转正时必须按层迁入 `include/` 和 `src/`。
- `md/` 只放文档，不放可运行逻辑。
- `tests/` 只放测试、mock、fixture，不放生产实现。
- `build/`、`data/`、`*.db`、`*.db-wal`、`*.db-shm` 是本地产物或运行数据，不作为手工编辑对象。
- 新增 `.cpp` 后必须确认 CMake target 能编译。当前 CMake 使用 `file(GLOB_RECURSE src/*.cpp)`，但新增 target、测试或目录规则时仍要同步检查 `CmakeLists.txt`。

## C++20 编码规范

- 语言标准固定为 C++20，保持 `CMAKE_CXX_STANDARD 20` 和 `CMAKE_CXX_STANDARD_REQUIRED ON`。调整 CMake 时应补充或保持 `CMAKE_CXX_EXTENSIONS OFF` / target `CXX_EXTENSIONS OFF`，禁止依赖非标准扩展。
- 所有源码、文档和配置使用 UTF-8 无 BOM。
- 格式化以本模块 `.clang-format` 为准：Google 基础风格、4 空格缩进、Tab 禁用、函数定义大括号换行、`PointerAlignment: Right`、`ColumnLimit: 0`。
- 头文件沿用当前项目的 `#pragma once`，并且必须自包含；头文件中直接使用的符号必须在该头文件中直接 include 对应头文件，不能依赖其他 `.cpp`、`.hpp` 或传递 include。
- 每个 `.cpp` 必须优先 include 自己对应的公开头文件或私有头文件，确保头文件改动后能通过该实现文件及时暴露编译问题；新增实现文件时不得只 include 依赖头而漏掉自身声明头。
- include 顺序保持稳定：相关头、C/C++ 标准库、第三方库、项目头；不要为了省事引入“大而全”公共头。
- 头文件禁止 `using namespace`；源文件也应尽量局部限定。
- 所有新增或重构后的类、结构体、枚举、成员属性、构造函数、析构函数、方法和重要自由函数都必须添加简明中文注释，说明含义、用途或边界；注释只写必要说明，不使用装饰性分隔线、emoji、特殊符号或无意义标记。
- 结构体属性和类成员属性的注释优先写在成员声明同一行的后面，例如 `int total = 0; // 数据总数`；只有注释过长、涉及边界条件或需要多句说明时，才写在成员声明上方。
- 优先值语义、`const`、局部对象和 RAII。
- 禁止裸 `new/delete`、`malloc/free` 管理资源；SQLite 连接、MySQL 连接、statement、事务、文件句柄、锁都必须由 RAII 对象托管。
- 默认使用 `std::unique_ptr` 表达独占所有权；只有确实共享生命周期时才使用 `std::shared_ptr`。
- 函数参数只在表达所有权语义时使用智能指针；普通输入使用值、`std::string_view`、`const T&`，非拥有可空对象用 `T*`。
- 函数应只做一个逻辑操作，复杂流程拆成有名字的私有函数或 use case 步骤。
- 不新增全局可变状态、隐式单例、复杂模板、宏技巧、协程或 C++23 特性，除非任务明确要求且有工程收益。
- 注释解释“为什么”和边界约束，不复述代码“做了什么”。

## 错误处理规范

- 可预期失败使用显式结果类型表达，例如 `domain::result::Result<T>`、application 层已有的 `ServiceResult<T>` 或后续统一后的项目 Result；不要新增第三套结果模型。
- 跨层错误必须保留语义：网络失败、解析失败、数据库失败、文件失败、参数错误、外部平台限流应能区分。
- 异常仅用于启动失败、不可恢复不变量破坏或无法在当前层处理的错误；禁止把异常当普通业务分支。
- 不吞错，不只返回 `false` 而丢失原因；旧接口暂时返回 `bool` 时，新增内部实现也要保留可记录的错误信息。
- API 层负责把 application 错误映射为 HTTP code；domain/application 不直接关心 HTTP code。

## SQLite 与数据存储规范

- SQLite 连接、prepared statement、transaction 必须由 RAII 类型管理。
- 任何 `sqlite3_*` 返回码都必须检查；失败路径必须包含足够定位的信息，但不得泄露敏感数据。
- 不允许跨线程共享未说明保护策略的 SQLite connection 或 statement。
- 连接池负责连接生命周期和线程隔离；DAO 不持有裸连接长期逃逸。
- 写操作必须考虑事务边界：文件写入和数据库写入组合时，由 application 定义失败回滚策略，`infrastructure/music/persistence` 和 `infrastructure/storage/filesystem` 只执行各自职责。
- schema 变更集中在 `SchemaManager` 或明确迁移组件；禁止在 api/application/provider 中散落建表 SQL。
- DAO/Repository 只负责表级持久化，不发 HTTP 请求，不解析外部平台响应，不操作本地音频文件。
- 测试数据库必须使用临时路径或 fixture，不直接破坏根目录 `song.db`。

## HTTP 与爬虫规范

- 所有外部请求必须设置连接超时、整体超时、响应大小上限和明确 User-Agent 策略。
- 重试必须有上限，按错误类型区分；不对参数错误和不可恢复解析错误盲目重试。
- 批量爬取必须有并发上限和取消/停止策略；禁止无限创建线程。
- provider 只适配具体音乐平台；新增平台时新增 provider 实现，不把平台判断塞进 application。
- URL 参数、签名、分页 token、平台专属 header 属于 provider；解析策略属于 parser；保存策略属于 application，具体文件/数据库写入属于 infrastructure adapter。
- 不采集与音乐数据存储无关的个人信息；日志不得记录 Cookie、Authorization、完整签名参数或敏感 query。
- 不引入绕过认证、破解付费内容、规避访问控制或破坏外部服务的逻辑。

## 架构演进决策

当前模块处于 `Drogon + Asio + SQLite` 过渡状态，并已开始补充 MySQL 基础设施。后续涉及 MySQL、Redis、协程或与其他模块统一时，按以下方向演进：

- 入站 HTTP 服务使用 Drogon，与 `echo`、`proxy` 模块保持一致；Drogon 类型只允许停留在 `infrastructure/http/server` 和组合根，不得进入 `api`、`application` 或 `domain`。
- 外部音乐平台抓取当前使用 `infrastructure/http/client/AsioHttpClient`，通过 Asio 协程和内部 I/O 线程池执行出站网络 I/O；application 新增流程只能依赖 `SongSearchProvider`、`AlbumSearchProvider`、`SingerCatalogProvider`、`AlbumDetailProvider`、`RawPageFetcher` 等窄 provider port 或 application 定义的 HTTP port，不允许在 application 中混用 Asio、Drogon 等 HTTP 细节。
- MySQL 只作为持久化 adapter，不改变 application 用例；先复用或定义 repository port，再提供 MySQL repository 实现，当前 SQLite DAO 只能作为过渡实现。
- Redis 只作为爬取任务状态、幂等键、重试队列、短期结果缓存或分布式锁 adapter；不得把业务规则写进 Redis key 拼接逻辑。
- 异步获取优先模型是：API 接收任务 -> application 写入任务状态 -> provider 异步抓取 -> parser 解析 -> repository 持久化 -> Redis/任务状态更新。不要让 controller 直接启动网络抓取和数据库写入细节。
- 当前 Drogon handler 必须通过 `DrogonHttpServer` adapter 转换为项目内 `api` 层的 `HttpRequest`/`HttpResponse`，并把可能阻塞的 controller 调用投递到受限 worker pool；后续高并发 I/O 目标方案优先 Asio/Drogon coroutine/async client，CPU 密集解析、批量持久化或兼容阻塞库时才使用独立线程池。

## 并发与线程安全规范

- 每个可跨线程共享的类必须说明线程安全语义：线程安全、线程不安全、或需外部同步。
- 共享状态必须最小化；优先不可变数据、局部对象和任务内数据。
- 使用 `std::lock_guard`、`std::unique_lock`、`std::scoped_lock` 等 RAII 锁；禁止手写裸 `lock()`/`unlock()` 成对逻辑。
- 持有锁时不得调用未知外部代码、HTTP 请求、回调或可能阻塞的长任务。
- 多把锁必须有固定顺序；无法固定时使用 `std::scoped_lock` 一次性获取。
- `ThreadPool` 只负责执行任务，不承载业务状态。
- Asio HTTP client 的共享状态必须由 strand、mutex 或任务所有权隔离；单个连接或连接上下文不得被多个协程无保护地并发读写。

## API 设计规范

- API 只暴露触发爬取、任务进度、健康检查或必要查询；不暴露任意文件读取、任意 URL 下载、环境变量、数据库路径或 debug dump。
- 入参必须有长度、格式和枚举范围校验。
- 响应模型使用 DTO，避免直接暴露数据库行结构或第三方平台原始响应。
- 对外错误响应保持稳定、简短、可诊断；内部错误细节写入脱敏日志。

## Migu 元数据关系表维护上下文

本节记录当前 Migu 歌手、歌曲、专辑元数据入库和控制台查询的关系设计。修改 `singers`、`singer_source_refs`、`songs`、`song_source_refs`、`song_artist_refs`、`albums`、`album_source_refs`、`album_song_refs`、`song_resources` 或相关 catalog/backfill SQL 时必须先阅读本节。

### 歌曲与来源表

- `songs` 当前作为公共歌曲标题池使用，最核心字段是 `title` 和 `normalized_title`；`normalized_title` 应保持唯一，来源表通过 `song_db_id` 复用标题记录。
- 平台差异字段必须留在 `song_source_refs` 或更具体的来源表中；`duration_seconds` 属于平台来源时长，不应放回公共 `songs`。
- `song_source_refs.source_title_pinyin` 属于 Migu 来源拼音，保留在 refs；不要移动到公共 `songs`。
- `song_source_refs.copyright_id` 目前作为资源链路兼容参数保留；`copyright_type`、`restrict_type`、`product_authorize_usage` 这类暂不服务采集和资源处理的字段不应继续扩大使用，后续确认资源链路不需要后可清理。
- `song_source_refs` 不保存专辑属性及封面、歌词远端地址。解析输入中的专辑名称与拼音在首次写入时分到 `albums/album_source_refs`，拼音使用 `album_source_refs.source_title_pinyin`。

### 歌曲、歌手和专辑关系

- `song_artist_refs` 是歌曲来源到歌手来源的关系表，只表达关系，不保存歌手属性。不得重新加入 `source`、`source_artist_image`、`source_name_apelling` 或已能从 `singer_source_refs` 得到的歌手字段。
- `song_artist_refs` 以 `song_source_ref_id`、`singer_source_ref_id`、`artist_role` 表达唯一关系；按歌手入口采集歌曲时，应把入口歌手对应的 `singer_source_refs.id` 写入 `singer_source_ref_id`，并标记 `is_seed=1`，主唱可标记 `is_primary=1`。
- Migu 歌手索引已整体入库后，`singerList` 中的歌手应优先通过 `source_artist_id` 匹配 `singer_source_refs` 再写关系；不要再把 `source_artist_id/name` 作为关系表持久字段兜底。
- `album_song_refs` 只保存非空的 `album_source_ref_id`、`song_source_ref_id`、disc、track 和来源顺序，平台身份通过两端来源表取得。
- 歌手歌曲响应已提供专辑身份和名称时，直接创建专辑摘要，再写专辑歌曲关系，不额外发详情请求。实体创建、关系、音质与资源结果在同一数据库事务提交。
- `album_artist_refs` 维护专辑与歌手来源的角色、顺序、主歌手和入口标记；`albums` 与 `album_source_refs` 不重复保存歌手属性。专辑身份 hash 使用平台与专辑来源 ID，不依赖名称、歌手或后到的物料 ID。
- `singer_resources`、`album_resources` 保存采集时实际下载的本地封面结果，不保存 source 或远端 URL；数据库事务失败时清理本次新增封面。

### 歌手索引与 tab 归属

- `singers` 是公共歌手名称池，核心字段是 `name` 和 `normalized_name`；`normalized_name` 应作为去重键。平台拼音、来源 ID、来源国家/地区、来源标签和头像应在来源表或索引归属表中维护。
- `singer_source_refs` 表达 Migu 歌手来源身份；同一 `source_singer_id` 可能出现在多个 tab/分类下，不应因为主来源唯一而丢失分类归属。
- tab、分类、性别分组、国家分组这类索引入口归属应由独立 membership/索引归属表记录；不要通过重复插入 `singer_source_refs` 来表达多 tab 归属。

### 控制台查询和计数

- 控制台列表和 overview 需要展示事实计数时，实时从关系表聚合：歌手歌曲数来自 `song_artist_refs`，歌手专辑数来自 `album_artist_refs`，专辑歌曲数来自 `album_song_refs`。
- 前端展示别名可以返回 `songs_count`、`albums_count`、`album_songs_count`，但这些是查询 DTO/API 层别名，不应新增写入时强维护的 summary 表。
- 专辑 `album_songs_count` 是当前关系数量，`songs_sync_count` 是最近一次专辑歌曲采集成功处理条数，必须分开读取与返回。专辑完成情况按 `album_source_refs.songs_sync_status` 统计，不能由已有关系数量覆盖；歌手歌曲入口可以创建关系，但不会因此完成专辑曲目采集。
- 三个来源表不保存派生的 `source_item_key`，兼容接口按平台 ID 计算；歌曲仍保留歌曲 ID、内容 ID 两组唯一约束及至少一个有效 ID 的数据库 CHECK。专辑标题只写 `albums.title`，`source_title` 仅可作为兼容查询别名。
- 数量过滤参数应跟事实计数一致：歌手支持 `min_songs_count/max_songs_count/min_albums_count/max_albums_count`；专辑支持 `min_songs_count/max_songs_count`，可兼容 `min_album_songs_count/max_album_songs_count`。
- 当真实关系已存在且没有同步错误时，歌手歌曲/专辑队列可以按完成状态展示；但 `1 running` 仍必须保持运行态，不能被事实计数覆盖。

### Migration 和 schema 状态

- MySQL DDL 以后进入 `sql/mysql/migrations/`；已执行过的 migration 文件不得修改内容，否则会触发 checksum mismatch。
- 运行期 SQL 按表放入 `sql/mysql/<table>/`，每个写入模板只修改本表；跨表顺序由事务 writer 编排。各表 `create.sql` 是结构快照，启动升级仍使用 migrations。
- 如果真实环境出现 checksum mismatch，应新增修复 migration 或提供明确的人工修复说明，不要直接改旧 migration 来“对齐”。
- `schema.sql` 或旧编号 SQL 只能作为 legacy/测试参考入口；生产初始化以 migration runner 为准。
- 新增/删除字段必须同步更新 schema status 检查、OpenAPI 文档、catalog/backfill SQL 和对应测试。

## Migu 资源下载链路维护上下文

本节记录当前 Migu 歌曲资源下载链路的已验证结论和后续修改约束。修改 `SongResourceDownloadWorkflow`、`AsyncSongResourceDownloadWorkflow`、资源 provider/parser/fetcher/blob store、`/api/migu/resources/process`、pacmtoken 或 `song_resources` 状态回写时必须先阅读本节。

### 当前主链路

- 资源处理入口是 `POST /api/migu/resources/process`；长时间任务使用 `async=true`，返回 `202 + batch_id`，前端通过 `/api/migu/tasks`、`/api/migu/tasks/{batch_id}/items`、`/api/migu/tasks/{batch_id}/events` 查询进度。
- 资源处理流程固定为：从 MySQL `song_resources` 认领 `status IN (0, 3)` 的目标 -> audio 调用 Migu listen 接口换取临时 URL -> parser 只解析 `url/version/audioFormatType/lrcUrl/mrcUrl/trcUrl` 和失败 `cannotCode/dialogInfo.text` -> 流式下载 -> blob store 完整提交 -> 成功后回写 `local_path/checksum/size/status=2`。
- 下载成功前不得回写完成状态。blob store 必须保持 staging/commit 语义；本地文件当前用临时文件、校验大小和 checksum 后提交，后续 rclone/API 存储也必须保持“完整对象提交后才回写 DB”的语义。
- 当前主流程不再自动上传百度网盘，也不在资源下载 workflow 中调用 BaiduPCS-Go、rclone 或其它云端 uploader。已验证 BaiduPCS-Go/Web/桌面客户端实际都走 `locateupload -> precreate -> rapidupload/superfile2 -> create` 类似链路，真实速度主要受本机上行或百度侧上传通道限制；后续除非用户明确要求重新做上传实验，不要把 BaiduPCS-Go 上传重新接回主流程。
- 当前稳定闭环是：后端只负责 provider 获取 URL、解析、下载到本地、回写 `local_path/size_bytes/checksum/status=2`；用户用百度网盘客户端手动上传 `D:\migu-resource-cache\migu` 到 `/Media/migu`；随后用 `D:\migu-resource-cache\cloud-reconcile` 下脚本做云端核对和数据库回写。
- 云端路径约定固定为 `cloud_backend='baidupcs'`，`cloud_path='/Media/' + local_path`。核对已写路径时只检查数据库路径字段和云端对象存在性，不要求本地文件仍存在；本地缓存可被用户手动移动或删除。
- 云端对账脚本目录约定：`audit/` 只读统计和核对，`writeback/` 才允许改库，`lib/` 放公共配置，`runs/` 放运行结果。推荐顺序是 `audit/summarize-missing-cloud-paths.ps1` -> `audit/reconcile-cloud-paths-meta.ps1` -> `writeback/backfill-existing-cloud-paths.ps1` -> `audit/verify-written-cloud-paths.ps1`。遇到 `cloud_unknown.csv` 或 BaiduPCS-Go `EOF`/网络错误时必须低并发重跑核对，不得直接当作云端缺失标记失败。
- 资源相对路径约定为 `migu/songs/{contentId}/audio/{format}.{ext}`、`migu/songs/{contentId}/lyrics/lyric.lrc`、`migu/songs/{contentId}/lyrics/mrc.mrc`、`migu/songs/{contentId}/lyrics/trc.trc`。不要改用短 `song_id`，`contentId` 更稳定且与平台资源接口一致。
- 同一首歌的 `lyric/mrc/trc` 只处理一次。并发场景下必须保留去重集合保护，避免每个音质重复下载歌词。关联 URL 存在但队列无可认领资源时返回 synthetic skipped item：`resource_id=0`、`related=true`、`synthetic=true`、`result_code=related_not_claimed`。
- 同步 workflow 保留小批量兼容；异步 workflow 是正式长跑入口。异步资源管线使用 application 自定义 `Task<T>`，application 层不得直接 include Boost.Asio；Asio 只能出现在 infrastructure async runtime、HTTP client、MySQL adapter 等基础设施实现中。

### Migu 业务码和状态不变量

- `440000` 表示“该歌曲暂无版权”，必须视为永久跳过，整首歌未完成资源写 `status=4`，`error_category=copyright`，`result_code=440000`。
- `440015` 表示数字专辑购买限制，必须视为永久跳过，整首歌未完成资源写 `status=4`，`error_category=resource`，`result_code=440015`。
- `440016` 表示数字专辑购买限制，必须视为永久跳过，整首歌未完成资源写 `status=4`，`error_category=resource`，`result_code=440016`。
- `440013` 属于登录态、账号状态或 token 相关限制；默认在 `refresh_pacmtoken_on_440013=true` 时刷新 pacmtoken 后额外重试一次。刷新后仍失败时保留为失败资源，不得写成永久跳过。
- `status=4` 是永久跳过状态，普通认领、retry、reset、recover-stale 不得把它拉回 `0`。当前 SQL 已通过 `sr.status <> 4` 和默认认领 `status IN (0, 3)` 保护，后续修改 SQL 时必须保留。
- `markSkipped` 针对这些永久限制会清空 `resource_kind/format_type` 条件，目的是跳过同一首歌下所有未完成资源，避免不同音质重复请求。已完成的 `status=2` 不应被改写。

### pacmtoken 和敏感信息

- 项目没有 Migu 登录态生成能力。`pacmtoken` 由前端从浏览器环境取得后调用 `POST /api/migu/pacmtoken/refresh` 写入后端运行期内存；后端不会把 token 写入 `.env`、MySQL 批次表、事件或日志。
- `POST /api/migu/pacmtoken/current` 只供前端显式同步本地 token；不要自动高频调用，不要把响应体写日志或埋点。
- 批次启动和长跑中可按 `pacmtoken_refresh_interval_seconds` 自动刷新，事件只允许记录 `pacmtokenRefreshed`、长度和时间，禁止记录 token 明文。
- 禁止在日志、批次 payload、测试输出、文档示例中打印 Cookie、pacmtoken、完整 listen URL、完整临时下载 URL、签名参数或 `Key/Tim/playSessionId`。

### 并发、重试和运行配置

- 前端/生产默认资源并发建议从 `3` 或 `5` 开始，`resource_download_concurrency` 范围 `1..32`。目前 listen 接口在真实测试中 `HQ 100/100` 成功，未观察到明确限流，但不能假设平台永不限流。
- 只对 `network_failed`、`timeout`、HTTP `5xx`、listen URL 获取网络失败这类可恢复错误重试。音频重试必须重新请求 listen URL，不能复用旧临时 URL。
- 不重试数字专辑、暂无版权、参数缺失、解析结构错误、持久化失败、`440000/440015/440016`。
- `resource_download_retries` 范围 `0..5`，`resource_download_timeout_ms` 范围 `10000..600000`，`pacmtoken_refresh_interval_seconds` 范围 `0..3600`，`0` 表示关闭长跑定时刷新。
- 当前 Windows/OpenSSL 环境下，Migu 下载域可能因为证书链导致 `TLS handshake failed: certificate verify failed`。真实测试时如出现大量 `network_failed` 且 message 指向证书校验，优先检查 `CRAWLER_OUTBOUND_VERIFY_TLS` 或 CA 配置；本地实测使用 `CRAWLER_OUTBOUND_VERIFY_TLS=false` 后下载正常。
- proxy 不是当前上线前置条件。先无 proxy 小批量生产跑通，再接 proxy 做 A/B 对比和提并发，避免同时引入过多变量。

### 前端和 API 对接注意事项

- 前端最小闭环：刷新 pacmtoken -> 选择 `format/limit/concurrency` -> `POST /api/migu/resources/process` 且 `async=true` -> 保存 `batch_id` -> 轮询 tasks/items/events。
- 下载前可用 `GET /api/migu/resources/queue?status=0,3&kind=audio&format=PQ` 展示待处理量。`status=4` 资源不要展示普通 retry/reset 操作。
- 资源 process 请求示例：

```json
{
  "source": "migu",
  "limit": 50,
  "async": true,
  "filter": {
    "kind": "audio",
    "format": "PQ",
    "status": [0, 3]
  },
  "options": {
    "resource_download_concurrency": 3,
    "resource_download_retries": 1,
    "resource_download_timeout_ms": 120000,
    "pacmtoken_refresh_interval_seconds": 300,
    "auto_refresh_pacmtoken": true,
    "refresh_pacmtoken_on_440013": true
  }
}
```

- 前端展示时把 `440000/440015/440016` 归为“永久跳过”，不要作为需要重试的失败告警；把 `440013` 展示为 token/登录态可能失效，引导用户刷新 pacmtoken 后再 retry。
- `related_not_claimed` 是 synthetic 关联资源跳过明细，不代表主资源下载失败，前端可折叠展示。

### 已验证结论和回归测试

- 真实测试结论：`HQ` listen 探测 `100/100` 成功；`PQ limit=30 concurrency=5` 真实异步下载 `30/30` 成功；历史 `440000/440015` 样本已正确写成 skipped；未观察到 `440013` 或平台 listen 限流。
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

- 修改 API、DTO 或文档后运行：

```powershell
.\.vscode\build-msvc-target-debug.cmd MiguCatalogApiSmokeTest
.\.vscode\build-msvc-target-debug.cmd MiguHttpRouteSmokeTest
.\.vscode\build-msvc-target-debug.cmd MiguApiDocumentTest
```

- 真实测试只在用户明确提供可用 MySQL 和 pacmtoken 后执行，且输出必须脱敏。推荐命令模式：

```powershell
$env:CRAWLER_OUTBOUND_VERIFY_TLS='false'
$env:CRAWLER_TEST_MIGU_ASYNC_RESOURCE_FORMAT='PQ'
$env:CRAWLER_TEST_MIGU_ASYNC_RESOURCE_LIMIT='30'
$env:CRAWLER_TEST_MIGU_ASYNC_RESOURCE_CONCURRENCY='5'
.\build\msvc-debug\Debug\MiguAsyncSongResourceBatchIngestionTest.exe
```

## 测试与验证要求

代码改动后至少执行与改动匹配的验证：

```powershell
cmake --preset msvc-vcpkg
cmake --build --preset msvc-vcpkg-debug
clangd --check=src/path/File.cpp --compile-commands-dir=build/msvc-debug
```

补充要求：

- 修改头文件后，应至少对一个包含该头的 `.cpp` 运行 `clangd --check`。
- 修改 CMake 后必须重新 configure。
- 修改 SQLite schema、DAO、transaction 或并发逻辑后必须增加或更新测试；现有测试未全部接入 CMake 时，要同步接入或在最终回复说明手动验证方式。
- 修改 provider/parser 时应使用离线 fixture 覆盖解析逻辑，不让单元测试依赖实时网络。
- 无法运行验证时，最终回复必须说明原因、未覆盖风险和建议的下一步。

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
- 风格争议以 `.clang-format` 和本文件为准；没有规则时跟随局部既有风格。

## 联网检索规范

本地代码和现有文档能解决时优先本地，不联网。需要最新外部事实、第三方库线程安全、标准依据、安全建议或版本行为时才联网。

联网时必须：

- 优先官方或权威来源：ISO C++ Core Guidelines、CMake 文档、SQLite 文档、Boost.Asio/Boost.MySQL 文档、cppreference、Google Engineering Practices、OWASP/SEI CERT。
- 查询词精确，不把项目私有路径、密钥、token、Cookie、日志、内部域名或大段私有代码发到网络。
- 不从随机博客复制代码；外部内容只作为规范依据或设计参考。
- 在最终回复中列出实际使用的来源链接和用途。
- 若外部资料与本项目规范冲突，先遵循本项目规范，并说明取舍。

本规范参考的外部依据：

- C++ Core Guidelines：函数单一职责、RAII、所有权、并发锁管理。
- CMake Documentation：`CXX_STANDARD`、`CXX_STANDARD_REQUIRED`、`CXX_EXTENSIONS`。
- Alistair Cockburn Hexagonal Architecture：Ports & Adapters、内外边界隔离。
- SQLite Documentation：线程模式、WAL 并发语义。
- Boost.Asio / Boost.MySQL Documentation：异步 I/O、连接池和协程并发约束。
- Google Engineering Practices：代码审查关注设计、功能、复杂度、测试和一致性。
