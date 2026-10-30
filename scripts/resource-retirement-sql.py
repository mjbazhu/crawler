#!/usr/bin/env python3
"""只读识别固定SQL中的退役资源物理列，不将JSON键/回执字段/协议别名当成旧列。"""
import re


def sql_tokens(text):
    """保留原偏移/换行，屏蔽注释和字符串；反引号仅用于标识符。"""
    masked = list(text)
    index = 0
    while index < len(text):
        start = index
        if text[index] in "'\"":
            quote = text[index]
            index += 1
            while index < len(text):
                if text[index] == "\\":
                    index += 2
                elif text[index] == quote:
                    index += 1
                    if index < len(text) and text[index] == quote:
                        index += 1
                    else:
                        break
                else:
                    index += 1
        elif text.startswith("/*", index):
            end = text.find("*/", index + 2)
            index = len(text) if end < 0 else end + 2
        elif text[index] == "#" or text.startswith("--", index) and (index + 2 == len(text) or text[index + 2].isspace()):
            end = text.find("\n", index)
            index = len(text) if end < 0 else end
        else:
            index += 1
            continue
        for offset in range(start, min(index, len(text))):
            if text[offset] not in "\r\n":
                masked[offset] = " "
    token_pattern = r"`(?:``|[^`])*`|[A-Za-z_][A-Za-z0-9_$]*|[();,.=]"
    return [(match.group().strip("`").replace("``", "`").lower(), match.start()) for match in re.finditer(token_pattern, "".join(masked))]


def physical_references(text, columns):
    """词法定向盘点非动态运行SQL；实际可执行性仍由删列后的SQL prepare验证。"""
    raw = sql_tokens(text)
    tokens, parents, stack, current = [], {0: None}, [], 0
    openings, matching, functions = [], {}, {}
    for word, offset in raw:
        if word in (";", "union"):
            current = len(parents)
            parents[current] = None if word == ";" else parents[tokens[-1][2]] if tokens else None
        position = len(tokens)
        tokens.append((word, offset, current))
        if word == "(":
            stack.append(current)
            openings.append(position)
            child = len(parents)
            parents[child] = current
            functions[child] = tokens[position - 1][0] if position else ""
            current = child
        elif word == ")" and stack:
            matching[openings.pop()] = position
            current = stack.pop()
    aliases, sources, updates, projected, insert_targets = {}, {}, {}, {}, {}
    declarations, insert_lists = set(), set()
    reserved = {"set", "where", "join", "left", "right", "inner", "outer", "cross", "on", "using", "force", "use", "ignore", "index", "order", "group", "having", "limit", "for", "union", "straight_join", "partition", "values", "select", "from", "window", "returning", "into"}
    found = set()

    def add(position, table, column, kind):
        found.add((text.count("\n", 0, tokens[position][1]) + 1, table, column, kind))

    def inherited(mapping, scope):
        while scope is not None:
            if scope in mapping:
                return mapping[scope]
            scope = parents[scope]
        return {}

    def table_for_alias(alias, scope):
        while scope is not None:
            if alias in aliases.get(scope, {}):
                return aliases[scope][alias]
            scope = parents[scope]
        return None

    for index, (word, _, scope) in enumerate(tokens):
        if word == "as" and index + 1 < len(tokens):
            projected.setdefault(scope, {})[tokens[index + 1][0]] = index + 1
        if word == "update" and index and tokens[index - 1][0] == "key":
            updates[scope] = inherited(insert_targets, scope)
            continue
        if word not in ("from", "join", "straight_join", "update", "into") or index + 1 >= len(tokens):
            continue
        table_index = index + 1
        table = tokens[table_index][0]
        if not re.fullmatch("[a-z_][a-z0-9_$]*", table):
            continue
        if table_index + 2 < len(tokens) and tokens[table_index + 1][0] == ".":
            table_index += 2
            table = tokens[table_index][0]
        declarations.add(table_index)
        aliases.setdefault(scope, {})[table] = table
        alias = table_index + 1
        if alias < len(tokens) and tokens[alias][0] == "as":
            alias += 1
        if alias < len(tokens) and re.fullmatch("[a-z_][a-z0-9_$]*", tokens[alias][0]) and tokens[alias][0] not in reserved:
            aliases[scope][tokens[alias][0]] = table
            declarations.add(alias)
        if word in ("from", "join", "straight_join", "update"):
            sources.setdefault(scope, set()).add(table)
        if word == "update":
            updates[scope] = table
        if word == "into":
            insert_targets[scope] = table
        if word == "into" and table_index + 1 < len(tokens) and tokens[table_index + 1][0] == "(":
            begin = table_index + 1
            for column_index in range(begin + 1, matching.get(begin, begin) + 1):
                insert_lists.add(column_index)
                column = tokens[column_index][0]
                if column in columns.get(table, ()):
                    add(column_index, table, column, "insert_column")
    candidates = set().union(*(set(value) for value in columns.values()))
    for index, (column, _, scope) in enumerate(tokens):
        if column not in candidates or index in declarations or index in insert_lists:
            continue
        previous = tokens[index - 1][0] if index else ""
        after = tokens[index + 1][0] if index + 1 < len(tokens) else ""
        if previous == "." and index >= 2:
            qualifier = tokens[index - 2][0]
            table = qualifier if qualifier in columns else table_for_alias(qualifier, scope)
            if column in columns.get(table, ()):
                add(index, table, column, "qualified_column")
            continue
        if after == "." or previous == "as":
            continue
        if functions.get(scope) == "values":
            target = inherited(insert_targets, scope)
            if isinstance(target, str) and column in columns.get(target, ()):
                add(index, target, column, "insert_value_column")
            continue
        update = inherited(updates, scope)
        if after == "=" and isinstance(update, str):
            if column in columns.get(update, ()):
                add(index, update, column, "update_column")
            continue
        projection = inherited(projected, scope).get(column)
        if projection is not None and index >= projection:
            continue
        tables = inherited(sources, scope)
        if len(tables) == 1:
            table = next(iter(tables))
            if column in columns.get(table, ()):
                add(index, table, column, "unqualified_column")
    return [{"line": line, "table": table, "column": column, "kind": kind} for line, table, column, kind in sorted(found)]
