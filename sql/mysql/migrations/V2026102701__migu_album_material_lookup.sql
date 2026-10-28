-- 物料身份按批次匹配并锁定，不修改历史 migration 或按名称合并来源。
CREATE INDEX idx_migu_dalbum_material_id ON migu_dalbum_details (source_material_id);
