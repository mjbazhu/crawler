SET
    @format_type := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @format_resource_type := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @iformat := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @aformat := CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci,
    @isize := ?,
    @asize := ?;
