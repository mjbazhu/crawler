CREATE OR REPLACE VIEW singer_catalog_sources AS
SELECT r.*,CONCAT('singer:',r.source_singer_id) AS source_item_key,
       COALESCE((SELECT m.source_tag FROM singer_index_memberships m WHERE m.singer_source_ref_id=r.id AND NULLIF(m.source_tag,'') IS NOT NULL ORDER BY m.id LIMIT 1),'') AS source_tag
FROM singer_source_refs r;
