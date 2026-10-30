SELECT
    sr.id,
    sr.song_source_ref_id,
    r.source,
    COALESCE(r.source_song_id, ''),
    COALESCE(r.source_content_id, ''),
    COALESCE(r.source_resource_type, ''),
    COALESCE(
        f.resource_type,
        (
            SELECT f2.resource_type
            FROM song_source_audio_formats f2
            WHERE f2.song_source_ref_id = sr.song_source_ref_id
            ORDER BY CASE WHEN FIELD(f2.format_type, 'PQ', 'HQ', 'SQ', 'ZQ24') = 0 THEN 99 ELSE FIELD(f2.format_type, 'PQ', 'HQ', 'SQ', 'ZQ24') END, f2.id ASC
            LIMIT 1
        ),
        ''
    ),
    COALESCE(r.copyright_id, ''),
    s.title,
    COALESCE((SELECT sra.name FROM song_artist_refs ar JOIN singer_source_refs ssr ON ssr.id = ar.singer_source_ref_id JOIN singers sra ON sra.id = ssr.singer_db_id WHERE ar.song_source_ref_id = r.id ORDER BY ar.is_seed DESC, ar.is_primary DESC, ar.artist_order ASC, ar.id ASC LIMIT 1), ''),
    COALESCE((SELECT a2_title.title FROM album_song_refs ar2 JOIN album_source_refs a2 ON a2.id=ar2.album_source_ref_id JOIN albums a2_title ON a2_title.id=a2.album_db_id WHERE ar2.song_source_ref_id=r.id ORDER BY ar2.id LIMIT 1),''),
    COALESCE((SELECT MAX(a2.source_album_kind) FROM album_song_refs ar2 JOIN album_source_refs a2 ON a2.id=ar2.album_source_ref_id WHERE ar2.song_source_ref_id=r.id),0),
    sr.resource_kind,
    sr.format_type,
    '',
    COALESCE(sr.local_path, ''),
    sr.status,
    sr.retry_count,
    COALESCE(
        f.format_type,
        (
            SELECT f2.format_type
            FROM song_source_audio_formats f2
            WHERE f2.song_source_ref_id = sr.song_source_ref_id
            ORDER BY CASE WHEN FIELD(f2.format_type, 'PQ', 'HQ', 'SQ', 'ZQ24') = 0 THEN 99 ELSE FIELD(f2.format_type, 'PQ', 'HQ', 'SQ', 'ZQ24') END, f2.id ASC
            LIMIT 1
        ),
        ''
    ) ,
    sr.claim_generation
FROM song_resources sr
JOIN song_source_refs r ON r.id = sr.song_source_ref_id
JOIN songs s ON s.id = r.song_db_id
LEFT JOIN song_source_audio_formats f ON f.song_source_ref_id = sr.song_source_ref_id
    AND f.format_type = sr.format_type
WHERE sr.id=?
