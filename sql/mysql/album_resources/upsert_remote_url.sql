INSERT INTO album_resources (album_source_ref_id, resource_kind, remote_url, status, created_at, updated_at)
VALUES (?, 'cover', NULLIF(?, ''), 0, UNIX_TIMESTAMP(), UNIX_TIMESTAMP())
ON DUPLICATE KEY UPDATE
    id = LAST_INSERT_ID(id),
    -- 推断摘要只补缺；详情已完成时，列表缩略图不能覆盖已有详情封面。
    remote_url = IF(
        ? = 1 OR (? = 1 AND EXISTS (SELECT 1 FROM album_source_refs a WHERE a.id=album_resources.album_source_ref_id AND a.detail_synced_at>0)),
        COALESCE(NULLIF(remote_url, ''), VALUES(remote_url)),
        COALESCE(VALUES(remote_url), remote_url)
    ),
    updated_at = VALUES(updated_at);
