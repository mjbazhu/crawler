CREATE TABLE IF NOT EXISTS song_artist_refs (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '歌曲歌手关系主键',
    song_source_ref_id BIGINT UNSIGNED NOT NULL COMMENT '关联 song_source_refs.id，表示该歌手关系属于哪个平台歌曲来源',
    singer_source_ref_id BIGINT UNSIGNED NOT NULL COMMENT '关联 singer_source_refs.id，表示该歌曲关联哪个唯一平台歌手来源',
    artist_role VARCHAR(64) NOT NULL DEFAULT 'artist' COMMENT '歌手角色，当前 singerList 统一为 artist',
    artist_order INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '歌手在平台返回列表中的顺序，从 1 开始；未知时为 0',
    is_primary TINYINT NOT NULL DEFAULT 0 COMMENT '是否主歌手：0 否，1 是',
    is_seed TINYINT NOT NULL DEFAULT 0 COMMENT '是否本次采集入口歌手：0 否，1 是',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录创建时间，Unix 秒时间戳',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录最后更新时间，Unix 秒时间戳',
    PRIMARY KEY (id),
    UNIQUE KEY uk_song_artist_singer (song_source_ref_id, singer_source_ref_id, artist_role),
    KEY idx_song_artist_song_source (song_source_ref_id),
    KEY idx_song_artist_singer_ref (singer_source_ref_id),
    KEY idx_song_artist_seed (is_seed, singer_source_ref_id),
    KEY idx_song_artist_order (song_source_ref_id, artist_role, artist_order),
    CONSTRAINT fk_song_artist_refs_song_source FOREIGN KEY (song_source_ref_id) REFERENCES song_source_refs (id) ON DELETE CASCADE,
    CONSTRAINT fk_song_artist_refs_singer_source FOREIGN KEY (singer_source_ref_id) REFERENCES singer_source_refs (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='歌曲与平台歌手关系表，支持多歌手、合唱和按来源歌手查询歌曲';
