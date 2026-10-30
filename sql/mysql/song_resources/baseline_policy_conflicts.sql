SELECT COUNT(*) FROM song_resources r
JOIN resource_cloud_migration_audit a ON a.owner_type='song' AND a.resource_id=r.id AND a.run_id=? AND a.disposition='imported'
JOIN resource_destination_states l ON l.destination='local'
JOIN resource_destination_states b ON b.destination='baidu'
JOIN resource_destination_states g ON g.destination='gdrive'
LEFT JOIN resource_cloud_receipts cb ON cb.owner_type='song' AND cb.resource_id=r.id AND cb.cloud_provider='baidu'
LEFT JOIN resource_cloud_receipts cg ON cg.owner_type='song' AND cg.resource_id=r.id AND cg.cloud_provider='gdrive'
WHERE r.id>? AND r.id<=? AND (r.local_skip_generation<>r.content_generation OR r.local_skip_target_revision<>l.target_revision OR r.local_skip_baseline<>a.run_id OR r.local_skip_reason='' OR
(?=0 AND (COALESCE(cb.transfer_policy,'')<>'skip' OR cb.policy_content_generation<>r.content_generation OR cb.policy_target_revision<>b.target_revision OR cb.baseline_id<>a.run_id OR cb.skip_reason='')) OR
(?=0 AND (COALESCE(cg.transfer_policy,'')<>'skip' OR cg.policy_content_generation<>r.content_generation OR cg.policy_target_revision<>g.target_revision OR cg.baseline_id<>a.run_id OR cg.skip_reason='')))
