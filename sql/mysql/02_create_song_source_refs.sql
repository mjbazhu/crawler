CREATE TABLE IF NOT EXISTS song_source_refs (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '歌曲平台来源主键，供平台音质参数和资源表关联使用',
    song_db_id BIGINT UNSIGNED NOT NULL COMMENT '关联 songs.id，表示该平台来源属于哪一首公共歌曲',

    source VARCHAR(32) NOT NULL COMMENT '音乐平台标识，例如 migu、qq、netease',
    source_item_key VARCHAR(128) NOT NULL COMMENT '平台来源唯一业务键，格式建议为 content:<contentId> 或 song:<songId>，用于避免外部 ID 为空时重复入库',
    source_song_id VARCHAR(64) COMMENT '平台歌曲 ID，例如 Migu songId=1001416340',
    source_content_id VARCHAR(64) COMMENT '平台内容或资源 ID，例如 Migu contentId/resId=600907000003024375',
    source_resource_type VARCHAR(16) COMMENT '平台资源类型，例如 Migu resourceType=2',

    source_title_pinyin VARCHAR(512) COMMENT '平台返回的歌曲拼音或排序名，例如 Migu songPinyin',
    source_duration_seconds INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '平台返回的歌曲时长，单位秒；不同平台可不一致',
    source_album_id VARCHAR(64) COMMENT '平台专辑 ID',
    source_album_name VARCHAR(512) COMMENT '平台返回的原始专辑名称',
    source_album_pinyin VARCHAR(512) COMMENT '平台返回的专辑拼音或排序名，例如 Migu albumPinyin',

    cover_url TEXT COMMENT '平台封面远程地址或路径',
    lyric_url TEXT COMMENT '平台歌词远程地址',
    mrc_url TEXT COMMENT '平台逐字歌词或扩展歌词远程地址',
    trc_url TEXT COMMENT '平台翻译歌词或扩展歌词远程地址',

    copyright_id VARCHAR(64) COMMENT '平台版权 ID',

    metadata_status TINYINT NOT NULL DEFAULT 1 COMMENT '元数据状态：0 待处理，1 已获取，2 已失效，3 失败',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录创建时间，Unix 秒时间戳',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录最后更新时间，Unix 秒时间戳',

    PRIMARY KEY (id),
    UNIQUE KEY uk_source_item (source, source_item_key),
    UNIQUE KEY uk_source_song (source, source_song_id),
    UNIQUE KEY uk_source_content (source, source_content_id),
    KEY idx_source_song_db_id (song_db_id),
    KEY idx_source_album (source, source_album_id),
    CONSTRAINT fk_song_source_refs_song FOREIGN KEY (song_db_id) REFERENCES songs (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='歌曲平台来源表，保存不同音乐平台的外部 ID、版权和元数据入口';
