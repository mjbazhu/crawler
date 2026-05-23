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
