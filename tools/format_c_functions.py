#!/usr/bin/env python3
"""Ensure exactly one empty line between adjacent top-level C function definitions.

Only changes whitespace between consecutive function definitions. Does not
rewrite comments, strings, preprocessor directives, or line endings.
"""
import argparse
from pathlib import Path
import re
import sys


def mask_noncode(source: str) -> str:
    """Replace comments, literals, and preprocessor lines with spaces, retaining newlines."""
    out = list(source)
    n = len(source)
    i = 0
    line_start = True
    while i < n:
        c = source[i]
        if line_start:
            j = i
            while j < n and source[j] in ' \t':
                j += 1
            if j < n and source[j] == '#':
                k = j
                while k < n:
                    if source[k] in '\r\n':
                        # A backslash before newline continues the directive.
                        prev = k - 1
                        if source[k] == '\n' and prev >= 0 and source[prev] == '\r':
                            prev -= 1
                        continued = prev >= 0 and source[prev] == '\\'
                        if not continued:
                            break
                    k += 1
                for x in range(i, k):
                    if source[x] not in '\r\n':
                        out[x] = ' '
                i = k
                line_start = False
                continue
        if c == '/' and i + 1 < n and source[i + 1] in ('/', '*'):
            block = source[i + 1] == '*'
            j = i + 2
            if block:
                while j < n and source[j:j+2] != '*/':
                    j += 1
                j = min(n, j + 2)
            else:
                while j < n and source[j] not in '\r\n':
                    j += 1
            for x in range(i, j):
                if source[x] not in '\r\n':
                    out[x] = ' '
            i = j
            continue
        if c in ('"', "'"):
            quote = c
            j = i + 1
            while j < n:
                if source[j] == '\\':
                    j += 2
                elif source[j] == quote:
                    j += 1
                    break
                else:
                    j += 1
            j = min(j, n)
            for x in range(i, j):
                if source[x] not in '\r\n':
                    out[x] = ' '
            i = j
            line_start = False
            continue
        if c not in ' \t\r\n':
            line_start = False
        if c in '\r\n':
            line_start = True
        i += 1
    return ''.join(out)


def is_function_prefix(prefix: str) -> bool:
    """Recognize ordinary C function declarators, not initializers or control blocks."""
    p = prefix.strip()
    if not p or '=' in p or not p.endswith(')'):
        return False
    # A function's final parameter-list closing parenthesis must be present.
    depth = 0
    opening = -1
    for i in range(len(p) - 1, -1, -1):
        if p[i] == ')':
            depth += 1
        elif p[i] == '(':
            depth -= 1
            if depth == 0:
                opening = i
                break
    if opening < 0:
        return False
    before = p[:opening].rstrip()
    m = re.search(r'([A-Za-z_]\w*)\s*$', before)
    if not m or m.group(1) in {'if', 'for', 'while', 'switch', 'return', 'sizeof'}:
        return False
    # Reject function-pointer variable definitions and compound constructs.
    if re.search(r'\(\s*\*\s*$', before):
        return False
    return True


def functions(masked: str):
    """Return (opening brace, closing brace) for top-level function bodies."""
    found = []
    depth = 0
    segment = 0
    func_open = None
    for i, c in enumerate(masked):
        if c == '{':
            if depth == 0:
                if is_function_prefix(masked[segment:i]):
                    func_open = i
                else:
                    func_open = None
            depth += 1
        elif c == '}':
            if depth:
                depth -= 1
                if depth == 0:
                    if func_open is not None:
                        found.append((func_open, i))
                    segment = i + 1
                    func_open = None
        elif c == ';' and depth == 0:
            segment = i + 1
    return found


def newline_for(source: str, position: int) -> str:
    # Prefer the newline on the line containing the closing brace.
    m = re.search(r'\r\n|\n|\r', source[position:])
    if m:
        return m.group()
    m = re.search(r'\r\n|\n|\r', source)
    return m.group() if m else '\n'


def format_source(source: str) -> str:
    masked = mask_noncode(source)
    spans = functions(masked)
    edits = []
    for (_, end), (next_open, _) in zip(spans, spans[1:]):
        gap_start = end + 1
        # Only when the next function's signature starts immediately after the
        # preceding function (ignoring whitespace and comments). This prevents
        # changing gaps containing unrelated top-level declarations.
        between = masked[gap_start:next_open]
        if ';' in between or (between.strip() and not is_function_prefix(between)):
            continue
        # Find the start of the next function signature, allowing multiline args.
        signature_match = re.search(r'\S', masked[gap_start:next_open])
        if not signature_match:
            continue
        signature_start = gap_start + signature_match.start()

        # A comment before the definition belongs to the next function. Format
        # the whitespace before that comment rather than rejecting the gap.
        content_match = re.search(r'\S', source[gap_start:signature_start])
        content_start = (gap_start + content_match.start()
                         if content_match else signature_start)
        whitespace = source[gap_start:content_start]
        if not whitespace or not whitespace.isspace():
            continue
        # Do not modify same-line definitions or comments.
        if not any(c in whitespace for c in '\r\n'):
            continue
        nl = newline_for(source, end)
        # Preserve the existing indentation on the comment/signature line.
        last_break = max(whitespace.rfind('\n'), whitespace.rfind('\r'))
        indent = whitespace[last_break + 1:]
        replacement = nl + nl + indent
        if whitespace != replacement:
            edits.append((gap_start, content_start, replacement))
    for a, b, replacement in reversed(edits):
        source = source[:a] + replacement + source[b:]
    return source


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paths', nargs='+', type=Path, help='C files or directories (recursively searched)')
    parser.add_argument('--check', action='store_true', help='Report files needing changes; do not write')
    args = parser.parse_args()
    paths = []
    for p in args.paths:
        if p.is_dir():
            paths.extend(sorted(p.rglob('*.c')))
        elif p.is_file() and p.suffix.lower() == '.c':
            paths.append(p)
        else:
            print(f'Skipping: {p}', file=sys.stderr)
    changed = 0
    for p in dict.fromkeys(paths):
        raw = p.read_bytes()
        # latin-1 is byte-preserving, including arbitrary source encodings.
        before = raw.decode('latin-1')
        after = format_source(before)
        if after != before:
            changed += 1
            print(f'{"Needs formatting" if args.check else "Formatted"}: {p}')
            if not args.check:
                p.write_bytes(after.encode('latin-1'))
    return 1 if args.check and changed else 0


if __name__ == '__main__':
    sys.exit(main())
