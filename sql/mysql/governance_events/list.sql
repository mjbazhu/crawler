SELECT
    e.id,
    e.source,
    e.action,
    e.target_type,
    e.target_id,
    COALESCE(
        NULLIF(e.target_name, ''),
        CASE
            WHEN e.target_type = 'resource' THEN song.title
            WHEN e.target_type = 'singer' THEN singer.name
            WHEN e.target_type = 'album' THEN COALESCE(NULLIF(album_ref.source_title, ''), album.title)
            WHEN e.target_type = 'batch' THEN COALESCE(
                NULLIF((SELECT SUBSTRING_INDEX(GROUP_CONCAT(COALESCE(NULLIF(bi.item_label, ''), NULLIF(bi.source_singer_id, ''), NULLIF(bi.source_album_id, ''), NULLIF(bi.source_song_id, ''), bi.item_key) ORDER BY bi.item_index SEPARATOR ' / '), ' / ', 1) FROM crawl_batch_items bi WHERE bi.batch_id = batch.batch_id), ''),
                CASE
                    WHEN batch.task_type = 'resourceProcess' THEN '资源下载批次'
                    WHEN batch.task_type IN ('singerSongs', 'singerSongsFromDb', 'singerSongsRetry') THEN '歌手歌曲批次'
                    WHEN batch.task_type IN ('singerAlbums', 'singerAlbumsFromDb', 'singerAlbumsRetry') THEN '歌手专辑批次'
                    WHEN batch.task_type IN ('albumDetails', 'albumDetail') THEN '专辑详情批次'
                    WHEN batch.task_type = 'albumSongs' THEN '专辑歌曲批次'
                    WHEN batch.task_type = 'singerProfileRefresh' THEN '歌手资料批次'
                    ELSE NULL
                END,
                batch.batch_id
            )
            ELSE NULL
        END,
        ''
    ),
    COALESCE(
        NULLIF(e.target_summary, ''),
        CASE
            WHEN e.target_type = 'resource' THEN CONCAT_WS(
                ' · ',
                NULLIF(CASE resource.resource_kind
                    WHEN 'audio' THEN '音频'
                    WHEN 'cover' THEN '封面'
                    WHEN 'lyric' THEN '歌词'
                    WHEN 'lrc' THEN '歌词'
                    WHEN 'mrc' THEN 'MRC'
                    WHEN 'trc' THEN 'TRC'
                    ELSE resource.resource_kind
                END, ''),
                NULLIF(resource.format_type, ''),
                NULLIF((SELECT sra.name FROM song_artist_refs ar JOIN singer_catalog_sources ssr ON ssr.id = ar.singer_source_ref_id JOIN singers sra ON sra.id = ssr.singer_db_id WHERE ar.song_source_ref_id = source_ref.id ORDER BY ar.is_seed DESC, ar.is_primary DESC, ar.artist_order ASC, ar.id ASC LIMIT 1), '')
            )
            WHEN e.target_type = 'singer' THEN CASE
                WHEN e.action LIKE '%singer-albums%' OR e.action LIKE '%singerAlbums%' THEN '歌手专辑补池'
                WHEN e.action LIKE '%singer-songs%' OR e.action LIKE '%singerSongs%' THEN '歌手歌曲补池'
                WHEN e.action LIKE '%profile%' THEN '歌手资料刷新'
                ELSE '歌手'
            END
            WHEN e.target_type = 'album' THEN COALESCE(NULLIF(album_ref.source_artist_name, ''), NULLIF(album.primary_artist_name, ''))
            WHEN e.target_type = 'batch' THEN COALESCE(
                NULLIF((SELECT GROUP_CONCAT(CONCAT_WS(' ', COALESCE(NULLIF(bi.item_label, ''), bi.item_key), NULLIF(bi.source_singer_id, ''), NULLIF(bi.source_album_id, ''), NULLIF(bi.source_song_id, ''), CASE WHEN COALESCE(bi.last_error, '') = '' THEN NULL ELSE CONCAT('error:', bi.last_error) END) ORDER BY bi.item_index SEPARATOR ' | ') FROM crawl_batch_items bi WHERE bi.batch_id = batch.batch_id), ''),
                CONCAT_WS(
                    ' · ',
                    NULLIF(CASE
                        WHEN batch.task_type = 'resourceProcess' THEN '资源下载批次'
                        WHEN batch.task_type IN ('singerSongs', 'singerSongsFromDb', 'singerSongsRetry') THEN '歌手歌曲批次'
                        WHEN batch.task_type IN ('singerAlbums', 'singerAlbumsFromDb', 'singerAlbumsRetry') THEN '歌手专辑批次'
                        WHEN batch.task_type IN ('albumDetails', 'albumDetail') THEN '专辑详情批次'
                        WHEN batch.task_type = 'albumSongs' THEN '专辑歌曲批次'
                        WHEN batch.task_type = 'singerProfileRefresh' THEN '歌手资料批次'
                        ELSE batch.task_type
                    END, ''),
                    NULLIF(batch.state, ''),
                    CASE WHEN batch.target_count > 0 THEN CONCAT(batch.target_count, ' 条') ELSE NULL END
                )
            )
            ELSE NULL
        END,
        ''
    ),
    COALESCE(
        NULLIF(e.target_source_id, ''),
        CASE
            WHEN e.target_type = 'resource' THEN COALESCE(NULLIF(source_ref.source_content_id, ''), NULLIF(source_ref.source_song_id, ''))
            WHEN e.target_type = 'singer' THEN singer_ref.source_singer_id
            WHEN e.target_type = 'album' THEN album_ref.source_album_id
            ELSE NULL
        END,
        ''
    ),
    COALESCE(
        NULLIF(e.target_owner_name, ''),
        CASE
            WHEN e.target_type = 'resource' THEN (SELECT sra.name FROM song_artist_refs ar JOIN singer_catalog_sources ssr ON ssr.id = ar.singer_source_ref_id JOIN singers sra ON sra.id = ssr.singer_db_id WHERE ar.song_source_ref_id = source_ref.id ORDER BY ar.is_seed DESC, ar.is_primary DESC, ar.artist_order ASC, ar.id ASC LIMIT 1)
            WHEN e.target_type = 'album' THEN COALESCE(NULLIF(album_ref.source_artist_name, ''), NULLIF(album.primary_artist_name, ''))
            ELSE NULL
        END,
        ''
    ),
    COALESCE(e.batch_id, ''),
    COALESCE(e.before_status, ''),
    COALESCE(e.after_status, ''),
    COALESCE(e.result_code, ''),
    COALESCE(e.error_category, ''),
    COALESCE(e.reason, ''),
    COALESCE(CAST(e.payload AS CHAR), '{}'),
    e.created_at
FROM governance_events e
LEFT JOIN song_resources resource ON e.target_type = 'resource'
    AND resource.id = CAST(CASE WHEN e.target_id REGEXP '^[0-9]+$' THEN e.target_id ELSE '0' END AS UNSIGNED)
LEFT JOIN song_catalog_sources source_ref ON source_ref.id = resource.song_source_ref_id
    AND source_ref.source = e.source
LEFT JOIN songs song ON song.id = source_ref.song_db_id
LEFT JOIN singer_catalog_sources singer_ref ON e.target_type = 'singer'
    AND singer_ref.source = e.source
    AND singer_ref.source_singer_id = e.target_id
LEFT JOIN singers singer ON singer.id = singer_ref.singer_db_id
LEFT JOIN album_catalog_sources album_ref ON e.target_type = 'album'
    AND album_ref.source = e.source
    AND album_ref.source_album_id = e.target_id
LEFT JOIN album_catalog album ON album.id = album_ref.album_db_id
LEFT JOIN crawl_batches batch ON e.target_type = 'batch'
    AND batch.source = e.source
    AND batch.batch_id = e.target_id
WHERE e.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (? = '' OR e.target_type = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = '' OR e.target_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = '' OR e.action = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = '' OR e.batch_id = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
ORDER BY e.id DESC
LIMIT ? OFFSET ?
