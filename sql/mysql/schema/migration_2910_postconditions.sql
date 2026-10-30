SELECT JSON_OBJECT(
 'tables',(SELECT JSON_ARRAYAGG(JSON_OBJECT('name',table_name,'engine',engine,'collation',table_collation,'type',table_type))
           FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name IN('resource_transfer_operations','resource_transfer_related')),
 'columns',(SELECT JSON_ARRAYAGG(JSON_OBJECT('table',table_name,'name',column_name,'type',column_type,'nullable',is_nullable,
              'charset',character_set_name,'collation',collation_name,'extra',extra,'expression',generation_expression,
              'default',column_default,'ordinal',ordinal_position))
            FROM information_schema.columns WHERE table_schema=DATABASE()
             AND (table_name='resource_transfer_related' OR (table_name='resource_transfer_operations' AND column_name IN('parent_operation_id','parent_task_id')))),
 'indexes',(SELECT JSON_ARRAYAGG(JSON_OBJECT('table',table_name,'name',index_name,'column',column_name,'ordinal',seq_in_index,
              'non_unique',non_unique,'prefix',sub_part,'type',index_type,'direction',collation,'visible',is_visible))
            FROM information_schema.statistics WHERE table_schema=DATABASE()
             AND (table_name='resource_transfer_related' OR (table_name='resource_transfer_operations' AND index_name='idx_transfer_parent_operation'))),
 'constraints',(SELECT JSON_ARRAYAGG(JSON_OBJECT('name',constraint_name,'type',constraint_type,'enforced',enforced))
                FROM information_schema.table_constraints WHERE constraint_schema=DATABASE() AND table_name='resource_transfer_related'),
 'foreign_keys',(SELECT JSON_ARRAYAGG(JSON_OBJECT('name',k.constraint_name,'column',k.column_name,'ordinal',k.ordinal_position,
                    'schema',k.referenced_table_schema,'table',k.referenced_table_name,'referenced',k.referenced_column_name,
                    'delete',r.delete_rule,'update',r.update_rule))
                 FROM information_schema.key_column_usage k JOIN information_schema.referential_constraints r
                   ON r.constraint_schema=k.constraint_schema AND r.constraint_name=k.constraint_name AND r.table_name=k.table_name
                 WHERE k.constraint_schema=DATABASE() AND k.table_name='resource_transfer_related'),
 'checks',(SELECT JSON_ARRAYAGG(JSON_OBJECT('name',c.constraint_name,'expression',c.check_clause))
           FROM information_schema.check_constraints c JOIN information_schema.table_constraints t
             ON t.constraint_schema=c.constraint_schema AND t.constraint_name=c.constraint_name
           WHERE t.constraint_schema=DATABASE() AND t.table_name='resource_transfer_related'),
 'database',DATABASE()
)
