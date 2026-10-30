INSERT INTO resource_discovery_observations(run_id,resource_id,content_generation,source_identity,values_digest,grade,observation,observed_at) VALUES(?,?,?,?,?,?,CAST(? AS JSON),?)
