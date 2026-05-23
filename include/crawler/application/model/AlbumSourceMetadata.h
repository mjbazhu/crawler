#pragma once

#include <cstdint>
#include <string>

namespace orchestrator::modules::crawler::application::model {

// 专辑平台来源解析元数据，承载 albums、album_source_refs 和平台扩展表入库参数。
struct AlbumSourceMetadata {
    std::string source = "migu";           // 音乐平台标识，例如 migu
    std::string title;                     // 公共专辑标题
    std::string normalized_title;          // 用于弱匹配的标准化标题
    std::string artist_name;               // 公共主艺人名称
    std::string source_item_key;           // 平台来源唯一业务键，例如 album:600927015009001083
    std::string source_album_id;           // 平台专辑或数字专辑 ID，例如 contentId/resId
    std::string source_material_id;        // 平台扩展物料 ID，例如 Migu 数字专辑 materialId
    std::string source_resource_type;      // 详情资源类型，例如 5
    std::string source_res_type;           // 列表资源类型，例如 ZJ-Album-Item resType=1
    std::string source_action;             // 平台跳转 action
    int source_album_kind = 0;             // 平台专辑类型：0 未知，1 普通专辑，2 数字专辑
    std::string source_title;              // 平台返回的原始专辑标题
    std::string source_artist_id;          // 平台歌手 ID
    std::string source_artist_name;        // 平台返回的原始歌手名
    std::string cover_url;                 // 平台封面地址
    std::string summary;                   // 专辑简介或详情文案
    std::string price;                     // 平台价格原始值
    std::string total_count;               // 曲目总数原始值
    std::string release_date;              // 列表或详情返回的发行日期
    std::string publish_date;              // 平台发布日期
    std::string issue_date;                // 平台发行日期字段，例如 Migu issueDate
    std::string online_date;               // 平台上线时间原始值
    std::string copyright_id;              // 平台版权 ID
    std::string source_item_id;            // 平台商品项 ID，例如 Migu itemId
    std::string source_campaign_id;        // 平台活动或业务 ID，例如 Migu redHareId
    std::string presale_date;              // 数字专辑预售时间原始值
    std::string first_start_date;          // 数字专辑首发开始时间原始值
    std::string first_end_date;            // 数字专辑首发结束时间原始值
    std::string sale_end_date;             // 数字专辑销售结束时间原始值
    std::string invalidate_date;           // 数字专辑失效日期原始值
    std::string original_first_start_date; // 数字专辑原始首发开始时间
    std::string original_first_end_date;   // 数字专辑原始首发结束时间
    std::string digital_period_status;     // 数字专辑阶段状态原始值
    std::string vip_type;                  // 平台 VIP 类型原始值
    std::string vip_flag;                  // 平台 VIP 标记原始值
    std::string vip_start_time;            // VIP 开始时间原始值
    std::string vip_end_time;              // VIP 结束时间原始值
    std::string effective_status;          // 平台有效状态原始值
    std::string convert_status;            // 平台转化状态原始值
    std::string forever_listen_flag;       // 永久可听标记原始值
    int forever_listen = -1;               // 是否永久可听：-1 未知，0 否，1 是
    int is_before_sale_end = -1;           // 是否未过销售结束时间：-1 未知，0 否，1 是
    int is_in_first_date = -1;             // 是否首发期内：-1 未知，0 否，1 是
    int is_call_payment = -1;              // 是否通话业务付费：-1 未知，0 否，1 是
    int is_call_payment_for_present = -1;  // 是否支持赠送付费：-1 未知，0 否，1 是
    int is_call_payment_for_purchase = -1; // 是否支持购买付费：-1 未知，0 否，1 是
    int is_knowledge_paid = -1;            // 是否知识付费：-1 未知，0 否，1 是
    int have_filter_tag = -1;              // 是否存在过滤标签：-1 未知，0 否，1 是
    int is_convert = -1;                   // 是否已转化：-1 未知，0 否，1 是
    int is_appointment = -1;               // 是否预约：-1 未知，0 否，1 是
    std::uint64_t play_num = 0;            // 平台播放数
    std::string play_num_desc;             // 平台播放数展示文本
    int detail_sync_status = 0;            // 详情同步状态：0 未同步，1 同步中，2 已完成，3 失败，4 部分完成
};

} // namespace orchestrator::modules::crawler::application::model
