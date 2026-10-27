# Migu 逐字歌词处理

## 能力与边界

本模块使用 C++20 解密已下载的 MRC，并生成 UTF-8 无 BOM 的完整逐字 JSON 和增强 LRC。生产流程不启动 Node.js/Python 子进程，不增加歌词网络接口，不修改原始歌词文件，也不自动上传云端。

依赖方向为 `domain <- application <- infrastructure`，CLI 协议层只依赖 application：

- `domain/music/TimedLyrics.h`：平台无关的标签、行和字/词片段。时间为相对整首歌曲起点的绝对毫秒，持续时间单独保存。
- `application/use_cases/LyricProcessingWorkflow`：只编排来源读取、校验、解码、导出、文件提交和可选索引登记。
- `application/ports/parsing/TimedLyricsDecoder`：替换来源格式时使用的窄 port。
- `infrastructure/music/migu/lyrics`：64 位协议变换、UTF-16LE 解码和原生 MRC 语法分别实现；64 位运算显式模拟 Java long 的回绕和算术右移。
- `infrastructure/music/lyrics/TimedLyricsEncoder`：从统一模型导出 JSON/LRC；不依赖 Migu provider 或数据库。
- `LocalLyricArtifactStore`：根目录约束、限量读取、SHA-256、固定文件名、失败回滚及幂等核对。
- `MySqlLyricArtifactRepository`：仅保存产物描述，同事务登记两种格式并核对原始资源当前校验值。

平台算法改变时替换 cipher；MRC 字段语法改变时替换 text parser。修改 decoder 或 encoder 后升级对应 `version()`，无需修改下载、HTTP、领域模型或用例策略。JSON schema 变化时更新导出器 `schemaVersion()`、文档及版本；来源校验值、处理版本和产物描述统一交给数据库索引保存。

## 在线处理

启动前通过运行环境配置 `CRAWLER_MIGU_MRC_KEY`，不把真实值写入仓库、请求、日志或批次数据。为空时不装配后处理器。密钥以 ASCII 参数注入，协议内部使用对应 UTF-16LE 字块；不兼容常见的 32 位 XXTEA。

歌词地址发现沿用现有 Migu listen 接口。MRC、TRC、LRC URL 只在当前任务内传递，不从数据库读取历史歌词远端地址，也不增加 resourceinfo 歌词地址回退。数据库保存资源任务、处理状态及已落盘文件的路径、大小和校验值；接口没有返回某种格式时，不拼接或推测对应文件地址。

本地存储模式下，`resources/process` 的同步和异步入口均在 MRC 文件提交、原始资源数据库确认之后调用后处理器。同步入口复用当前下载 worker，异步入口把 CPU 解码、文件和同步索引工作投递到现有受限 blocking executor。每条后处理结果通过 `resourcePostProcessingCompleted` 或 `resourcePostProcessingFailed` 阶段事件报告，原始资源下载成功状态保持独立。

同步和异步下载通过 `ResourcePostProcessor` 窄 port 委托歌词处理，不直接依赖具体 MRC 解码器、导出器或文件适配器。后续拆分下载 workflow 时保留完成后的委托顺序即可，歌词业务逻辑继续由独立用例维护。

远端 rclone 复制模式没有本地来源文件，当前不装配此本地后处理器。后续增加对象存储读取 adapter 即可复用用例，不需要向 application 引入 rclone 或 HTTP 类型。

## 产物与状态

原始 `song_resources` 的 MRC 行继续表示下载结果。新增 migration `V2026102603__lyric_resource_artifacts.sql` 创建 `song_resource_artifacts`，每个来源分别登记 `word_json`、`enhanced_lrc` 的路径、大小、SHA-256、来源校验值、处理器版本及状态。状态为 0 待处理、2 完成、3 失败；此表不参与原始下载队列认领。

原始和派生歌词统一放在歌曲的 `lyrics` 目录，调用方无需计算版本目录或 hash 路径：

```text
migu/songs/{contentId}/lyrics/
  lyric.lrc
  mrc.mrc
  trc.trc
  words.json
  words.enhanced.lrc
```

两个产物先写入同文件系统的独占临时目录，保留旧文件备份后逐个替换；正常失败会恢复旧文件，无法恢复时保留临时备份并返回 `lyric_artifact_rollback_failed`。原始三种歌词不被覆盖。相同内容重试不改写文件，来源或处理版本变化时继续使用固定路径，损坏的派生文件允许重新生成。替换期间不提供跨文件的瞬时一致读取保证，读取一组产物时须核对数据库完成状态、来源版本和文件校验值。单个 adapter 实例串行化发布，多实例共享目录由调用方和资源队列认领保证互斥。

索引在两个文件完整写入之后，通过同一数据库事务登记；数据库不可用时保留文件供重试，仍返回索引失败而不报告处理成功。相同来源、版本已成功的索引不会被后续失败降级；来源变更后旧处理器结果会被数据库行锁及校验值检查拒绝。固定路径更新后索引尚未成功时，数据库校验值可能暂时与文件不同，应等待重试登记完成后再使用索引结果。

不再生成 `words.manifest.json`。来源 SHA-256、处理版本、schema 版本，以及产物的 `size_bytes`、`checksum`、`local_path` 和完成状态统一保存到 `song_resource_artifacts`；原始歌词和音频仍由 `song_resources` 保存实际大小和校验值。重处理时只移除当前歌曲目录内的旧清单，不扫描其他目录，也不删除历史长路径文件。导出器仍为 `exports-v2`，JSON schema 仍为 1，无需数据库 schema 迁移。云端归档继续使用已核对的数据库路径，新处理版本会清除旧云端位置，后续上传后再核对回写。

原始文件名继续使用 `lyric.lrc`、`mrc.mrc`、`trc.trc`。歌曲身份由 `contentId` 目录表达，文件名已能区分三种来源格式；路径规划、数据库 `local_path` 和云端 `/Media/` 路径约定均已使用这些名称。仅为统一命名风格改名会产生数据库及云端对象迁移成本，没有新增采集能力，因此保留现有名称。增强 LRC 独立使用 `words.enhanced.lrc`，避免与来源 LRC 混淆。

JSON v1 示例：

```json
{
  "schema_version": 1,
  "time_unit": "ms",
  "time_base": "absolute",
  "metadata": [{"name": "ti", "value": "示例"}],
  "lines": [{
    "start_ms": 17100,
    "duration_ms": 900,
    "words": [
      {"text": "甲", "start_ms": 17100, "duration_ms": 550},
      {"text": "乙", "start_ms": 17650, "duration_ms": 350}
    ]
  }]
}
```

重复标签、空格、标点、零时长署名和词持续时间均保留在 JSON 中。增强 LRC 使用 `[mm:ss.mmm]<mm:ss.mmm>文字`，在间隙和末词结束处追加时间标签；播放器仍须支持增强 LRC。重叠词等复杂时间信息以 JSON 为准。LRC 正文中的尖括号、方括号替换为全角字符以避免被误读为时间标签，JSON 保留原文。

零起点、零时长署名行若只给前缀标零时间，末尾未单独标时的作者名称按该行的零时间保留为独立文本片段。正常歌词存在未标时尾部时仍返回格式错误，不推测播放时间。

## 离线历史文件

构建与执行：

```powershell
.\.vscode\build-msvc-target-debug.cmd crawler_lyrics
.\build\msvc-debug\Debug\crawler_lyrics.exe "D:\migu-resource-cache" "migu/songs/{contentId}/lyrics/mrc.mrc"
```

密钥从同名环境变量读取，不作为命令行参数。可在根目录参数后列出多个相对 MRC 路径。输入路径不能为绝对路径或越过存储根目录，来源大小上限为 1 MiB。

离线程序只导出 `words.json` 和 `words.enhanced.lrc`，不生成额外元信息文件，也不连接 MySQL；hash、大小和处理版本只作为返回描述供调用方使用，不另行持久化。需要维护数据库索引的历史补齐入口应向同一 `LyricProcessingWorkflow` 注入 repository，并传入原始资源 ID 与已登记 checksum，禁止通过重置已完成下载来重取来源。当前没有新增任意文件读取 HTTP API。

## 离线验证

```powershell
.\.vscode\build-msvc-target-debug.cmd MiguTimedLyricsTest
.\.vscode\build-msvc-target-debug.cmd LyricProcessingWorkflowTest
.\.vscode\build-msvc-target-debug.cmd LocalLyricArtifactStoreTest
ctest --test-dir build/msvc-debug -R "MiguTimedLyricsTest|LyricProcessingWorkflowTest|LocalLyricArtifactStoreTest|AsyncSongResourceDownloadWorkflowTest|SongResourceDownloadWorkflowTest" --output-on-failure
```

`tests/lyrics/fixtures` 仅含人工合成歌词及固定测试密钥 `test-mrc-key` 对应的密文，不包含平台密钥或用户上传的真实歌词。真实 MySQL migration 和联网下载须在有明确授权的环境另行验证。

当前本机 clangd 的默认 `--check` 会在含 `break/continue` 的代码上报告 `ExtractFunction` 重构动作试探失败。复核编译诊断时使用 `--tweaks=DumpAST` 排除该重构动作；此选项不屏蔽 C++ 解析或类型诊断，例如：

```powershell
clangd --check=src/infrastructure/music/migu/lyrics/MiguMrcCipher.cpp --compile-commands-dir=build/msvc-debug --tweaks=DumpAST
```

## 协议研究来源

算法行为参考并通过用户提供样本核验：[MRC 解码参考实现](https://github.com/Domdkw/miguMusic-api-enhanced/blob/main/src/utils/decryptMrc.ts)。本模块独立实现 C++ 整数语义、输入验证和文件协议，不复制参考实现的格式化流程；平台参数通过配置注入。
