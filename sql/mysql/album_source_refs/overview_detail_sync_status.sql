SELECT detail_sync_status, COUNT(*) FROM album_source_refs WHERE source = 'migu' GROUP BY detail_sync_status
