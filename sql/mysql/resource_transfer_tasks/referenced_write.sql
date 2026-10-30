(SELECT id FROM song_resources WHERE local_path=? LIMIT 1)
UNION ALL (SELECT id FROM singer_resources WHERE local_path=? LIMIT 1)
UNION ALL (SELECT id FROM album_resources WHERE local_path=? LIMIT 1)
UNION ALL (SELECT id FROM song_resource_artifacts WHERE local_path=? LIMIT 1) LIMIT 1
