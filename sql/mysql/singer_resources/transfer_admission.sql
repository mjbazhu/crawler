SELECT r.content_generation,d.target_revision,
       CAST(IF(d.destination='local',r.local_skip_generation,COALESCE(c.policy_content_generation,0)) AS UNSIGNED),
       CAST(IF(d.destination='local',r.local_skip_target_revision,COALESCE(c.policy_target_revision,0)) AS UNSIGNED),
       IF(d.destination='local',r.local_skip_baseline,COALESCE(c.baseline_id,'')),
       IF(d.destination='local',r.local_skip_reason,COALESCE(c.skip_reason,'')),
       CAST(IF(d.destination='local',r.local_skip_generation>0,COALESCE(c.transfer_policy='skip',0)) AS UNSIGNED),
       d.dispatch_state,d.baseline_ready,d.target_fingerprint<>'',r.status,EXISTS(SELECT 1 FROM resource_baseline_blocks blocked WHERE blocked.owner_type='singer' AND blocked.resource_id=r.id AND blocked.content_generation=r.content_generation)
FROM singer_resources r
JOIN resource_destination_states d ON d.destination=?
LEFT JOIN resource_cloud_receipts c ON c.owner_type='singer' AND c.resource_id=r.id AND c.cloud_provider=d.destination
WHERE r.id=?
