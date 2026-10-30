INSERT INTO crawler_schema_versions(component,schema_version,schema_name,applied_at,updated_at)
VALUES('crawler_mysql',?,?,UNIX_TIMESTAMP(),UNIX_TIMESTAMP())
ON DUPLICATE KEY UPDATE
 updated_at=IF(schema_version<VALUES(schema_version)
   OR (schema_version=VALUES(schema_version)
       AND NOT(CAST(schema_name AS BINARY)<=>CAST(VALUES(schema_name) AS BINARY))),
   VALUES(updated_at),updated_at),
 schema_name=IF(schema_version<=VALUES(schema_version),VALUES(schema_name),schema_name),
 schema_version=GREATEST(schema_version,VALUES(schema_version))
