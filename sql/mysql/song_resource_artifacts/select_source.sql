SELECT checksum
FROM song_resources
WHERE id = ? AND resource_kind = 'mrc' AND status = 2
FOR UPDATE
