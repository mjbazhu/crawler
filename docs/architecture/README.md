# 架构与模块边界

本文面向项目维护者，说明 Crawler 模块的运行结构、模块范围、分层边界和演进约束。智能体按 [读取索引](../agents/README.md) 定位相关章节，执行规范由该目录单独管理。

## 当前运行结构

| 入口或组件 | 当前职责 | 代码依据 |
| --- | --- | --- |
| 进程入口 | 读取环境配置，初始化应用，运行 HTTP 服务或单次 CLI 采集 | [main.cpp](../../src/main.cpp) |
| 组合根 | 创建 HTTP client、Migu provider/parser、存储、执行器和服务，通过窄 port 装配 | [ServerApplication.cpp](../../src/infrastructure/app/ServerApplication.cpp) |
| 入站 HTTP | Drogon adapter 接入请求，项目路由与 controller 转换协议并调用 application | [DrogonHttpServer.h](../../include/crawler/infrastructure/http/server/DrogonHttpServer.h)、[CrawlerRoutes.h](../../include/crawler/api/routing/CrawlerRoutes.h) |
| 持久化 | 默认装配 MySQL repository 和事务 writer；显式 SQLite 调试配置保留同步 DAO | [PersistenceAssembly.cpp](../../src/infrastructure/app/PersistenceAssembly.cpp) |
| 资源存储 | 默认使用本地文件 adapter；保留显式配置的 rclone adapter；采集装配关闭独立百度云自动上传 | [ServerApplication.cpp](../../src/infrastructure/app/ServerApplication.cpp) |

现状基于 2026-10-26 的源码静态核对，未在本次文档复核中启动服务。元数据关系见 [数据模型](../migu/metadata.md)，资源处理见 [资源流程](../migu/resources.md)。

## 模块定位

`modules/crawler` 只负责音乐数据采集、音乐元数据解析、音乐参数信息整理，以及把这些数据可靠地写入本地存储。

允许的职责：

- 从受支持的音乐数据源获取歌曲、专辑、歌手、榜单或搜索结果等音乐数据。
- 解析外部响应为本模块领域模型，例如 `Song`、`Album`、`Singer` 和明确命名的参数对象。
- 通过 MySQL 生产 adapter 或 SQLite 调试兼容 adapter 保存音乐元数据，保存必要的二进制资源到本地文件系统。
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
- `infrastructure/music/persistence/mysql`：只提供 MySQL 连接池、基础 CRUD 仓储辅助、事务和 MySQL repository 实现；不得把 MySQL 类型泄漏到 application/domain。
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

## 架构演进决策

本节约束后续演进方向；提及 Redis 等替换点不表示当前已经部署该 adapter。

当前装配使用 Drogon 入站服务、Asio 出站 I/O 和 MySQL 默认持久化；SQLite 作为显式选择的调试兼容后端保留。依据是 `ServerApplication::Config::persistence_backend` 与 `assemblePersistence`，现状链接见 [当前交接](../agents/current.md)。后续涉及 MySQL、Redis、协程或与其他模块统一时，按以下方向演进：

- 入站 HTTP 服务使用 Drogon，与 `echo`、`proxy` 模块保持一致；Drogon 类型只允许停留在 `infrastructure/http/server` 和组合根，不得进入 `api`、`application` 或 `domain`。
- 外部音乐平台抓取当前使用 `infrastructure/http/client/AsioHttpClient`，通过 Asio 协程和内部 I/O 线程池执行出站网络 I/O；application 新增流程只能依赖 `SongSearchProvider`、`AlbumSearchProvider`、`SingerCatalogProvider`、`AlbumDetailProvider`、`RawPageFetcher` 等窄 provider port 或 application 定义的 HTTP port，不允许在 application 中混用 Asio、Drogon 等 HTTP 细节。
- MySQL 只作为持久化 adapter，不改变 application 用例；先复用或定义 repository port，再提供 MySQL repository 实现，当前 SQLite DAO 只能作为过渡实现。
- Redis 只作为爬取任务状态、幂等键、重试队列、短期结果缓存或分布式锁 adapter；不得把业务规则写进 Redis key 拼接逻辑。
- 异步获取优先模型是：API 接收任务 -> application 写入任务状态 -> provider 异步抓取 -> parser 解析 -> repository 持久化 -> Redis/任务状态更新。不要让 controller 直接启动网络抓取和数据库写入细节。
- 当前 Drogon handler 必须通过 `DrogonHttpServer` adapter 转换为项目内 `api` 层的 `HttpRequest`/`HttpResponse`，并把可能阻塞的 controller 调用投递到受限 worker pool；后续高并发 I/O 目标方案优先 Asio/Drogon coroutine/async client，CPU 密集解析、批量持久化或兼容阻塞库时才使用独立线程池。
