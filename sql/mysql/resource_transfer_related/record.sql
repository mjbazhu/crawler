INSERT INTO resource_transfer_related(parent_task_id,discovered_mask,claim_generation,created_at,updated_at)
VALUES(?,?,?,UNIX_TIMESTAMP(),UNIX_TIMESTAMP())
ON DUPLICATE KEY UPDATE discovered_mask=IF(child_operation_id IS NULL,VALUES(discovered_mask),discovered_mask),
 claim_generation=IF(child_operation_id IS NULL,VALUES(claim_generation),claim_generation),updated_at=UNIX_TIMESTAMP()
