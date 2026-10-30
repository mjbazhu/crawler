SELECT COUNT(*) FROM resource_cloud_migration_audit a
JOIN resource_destination_states t ON t.destination=?
LEFT JOIN resource_cloud_receipts c ON c.owner_type=a.owner_type AND c.resource_id=a.resource_id AND c.cloud_provider=t.destination
WHERE a.run_id=? AND a.owner_type=? AND a.resource_id>? AND a.resource_id<=? AND
(c.id IS NULL OR c.policy_content_generation<>a.content_generation OR c.policy_target_revision<>t.target_revision OR c.baseline_id<>a.run_id OR
 (a.disposition='imported' AND (c.transfer_policy<>'skip' OR c.skip_reason='')) OR
 (a.disposition='allow_transfer' AND (c.transfer_policy<>'normal' OR c.skip_reason<>'')))
