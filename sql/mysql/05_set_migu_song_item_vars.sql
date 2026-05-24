SET
    @source := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @source_song_id := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @source_content_id := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @source_resource_type := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @title := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @normalized_title := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @source_duration_seconds := ?,
    @song_pinyin := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @album_id := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @album_name := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @album_pinyin := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @raw_cover_url := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @cover_url := CASE
        WHEN @raw_cover_url LIKE '/data/oss/%' THEN CONCAT('https://d.musicapp.migu.cn', @raw_cover_url)
        WHEN @raw_cover_url LIKE 'data/oss/%' THEN CONCAT('https://d.musicapp.migu.cn/', @raw_cover_url)
        WHEN @raw_cover_url LIKE '//%' THEN CONCAT('https:', @raw_cover_url)
        ELSE @raw_cover_url
    END,
    @lyric_url := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @mrc_url := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @trc_url := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @copyright_id := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci;
