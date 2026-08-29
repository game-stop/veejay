#!/usr/bin/env python3
"""Grammar-based OpenMP team-region migration tool for VeeJay effects.
Instead of heuristically guessing which C statements are "serial", this tool
uses the OpenMP specification's context rules. When the effect's own
# pragma omp parallel is removed, the entire apply function executes inside
the dispatcher's parallel region. Therefore, any code that was originally in
a Single-Threaded Context (STC) must be explicitly wrapped in #pragma omp
single to preserve its original execution semantics.
"""
from __future__ import annotations
import argparse
import bisect
import dataclasses
import difflib
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Iterable, Iterator, Optional, Sequence

VERSION = "0.9.1-GRAMMAR"
INFO = "info"
REVIEW = "review_required"
BLOCKED = "blocked"

@dataclasses.dataclass
class Finding:
    path: str
    line: int
    severity: str
    code: str
    message: str
    excerpt: str = ""
    def as_dict(self) -> dict[str, object]:
        return dataclasses.asdict(self)

@dataclasses.dataclass
class Change:
    path: str
    line: int
    code: str
    before: str
    after: str
    def as_dict(self) -> dict[str, object]:
        return dataclasses.asdict(self)

@dataclasses.dataclass
class TextEdit:
    start: int
    end: int
    replacement: str

@dataclasses.dataclass
class Pragma:
    start: int
    end: int
    line: int
    indent: str
    tokens: list[str]
    raw: str
    newline: str

@dataclasses.dataclass
class Function:
    name: str
    start: int
    params_start: int
    params_end: int
    body_start: int
    end: int
    line: int
    is_static: bool

@dataclasses.dataclass
class UnitFile:
    path: Path
    original: str
    migrated: str
    findings: list[Finding] = dataclasses.field(default_factory=list)
    changes: list[Change] = dataclasses.field(default_factory=list)
    removed_num_threads: list[str] = dataclasses.field(default_factory=list)

    @property
    def changed(self) -> bool:
        return self.original != self.migrated

@dataclasses.dataclass
class EffectMigration:
    source: Path
    units: list[UnitFile]

    @property
    def status(self) -> str:
        return overall_status(self.units)

def line_starts(text: str) -> list[int]:
    starts = [0]
    starts.extend(match.end() for match in re.finditer(r"\n", text))
    return starts

def line_number(starts: Sequence[int], offset: int) -> int:
    return bisect.bisect_right(starts, offset)

def line_excerpt(text: str, offset: int) -> str:
    start = text.rfind("\n", 0, offset) + 1
    end = text.find("\n", offset)
    if end < 0:
        end = len(text)
    return text[start:end].strip()

def mask_c_source(text: str) -> str:
    out = list(text)
    normal, line_comment, block_comment, string, character = range(5)
    state = normal
    escaped = False
    i = 0
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == normal:
            if ch == "/" and nxt == "/":
                out[i] = out[i + 1] = " "
                state = line_comment
                i += 2
                continue
            if ch == "/" and nxt == "*":
                out[i] = out[i + 1] = " "
                state = block_comment
                i += 2
                continue
            if ch == '"':
                out[i] = " "
                state = string
                escaped = False
            elif ch == "'":
                out[i] = " "
                state = character
                escaped = False
            elif state == line_comment:
                if ch == "\n":
                    state = normal
                else:
                    out[i] = " "
            elif state == block_comment:
                if ch == "*" and nxt == "/":
                    out[i] = out[i + 1] = " "
                    state = normal
                    i += 2
                    continue
                if ch != "\n":
                    out[i] = " "
        elif state in (string, character):
            if ch == "\n":
                state = normal
                escaped = False
            else:
                out[i] = " "
                if escaped:
                    escaped = False
                elif ch == "\\":
                    escaped = True
                elif (state == string and ch == '"') or (state == character and ch == "'"):
                    state = normal
        i += 1
    return "".join(out)

def split_top_level(text: str) -> list[str]:
    tokens: list[str] = []
    start: Optional[int] = None
    depth = 0
    for i, ch in enumerate(text):
        if ch == "(":
            if start is None:
                start = i
            depth += 1
        elif ch == ")":
            depth = max(0, depth - 1)
        if ch.isspace() and depth == 0:
            if start is not None:
                tokens.append(text[start:i])
                start = None
        elif start is None:
            start = i
    if start is not None:
        tokens.append(text[start:])
    return tokens

def clause_name(token: str) -> str:
    return token.split("(", 1)[0]

def pragma_iter(text: str) -> Iterator[Pragma]:
    starts = line_starts(text)
    lines = text.splitlines(keepends=True)
    offset = 0
    i = 0
    while i < len(lines):
        first = lines[i]
        logical = first
        end = offset + len(first)
        j = i
        while re.search(r"\\\s*(?:\r?\n)?$", logical):
            j += 1
            if j >= len(lines):
                break
            logical += lines[j]
            end += len(lines[j])
        flattened = re.sub(r"\\\s*\r?\n", " ", logical)
        match = re.match(
            r"(?s)^(?P<indent>[ \t]*)#[ \t]*pragma[ \t]+omp(?:[ \t]+(?P<body>.*?))?(?P<nl>\r?\n)?$",
            flattened,
        )
        if match:
            body = (match.group("body") or "").strip()
            yield Pragma(
                start=offset,
                end=end,
                line=line_number(starts, offset),
                indent=match.group("indent"),
                tokens=split_top_level(body),
                raw=text[offset:end],
                newline=match.group("nl") or ("\n" if logical.endswith("\n") else ""),
            )
        for k in range(i, j + 1):
            offset += len(lines[k])
        i = j + 1

def matching_delimiter(masked: str, opening: int, left: str, right: str) -> int:
    depth = 0
    for i in range(opening, len(masked)):
        if masked[i] == left:
            depth += 1
        elif masked[i] == right:
            depth -= 1
            if depth == 0:
                return i
    return -1

def functions_in(text: str) -> list[Function]:
    masked = mask_c_source(text)
    starts = line_starts(text)
    pattern = re.compile(
        r"(?m)^[ \t]*(?:(?:static|inline|extern)\s+)*"
        r"(?:[A-Za-z_]\w*[ \t\n*]+)+(?P<name>[A-Za-z_]\w*)"
        r"[ \t\n]*\([^;{}]*\)[ \t\n]*\{"
    )
    result: list[Function] = []
    for match in pattern.finditer(masked):
        if match.group("name") in {"if", "for", "while", "switch", "do"}:
            continue
        name_end = match.end("name")
        paren = masked.find("(", name_end, match.end())
        paren_close = matching_delimiter(masked, paren, "(", ")")
        brace = masked.find("{", match.start(), match.end())
        close = matching_delimiter(masked, brace, "{", "}")
        if paren < 0 or paren_close < 0 or close < 0:
            continue
        prefix = masked[match.start() : match.start("name")]
        result.append(
            Function(
                name=match.group("name"),
                start=match.start(),
                params_start=paren + 1,
                params_end=paren_close,
                body_start=brace,
                end=close + 1,
                line=line_number(starts, match.start()),
                is_static=bool(re.search(r"\bstatic\b", prefix)),
            )
        )
    return result

def apply_text_edits(text: str, edits: Iterable[TextEdit]) -> str:
    ordered = sorted(edits, key=lambda edit: (edit.start, edit.end), reverse=True)
    last_start = len(text) + 1
    for edit in ordered:
        if edit.end > last_start:
            raise ValueError("overlapping source edits")
        text = text[: edit.start] + edit.replacement + text[edit.end :]
        last_start = edit.start
    return text

def function_at(functions: Sequence[Function], offset: int) -> Optional[Function]:
    candidates = [fn for fn in functions if fn.body_start < offset < fn.end]
    return min(candidates, key=lambda fn: fn.end - fn.body_start) if candidates else None

def skip_c_space(masked: str, offset: int, end: int) -> int:
    while offset < end and masked[offset].isspace():
        offset += 1
    return offset

def word_at(masked: str, offset: int, word: str) -> bool:
    end = offset + len(word)
    if masked[offset:end] != word:
        return False
    before_ok = offset == 0 or not (masked[offset - 1].isalnum() or masked[offset - 1] == "_")
    after_ok = end >= len(masked) or not (masked[end].isalnum() or masked[end] == "_")
    return before_ok and after_ok

def c_statement_end(masked: str, offset: int, limit: int) -> int:
    offset = skip_c_space(masked, offset, limit)
    if offset >= limit:
        return offset
    if masked[offset] == "#":
        newline = masked.find("\n", offset, limit)
        return limit if newline < 0 else newline + 1
    if masked[offset] == "{":
        closing = matching_delimiter(masked, offset, "{", "}")
        return limit if closing < 0 or closing >= limit else closing + 1
    for keyword in ("if", "for", "while", "switch"):
        if not word_at(masked, offset, keyword):
            continue
        opening = masked.find("(", offset + len(keyword), limit)
        if opening < 0:
            return limit
        closing = matching_delimiter(masked, opening, "(", ")")
        if closing < 0 or closing >= limit:
            return limit
        statement_end = c_statement_end(masked, closing + 1, limit)
        if keyword == "if":
            cursor = skip_c_space(masked, statement_end, limit)
            if word_at(masked, cursor, "else"):
                statement_end = c_statement_end(masked, cursor + len("else"), limit)
        return statement_end
    if word_at(masked, offset, "do"):
        body_end = c_statement_end(masked, offset + len("do"), limit)
        cursor = skip_c_space(masked, body_end, limit)
        if word_at(masked, cursor, "while"):
            opening = masked.find("(", cursor + len("while"), limit)
            if opening >= 0:
                closing = matching_delimiter(masked, opening, "(", ")")
                if 0 <= closing < limit:
                    semicolon = masked.find(";", closing + 1, limit)
                    return limit if semicolon < 0 else semicolon + 1
        return body_end
    parens = brackets = braces = 0
    for cursor in range(offset, limit):
        char = masked[cursor]
        if char == "(":
            parens += 1
        elif char == ")":
            parens = max(0, parens - 1)
        elif char == "[":
            brackets += 1
        elif char == "]":
            brackets = max(0, brackets - 1)
        elif char == "{":
            braces += 1
        elif char == "}":
            if braces == 0 and parens == 0 and brackets == 0:
                return cursor
            braces = max(0, braces - 1)
        elif char == ";" and parens == 0 and brackets == 0 and braces == 0:
            return cursor + 1
    return limit

def associated_statement(masked: str, pragma_end: int, directive: str) -> tuple[int, int]:
    i = pragma_end
    while i < len(masked) and masked[i].isspace():
        i += 1
    if directive in {"for", "loop"}:
        match = re.match(r"for\s*\(", masked[i:])
        if not match:
            return (i, i)
        paren = masked.find("(", i, i + match.end())
        close = matching_delimiter(masked, paren, "(", ")")
        if close < 0:
            return (i, i)
        i = close + 1
        while i < len(masked) and masked[i].isspace():
            i += 1
        if i < len(masked) and masked[i] == "{":
            close = matching_delimiter(masked, i, "{", "}")
            return (i, close + 1 if close >= 0 else i)
        semicolon = masked.find(";", i)
        return (i, semicolon + 1 if semicolon >= 0 else i)
    if masked[i] == "{":
        close = matching_delimiter(masked, i, "{", "}")
        return (i, close + 1 if close >= 0 else i)
    semicolon = masked.find(";", i)
    return (i, semicolon + 1 if semicolon >= 0 else i)

def is_apply_entry_name(name: str) -> bool:
    return bool(re.search(r"_apply(?:N|\d+)?\Z", name))

def transform_by_grammar(unit: UnitFile) -> None:
    text = unit.migrated
    masked = mask_c_source(text)
    starts = line_starts(text)
    functions = functions_in(text)
    
    edits: list[TextEdit] = []
    
    for func in functions:
        if not is_apply_entry_name(func.name):
            continue
            
        body_start = func.body_start + 1
        body_end = func.end - 1
        
        events = []
        
        for pragma in pragma_iter(text):
            if not (body_start <= pragma.start < body_end):
                continue
                
            tokens = pragma.tokens
            if not tokens:
                continue
                
            directive = tokens[0]
            
            if directive == 'parallel':
                if len(tokens) > 1 and tokens[1] in ('for', 'sections', 'loop'):
                    new_body = ' '.join(tokens[1:])
                    new_pragma = f"{pragma.indent}#pragma omp {new_body}{pragma.newline}"
                    edits.append(TextEdit(pragma.start, pragma.end, new_pragma))
                    unit.changes.append(
                        Change(str(unit.path), pragma.line, "CONVERT_COMBINED_DIRECTIVE",
                               pragma.raw.rstrip("\r\n"), new_pragma.rstrip("\r\n"))
                    )
                else:
                    edits.append(TextEdit(pragma.start, pragma.end, ""))
                    unit.changes.append(
                        Change(str(unit.path), pragma.line, "REMOVE_PARALLEL_REGION",
                               pragma.raw.rstrip("\r\n"), "<removed>")
                    )
                    
                assoc_start, assoc_end = associated_statement(masked, pragma.end, directive)
                if assoc_start < assoc_end:
                    events.append((pragma.start, 'enter', 'MTC'))
                    events.append((assoc_end, 'exit', 'MTC'))
                    
            elif directive in ('single', 'master'):
                assoc_start, assoc_end = associated_statement(masked, pragma.end, directive)
                if assoc_start < assoc_end:
                    events.append((pragma.start, 'enter', 'PROTECTED'))
                    events.append((assoc_end, 'exit', 'PROTECTED'))
                    
            elif directive in ('for', 'sections', 'simd', 'loop'):
                assoc_start, assoc_end = associated_statement(masked, pragma.end, directive)
                if assoc_start < assoc_end:
                    events.append((pragma.start, 'enter', 'MTC'))
                    events.append((assoc_end, 'exit', 'MTC'))
        
        events.sort(key=lambda x: (x[0], 0 if x[1] == 'exit' else 1))
        
        stc_regions = []
        current_stc_start = None
        
        stack = ['STC']
        event_idx = 0
        
        offsets = sorted(list(set([body_start] + [e[0] for e in events] + [body_end])))
        
        for i in range(len(offsets) - 1):
            seg_start = offsets[i]
            seg_end = offsets[i+1]
            
            while event_idx < len(events) and events[event_idx][0] == seg_start:
                _, action, ctx = events[event_idx]
                if action == 'enter':
                    stack.append(ctx)
                elif action == 'exit':
                    if stack and stack[-1] == ctx:
                        stack.pop()
                event_idx += 1
                
            current_ctx = stack[-1] if stack else 'STC'
            
            if current_ctx == 'STC':
                if current_stc_start is None:
                    current_stc_start = seg_start
            else:
                if current_stc_start is not None:
                    stc_regions.append((current_stc_start, seg_start))
                    current_stc_start = None
                    
        if current_stc_start is not None:
            stc_regions.append((current_stc_start, body_end))
            
        for reg_start, reg_end in stc_regions:
            s = reg_start
            while s < reg_end and masked[s].isspace():
                s += 1
            e = reg_end - 1
            while e > s and masked[e].isspace():
                e -= 1
            e += 1
            
            if s >= e:
                continue
                
            line_start = text.rfind("\n", 0, s) + 1
            indent = text[line_start:s]
            pragma_indent = indent if not indent.strip() else ("\t" if "\t" in indent else "    ")
            newline = "\n"
            
            if masked[s] == '{' and masked[e-1] == '}':
                replacement = f"{pragma_indent}#pragma omp single{newline}{indent}"
                edits.append(TextEdit(s, s, replacement))
                unit.changes.append(
                    Change(str(unit.path), line_number(starts, s), "INSERT_SINGLE_GRAMMAR",
                           "<stc block>", f"#pragma omp single {{ ... }}")
                )
            else:
                replacement_start = f"{pragma_indent}#pragma omp single{newline}{pragma_indent}{{{newline}{indent}"
                replacement_end = f"{newline}{pragma_indent}}}"
                edits.append(TextEdit(s, s, replacement_start))
                edits.append(TextEdit(e, e, replacement_end))
                unit.changes.append(
                    Change(str(unit.path), line_number(starts, s), "INSERT_SINGLE_GRAMMAR",
                           "<stc block>", f"#pragma omp single {{ ... }}")
                )

    unit.migrated = apply_text_edits(text, edits)

def remove_unused_parallel_mode_assignments(unit: UnitFile) -> None:
    text = unit.migrated
    masked = mask_c_source(text)
    if not re.search(
        r"\bvj_effect\s*\*\s*ve\s*=\s*\([^;]+\)\s*"
        r"vj_calloc\s*\(\s*sizeof\s*\(\s*vj_effect\s*\)\s*\)\s*;",
        masked,
    ):
        return
    assignments = list(
        re.finditer(r"\bve\s*->\s*parallel\s*=\s*\(?\s*1\s*\)?\s*;", masked)
    )
    if assignments:
        spans = [(m.start(), m.end()) for m in assignments]
        text = unit.migrated
        starts = line_starts(text)
        expanded = []
        for start, end in spans:
            line_start = text.rfind("\n", 0, start) + 1
            line_end = text.find("\n", end)
            if line_end < 0: line_end = len(text)
            else: line_end += 1
            if not text[line_start:start].strip() and not text[end:line_end].strip():
                start, end = line_start, line_end
            expanded.append((start, end))
        unit.changes.append(
            Change(str(unit.path), line_number(starts, spans[0][0]), "REMOVE_UNUSED_PARALLEL_MODE",
                   text[spans[0][0]:spans[0][1]].rstrip("\r\n"), "")
        )
        unit.migrated = apply_text_edits(text, [TextEdit(s, e, "") for s, e in expanded])

def cleanup_thread_bookkeeping(units: Sequence[UnitFile]) -> None:
    for unit in units:
        text = unit.migrated
        masked = mask_c_source(text)
        starts = line_starts(text)
        
        assignment_re = re.compile(r"\b[A-Za-z_]\w*\s*->\s*n_threads\s*=\s*[^;]+;")
        matches = list(assignment_re.finditer(masked))
        if matches:
            spans = [(m.start(), m.end()) for m in matches]
            expanded = []
            for start, end in spans:
                line_start = text.rfind("\n", 0, start) + 1
                line_end = text.find("\n", end)
                if line_end < 0: line_end = len(text)
                else: line_end += 1
                if not text[line_start:start].strip() and not text[end:line_end].strip():
                    start, end = line_start, line_end
                expanded.append((start, end))
            unit.changes.append(
                Change(str(unit.path), line_number(starts, spans[0][0]), "REMOVE_THREAD_FIELD_ASSIGNMENT",
                       "<removed>", "")
            )
            unit.migrated = apply_text_edits(text, [TextEdit(s, e, "") for s, e in expanded])

def audit_unit(unit: UnitFile) -> None:
    text = unit.migrated
    starts = line_starts(text)
    
    for pragma in pragma_iter(text):
        if pragma.tokens and pragma.tokens[0] == 'parallel':
            unit.findings.append(
                Finding(str(unit.path), pragma.line, BLOCKED, "REMAINING_PARALLEL",
                        "A #pragma omp parallel was not removed. Manual review required.",
                        pragma.raw.strip())
            )

def overall_status(units: Sequence[UnitFile]) -> str:
    severities = {finding.severity for unit in units for finding in unit.findings}
    if BLOCKED in severities: return BLOCKED
    if REVIEW in severities: return REVIEW
    return "safe"

def report_dict(units: Sequence[UnitFile]) -> dict[str, object]:
    status = overall_status(units)
    return {
        "tool": "vje_omp_migrate",
        "version": VERSION,
        "status": status,
        "summary": {
            "files": len(units),
            "changed_files": sum(unit.changed for unit in units),
            "changes": sum(len(unit.changes) for unit in units),
            "blocked": sum(finding.severity == BLOCKED for unit in units for finding in unit.findings),
            "review_required": sum(finding.severity == REVIEW for unit in units for finding in unit.findings),
        },
        "files": [
            {
                "path": str(unit.path),
                "changed": unit.changed,
                "sha256_before": hashlib.sha256(unit.original.encode()).hexdigest(),
                "sha256_after": hashlib.sha256(unit.migrated.encode()).hexdigest(),
                "changes": [change.as_dict() for change in unit.changes],
                "findings": [finding.as_dict() for finding in unit.findings],
            }
            for unit in units
        ],
    }

def read_source(path: Path) -> str:
    with path.open("r", encoding="utf-8", newline="") as stream:
        return stream.read()

def migrate(paths: Sequence[Path]) -> list[UnitFile]:
    units: list[UnitFile] = []
    for path in paths:
        source = read_source(path)
        units.append(UnitFile(path=path, original=source, migrated=source))
        
    for unit in units:
        remove_unused_parallel_mode_assignments(unit)
        transform_by_grammar(unit)
        cleanup_thread_bookkeeping([unit])
        audit_unit(unit)
        
    return units

def print_human_report(units: Sequence[UnitFile]) -> None:
    report = report_dict(units)
    summary = report["summary"]
    assert isinstance(summary, dict)
    print(
        f"vje_omp_migrate {VERSION}: {report['status']} —  "
        f"{summary['changed_files']}/{summary['files']} files changed, "
        f"{summary['changes']} edits, {summary['blocked']} blockers, "
        f"{summary['review_required']} review findings"
    )
    for unit in units:
        print(f"\n{unit.path}")
        for change in unit.changes:
            after = change.after.strip() or "<removed>"
            print(f"  L{change.line:<5} EDIT   {change.code}: {after}")
        for finding in unit.findings:
            level = {INFO: "INFO", REVIEW: "REVIEW", BLOCKED: "BLOCK"}[finding.severity]
            print(f"  L{finding.line:<5} {level:<6} {finding.code}: {finding.message}")

def write_outputs(units: Sequence[UnitFile], output_dir: Optional[Path], in_place: bool) -> None:
    if not output_dir and not in_place:
        return
    if output_dir:
        output_dir.mkdir(parents=True, exist_ok=True)
    for unit in units:
        target = unit.path if in_place else output_dir / unit.path.name
        if in_place:
            mode = unit.path.stat().st_mode
            handle, temporary_name = tempfile.mkstemp(prefix=unit.path.name + ".", dir=unit.path.parent)
            try:
                with os.fdopen(handle, "w", encoding="utf-8", newline="") as stream:
                    stream.write(unit.migrated)
                os.chmod(temporary_name, mode)
                os.replace(temporary_name, unit.path)
            finally:
                if os.path.exists(temporary_name):
                    os.unlink(temporary_name)
        else:
            target.write_text(unit.migrated, encoding="utf-8", newline="")
            shutil.copymode(unit.path, target)

def is_target_c_file(path: Path) -> bool:
    if not path.is_file() or path.suffix != ".c":
        return False
    try:
        text = read_source(path)
        if "_apply" not in text or "_init" not in text:
            return False
            
        funcs = functions_in(text)
        has_apply = any("_apply" in f.name for f in funcs)
        has_init = any("_init" in f.name for f in funcs)
        
        return has_apply and has_init
    except Exception:
        return False

def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Grammar-based OpenMP migration tool for VeeJay effects.",
        epilog="Exit status: 0 safe, 2 review required, 3 blocked, 4 input failure.",
    )
    parser.add_argument("--version", action="version", version=f"%(prog)s {VERSION}")
    parser.add_argument("source", type=Path, nargs="+", help="effect .c files or directory paths")
    destination = parser.add_mutually_exclusive_group()
    destination.add_argument("-o", "--output-dir", type=Path, help="write migrated copies here")
    destination.add_argument("--in-place", action="store_true", help="atomically replace input files")
    parser.add_argument("--json-report", type=Path, help="write a machine-readable report")
    parser.add_argument("--quiet", action="store_true", help="suppress the human report")
    return parser.parse_args(argv)

def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    
    expanded_paths: list[Path] = []
    for path in args.source:
        if path.is_file():
            expanded_paths.append(path)
        elif path.is_dir():
            for child in path.rglob("*.c"):
                if is_target_c_file(child):
                    expanded_paths.append(child)
        else:
            print(f"Input path not found: {path}", file=sys.stderr)
            return 4
            
    if not expanded_paths:
        print("No valid target .c files found in the provided sources.", file=sys.stderr)
        return 4
            
    units = migrate(expanded_paths)
    status = overall_status(units)
    
    if args.in_place and status != "safe":
        print("Refusing --in-place because the migration is not chain-ready.", file=sys.stderr)
        if not args.quiet:
            print_human_report(units)
        return 3 if status == BLOCKED else 2
        
    write_outputs(units, args.output_dir, args.in_place)
    
    if args.json_report:
        args.json_report.parent.mkdir(parents=True, exist_ok=True)
        report = report_dict(units)
        args.json_report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        
    if not args.quiet:
        print_human_report(units)
        
    return 3 if status == BLOCKED else 2 if status == REVIEW else 0

if __name__ == "__main__":
    raise SystemExit(main())