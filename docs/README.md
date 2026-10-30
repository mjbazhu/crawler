# Crawler 项目文档

索引更新日期：2026-10-30，覆盖当前架构、歌手资料/封面验收、全量路由核对及配置、启动、恢复指南；统一云端回执已完成基础迁移/历史导入，桥接准入及独立目标装配已接入。

这里是项目维护者和智能体共用的资料入口。架构、业务、接口和运行说明按职责维护；智能体的任务分类、执行规范和会话交接集中在 `agents/`。下面的覆盖状态表示说明材料是否具备，不代表已完成当前环境的运行验收。

历史逐对象核对、续跑和审阅报告见[历史自动发现](operations/resource-history-discovery.md)；数据库三端容量配置及满队503见[持久队列容量](operations/resource-transfer-tasks.md#持久队列容量)。

## 按目的查找

云端实施进度见[统一资源云端回执重构](roadmap/resource-cloud-receipts.md)：持久三路、版本确认、历史跳过、旧 process 桥接及受控规模验证已实现。实际开发 `crawler` 已完成 V2912 扩展和45,558条旧完成资源的双端回执/本地 skip 补建。当前源码新增 V2913，将云读写统一到 `resource_cloud_receipts`，封存后删除31个旧物理云列及2索引，保留旧 HTTP JSON 投影和三张 legacy 映射表；实际库已完成本轮收缩及前后逐值核对，进度见[迁移记录](operations/resource-database-migration.md#v2913-物理云列收缩状态)。[本次物理收缩](operations/resource-schema-retirement.md)与未来全部旧协议退役分别验收，业务运行另行执行。

| 目的 | 现有入口 | 覆盖情况 |
| --- | --- | --- |
| 理解模块与架构 | [架构与模块边界](architecture/README.md) | 当前装配、四层边界及封面本地/云端数据流已具备，历史元数据/歌曲链路另有入口 |
| 查看项目推进路线 | [项目路线图](roadmap/README.md) | M1 现状校准、M2 运行闭环、M3 可靠性验收、M4 按证据演进；含依赖、交付、验收和状态 |
| 查看元数据剩余工作 | [元数据专项剩余项](roadmap/README.md#元数据专项剩余项) | 区分 `7235cf4` 历史证据与后续歌手资料/封面完成项，待取证、历史修复与未来回执重构保留各自边界 |
| 推进歌手/专辑封面本地化 | [本地封面下载专项](roadmap/metadata-cover-download.md) | 独立队列、下载、文件回执、本地接口及刷新恢复已接入，当前 API/OpenAPI 为 `1.9.0`；克隆 main20 后 40 张真实图片通过，集成 main 字段清理后的歌手/专辑目录及封面联调也已通过 |
| 接入歌手/专辑封面云端存储 | [云端实现与验收](roadmap/metadata-cover-cloud-storage.md) | 百度与 Google 适配已实现且各 40 张实测通过；main 歌手资料已集成，17 项回归、7 项新库及 40 张目录/封面联调通过，统一回执表另行实施 |
| 查看 PR #7 全量复核 | [复核与边界修复](roadmap/metadata-cover-cloud-storage.md#pr-7-全量复核与修复2026-10-29) | 损坏复用、明确云端缺失、DB 提交未知、返回快照及后台生命周期的修复和本轮验证；历史云端实测与本轮结果分别记录 |
| 理解音乐元数据 | [Migu 元数据模型与关系](migu/metadata.md) | 实体与来源分工、关系不变量、事实计数和同步计数、迁移约束 |
| 查看歌手资料完善结果 | [歌手资料完善与验收记录](migu/singer-profile-validation.md) | PR #5、全量刷新与头像修复回归、剩余字段缺口、证据位置和验证边界 |
| 理解资源处理 | [Migu 资源流程](migu/resources.md) | 认领、URL 获取、完整提交、状态码、重试、token 与接口注意事项 |
| 处理逐字歌词 | [MRC 解密与歌词产物](migu/lyrics.md) | C++ 解密、逐字 JSON、增强 LRC、固定路径、数据库索引与离线导出 |
| 调用 HTTP API | [Migu API 说明](api/mg/migu-api.md)、[静态 OpenAPI](api/mg/openapi-migu.yaml) | 运行期/静态OpenAPI按四种补建类型展开后77个方法/路径一致；字段语义仍按影响检查 |
| 理解批次事件 | [资源批次事件规范](observability/resource-batch-events.md) | 事件名、payload、trace 和脱敏规则 |
| 修改 MySQL 存储 | [表职责与执行顺序](../sql/mysql/README.md) | 表级持久化、事务写入、结构快照、migration 与独立库验证要求 |
| 独立升级资源回执数据库 | [数据库迁移](operations/resource-database-migration.md) | 不启动服务的 schema CLI、双平台回执结构、备份恢复及实际 crawler 数据保留验证 |
| 构建并首次运行 | [快速启动](getting-started.md)、[CMakeLists.txt](../CMakeLists.txt)、[CMakePresets.json](../CMakePresets.json) | Windows/MSVC、vcpkg 路径覆盖、独立库、迁移、启动/停止及最小调用步骤已具备 |
| 查找配置及生效方式 | [配置说明](configuration.md)、[配置示例](../.env.example)、[启动入口](../src/main.cpp) | 环境优先级、实际默认值、重启/热更新边界、歌曲存储与封面上传开关已区分 |
| 选择测试与执行条件 | [测试环境分类](agents/validation.md#测试环境与执行条件)、[任务与 target 对照](agents/validation.md#按任务选择验证) | 已有离线、本机 I/O、独立 MySQL、真实平台分类及 target 映射；引用现有执行说明，保持一处维护 |
| 本机资源运行与对账 | [资源运行说明](operations/resource-storage.md) | 歌曲手动归档、封面自动上传及历史 TLS/代理背景分开说明 |
| 演练资源双端历史声明 | [历史导入维护工具](operations/resource-cloud-import.md) | 只读清单、目标修订、双端及本地 skip、分页事务、审计及续跑；不启用现有 worker 或生产基线 |
| 理解持久资源任务与三路传输 | [持久任务与三路传输](operations/resource-transfer-tasks.md) | 原子三路、独立来源、本地发布、双端文件上传/核对及逐端预算；正式调度、目标身份和配置仍待接线 |
| 停止、恢复与备份 | [服务运行与恢复](operations/service-lifecycle.md) | 协作取消、两类 stale 入口、文件/云端异常、迁移失败、备份还原及未验证边界已具备 |
| 使用智能体开发 | [读取索引](agents/README.md)、[任务分类指南](agents/task-routing.md) | 任务识别、首查代码、处理步骤、联动、验证和交接；开发及 Git 规则由索引继续导航 |

## 推荐阅读顺序

- 初次了解模块：先看架构的运行结构和模块范围，再按工作内容选择元数据、SQL 或资源流程。
- 对接接口：定位 API 说明及 OpenAPI 中的相关路由和 schema，再核对该业务的字段、计数及状态语义。
- 修改并验证：先选对应业务资料，再按现有测试分类确定环境与 target；构建、静态检查、测试执行和真实平台验证分别报告。
- 排查资源运行：结合批次事件、资源流程、实际启动配置与运行说明定位阶段；本机路径和历史实验需按当前环境核对。

## 当前覆盖与已知差异

架构、核心业务、API、SQL、事件、智能体执行说明及三份运行指南均已具备。实际执行范围统一见[文档校准与指南验收](operations/service-lifecycle.md#文档校准与指南验收2026-10-29)，不能将指南存在等同于全部部署和故障场景通过。

- 歌曲 filesystem 主装配不自动上传，API 中云端字段保留兼容语义，示例已校准；封面独立 process 按开关上传百度或 Google，见[封面云端实现](roadmap/metadata-cover-cloud-storage.md)。
- API 清单、静态及运行期 OpenAPI 已完成全量方法/路径覆盖核对，运行期增加引用与路径参数检查；并未逐项重审所有请求/响应字段。受影响字段仍需与代码/SQL 联动核对。
- 快速启动以现有 Windows 工具链为目标，配置示例和本机历史路径需按环境替换；空白机器安装、备份还原及真实中断演练不由历史测试代替。
- 历史成功率和上传实验缺少完整的日期、提交及原始日志信息，引用时保留证据边界；新的运行结论需要对应环境的实际验证。

## 待补文档与优先顺序

本表维护剩余说明与演练缺口；已建立的指南通过实际入口引用，状态不退回“待编写”。

| 优先级 | 工作项 | 当前缺口与完成标准 |
| --- | --- | --- |
| 高 | 其他契约字段语义 | 路由覆盖及已确认上传差异已校准；后续改动继续核对具体字段、计数和状态，逐项记录范围 |
| 高 | 新环境启动演练 | [快速启动](getting-started.md)已编写，继续验证空白工具环境安装及部署路径；保留条件、命令和结果 |
| 中 | 配置维护 | [配置说明](configuration.md)已汇总入口变量与热更新边界；新增或变更配置时同步读取、默认值、校验及消费者 |
| 中 | 故障与还原演练 | [服务恢复](operations/service-lifecycle.md)已有步骤；备份还原、真实进程中断、系统重启及多实例协调仍需独立验收 |
| 中 | 其他数据流说明 | 当前封面数据流和边界已补；其他流程按实际故障或改动补充必要状态图，不复制多套业务正文 |

测试资料已有执行分类和入口，后续主要补实际环境验证及使用体验，不再把它标记为完全缺失，也不复制第二套 target 清单。工程推进顺序、前置依赖和阶段验收统一见 [项目路线图](roadmap/README.md)。

## 维护与专题边界

- 总索引维护资料入口、覆盖状态和文档缺口；架构正文在 `architecture/`，模型与业务流程在 `migu/`，API 在 `api/`，事件在 `observability/`，运行说明在 `operations/`。
- 架构文档说明当前结构、职责和依赖约束；[项目 roadmap](roadmap/README.md) 维护未来工作、推进顺序、前置条件、验收和阶段状态；本文继续维护文档覆盖与缺口。
- `agents/` 维护智能体怎样分类、执行和交接，通过链接引用项目知识；会话状态不作为项目 roadmap 的长期待办。
- 已完成且需跨任务引用的验收结论按主题保存在项目文档中，注明日期、交付版本、范围、结果与证据位置，并从本索引导航；`agents/current.md` 保留当前状态和记录链接。后续任务可重写交接，历史验收记录按新日期和版本补充，保留原有结论的适用范围。
- 同一主题保留一份正文；已有资料优先补充对应章节，新主题具备独立内容时再新增文件。
- 文档或代码变更后同步更新相关入口和覆盖状态；说明已写、静态检查通过、测试通过和运行验收完成分别记录。

历史发现后的[单端采信闭环](operations/resource-history-adoption.md)、[旧writer迁移与清理预演](operations/resource-compatibility-cleanup.md)、[上线证据与演练](operations/resource-release-readiness.md)及[物理云列收缩与旧协议退役](operations/resource-schema-retirement.md)分别维护实现、操作和实际验收边界。

正式双 RC 配置、调用方盘点与停写声明见[生产部署准备](operations/resource-production-deployment.md)；该工具不启停生产服务或自动放行切换。
