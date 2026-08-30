#!/usr/bin/env python3
"""Report collective-safety review candidates without modifying source files."""

import argparse
import pathlib
import re
import sys
from dataclasses import dataclass


FUNCTION_RE = re.compile(
    r"(?m)^[ \t]*(?:static[ \t]+)?(?:inline[ \t]+)?"
    r"(?:[A-Za-z_][\w \t*]*?[ \t]+)?([A-Za-z_]\w*)[ \t]*"
    r"\([^;{}]*\)[ \t\n]*\{"
)
CALL_RE = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
OMP_RE = re.compile(
    r"#\s*pragma\s+omp\s+(?:for|sections|single|master)\b|"
    r"_Pragma\s*\(\s*\"omp\s+(?:for|sections|single|master)\b"
)
CONTROL_RE = re.compile(r"\b(?:if|switch)\s*\(")


@dataclass(frozen=True)
class Function:
    name: str
    path: pathlib.Path
    line: int
    body: str


def matching_delimiter(text, opening, left="{", right="}"):
    depth = 0
    index = opening
    state = "code"

    while index < len(text):
        char = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""

        if state == "code":
            if char == "/" and following == "*":
                state = "block-comment"
                index += 1
            elif char == "/" and following == "/":
                state = "line-comment"
                index += 1
            elif char == '"':
                state = "string"
            elif char == "'":
                state = "character"
            elif char == left:
                depth += 1
            elif char == right:
                depth -= 1
                if depth == 0:
                    return index
        elif state == "block-comment" and char == "*" and following == "/":
            state = "code"
            index += 1
        elif state == "line-comment" and char == "\n":
            state = "code"
        elif state in ("string", "character"):
            if char == "\\":
                index += 1
            elif (state == "string" and char == '"') or (
                state == "character" and char == "'"
            ):
                state = "code"

        index += 1

    raise ValueError(f"unmatched {left}")


def matching_brace(text, opening):
    return matching_delimiter(text, opening)


def parse_functions(path):
    text = path.read_text(encoding="utf-8")
    functions = []

    for match in FUNCTION_RE.finditer(text):
        opening = text.find("{", match.start(), match.end())
        try:
            closing = matching_brace(text, opening)
        except ValueError:
            continue
        functions.append(
            Function(
                match.group(1),
                path,
                text.count("\n", 0, match.start()) + 1,
                text[opening + 1 : closing],
            )
        )

    return functions


def omp_macro_names(text):
    logical_lines = re.sub(r"\\\n", " ", text)
    names = set()

    for match in re.finditer(r"(?m)^\s*#\s*define\s+([A-Za-z_]\w*)[^\n]*", logical_lines):
        if OMP_RE.search(match.group(0)):
            names.add(match.group(1))

    return names


def has_worksharing(text, omp_macros):
    if OMP_RE.search(text):
        return True
    return any(re.search(rf"\b{re.escape(name)}\b", text) for name in omp_macros)


def registered_callbacks(table_path):
    text = table_path.read_text(encoding="utf-8")
    table_start = text.index("} vj_fx[] = {") + len("} vj_fx[] = ")
    table_end = matching_brace(text, table_start)
    callbacks = []

    for entry in re.finditer(r"\{([^{}]+)\}", text[table_start + 1 : table_end]):
        fields = [field.strip() for field in entry.group(1).split(",")]
        if len(fields) < 11:
            continue
        for callback in fields[5:7]:
            if callback != "NULL":
                callbacks.append(callback)

    return sorted(set(callbacks))


def reachable_functions(root, functions):
    reachable = set()
    pending = [root]

    while pending:
        name = pending.pop()
        if name in reachable or name not in functions:
            continue
        reachable.add(name)
        pending.extend(
            call for call in CALL_RE.findall(functions[name].body) if call in functions
        )

    return reachable


def shared_control_reasons(function, worksharing_capable, omp_macros):
    reasons = []

    for match in CONTROL_RE.finditer(function.body):
        opening_parenthesis = function.body.find("(", match.start(), match.end())
        try:
            closing_parenthesis = matching_delimiter(
                function.body, opening_parenthesis, "(", ")"
            )
        except ValueError:
            continue

        condition = function.body[opening_parenthesis + 1 : closing_parenthesis]
        if "->" not in condition:
            continue

        opening_brace = closing_parenthesis + 1
        while (
            opening_brace < len(function.body)
            and function.body[opening_brace].isspace()
        ):
            opening_brace += 1
        if opening_brace >= len(function.body) or function.body[opening_brace] != "{":
            continue

        try:
            closing_brace = matching_brace(function.body, opening_brace)
        except ValueError:
            continue
        controlled = function.body[opening_brace + 1 : closing_brace]
        calls_worksharing = any(
            call in worksharing_capable for call in CALL_RE.findall(controlled)
        )

        if has_worksharing(controlled, omp_macros) or calls_worksharing:
            line = function.line + function.body.count("\n", 0, match.start())
            compact_condition = " ".join(condition.split())
            if len(compact_condition) > 72:
                compact_condition = compact_condition[:69] + "..."
            reasons.append(f"{function.name}:{line}: {compact_condition}")

    return reasons


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--all",
        action="store_true",
        help="also list registered callbacks with no reachable worksharing",
    )
    args = parser.parse_args()

    effects_dir = pathlib.Path(__file__).resolve().parent
    table_path = effects_dir.parent / "libvje.c"
    callbacks = registered_callbacks(table_path)
    definitions = {}
    omp_macros = set()
    worksharing_sources = set()

    for source_path in sorted(effects_dir.glob("*.c")):
        source_text = source_path.read_text(encoding="utf-8")
        omp_macros.update(omp_macro_names(source_text))
        if OMP_RE.search(source_text):
            worksharing_sources.add(source_path)
        for function in parse_functions(source_path):
            definitions.setdefault(function.name, function)

    worksharing_capable = {
        name
        for name in definitions
        if any(
            has_worksharing(definitions[reachable].body, omp_macros)
            for reachable in reachable_functions(name, definitions)
        )
    }

    missing = []
    collective = []
    serial = []
    candidates = []

    for callback in callbacks:
        if callback not in definitions:
            missing.append(callback)
            continue

        reachable = reachable_functions(callback, definitions)
        worksharing = sorted(
            name
            for name in reachable
            if has_worksharing(definitions[name].body, omp_macros)
        )
        if not worksharing and definitions[callback].path not in worksharing_sources:
            serial.append(callback)
            continue

        collective.append(callback)
        reasons = []
        for name in worksharing:
            reasons.extend(
                shared_control_reasons(
                    definitions[name], worksharing_capable, omp_macros
                )
            )

        if reasons:
            candidates.append((callback, definitions[callback], reasons))

    print(f"registered callbacks: {len(callbacks)}")
    print(f"callbacks reaching worksharing: {len(collective)}")
    print(f"callbacks with no reachable worksharing: {len(serial)}")
    print(f"callbacks missing a parsed definition: {len(missing)}")
    print(f"collective-safety review candidates: {len(candidates)}")

    for callback, function, reasons in candidates:
        location = f"{function.path.relative_to(effects_dir.parent.parent)}:{function.line}"
        print(f"REVIEW {callback} {location}")
        for reason in sorted(set(reasons)):
            print(f"  - {reason}")

    if args.all:
        for callback in serial:
            function = definitions[callback]
            location = f"{function.path.relative_to(effects_dir.parent.parent)}:{function.line}"
            print(f"SERIAL {callback} {location}")

    if missing:
        for callback in missing:
            print(f"MISSING {callback}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
