SELECT SHA2(CONCAT(@@server_uuid,':',DATABASE()),256);
