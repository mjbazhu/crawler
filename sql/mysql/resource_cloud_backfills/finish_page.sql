UPDATE resource_cloud_backfills SET last_committed_id=?,processed_count=processed_count+?,
    imported_count=imported_count+?,conflict_count=conflict_count+?,state=?,updated_at=UNIX_TIMESTAMP()
WHERE run_id=? AND manifest_hash=? AND last_committed_id=?;
