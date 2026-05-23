SELECT COUNT(*)
FROM singer_source_refs
JOIN singers ON singers.id = singer_source_refs.singer_db_id
WHERE singer_source_refs.source = 'migu'
  AND singer_source_refs.source_country = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND singers.gender = ?
