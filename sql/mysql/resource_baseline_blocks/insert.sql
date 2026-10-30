INSERT INTO resource_baseline_blocks(owner_type,resource_id,content_generation,baseline_id)
VALUES(?,?,?,?) ON DUPLICATE KEY UPDATE baseline_id=resource_baseline_blocks.baseline_id
