# 开发规范

编写或审查代码、脚本和构建配置前按任务读取对应章节。目录和依赖方向见 [架构](../architecture/README.md)，测试选择见 [验证要求](validation.md)。

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
