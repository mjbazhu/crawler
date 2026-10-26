SELECT albums_sync_status, COUNT(*) FROM singer_source_refs WHERE source = 'migu' GROUP BY albums_sync_status
