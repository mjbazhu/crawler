SELECT operation_id,request_hash FROM resource_transfer_operations WHERE idempotency_key=? FOR UPDATE
