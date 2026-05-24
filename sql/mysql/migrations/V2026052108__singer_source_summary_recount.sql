DROP TEMPORARY TABLE IF EXISTS crawler_singer_source_summary_recount;

CREATE TEMPORARY TABLE crawler_singer_source_summary_recount (
    singer_source_ref_id BIGINT UNSIGNED NOT NULL,
    songs_count BIGINT UNSIGNED NOT NULL,
    albums_count BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY (singer_source_ref_id)
) ENGINE=InnoDB;

INSERT INTO crawler_singer_source_summary_recount (
    singer_source_ref_id,
    songs_count,
    albums_count
)
SELECT
    r.id AS singer_source_ref_id,
    (
        SELECT COUNT(DISTINCT ar.song_source_ref_id)
        FROM song_artist_refs ar
        WHERE ar.source = r.source
          AND (
              ar.singer_source_ref_id = r.id
              OR (
                  ar.source_artist_id IS NOT NULL
                  AND ar.source_artist_id <> ''
                  AND ar.source_artist_id = r.source_singer_id
              )
          )
    ) AS songs_count,
    (
        SELECT COUNT(DISTINCT al.id)
        FROM album_source_refs al
        WHERE al.source = r.source
          AND al.source_artist_id = r.source_singer_id
    ) AS albums_count
FROM singer_source_refs r;

UPDATE singer_source_refs r
JOIN crawler_singer_source_summary_recount summary ON summary.singer_source_ref_id = r.id
SET
    r.songs_sync_count = summary.songs_count,
    r.songs_sync_status = CASE
        WHEN summary.songs_count > 0 AND COALESCE(r.songs_sync_error, '') = '' THEN 2
        ELSE r.songs_sync_status
    END,
    r.songs_synced_at = CASE
        WHEN summary.songs_count > 0 AND COALESCE(r.songs_sync_error, '') = '' AND r.songs_synced_at = 0 THEN UNIX_TIMESTAMP()
        ELSE r.songs_synced_at
    END,
    r.songs_claim_token = CASE
        WHEN summary.songs_count > 0 AND COALESCE(r.songs_sync_error, '') = '' THEN ''
        ELSE r.songs_claim_token
    END,
    r.songs_claimed_at = CASE
        WHEN summary.songs_count > 0 AND COALESCE(r.songs_sync_error, '') = '' THEN 0
        ELSE r.songs_claimed_at
    END,
    r.songs_claim_owner = CASE
        WHEN summary.songs_count > 0 AND COALESCE(r.songs_sync_error, '') = '' THEN ''
        ELSE r.songs_claim_owner
    END,
    r.updated_at = UNIX_TIMESTAMP()
WHERE r.songs_sync_count <> summary.songs_count
   OR (summary.songs_count > 0 AND COALESCE(r.songs_sync_error, '') = '' AND r.songs_sync_status <> 2)
   OR (summary.songs_count > 0 AND COALESCE(r.songs_sync_error, '') = '' AND r.songs_synced_at = 0)
   OR (summary.songs_count > 0 AND COALESCE(r.songs_sync_error, '') = '' AND (r.songs_claim_token <> '' OR r.songs_claimed_at <> 0 OR r.songs_claim_owner <> ''));

UPDATE singer_source_refs r
JOIN crawler_singer_source_summary_recount summary ON summary.singer_source_ref_id = r.id
SET
    r.albums_sync_count = summary.albums_count,
    r.albums_sync_status = CASE
        WHEN summary.albums_count > 0 AND COALESCE(r.albums_sync_error, '') = '' THEN 2
        ELSE r.albums_sync_status
    END,
    r.albums_synced_at = CASE
        WHEN summary.albums_count > 0 AND COALESCE(r.albums_sync_error, '') = '' AND r.albums_synced_at = 0 THEN UNIX_TIMESTAMP()
        ELSE r.albums_synced_at
    END,
    r.albums_claim_token = CASE
        WHEN summary.albums_count > 0 AND COALESCE(r.albums_sync_error, '') = '' THEN ''
        ELSE r.albums_claim_token
    END,
    r.albums_claimed_at = CASE
        WHEN summary.albums_count > 0 AND COALESCE(r.albums_sync_error, '') = '' THEN 0
        ELSE r.albums_claimed_at
    END,
    r.albums_claim_owner = CASE
        WHEN summary.albums_count > 0 AND COALESCE(r.albums_sync_error, '') = '' THEN ''
        ELSE r.albums_claim_owner
    END,
    r.updated_at = UNIX_TIMESTAMP()
WHERE r.albums_sync_count <> summary.albums_count
   OR (summary.albums_count > 0 AND COALESCE(r.albums_sync_error, '') = '' AND r.albums_sync_status <> 2)
   OR (summary.albums_count > 0 AND COALESCE(r.albums_sync_error, '') = '' AND r.albums_synced_at = 0)
   OR (summary.albums_count > 0 AND COALESCE(r.albums_sync_error, '') = '' AND (r.albums_claim_token <> '' OR r.albums_claimed_at <> 0 OR r.albums_claim_owner <> ''));

DROP TEMPORARY TABLE IF EXISTS crawler_singer_source_summary_recount;

INSERT INTO crawler_schema_versions (
    component,
    schema_version,
    schema_name,
    applied_at,
    updated_at
)
VALUES (
    'crawler_mysql',
    2026052108,
    'singer source summary recount',
    UNIX_TIMESTAMP(),
    UNIX_TIMESTAMP()
)
ON DUPLICATE KEY UPDATE
    schema_version = GREATEST(schema_version, VALUES(schema_version)),
    schema_name = VALUES(schema_name),
    updated_at = VALUES(updated_at);
