UPDATE resource_cloud_backfills SET control_state=?,control_generation=control_generation+1,updated_at=UNIX_TIMESTAMP()
WHERE run_id=? AND manifest_hash=? AND control_generation=?;
