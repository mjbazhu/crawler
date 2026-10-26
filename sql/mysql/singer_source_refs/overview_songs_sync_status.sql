SELECT songs_sync_status, COUNT(*) FROM singer_source_refs WHERE source = 'migu' GROUP BY songs_sync_status
