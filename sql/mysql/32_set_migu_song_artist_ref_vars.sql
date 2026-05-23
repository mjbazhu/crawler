SET
    @artist_source_id := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @artist_name := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @artist_image := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @artist_name_spelling := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @artist_role := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @artist_order := ?,
    @artist_is_primary := ?,
    @artist_is_seed := ?;
