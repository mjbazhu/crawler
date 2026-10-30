-- 认领与未决事实统一位于回执表；退役档案不是当前运行态。
SELECT resource_id FROM resource_cloud_receipts WHERE owner_type='singer' AND resource_id=?
 AND (task_state IN('running','uncertain') OR attempt IS NOT NULL OR running_batch_id<>'') ORDER BY cloud_provider FOR UPDATE
