#pragma once

#include "crawler/application/model/MetadataResourceFile.h"
#include "crawler/application/model/MetadataCoverCloud.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::application::model {

// 封面所有者；两张资源表的数字主键必须与类型一起使用。
enum class CoverOwner { Singer,
                        Album };

// 稳定的协议类型名，不接受自由拼接的表名。
inline std::string_view coverOwnerName(CoverOwner owner)
{
    return owner == CoverOwner::Singer ? "singer" : "album";
}

// 封面队列记录；运行状态与上次成功文件回执可以同时存在。
struct MetadataCoverRecord {
    CoverOwner owner = CoverOwner::Singer; // 资源表归属
    std::uint64_t resource_id = 0;         // 对应资源表主键
    std::uint64_t source_ref_id = 0;       // 所属平台来源主键
    std::string source = "migu";           // 平台
    std::string source_id;                 // 平台歌手或规范专辑 ID
    std::string remote_url;                // 已入库的图片 URL，仅用于本次请求
    MetadataResourceFile file;             // 已提交文件与当前下载状态
    std::uint64_t claim_generation = 0;    // 当前认领代次
    std::uint64_t claimed_at = 0;          // 独立认领时间，元数据更新不能延长认领
    std::string running_batch_id;          // 当前下载批次
    bool available = false;                // 本地回执是否仍指向存在的文件
    MetadataCoverCloud cloud;              // 独立云端状态与回执
};

// 队列查询只接受来源和资源身份，不接收用户提供的 URL 或文件路径。
struct MetadataCoverQuery {
    CoverOwner owner = CoverOwner::Singer;     // 所有者类型
    std::string source = "migu";               // 平台
    std::vector<std::uint64_t> resource_ids;   // 资源 ID 筛选
    std::vector<std::uint64_t> source_ref_ids; // 目录页批量读取所需来源 ID
    std::vector<int> statuses;                 // 空表示查询全部状态
    int page = 1;                              // 页码
    int page_size = 20;                        // 单页上限 200
    std::vector<int> cloud_statuses;           // 云端状态筛选
    bool cloud_candidates = false;             // process 自动包含本地成功但云端缺失的项
    std::string cloud_backend;                 // 当前选中的云端后端
    std::string cloud_prefix;                  // 当前云端目标前缀
};

// 封面队列分页。
struct MetadataCoverPage {
    std::vector<MetadataCoverRecord> items; // 当前页记录
    std::uint64_t total = 0;                // 筛选总数
    int page = 1;                           // 页码
    int page_size = 20;                     // 每页数量
};

// 单批下载策略；刷新只对显式指定的资源生效。
struct MetadataCoverOptions {
    MetadataCoverQuery query; // 目标筛选
    int limit = 20;           // 单批最多 200 项
    int concurrency = 3;      // 后台有界并发
    int retries = 1;          // 可恢复失败的额外重试次数
    int timeout_ms = 15000;   // 单张图片总超时
    bool refresh = false;     // 指定成功项重新下载或修复本地丢失文件
};

// 下载保存上下文，用于把文件日志绑定到正确所有者及认领代次。
struct MetadataCoverSaveRequest {
    MetadataCoverRecord target;                // 已认领资源与旧文件回执
    int timeout_ms = 15000;                    // 本次总超时
    std::function<bool()> cancelled;           // 协作取消检查
    std::uint64_t max_bytes = 8 * 1024 * 1024; // 单次内容上限，不能超过既有图片上限
};

// 已验证的本地图片内容，不包含绝对路径。
struct MetadataCoverContent {
    std::string bytes;        // 图片字节
    std::string content_type; // 按实际图片签名确定的 MIME
    std::string checksum;     // 与文件回执一致的 SHA-256
};

// 单个资源下载结果，供同步响应和批次 item 复用。
struct MetadataCoverItem {
    MetadataCoverRecord record; // 所有者、回执与错误
    bool success = false;       // 是否已确认成功或复用已有文件
    bool skipped = false;       // 未认领或已有成功文件
    std::string error_category; // 本次整体处理错误，不覆盖本地文件状态
    std::string result_code;    // 本次整体结果码
    std::string last_error;     // 本次整体脱敏说明
};

} // namespace orchestrator::modules::crawler::application::model
