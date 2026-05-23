CREATE TABLE IF NOT EXISTS songs (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '本地歌曲主键，供平台来源、资源、音质等子表关联使用',
    title VARCHAR(512) NOT NULL COMMENT '跨平台归一后的歌曲标题',
    normalized_title VARCHAR(512) NOT NULL COMMENT '用于去重和检索的标准化标题，例如去空白、统一大小写后的标题',
    duration_seconds INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '歌曲时长，单位秒；未知时为 0',
    isrc VARCHAR(32) COMMENT '国际标准录音编码；平台未返回时为空',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录创建时间，Unix 秒时间戳',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录最后更新时间，Unix 秒时间戳',
    PRIMARY KEY (id),
    UNIQUE KEY uk_songs_identity (normalized_title, duration_seconds),
    KEY idx_songs_title (title),
    KEY idx_songs_normalized_title (normalized_title),
    KEY idx_songs_isrc (isrc)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='歌曲公共基础信息表，只保存平台无关字段';

CREATE TABLE IF NOT EXISTS song_source_refs (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '歌曲平台来源主键，供平台音质参数和资源表关联使用',
    song_db_id BIGINT UNSIGNED NOT NULL COMMENT '关联 songs.id，表示该平台来源属于哪一首公共歌曲',
    source VARCHAR(32) NOT NULL COMMENT '音乐平台标识，例如 migu、qq、netease',
    source_item_key VARCHAR(128) NOT NULL COMMENT '平台来源唯一业务键，格式建议为 content:<contentId> 或 song:<songId>，用于避免外部 ID 为空时重复入库',
    source_song_id VARCHAR(64) COMMENT '平台歌曲 ID，例如 Migu songId=1001416340',
    source_content_id VARCHAR(64) COMMENT '平台内容或资源 ID，例如 Migu contentId/resId=600907000003024375',
    source_resource_type VARCHAR(16) COMMENT '平台资源类型，例如 Migu resourceType=2',
    source_title VARCHAR(512) COMMENT '平台返回的原始歌曲标题',
    source_title_pinyin VARCHAR(512) COMMENT '平台返回的歌曲拼音或排序名，例如 Migu songPinyin',
    source_album_id VARCHAR(64) COMMENT '平台专辑 ID',
    source_album_name VARCHAR(512) COMMENT '平台返回的原始专辑名称',
    source_album_pinyin VARCHAR(512) COMMENT '平台返回的专辑拼音或排序名，例如 Migu albumPinyin',
    source_primary_artist_id VARCHAR(64) COMMENT '平台主歌手 ID；多歌手关系后续可扩展独立关系表',
    source_primary_artist_name VARCHAR(512) COMMENT '平台主歌手名称',
    cover_url TEXT COMMENT '平台封面远程地址或路径',
    lyric_url TEXT COMMENT '平台歌词远程地址',
    mrc_url TEXT COMMENT '平台逐字歌词或扩展歌词远程地址',
    copyright_id VARCHAR(64) COMMENT '平台版权 ID',
    copyright_type INT NOT NULL DEFAULT 0 COMMENT '平台版权类型；语义由具体平台适配器解释',
    restrict_type INT NOT NULL DEFAULT 0 COMMENT '平台限制类型；语义由具体平台适配器解释',
    product_authorize_usage TEXT COMMENT '平台授权使用范围原始值，例如 Migu productAuthorizeUsage',
    metadata_status TINYINT NOT NULL DEFAULT 1 COMMENT '元数据状态：0 待处理，1 已获取，2 已失效，3 失败',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录创建时间，Unix 秒时间戳',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录最后更新时间，Unix 秒时间戳',
    PRIMARY KEY (id),
    UNIQUE KEY uk_source_item (source, source_item_key),
    UNIQUE KEY uk_source_song (source, source_song_id),
    UNIQUE KEY uk_source_content (source, source_content_id),
    KEY idx_source_song_db_id (song_db_id),
    KEY idx_source_album (source, source_album_id),
    KEY idx_source_artist (source, source_primary_artist_id),
    CONSTRAINT fk_song_source_refs_song FOREIGN KEY (song_db_id) REFERENCES songs (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='歌曲平台来源表，保存不同音乐平台的外部 ID、版权和元数据入口';

CREATE TABLE IF NOT EXISTS song_source_audio_formats (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '平台音质参数主键',
    song_source_ref_id BIGINT UNSIGNED NOT NULL COMMENT '关联 song_source_refs.id，表示该音质参数属于哪个平台来源',
    format_type VARCHAR(16) NOT NULL COMMENT '平台音质档位，例如 PQ、HQ、SQ',
    resource_type VARCHAR(16) COMMENT '平台资源类型，例如 Migu resourceType=2',
    iformat VARCHAR(32) COMMENT '平台内部资源格式编码，例如 Migu iformat',
    aformat VARCHAR(32) COMMENT '平台音频格式编码，例如 Migu aformat',
    isize BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '平台返回的内部资源大小，单位字节；未知时为 0',
    asize BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '平台返回的音频资源大小，单位字节；未知时为 0',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录创建时间，Unix 秒时间戳',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录最后更新时间，Unix 秒时间戳',
    PRIMARY KEY (id),
    UNIQUE KEY uk_source_format (song_source_ref_id, format_type),
    KEY idx_audio_format_type (format_type),
    CONSTRAINT fk_song_source_audio_formats_ref FOREIGN KEY (song_source_ref_id) REFERENCES song_source_refs (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='歌曲平台音质参数表，只保存资源获取所需参数，不保存实际文件';

CREATE TABLE IF NOT EXISTS song_resources (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '资源记录主键',
    song_source_ref_id BIGINT UNSIGNED NOT NULL COMMENT '关联 song_source_refs.id，表示该资源来自哪个平台来源',
    resource_kind VARCHAR(32) NOT NULL COMMENT '资源类型，例如 audio、cover、lyric、mrc',
    format_type VARCHAR(16) NOT NULL DEFAULT '' COMMENT '音频资源的音质档位，例如 PQ、HQ；非音频资源使用空字符串',
    remote_url TEXT COMMENT '资源远程地址；可过期地址只作临时记录，稳定重建参数应保存在来源和音质参数表',
    local_path TEXT COMMENT '资源保存到本地文件系统后的相对或绝对路径',
    size_bytes BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '本地已保存资源大小，单位字节；未知或未下载时为 0',
    checksum VARCHAR(128) COMMENT '本地资源校验值，例如 SHA-256；未计算时为空',
    status TINYINT NOT NULL DEFAULT 0 COMMENT '资源状态：0 待获取，1 获取中，2 已完成，3 失败，4 跳过',
    retry_count INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '资源获取重试次数',
    last_error TEXT COMMENT '最近一次资源获取失败原因，需脱敏后写入',
    fetched_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '资源最近一次成功获取时间，Unix 秒时间戳',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录创建时间，Unix 秒时间戳',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录最后更新时间，Unix 秒时间戳',
    PRIMARY KEY (id),
    UNIQUE KEY uk_resource (song_source_ref_id, resource_kind, format_type),
    KEY idx_resource_status (status),
    KEY idx_resource_kind (resource_kind),
    KEY idx_resource_fetch_order (status, retry_count, updated_at),
    CONSTRAINT fk_song_resources_ref FOREIGN KEY (song_source_ref_id) REFERENCES song_source_refs (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='歌曲资源表，保存封面、歌词、音频等资源的获取任务和本地保存结果';

CREATE TABLE IF NOT EXISTS singers (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '本地歌手主键，供歌曲、专辑和平台来源表关联使用',
    name VARCHAR(512) NOT NULL COMMENT '跨平台归一后的歌手名称，例如 周杰伦',
    normalized_name VARCHAR(512) NOT NULL DEFAULT '' COMMENT '用于检索和弱匹配的标准化歌手名称',
    name_pinyin VARCHAR(512) COMMENT '歌手拼音或排序名，平台未返回时为空',
    gender INT NOT NULL DEFAULT 0 COMMENT '性别：0 未知，1 男，2 女，3 组合',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录创建时间，Unix 秒时间戳',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录最后更新时间，Unix 秒时间戳',
    status TINYINT NOT NULL DEFAULT 1 COMMENT '状态：0 待处理，1 正常，2 失效，3 失败',
    PRIMARY KEY (id),
    KEY idx_singers_name (name),
    KEY idx_singers_normalized_name (normalized_name),
    KEY idx_singers_status (status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='歌手公共基础信息表，只保存平台无关字段';

CREATE TABLE IF NOT EXISTS singer_source_refs (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '歌手平台来源主键',
    singer_db_id BIGINT UNSIGNED NOT NULL COMMENT '关联 singers.id，表示该平台来源属于哪个公共歌手',
    source VARCHAR(32) NOT NULL COMMENT '音乐平台标识，例如 migu、qq、netease',
    source_item_key VARCHAR(128) NOT NULL COMMENT '平台来源唯一业务键，例如 singer:112',
    source_singer_id VARCHAR(64) NOT NULL COMMENT '平台歌手 ID，例如 Migu resId=112',
    source_view_id VARCHAR(128) COMMENT '平台 viewId，例如 4002-112',
    source_res_type VARCHAR(32) COMMENT '平台资源类型，例如 Migu resType=2002',
    source_action TEXT COMMENT '平台跳转 action，例如 mgmusic://singer-info?id=112',
    source_track VARCHAR(128) COMMENT '平台埋点或来源标识，例如 gsgc_flgs',
    source_name VARCHAR(512) COMMENT '平台返回的原始歌手名称',
    source_display_id VARCHAR(64) COMMENT '平台展示 ID，例如 Migu txt2=112',
    source_tag VARCHAR(128) COMMENT '平台标签或歌手类型，例如 Migu txt3=热',
    source_metric VARCHAR(128) COMMENT '平台热度、播放量或数量原始值，例如 Migu txt4=5959566',
    source_country VARCHAR(128) COMMENT '平台返回的国家或地区，例如 华语、欧美、日韩',
    source_gender INT NOT NULL DEFAULT 0 COMMENT '平台分类推导出的性别：0 未知，1 男，2 女，3 组合',
    avatar_url TEXT COMMENT '平台头像地址',
    metadata_status TINYINT NOT NULL DEFAULT 1 COMMENT '歌手元数据状态：0 待处理，1 已获取，2 已失效，3 失败',
    songs_sync_status TINYINT NOT NULL DEFAULT 0 COMMENT '歌手歌曲列表同步状态：0 未同步，1 同步中，2 已完成，3 失败，4 部分完成',
    songs_synced_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '歌手歌曲列表最近一次同步完成时间，Unix 秒时间戳',
    songs_sync_count INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '最近一次同步到的歌曲数量',
    songs_sync_error TEXT COMMENT '歌手歌曲列表最近一次同步失败原因，需脱敏',
    songs_claim_token VARCHAR(128) NOT NULL DEFAULT '' COMMENT '歌手歌曲同步最近一次认领令牌',
    songs_claimed_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '歌手歌曲同步最近一次认领时间，Unix 秒时间戳',
    songs_claim_owner VARCHAR(128) NOT NULL DEFAULT '' COMMENT '歌手歌曲同步最近一次认领方',
    albums_sync_status TINYINT NOT NULL DEFAULT 0 COMMENT '歌手专辑列表同步状态：0 未同步，1 同步中，2 已完成，3 失败，4 部分完成',
    albums_synced_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '歌手专辑列表最近一次同步完成时间，Unix 秒时间戳',
    albums_sync_count INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '最近一次同步到的专辑数量',
    albums_sync_error TEXT COMMENT '歌手专辑列表最近一次同步失败原因，需脱敏',
    albums_claim_token VARCHAR(128) NOT NULL DEFAULT '' COMMENT '歌手专辑同步最近一次认领令牌',
    albums_claimed_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '歌手专辑同步最近一次认领时间，Unix 秒时间戳',
    albums_claim_owner VARCHAR(128) NOT NULL DEFAULT '' COMMENT '歌手专辑同步最近一次认领方',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录创建时间，Unix 秒时间戳',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录最后更新时间，Unix 秒时间戳',
    PRIMARY KEY (id),
    UNIQUE KEY uk_singer_source_item (source, source_item_key),
    UNIQUE KEY uk_singer_source_id (source, source_singer_id),
    KEY idx_singer_source_db_id (singer_db_id),
    KEY idx_singer_source_name (source, source_name),
    KEY idx_singer_songs_sync (source, songs_sync_status, updated_at),
    KEY idx_singer_albums_sync (source, albums_sync_status, updated_at),
    KEY idx_singer_songs_queue (source, songs_sync_status, source_country, source_gender, updated_at),
    KEY idx_singer_albums_queue (source, albums_sync_status, source_country, source_gender, updated_at),
    KEY idx_singer_songs_claim (source, songs_claim_token),
    KEY idx_singer_albums_claim (source, albums_claim_token),
    CONSTRAINT fk_singer_source_refs_singer FOREIGN KEY (singer_db_id) REFERENCES singers (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='歌手平台来源表，保存平台外部 ID、头像、标签和歌曲/专辑同步状态';

CREATE TABLE IF NOT EXISTS albums (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '本地专辑主键，供平台来源表关联使用',
    title VARCHAR(512) NOT NULL COMMENT '跨平台归一后的专辑标题',
    normalized_title VARCHAR(512) NOT NULL DEFAULT '' COMMENT '用于去重和检索的标准化专辑标题',
    primary_artist_name VARCHAR(512) NOT NULL DEFAULT '' COMMENT '跨平台主艺人名称；未知时为空字符串',
    release_date VARCHAR(32) NOT NULL DEFAULT '' COMMENT '公开发行日期，使用可跨平台比较的原始日期文本；未知时为空字符串',
    identity_hash CHAR(64) NOT NULL COMMENT '专辑去重键 SHA-256，来源于 normalized_title、primary_artist_name 和 release_date',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录创建时间，Unix 秒时间戳',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录最后更新时间，Unix 秒时间戳',
    PRIMARY KEY (id),
    UNIQUE KEY uk_albums_identity_hash (identity_hash),
    KEY idx_albums_title (title),
    KEY idx_albums_normalized_title (normalized_title),
    KEY idx_albums_artist (primary_artist_name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='专辑公共基础信息表，只保存平台无关字段';

CREATE TABLE IF NOT EXISTS album_source_refs (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '专辑平台来源主键',
    album_db_id BIGINT UNSIGNED NOT NULL COMMENT '关联 albums.id，表示该平台来源属于哪个公共专辑',
    source VARCHAR(32) NOT NULL COMMENT '音乐平台标识，例如 migu、qq、netease',
    source_item_key VARCHAR(128) NOT NULL COMMENT '平台来源唯一业务键，例如 album:600927015009001083',
    source_album_id VARCHAR(64) NOT NULL COMMENT '平台专辑或数字专辑 ID，例如 Migu contentId/resId',
    source_resource_type VARCHAR(16) COMMENT '详情资源类型，例如 Migu resourceType=5',
    source_res_type VARCHAR(32) COMMENT '列表资源类型，例如 Migu resType=1',
    source_action TEXT COMMENT '平台跳转 action，例如 mgmusic://digital-album-info?id=...',
    source_album_kind TINYINT NOT NULL DEFAULT 0 COMMENT '平台专辑类型：0 未知，1 普通专辑，2 数字专辑',
    source_title VARCHAR(512) COMMENT '平台返回的原始专辑标题',
    source_artist_id VARCHAR(64) COMMENT '平台主歌手 ID',
    source_artist_name VARCHAR(512) COMMENT '平台返回的原始歌手名称',
    cover_url TEXT COMMENT '平台封面远程地址',
    summary MEDIUMTEXT COMMENT '平台专辑简介或详情文案',
    total_count VARCHAR(64) COMMENT '平台曲目总数原始值',
    release_date VARCHAR(32) COMMENT '发行日期或首发日期原始值',
    publish_date VARCHAR(32) COMMENT '平台发布日期原始值',
    play_num BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '平台播放数',
    play_num_desc VARCHAR(64) COMMENT '平台播放数展示文本',
    detail_sync_status TINYINT NOT NULL DEFAULT 0 COMMENT '专辑详情同步状态：0 未同步，1 同步中，2 已完成，3 失败，4 部分完成',
    detail_synced_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '专辑详情最近一次同步完成时间，Unix 秒时间戳',
    detail_sync_error TEXT COMMENT '专辑详情最近一次同步失败原因，需脱敏',
    detail_claim_token VARCHAR(128) NOT NULL DEFAULT '' COMMENT '专辑详情同步最近一次认领令牌',
    detail_claimed_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '专辑详情同步最近一次认领时间，Unix 秒时间戳',
    detail_claim_owner VARCHAR(128) NOT NULL DEFAULT '' COMMENT '专辑详情同步最近一次认领方',
    songs_sync_status TINYINT NOT NULL DEFAULT 0 COMMENT '专辑歌曲列表同步状态：0 未同步，1 同步中，2 已完成，3 失败，4 部分完成',
    songs_synced_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '专辑歌曲列表最近一次同步完成时间，Unix 秒时间戳',
    songs_sync_count INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '最近一次同步到的专辑歌曲数量',
    songs_sync_error TEXT COMMENT '专辑歌曲列表最近一次同步失败原因，需脱敏',
    songs_claim_token VARCHAR(128) NOT NULL DEFAULT '' COMMENT '专辑歌曲同步最近一次认领令牌',
    songs_claimed_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '专辑歌曲同步最近一次认领时间，Unix 秒时间戳',
    songs_claim_owner VARCHAR(128) NOT NULL DEFAULT '' COMMENT '专辑歌曲同步最近一次认领方',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录创建时间，Unix 秒时间戳',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录最后更新时间，Unix 秒时间戳',
    PRIMARY KEY (id),
    UNIQUE KEY uk_album_source_item (source, source_item_key),
    UNIQUE KEY uk_album_source_id (source, source_album_id),
    KEY idx_album_source_db_id (album_db_id),
    KEY idx_album_source_artist (source, source_artist_id),
    KEY idx_album_detail_sync (source, detail_sync_status, updated_at),
    KEY idx_album_songs_sync (source, songs_sync_status, updated_at),
    KEY idx_album_source_kind (source, source_album_kind, detail_sync_status),
    KEY idx_album_detail_claim (source, detail_claim_token),
    KEY idx_album_songs_claim (source, songs_claim_token),
    CONSTRAINT fk_album_source_refs_album FOREIGN KEY (album_db_id) REFERENCES albums (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='专辑平台来源表，保存平台专辑 ID、详情和后续资源获取参数';

SET @crawler_schema_name := DATABASE();
SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'album_source_refs'
          AND column_name = 'detail_sync_status'
    ),
    'SELECT 1',
    'ALTER TABLE album_source_refs ADD COLUMN detail_sync_status TINYINT NOT NULL DEFAULT 0 COMMENT ''专辑详情同步状态：0 未同步，1 同步中，2 已完成，3 失败，4 部分完成'' AFTER play_num_desc'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singer_source_refs'
          AND column_name = 'songs_claim_token'
    ),
    'SELECT 1',
    'ALTER TABLE singer_source_refs ADD COLUMN songs_claim_token VARCHAR(128) NOT NULL DEFAULT '''' COMMENT ''歌手歌曲同步最近一次认领令牌'' AFTER songs_sync_error'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singer_source_refs'
          AND column_name = 'songs_claimed_at'
    ),
    'SELECT 1',
    'ALTER TABLE singer_source_refs ADD COLUMN songs_claimed_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''歌手歌曲同步最近一次认领时间，Unix 秒时间戳'' AFTER songs_claim_token'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singer_source_refs'
          AND column_name = 'songs_claim_owner'
    ),
    'SELECT 1',
    'ALTER TABLE singer_source_refs ADD COLUMN songs_claim_owner VARCHAR(128) NOT NULL DEFAULT '''' COMMENT ''歌手歌曲同步最近一次认领方'' AFTER songs_claimed_at'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singer_source_refs'
          AND column_name = 'albums_claim_token'
    ),
    'SELECT 1',
    'ALTER TABLE singer_source_refs ADD COLUMN albums_claim_token VARCHAR(128) NOT NULL DEFAULT '''' COMMENT ''歌手专辑同步最近一次认领令牌'' AFTER albums_sync_error'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singer_source_refs'
          AND column_name = 'albums_claimed_at'
    ),
    'SELECT 1',
    'ALTER TABLE singer_source_refs ADD COLUMN albums_claimed_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''歌手专辑同步最近一次认领时间，Unix 秒时间戳'' AFTER albums_claim_token'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singer_source_refs'
          AND column_name = 'albums_claim_owner'
    ),
    'SELECT 1',
    'ALTER TABLE singer_source_refs ADD COLUMN albums_claim_owner VARCHAR(128) NOT NULL DEFAULT '''' COMMENT ''歌手专辑同步最近一次认领方'' AFTER albums_claimed_at'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'album_source_refs'
          AND column_name = 'detail_synced_at'
    ),
    'SELECT 1',
    'ALTER TABLE album_source_refs ADD COLUMN detail_synced_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''专辑详情最近一次同步完成时间，Unix 秒时间戳'' AFTER detail_sync_status'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'album_source_refs'
          AND column_name = 'detail_sync_error'
    ),
    'SELECT 1',
    'ALTER TABLE album_source_refs ADD COLUMN detail_sync_error TEXT COMMENT ''专辑详情最近一次同步失败原因，需脱敏'' AFTER detail_synced_at'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'album_source_refs'
          AND column_name = 'songs_sync_status'
    ),
    'SELECT 1',
    'ALTER TABLE album_source_refs ADD COLUMN songs_sync_status TINYINT NOT NULL DEFAULT 0 COMMENT ''专辑歌曲列表同步状态：0 未同步，1 同步中，2 已完成，3 失败，4 部分完成'' AFTER detail_sync_error'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'album_source_refs'
          AND column_name = 'songs_synced_at'
    ),
    'SELECT 1',
    'ALTER TABLE album_source_refs ADD COLUMN songs_synced_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''专辑歌曲列表最近一次同步完成时间，Unix 秒时间戳'' AFTER songs_sync_status'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'album_source_refs'
          AND column_name = 'songs_sync_count'
    ),
    'SELECT 1',
    'ALTER TABLE album_source_refs ADD COLUMN songs_sync_count INT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''最近一次同步到的专辑歌曲数量'' AFTER songs_synced_at'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'album_source_refs'
          AND column_name = 'songs_sync_error'
    ),
    'SELECT 1',
    'ALTER TABLE album_source_refs ADD COLUMN songs_sync_error TEXT COMMENT ''专辑歌曲列表最近一次同步失败原因，需脱敏'' AFTER songs_sync_count'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'album_source_refs'
          AND column_name = 'detail_claim_token'
    ),
    'SELECT 1',
    'ALTER TABLE album_source_refs ADD COLUMN detail_claim_token VARCHAR(128) NOT NULL DEFAULT '''' COMMENT ''专辑详情同步最近一次认领令牌'' AFTER detail_sync_error'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'album_source_refs'
          AND column_name = 'detail_claimed_at'
    ),
    'SELECT 1',
    'ALTER TABLE album_source_refs ADD COLUMN detail_claimed_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''专辑详情同步最近一次认领时间，Unix 秒时间戳'' AFTER detail_claim_token'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'album_source_refs'
          AND column_name = 'detail_claim_owner'
    ),
    'SELECT 1',
    'ALTER TABLE album_source_refs ADD COLUMN detail_claim_owner VARCHAR(128) NOT NULL DEFAULT '''' COMMENT ''专辑详情同步最近一次认领方'' AFTER detail_claimed_at'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'album_source_refs'
          AND column_name = 'songs_claim_token'
    ),
    'SELECT 1',
    'ALTER TABLE album_source_refs ADD COLUMN songs_claim_token VARCHAR(128) NOT NULL DEFAULT '''' COMMENT ''专辑歌曲同步最近一次认领令牌'' AFTER songs_sync_error'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'album_source_refs'
          AND column_name = 'songs_claimed_at'
    ),
    'SELECT 1',
    'ALTER TABLE album_source_refs ADD COLUMN songs_claimed_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''专辑歌曲同步最近一次认领时间，Unix 秒时间戳'' AFTER songs_claim_token'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'album_source_refs'
          AND column_name = 'songs_claim_owner'
    ),
    'SELECT 1',
    'ALTER TABLE album_source_refs ADD COLUMN songs_claim_owner VARCHAR(128) NOT NULL DEFAULT '''' COMMENT ''专辑歌曲同步最近一次认领方'' AFTER songs_claimed_at'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singer_source_refs'
          AND index_name = 'idx_singer_songs_claim'
    ),
    'SELECT 1',
    'ALTER TABLE singer_source_refs ADD KEY idx_singer_songs_claim (source, songs_claim_token)'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'singer_source_refs'
          AND index_name = 'idx_singer_albums_claim'
    ),
    'SELECT 1',
    'ALTER TABLE singer_source_refs ADD KEY idx_singer_albums_claim (source, albums_claim_token)'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'album_source_refs'
          AND index_name = 'idx_album_detail_claim'
    ),
    'SELECT 1',
    'ALTER TABLE album_source_refs ADD KEY idx_album_detail_claim (source, detail_claim_token)'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'album_source_refs'
          AND index_name = 'idx_album_songs_claim'
    ),
    'SELECT 1',
    'ALTER TABLE album_source_refs ADD KEY idx_album_songs_claim (source, songs_claim_token)'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

CREATE TABLE IF NOT EXISTS migu_dalbum_details (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT 'Migu 数字专辑详情主键',
    album_source_ref_id BIGINT UNSIGNED NOT NULL COMMENT '关联 album_source_refs.id',
    source_album_id VARCHAR(64) NOT NULL COMMENT 'Migu 数字专辑 ID，例如 dAlbumId/contentId',
    source_material_id VARCHAR(64) COMMENT 'Migu 物料 ID，例如 materialId',
    copyright_id VARCHAR(64) COMMENT 'Migu 版权 ID',
    price VARCHAR(64) COMMENT 'Migu 价格原始值',
    online_date VARCHAR(32) COMMENT 'Migu 上线时间原始值',
    source_item_id VARCHAR(64) COMMENT 'Migu 商品项 ID，例如 itemId',
    source_campaign_id VARCHAR(64) COMMENT 'Migu 活动或业务 ID，例如 redHareId',
    presale_date VARCHAR(32) COMMENT '预售时间原始值',
    first_start_date VARCHAR(32) COMMENT '首发开始时间原始值',
    first_end_date VARCHAR(32) COMMENT '首发结束时间原始值',
    sale_end_date VARCHAR(32) COMMENT '销售结束时间原始值',
    invalidate_date VARCHAR(32) COMMENT '失效日期原始值',
    original_first_start_date VARCHAR(32) COMMENT '原始首发开始时间',
    original_first_end_date VARCHAR(32) COMMENT '原始首发结束时间',
    digital_period_status VARCHAR(32) COMMENT '数字专辑阶段状态原始值',
    vip_type VARCHAR(32) COMMENT 'VIP 类型原始值',
    vip_flag VARCHAR(32) COMMENT 'VIP 标记原始值',
    vip_start_time VARCHAR(32) COMMENT 'VIP 开始时间原始值',
    vip_end_time VARCHAR(32) COMMENT 'VIP 结束时间原始值',
    effective_status VARCHAR(32) COMMENT '有效状态原始值',
    convert_status VARCHAR(32) COMMENT '转化状态原始值',
    forever_listen_flag VARCHAR(32) COMMENT '永久可听标记原始值',
    forever_listen TINYINT NOT NULL DEFAULT -1 COMMENT '是否永久可听：-1 未知，0 否，1 是',
    is_before_sale_end TINYINT NOT NULL DEFAULT -1 COMMENT '是否未过销售结束时间：-1 未知，0 否，1 是',
    is_in_first_date TINYINT NOT NULL DEFAULT -1 COMMENT '是否首发期内：-1 未知，0 否，1 是',
    is_call_payment TINYINT NOT NULL DEFAULT -1 COMMENT '是否通话业务付费：-1 未知，0 否，1 是',
    is_call_payment_for_present TINYINT NOT NULL DEFAULT -1 COMMENT '是否支持赠送付费：-1 未知，0 否，1 是',
    is_call_payment_for_purchase TINYINT NOT NULL DEFAULT -1 COMMENT '是否支持购买付费：-1 未知，0 否，1 是',
    is_knowledge_paid TINYINT NOT NULL DEFAULT -1 COMMENT '是否知识付费：-1 未知，0 否，1 是',
    have_filter_tag TINYINT NOT NULL DEFAULT -1 COMMENT '是否存在过滤标签：-1 未知，0 否，1 是',
    is_convert TINYINT NOT NULL DEFAULT -1 COMMENT '是否已转化：-1 未知，0 否，1 是',
    is_appointment TINYINT NOT NULL DEFAULT -1 COMMENT '是否预约：-1 未知，0 否，1 是',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录创建时间，Unix 秒时间戳',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录最后更新时间，Unix 秒时间戳',
    PRIMARY KEY (id),
    UNIQUE KEY uk_migu_dalbum_source_ref (album_source_ref_id),
    UNIQUE KEY uk_migu_dalbum_source_album (source_album_id),
    KEY idx_migu_dalbum_sale (is_before_sale_end, sale_end_date),
    KEY idx_migu_dalbum_period (digital_period_status, first_start_date),
    CONSTRAINT fk_migu_dalbum_source_ref FOREIGN KEY (album_source_ref_id) REFERENCES album_source_refs (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Migu 数字专辑专有详情表，保存售卖期、VIP、永久可听和付费状态';

CREATE TABLE IF NOT EXISTS crawl_batches (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '采集批次本地主键',
    batch_id VARCHAR(128) NOT NULL COMMENT '批次业务 ID，返回给前端轮询和停止任务使用',
    source VARCHAR(32) NOT NULL DEFAULT 'migu' COMMENT '音乐平台标识，例如 migu',
    task_type VARCHAR(64) NOT NULL COMMENT '任务类型：singerIndex、singerSongs、singerAlbums、albumDetails、albumSongs',
    state VARCHAR(32) NOT NULL DEFAULT 'Pending' COMMENT '批次状态：Pending、Running、Completed、Failed、Cancelled',
    request_params JSON COMMENT '前端请求参数快照，便于刷新后复原任务上下文',
    settings_snapshot JSON COMMENT '任务创建时的运行设置快照',
    limit_count INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '本批次每个目标或补池的最大处理数量',
    target_count INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '批次目标数量；补池任务为实际选中的专辑数量',
    processed_count INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '已处理目标数量',
    success_count INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '成功目标数量',
    failure_count INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '失败目标数量',
    cancel_requested TINYINT NOT NULL DEFAULT 0 COMMENT '是否已请求取消：0 否，1 是',
    last_error TEXT COMMENT '批次最近错误，需脱敏',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '创建时间，Unix 秒时间戳',
    started_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '开始时间，Unix 秒时间戳',
    finished_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '结束时间，Unix 秒时间戳；0 表示未结束',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '更新时间，Unix 秒时间戳',
    PRIMARY KEY (id),
    UNIQUE KEY uk_crawl_batches_batch_id (batch_id),
    KEY idx_crawl_batches_state (state, updated_at),
    KEY idx_crawl_batches_type (source, task_type, updated_at),
    KEY idx_crawl_batches_created_at (created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='采集批次主表，持久化后台任务状态，支持服务重启后查询';

CREATE TABLE IF NOT EXISTS crawl_batch_items (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '采集批次目标本地主键',
    batch_id VARCHAR(128) NOT NULL COMMENT '关联 crawl_batches.batch_id',
    item_index INT UNSIGNED NOT NULL COMMENT '目标在批次内的顺序，从 0 开始',
    item_key VARCHAR(256) NOT NULL COMMENT '目标业务键，例如 singer:112、album:600927015009001083',
    item_type VARCHAR(64) NOT NULL COMMENT '目标类型：singerIndexTab、singerSongs、singerAlbums、albumDetail、albumSongs',
    item_label VARCHAR(512) COMMENT '前端展示名称，例如歌手 ID、分类 tab、专辑标题',
    source VARCHAR(32) NOT NULL DEFAULT 'migu' COMMENT '音乐平台标识，例如 migu',
    source_singer_id VARCHAR(64) COMMENT '平台歌手 ID',
    source_album_id VARCHAR(64) COMMENT '平台专辑 ID',
    source_song_id VARCHAR(64) COMMENT '平台歌曲 ID；当前预留给后续资源任务',
    state VARCHAR(32) NOT NULL DEFAULT 'Pending' COMMENT '目标状态：Pending、Running、Completed、Failed、Cancelled',
    retry_count INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '目标已重试次数',
    written_count INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '该目标写入或解析出的记录数量；未知时为 0',
    error_category VARCHAR(64) COMMENT '失败分类：network、parse、persistence、copyright、resource、invalid_target、timeout、unknown',
    result_code VARCHAR(64) COMMENT '结果编码：ok、network_failed、parse_failed、persist_failed、partial_completed',
    started_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '目标开始时间，Unix 秒时间戳',
    finished_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '目标结束时间，Unix 秒时间戳；0 表示未结束',
    last_error TEXT COMMENT '目标最近失败原因，需脱敏',
    payload JSON COMMENT '目标额外上下文，例如 source_res_type、source_album_kind',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '创建时间，Unix 秒时间戳',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '更新时间，Unix 秒时间戳',
    PRIMARY KEY (id),
    UNIQUE KEY uk_crawl_batch_item_index (batch_id, item_index),
    KEY idx_crawl_batch_items_batch_state (batch_id, state, item_index),
    KEY idx_crawl_batch_items_type_state (source, item_type, state, updated_at),
    KEY idx_crawl_batch_items_singer (source, source_singer_id),
    KEY idx_crawl_batch_items_album (source, source_album_id),
    CONSTRAINT fk_crawl_batch_items_batch FOREIGN KEY (batch_id) REFERENCES crawl_batches (batch_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='采集批次目标表，记录每个歌手、分类或专辑的执行状态';

SET @crawler_schema_name := DATABASE();
SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'crawl_batch_items'
          AND column_name = 'error_category'
    ),
    'SELECT 1',
    'ALTER TABLE crawl_batch_items ADD COLUMN error_category VARCHAR(64) COMMENT ''失败分类：network、parse、persistence、copyright、resource、invalid_target、timeout、unknown'' AFTER written_count'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

SET @crawler_alter_sql := IF(
    EXISTS(
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = @crawler_schema_name
          AND table_name = 'crawl_batch_items'
          AND column_name = 'result_code'
    ),
    'SELECT 1',
    'ALTER TABLE crawl_batch_items ADD COLUMN result_code VARCHAR(64) COMMENT ''结果编码：ok、network_failed、parse_failed、persist_failed、partial_completed'' AFTER error_category'
);
PREPARE crawler_alter_stmt FROM @crawler_alter_sql;
EXECUTE crawler_alter_stmt;
DEALLOCATE PREPARE crawler_alter_stmt;

CREATE TABLE IF NOT EXISTS crawl_batch_events (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '采集事件主键',
    batch_id VARCHAR(128) NOT NULL COMMENT '关联 crawl_batches.batch_id',
    item_index INT UNSIGNED COMMENT '关联批次内目标序号；批次级事件为空',
    event_type VARCHAR(64) NOT NULL COMMENT '事件类型：batchStarted、itemStarted、itemCompleted、itemFailed、cancelRequested、timeout、batchFinished',
    message TEXT COMMENT '事件描述，需脱敏',
    payload JSON COMMENT '事件附加上下文',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '事件创建时间，Unix 秒时间戳',
    PRIMARY KEY (id),
    KEY idx_crawl_batch_events_batch (batch_id, id),
    KEY idx_crawl_batch_events_type (event_type, created_at),
    CONSTRAINT fk_crawl_batch_events_batch FOREIGN KEY (batch_id) REFERENCES crawl_batches (batch_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='采集批次事件流，用于前端查看任务生命周期与失败过程';

CREATE TABLE IF NOT EXISTS song_artist_refs (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '歌曲歌手关系主键',
    song_source_ref_id BIGINT UNSIGNED NOT NULL COMMENT '关联 song_source_refs.id，表示该歌手关系属于哪个平台歌曲来源',
    source VARCHAR(32) NOT NULL COMMENT '音乐平台标识，例如 migu、qq、netease',
    source_artist_id VARCHAR(64) COMMENT '平台歌手 ID',
    source_artist_name VARCHAR(512) NOT NULL DEFAULT '' COMMENT '平台歌手名称',
    source_artist_image TEXT COMMENT '平台歌手头像',
    source_name_spelling VARCHAR(512) NOT NULL DEFAULT '' COMMENT '平台歌手拼音或排序名',
    artist_role VARCHAR(64) NOT NULL DEFAULT 'artist' COMMENT '歌手角色，当前 singerList 统一为 artist',
    artist_order INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '歌手在平台返回列表中的顺序，从 1 开始；未知时为 0',
    is_primary TINYINT NOT NULL DEFAULT 0 COMMENT '是否主歌手：0 否，1 是',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录创建时间，Unix 秒时间戳',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录最后更新时间，Unix 秒时间戳',
    PRIMARY KEY (id),
    UNIQUE KEY uk_song_artist_order (song_source_ref_id, artist_role, artist_order),
    KEY idx_song_artist_source_id (source, source_artist_id),
    KEY idx_song_artist_name (source, source_artist_name),
    CONSTRAINT fk_song_artist_refs_song_source FOREIGN KEY (song_source_ref_id) REFERENCES song_source_refs (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='歌曲与平台歌手关系表，支持多歌手、合唱和按来源歌手查询歌曲';

CREATE TABLE IF NOT EXISTS album_song_refs (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '专辑歌曲关系主键',
    album_source_ref_id BIGINT UNSIGNED COMMENT '关联 album_source_refs.id；专辑来源尚未入库时为空',
    song_source_ref_id BIGINT UNSIGNED NOT NULL COMMENT '关联 song_source_refs.id，表示该曲目对应的平台歌曲来源',
    source VARCHAR(32) NOT NULL COMMENT '音乐平台标识，例如 migu、qq、netease',
    source_album_id VARCHAR(64) NOT NULL DEFAULT '' COMMENT '平台专辑 ID',
    source_song_id VARCHAR(64) COMMENT '平台歌曲 ID',
    source_content_id VARCHAR(64) COMMENT '平台内容或资源 ID',
    disc_no INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Disc 序号，未知时为 0',
    disc_name VARCHAR(128) NOT NULL DEFAULT '' COMMENT 'Disc 原始文本，例如 Disc 1',
    track_no INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '专辑内曲序，未知时为 0',
    song_order INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '平台返回歌曲顺序，从 1 开始；未知时为 0',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录创建时间，Unix 秒时间戳',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录最后更新时间，Unix 秒时间戳',
    PRIMARY KEY (id),
    UNIQUE KEY uk_album_song_source_ref (source, source_album_id, song_source_ref_id),
    KEY idx_album_song_album (source, source_album_id, disc_no, track_no, song_order),
    KEY idx_album_song_song (song_source_ref_id),
    CONSTRAINT fk_album_song_refs_album_source FOREIGN KEY (album_source_ref_id) REFERENCES album_source_refs (id) ON DELETE SET NULL,
    CONSTRAINT fk_album_song_refs_song_source FOREIGN KEY (song_source_ref_id) REFERENCES song_source_refs (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='专辑与平台歌曲关系表，保存 Disc、曲序和专辑内顺序';

CREATE TABLE IF NOT EXISTS crawler_schema_versions (
    component VARCHAR(64) NOT NULL COMMENT 'schema 组件名，例如 crawler_mysql',
    schema_version BIGINT UNSIGNED NOT NULL COMMENT 'schema 版本号，使用 YYYYMMDDNN 格式递增',
    schema_name VARCHAR(128) NOT NULL DEFAULT '' COMMENT 'schema 名称或说明',
    applied_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '版本写入时间，Unix 秒时间戳',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录更新时间，Unix 秒时间戳',
    PRIMARY KEY (component)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Crawler MySQL schema 版本表';

INSERT INTO crawler_schema_versions (
    component,
    schema_version,
    schema_name,
    applied_at,
    updated_at
)
VALUES (
    'crawler_mysql',
    2026052001,
    'migu metadata catalog with song artist and album song refs',
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
)
ON DUPLICATE KEY UPDATE
    schema_version = GREATEST(schema_version, VALUES(schema_version)),
    schema_name = VALUES(schema_name),
    updated_at = VALUES(updated_at);
