# 当前任务与会话交接

更新日期：2026-10-29。工作区：`C:/Users/nidie/.codex/worktrees/artist-album-cover-storage/crawler`，分支：`codex/artist-album-cover-storage`，基线：`21b19a5`。本地封面与指定 main20 克隆库实测已完成。两类封面的百度与 Google 云端接入及真实验收已完成，用户明确要求开启配置后自动上传并回写。原 main 工作区不作代码修改。

当前交付指示：用户已授权整理文档、提交、推送、创建 PR，最新指示为暂不合并 PR。主分支已通过 PR #5/#6 合入歌手资料清理和长期验收；本轮需保留这些已提交内容并核对集成兼容。下一阶段[统一云端回执重构](../roadmap/resource-cloud-receipts.md)方案已确认但尚未实施，不在当前交付时新增回执表或删云端列。

## 实施状态

本地闭环已经实现，40 张真实歌手/专辑图片通过克隆库上的 HTTP 接口完成下载及读取验收。此前本地版本 `1.8.0` 已验收；本轮顶部 API 清单、推荐流程与两份 OpenAPI 已更新到 `1.9.0`，包含云端状态及补传规则。详细范围、接口和验收统一维护在[封面专项](../roadmap/metadata-cover-download.md)。并行歌手字段清理与当前目录查询存在兼容缺口，见下方，不能把它计作联调通过。

用户随后明确要求把现有 Google 后端也一起接入并直接实现。已实现共用 `CloudFileStore` 与 `MetadataCoverCloudWorkflow`：百度复用 BaiduPCS-Go uploader，加有界进程及云端回读 SHA-256 核验；Google 复用 rclone RC 配置与鉴权，使用 `operations/uploadfile` 上传同一字节快照，`operations/stat` 核验大小和完整摘要。两者共用云端状态、认领、回写及现有 `/covers/process`，配置选择一个后端，不要求手动步骤。

本轮新增 `V2026102901__metadata_cover_cloud_storage.sql`（已在独立库和克隆库执行及重跑，checksum `ee00f3811554d6eb404813a48f58b607`）、两表 cloud/process SQL、共用模型与 port、云端用例、有界进程、两个后端 adapter、装配及 DTO。API/OpenAPI `1.9.0` 已对齐。百度、Google 各 20 张歌手图和 20 张专辑图真实上传、核验、DB 回写及第二轮无候选均通过；Google 另独立列出 40 个对象，大小/MD5 与本地 SHA-256 回执一致。完整实现、配置和证据见[云端存储](../roadmap/metadata-cover-cloud-storage.md)。

- `MetadataCoverDownloadWorkflow` 通过窄队列、保存和读取 port 编排既有 URL 下载、完整文件提交、条件回写及补偿；刷新失败保留旧图，提交不确定保留日志。
- `MySqlMetadataCoverQueue` 读写两张现有资源表，使用类型与资源 ID 识别所有者。封面 migration 在发布前避让为 `V2026102802__metadata_cover_download_claims.sql`，DDL 字节与 checksum 不变；源库的 `2026102801/singer_profile_field_cleanup` 已由并行会话执行，其历史记录保持原样。早期封面原型测试库使用过冲突的 `2801`，不再复用为最终验收库。
- 新增 `/api/migu/covers/queue`、`process`、`recover-stale` 和 `/{owner_type}/{resource_id}/content`。歌手/专辑列表和详情补本地 cover 对象，平台 URL 保持原语义；图片读取核对大小、签名和 SHA-256，支持 ETag。
- `BatchTaskManager` 复用相同生命周期实现，新增带批次身份/取消检查的处理器；取消和超时结束尚未开始的 item，批次事件补 item_type/item_key。
- 文件日志新增所有者类型，旧日志默认 song；恢复检查正确资源表及代次，专辑别名迁移后的文件引用也受保护。
- 新输入以 `source_album_cover_url` 表达明确专辑图片，普通歌曲图片不再直接回填专辑；历史地址不猜改。
- 修复验证发现的两个接入边界：Asio 非 2xx 不触发响应回调，必须从 transfer 结果读取重定向头；Drogon adapter 需声明两个动态占位参数才能注册封面内容路由。
- 真实实测修复：Windows OpenSSL 原先未加载系统 ROOT，导致 40 张图片均在证书校验失败后返回 `network_failed`；新增只读系统根装载，保留 TLS 链与主机名校验。Drogon 普通 header 转发还会把 WebP 回退成 HTML，现显式保留实际 MIME，增加 WebP/AVIF 真路由回归。
- schema 状态接口更新最新版本 `2026102802` 与封面 pending 登记，并修正 `2026102602` 的过期 expected checksum 为实际未修改文件的 `348b0d48b63c5d4b3e114ac16516a16f`；没有改写该历史 SQL 或数据库迁移记录。新队列集成测试断言 `schema.ok`；本轮新增 V2901 后，最新可用版本为 `2026102901`。

## 构建与证据

C 盘链接空间不足后，当前有效构建目录改为 `D:/codex-builds/crawler-cover-local-20261028`，使用原 MSVC/vcpkg preset，额外仅在本地 cache 设置 `CMAKE_EXE_LINKER_FLAGS_DEBUG=/DEBUG:NONE /INCREMENTAL:NO`。源码中的工具链和默认 preset 没有为此修改。

C 盘本 worktree 的 16 个链接符号/增量文件及旧 crawler_core.lib 已移动保留到 `D:/codex-builds/crawler-cover-local-symbols-20261028`；此前删除命令被自动审批拦截，未执行删除。原主工作区及其他 worktree 未清理。

有效构建命令先调用 `D:/VS/Common7/Tools/VsDevCmd.bat -arch=x64 -host_arch=x64`，再执行：

```text
cmake --preset msvc-vcpkg -B D:/codex-builds/crawler-cover-local-20261028 -DCMAKE_EXE_LINKER_FLAGS_DEBUG="/DEBUG:NONE /INCREMENTAL:NO"
cmake --build D:/codex-builds/crawler-cover-local-20261028 --target <受影响 targets> -j 4
```

证据目录：`D:/codex-runs/crawler-cover-local/20261028/`。

- `demo` 及受影响 target 构建成功。初始实现的 15 项离线/本机测试见 `offline_final.txt`；补 TLS 信任、MIME、schema 和契约后，受影响的 8 项回归再次通过，见 `api-ca-mime-final-tests.txt`。最终构建见 `windows-ca-build.txt`、`webp-mime-build.txt`。
- `V2026102802` 的 4 项全新独立 MySQL 回归均通过，目录为 `mysql-v2802-20261029_001341/`，汇总 `mysql-v2802-latest.json`。四库分别为 `crawler_ownership_test_cover2_queue_20261029_001341`、`cover2_url_20261029_001341`、`cover2_ownership_20261029_001341`、`cover2_identity_20261029_001341`，后三项同样带 `crawler_ownership_test_` 前缀。覆盖 schema 状态、迁移重跑、并发认领、代次、URL 竞态、别名归一、文件回执、回滚和 174 份 SQL prepare；各日志以 target 命名。
- 用户指定源库 `crawler_ownership_test_main20_20261028_154114` 已通过单事务快照克隆为 `crawler_ownership_test_cover_main20_20261028_235106`，22 张表、5 个视图、数据导出哈希校验一致，无跨库视图或外键引用。源库另有会话持续刷新资料，本会话只读导出，没有停止其任务或写入源库。
- 克隆库真实 HTTP 验收：20 张歌手图、20 张专辑图全部成功，共 4,812,571 字节，包含 23 WebP、15 JPEG、2 PNG；核对任务/明细/事件、实际文件、大小、SHA-256、本地内容、MIME、ETag/304、第二轮无待下载项、参数 400、缺失 404 和 stale 恢复。TLS 校验始终开启；另用本机自签名服务确认握手阶段拒绝且未产生文件。
- 实测证据位于 `main20-20261028_235106/`：`manifest.json`、`http-cover-result.json`、`served-openapi.json`、`untrusted-tls/result.json`；外层日志为 `main20-clone-verified.txt`、`main20-live-http-final.txt`、`main20-untrusted-tls.txt`。此前默认库的 `77/SKIP` 是历史探测，已由本轮 main20 实测补齐，不能再写成当前无 URL 来源。
- 本地阶段 API/OpenAPI `1.8.0` 的 4 个新增路径、6 个 schema 和 2 个目录字段一致，见历史 `contract-check.txt` 和 `main20-20261028_235106/served-openapi.json`。当前 `runtime-openapi.json` 已更新为含云端字段的 `1.9.0`，本轮契约验证见下方。
- 初始 19 个实现及本轮 4 个受影响实现的 clangd 编译/preamble/AST 阶段无诊断。单文件日志为 `clangd-<文件名>.txt` 和 `clangd-final-<文件名>.txt`；部分命令因编辑器 ExtractFunction/ExpandDeducedType 等 tweak 自检失败而非零退出，不能计为全部 clangd 检查通过。
- 本地实现收尾及本轮云端规划检查的修改/新增文件均为 UTF-8 无 BOM，当前 194 个本地 Markdown 文件链接有效，新增生产文件不超过 300 行，新增 C++ 文件及已跟踪改动行符合 `.clang-format`，Git diff 无空白错误；本轮云端验证见下方。

本轮云端新增验证：15 项离线/本机回归通过（`cloud-offline-final.txt`），两项全新独立 MySQL 回归通过（`mysql-cloud-latest.json`），186 份运行期 SQL prepare 通过（`cloud-sql-prepare.txt`）。真实云端结果分别位于 `cloud-live-baidupcs-20261029_0130/` 和 `cloud-live-rclone-20261029_0130/`；迁移 checksum 与 OpenAPI 实际服务一致（`cloud-contract-final.txt`）。六个 clangd 实现检查无编译诊断，两个仅有编辑器 tweak 自检失败，未计为全命令通过。

百度真实 `meta` 的“类型”输出差异已修复并增加回归。Google 原配置 RC 地址连接重置，实测通过已有 Drive 授权配置及官方摘要校验的便携版临时 RC 完成；原私有 env 未改，运行时需提供可达的 RC。两个自建 crawler 及临时 RC 测试进程已停止，远端对象、克隆库和证据保留。所有上传使用独立 `crawler-tests/cover-cloud-20261029_0130` 子目录，未触及已有正式对象。

测试连接参数从原工作区 `.env.test` 只读取得，通过子进程环境传递；数据库名显式覆盖为 `crawler_ownership_test_*`。没有把凭据打印或复制进本 worktree，没有写入原来源库。所有 MySQL 测试串行推进；真实探测也应避开全局 migration 锁的同时竞争。

## 当前修改与后续

修改包括规划/交接文档、封面模型及窄 port、队列与 SQL/migration、独立 workflow/门面、文件保存/读取/恢复、HTTP adapter/路由/DTO/目录查询、组合根及两份 OpenAPI、四个新测试和手动真实探测，并联动既有针对性测试与事件说明。改动尚未暂存或提交，原 main 工作区干净。

1. 克隆源已应用并行 `singer-profile-completion` 的字段清理，删除 `singers.status` 等；本分支旧歌手目录 SQL 仍引用该列，真实歌手详情返回 404，专辑详情已验证返回正确 cover。这是跨分支联调缺口，证据 `main20-20261028_235106/catalog-integration.json`；封面队列/下载/内容本身通过。用户已明确选择“保持两会话独立，统一合并后再联调歌手目录”，本轮保留该边界，不整合另一会话的未提交变更。
2. 若定位历史专辑 URL 归属问题，先按保存的原响应核对再定向修复；本次仅保护新输入，未批量清理历史数据。
3. 两家后端的自动上传、已有文件补传、对象核验及条件回写已完成，配置及验收见[云端存储](../roadmap/metadata-cover-cloud-storage.md)。歌手资料分支仍保持独立，统一合并后再联调目录。
4. 已收到提交、推送和创建 PR 的明确授权，先展示精确文件清单并按职责提交；PR 保持未合并。合并后的 SQL 重构以当时最新 main 为基线，在独立分支继续；此轮只固化方案。
