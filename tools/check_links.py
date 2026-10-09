#!/usr/bin/env python3
"""Fail on a broken relative link or image in any tracked Markdown file.

    tools/check_links.py            check every tracked *.md (exit 1 and list each broken link)

Checks [text](path), ![alt](path), [name]: path and <img src>/<a href> targets that are not URLs:
the file or folder must exist in the checkout, and a #heading on a Markdown target must exist in it
(GitHub-style anchors). Links inside code blocks and inline code are ignored. Only the standard
library is used.
"""
import pathlib
import re
import subprocess
import sys
from urllib.parse import unquote

ROOT = pathlib.Path(__file__).resolve().parents[1]
INLINE = re.compile(r'!?\[(?:[^\[\]]|\[[^\]]*\])*\]\(\s*<?([^)\s>]+)>?(?:\s+"[^"]*")?\s*\)')
REFDEF = re.compile(r'^\s{0,3}\[[^\]]+\]:\s*<?(\S+?)>?(?:\s+"[^"]*")?\s*$', re.M)
HTML = re.compile(r'<(?:img|a)\b[^>]*?\b(?:src|href)\s*=\s*"([^"]+)"', re.I)
EXTERNAL = re.compile(r'^(?:[a-z][a-z0-9+.-]*:|//)', re.I)


def strip_code(text, inline=True):
    """Blank fenced code blocks and inline code spans (keeps line numbers); inline=False keeps the
    text of inline code (headings: GitHub anchors include it)."""
    out, fence = [], None
    for line in text.split('\n'):
        m = re.match(r'^\s{0,3}(`{3,}|~{3,})', line)
        if fence:
            if m and m.group(1)[0] == fence[0] and len(m.group(1)) >= len(fence):
                fence = None
            out.append('')
            continue
        if m:
            fence = m.group(1)
            out.append('')
            continue
        out.append(re.sub(r'(`+)((?:(?!\1).)+?)\1', '' if inline else r'\2', line))
    return '\n'.join(out)


def slug(heading):
    text = re.sub(r'!?\[([^\]]*)\]\([^)]*\)', r'\1', heading)  # [text](url) -> text
    text = re.sub(r'<[^>]+>', '', text).strip().lower()
    text = re.sub(r'[^\w\- ]', '', text)
    return text.replace(' ', '-')


def anchors(path, cache={}):
    if path not in cache:
        found, seen = set(), {}
        for line in strip_code(path.read_text(encoding='utf-8'), inline=False).split('\n'):
            m = re.match(r'^\s{0,3}#{1,6}\s+(.*?)\s*#*\s*$', line)
            if m:
                base = slug(m.group(1))
                n = seen.get(base, 0)
                seen[base] = n + 1
                found.add(base if n == 0 else f'{base}-{n}')
        for m in re.finditer(r'<a\s+(?:name|id)="([^"]+)"', path.read_text(encoding='utf-8')):
            found.add(m.group(1))
        cache[path] = found
    return cache[path]


def check(md):
    problems = []
    text = strip_code(md.read_text(encoding='utf-8'))
    for regex in (INLINE, REFDEF, HTML):
        for m in regex.finditer(text):
            target = m.group(1)
            if EXTERNAL.match(target):
                continue
            line = text.count('\n', 0, m.start()) + 1
            path_part, _, frag = target.partition('#')
            path_part = unquote(path_part)
            if path_part:
                dest = (ROOT / path_part.lstrip('/')) if path_part.startswith('/') else (md.parent / path_part)
            else:
                dest = md
            dest = dest.resolve()
            if not dest.exists():
                problems.append(f'{md.relative_to(ROOT)}:{line}: missing {target}')
            elif ROOT not in dest.parents and dest != ROOT:
                problems.append(f'{md.relative_to(ROOT)}:{line}: outside the repo {target}')
            elif frag and dest.suffix.lower() == '.md' and unquote(frag).lower() not in anchors(dest):
                problems.append(f'{md.relative_to(ROOT)}:{line}: no heading #{frag} in {target}')
    return problems


def main():
    # tracked files plus new ones not yet added (git-ignored files are skipped)
    files = subprocess.run(['git', 'ls-files', '-z', '-co', '--exclude-standard', '--', '*.md'], cwd=ROOT, check=True,
                           capture_output=True, text=True).stdout.split('\0')
    mds = [ROOT / f for f in files if f and (ROOT / f).is_file()]
    problems = [p for md in mds for p in check(md)]
    for p in problems:
        print(p)
    print(f'check_links: {len(mds)} Markdown files, {len(problems)} broken link(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
