CREATE TABLE IF NOT EXISTS singer_index_memberships (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '歌手分类归属主键',
    singer_source_ref_id BIGINT UNSIGNED NOT NULL COMMENT '关联 singer_source_refs.id，表示唯一平台歌手来源',
    tab VARCHAR(64) NOT NULL COMMENT '平台歌手分类 tab，例如 huayu-nan、oumei-group',
    index_order INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '歌手在当前分类 tab 中的顺序，从 1 开始；未知时为 0',
    source_tag VARCHAR(128) COMMENT '该分类列表项的平台标签，例如 热',
    source_metric VARCHAR(128) COMMENT '该分类列表项的平台热度、播放量或数量原始值',
    created_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录创建时间，Unix 秒时间戳',
    updated_at BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '记录最后更新时间，Unix 秒时间戳',
    PRIMARY KEY (id),
    UNIQUE KEY uk_singer_index_membership (singer_source_ref_id, tab),
    KEY idx_singer_index_tab (tab, index_order),
    KEY idx_singer_index_ref (singer_source_ref_id),
    CONSTRAINT fk_singer_index_memberships_ref FOREIGN KEY (singer_source_ref_id) REFERENCES singer_source_refs (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='歌手分类归属表，只记录唯一歌手来源出现在哪些平台分类 tab 中';

INSERT INTO singer_index_memberships (
    singer_source_ref_id,
    tab,
    index_order,
    source_tag,
    source_metric,
    created_at,
    updated_at
)
SELECT
    seeded.singer_source_ref_id,
    seeded.tab,
    seeded.index_order,
    seeded.source_tag,
    seeded.source_metric,
    seeded.created_at,
    seeded.updated_at
FROM (
    SELECT
        r.id AS singer_source_ref_id,
        CASE
            WHEN r.source_country = '华语' AND s.gender = 1 THEN 'huayu-nan'
            WHEN r.source_country = '华语' AND s.gender = 2 THEN 'huayu-nv'
            WHEN r.source_country = '华语' AND s.gender = 3 THEN 'huayu-group'
            WHEN r.source_country = '欧美' AND s.gender = 1 THEN 'oumei-nan'
            WHEN r.source_country = '欧美' AND s.gender = 2 THEN 'oumei-nv'
            WHEN r.source_country = '欧美' AND s.gender = 3 THEN 'oumei-group'
            WHEN r.source_country = '日韩' AND s.gender = 1 THEN 'rihan-nan'
            WHEN r.source_country = '日韩' AND s.gender = 2 THEN 'rihan-nv'
            WHEN r.source_country = '日韩' AND s.gender = 3 THEN 'rihan-group'
            ELSE NULL
        END AS tab,
        0 AS index_order,
        r.source_tag,
        r.source_metric,
        UNIX_TIMESTAMP() AS created_at,
        UNIX_TIMESTAMP() AS updated_at
    FROM singer_source_refs r
    JOIN singers s ON s.id = r.singer_db_id
    WHERE r.source = 'migu'
) seeded
WHERE seeded.tab IS NOT NULL
ON DUPLICATE KEY UPDATE
    source_tag = VALUES(source_tag),
    source_metric = VALUES(source_metric),
    updated_at = VALUES(updated_at);
