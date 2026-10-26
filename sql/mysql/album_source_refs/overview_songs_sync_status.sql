SELECT songs_sync_status, COUNT(*) FROM album_source_refs WHERE source = 'migu' GROUP BY songs_sync_status
