# Crawler 智能体入口

本文件约束 `backend-cpp/modules/crawler` 的代码、文档、脚本和数据结构改动。智能体执行规范、读取指引和交接信息统一维护在 `docs/agents/`；项目架构、业务与运行说明由 [项目文档索引](docs/README.md) 管理。

## 开始任务

1. 先读 [文档索引](docs/agents/README.md)，再按 [任务分类指南](docs/agents/task-routing.md) 选择首查代码、联动范围、验证和完成标准；跨主题任务补读相关部分。
2. 切换会话、接续任务或核对当前状态时，再读 [当前交接](docs/agents/current.md)，并核对 Git 状态与相关源码。
3. 修改代码或构建配置前，读取 `CMakeLists.txt`、`CMakePresets.json`、`.clang-format`、`.clangd` 和相关源码。
4. 提交、推送、创建 PR 或合并前，读取 [Git 流程](docs/agents/git-workflow.md)，遵循精确暂存、提交前确认和标准合并要求。

索引中的文件需要显式读取，不能假定 Markdown 链接会自动加载正文。按任务引用的规范同样适用；不要在每次会话中无差别加载全部主题。

## 核心约束

- 本模块只负责获取音乐数据、解析和整理音乐元数据，以及可靠存储；不扩展播放器、账号、支付、前端 UI、通用下载器或反爬绕过平台。
- 遵循 KISS、YAGNI、SOLID、DRY；采用 `api`、`application`、`domain`、`infrastructure` 四层。高层依赖窄 port，具体实现由组合根装配。
- 公共 include 使用 `crawler/` 前缀，生产 namespace 根为 `orchestrator::modules::crawler`；不恢复旧目录、旧顶层 namespace 或杂物目录。
- 默认使用简体中文沟通和注释；源码、文档、配置采用 UTF-8 无 BOM。保留用户原有注释，不做无关重写。
- 保留用户已有修改和暂存内容；不把本地运行数据库、WAL/SHM、缓存和构建产物当作业务代码修改对象。
- 禁止硬编码或输出 Cookie、Authorization、pacmtoken、签名密钥和敏感 URL；不引入绕过认证、破解付费内容或规避访问控制的逻辑。
- 真实外部平台与数据库测试遵循 [验证要求](docs/agents/validation.md)，结果必须区分本次验证与历史记录。

## 文档维护

- 新增智能体执行规范、读取指引或交接文件时，统一放入 `docs/agents/`，并更新该目录索引中的读取条件。
- 本入口只保留核心约束和导航；执行规则归 `docs/agents/`，项目知识归项目文档对应目录，阶段结果归当前交接。
- 架构、元数据、资源流程、运行说明、API、OpenAPI 和表级 SQL 资料在各自职责目录维护；智能体索引引用原文，避免复制多份完整说明。
- 完成工作时报告实际修改、验证结果和未覆盖项；需要交接时更新当前状态，不把聊天全文或每轮过程追加到入口。
