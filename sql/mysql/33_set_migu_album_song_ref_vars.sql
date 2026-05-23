SET
    @album_ref_source_album_id := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @album_ref_source_song_id := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @album_ref_source_content_id := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @album_ref_disc_no := ?,
    @album_ref_disc_name := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @album_ref_track_no := ?,
    @album_ref_song_order := ?;
