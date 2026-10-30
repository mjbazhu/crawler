-- 内部关联发现和 producer 最多两层；生成列索引按父操作有界查找。
SELECT operation_id FROM resource_transfer_operations
WHERE parent_operation_id=?
ORDER BY operation_id LIMIT 601
