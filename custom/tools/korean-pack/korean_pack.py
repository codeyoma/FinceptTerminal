#!/usr/bin/env python3
"""Find, check and add new strings for the Korean language pack (codeyoma/krx-quant#132).

  korean_pack.py extract <new.json>              list source strings the Korean pack lacks
  korean_pack.py check <new.json> <done.json>    check a translation file before merging
  korean_pack.py merge <new.json> <done.json>    add the translations to fincept_ko_KR.ts

<done.json> maps each entry id (a string) to its Korean text; a numerus entry maps to a list
with one form that keeps %n. See custom/README.md, "한국어 언어 팩".
"""
import collections
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

FINCEPT_QT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../fincept-qt'))
KOREAN_TS = os.path.join(FINCEPT_QT, 'translations', 'fincept_ko_KR.ts')
LUPDATE_TIMEOUT_S = 600

# lupdate 6.8's C++ parser never finishes this file (it has no tr() calls).
SKIP_FILES = {'src/services/news/RssFeedProbeService.cpp'}

# Class::tr() calls inside an anonymous namespace: lupdate drops the enclosing namespace, while
# tr() at run time uses the meta-object's qualified class name.
CONTEXT_FIXES = {
    'PythonSetupManager': 'fincept::python::PythonSetupManager',
    'ScreenerSubTab': 'fincept::screens::fno::ScreenerSubTab',
    'LegEditorTable': 'fincept::screens::fno::LegEditorTable',
}

HANGUL = re.compile('[가-힣]')
PLACEHOLDER = re.compile(r'%L?(?:[1-9][0-9]?|n)')
TAG = re.compile(r'</?([a-zA-Z][a-zA-Z0-9]*)\b')


def lupdate_binary():
    qt_bin = os.environ.get('QT_BIN')
    candidates = [os.path.join(qt_bin, 'lupdate')] if qt_bin else []
    candidates.append(shutil.which('lupdate') or '')
    candidates.append(os.path.expanduser('~/Qt/6.8.3/macos/bin/lupdate'))
    for path in candidates:
        if path and os.access(path, os.X_OK):
            return path
    sys.exit('lupdate not found: set QT_BIN to the Qt bin directory')


def extract(out_path):
    sources = []
    for root, _, files in os.walk(os.path.join(FINCEPT_QT, 'src')):
        for name in files:
            if name.endswith(('.cpp', '.h', '.ui')):
                rel = os.path.relpath(os.path.join(root, name), FINCEPT_QT)
                if rel not in SKIP_FILES:
                    sources.append(rel)
    with tempfile.TemporaryDirectory() as tmp:
        scan = os.path.join(tmp, 'scan.ts')
        shutil.copy(KOREAN_TS, scan)
        listing = os.path.join(tmp, 'files.lst')
        with open(listing, 'w') as f:
            f.write('\n'.join(sorted(sources)))
        subprocess.run([lupdate_binary(), '-I', 'src', '-I', '.', '@' + listing, '-ts', scan,
                        '-locations', 'absolute', '-silent'],
                       cwd=FINCEPT_QT, check=True, timeout=LUPDATE_TIMEOUT_S,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        scanned = ET.parse(scan).getroot()
    scan_dir = os.path.dirname(scan)

    def location(loc):
        # lupdate writes file names relative to the .ts file's directory as given. Resolve the ".."
        # lexically: realpath would follow /var -> /private/var first and climb one level short.
        path = os.path.realpath(os.path.normpath(os.path.join(scan_dir, loc.get('filename'))))
        return f"{os.path.relpath(path, os.path.realpath(FINCEPT_QT))}:{loc.get('line')}"

    existing = set()
    by_text = collections.defaultdict(set)
    for context in ET.parse(KOREAN_TS).getroot().findall('context'):
        name = context.findtext('name')
        for message in context.findall('message'):
            source = message.findtext('source')
            existing.add((name, source, message.findtext('comment')))
            translation = message.find('translation')
            if translation.text:
                by_text[source].add(translation.text)

    entries = []
    for context in scanned.findall('context'):
        name = context.findtext('name')
        for message in context.findall('message'):
            kind = message.find('translation').get('type')
            if kind in ('vanished', 'obsolete'):
                continue
            # A finished entry under a bare context (the common pack has some) still needs its
            # qualified twin, or the string stays English at run time.
            if kind != 'unfinished' and name not in CONTEXT_FIXES:
                continue
            source = message.findtext('source')
            if HANGUL.search(source):  # KRX screens write Korean in tr() already
                continue
            name_fixed = CONTEXT_FIXES.get(name, name)
            comment = message.findtext('comment')
            if (name_fixed, source, comment) in existing:
                continue
            entry = {'id': len(entries), 'context': name_fixed, 'source': source,
                     'locations': [location(loc) for loc in message.findall('location')][:3]}
            if comment:
                entry['comment'] = comment
            if message.get('numerus') == 'yes':
                entry['numerus'] = True
            if by_text.get(source):
                entry['existing_translations_elsewhere'] = sorted(by_text[source])
            entries.append(entry)
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(entries, f, ensure_ascii=False, indent=1)
    print(f'{len(entries)} new strings in {len({e["context"] for e in entries})} contexts -> {out_path}')


def edge_spaces(text):
    return len(text) - len(text.lstrip(' \t')), len(text) - len(text.rstrip(' \t'))


def check(entries, done):
    errors = []
    for entry in entries:
        key = str(entry['id'])
        if key not in done:
            errors.append(f'{key}: missing')
            continue
        text = done[key]
        if entry.get('numerus'):
            if not (isinstance(text, list) and len(text) == 1 and isinstance(text[0], str)):
                errors.append(f'{key}: numerus entry needs a list with one form')
                continue
            text = text[0]
        elif not isinstance(text, str):
            errors.append(f'{key}: not a string')
            continue
        source = entry['source']
        if not text.strip():
            errors.append(f'{key}: empty')
        if collections.Counter(PLACEHOLDER.findall(source)) != collections.Counter(PLACEHOLDER.findall(text)):
            errors.append(f'{key}: placeholders differ')
        if source.count('\n') != text.count('\n'):
            errors.append(f'{key}: line count differs')
        if collections.Counter(t.lower() for t in TAG.findall(source)) != \
                collections.Counter(t.lower() for t in TAG.findall(text)):
            errors.append(f'{key}: HTML tags differ')
        if edge_spaces(source) != edge_spaces(text):
            errors.append(f'{key}: leading or trailing spaces differ')
        if re.search(r'(^|\s)&[A-Za-z]', source) and '&' not in text:
            errors.append(f'{key}: shortcut & dropped')
    unknown = set(done) - {str(e['id']) for e in entries}
    if unknown:
        errors.append(f'unknown ids: {sorted(unknown)[:10]}')
    return errors


def escape(text):
    return text.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def message_xml(entry, text):
    lines = ['        <message numerus="yes">' if entry.get('numerus') else '        <message>',
             f"            <source>{escape(entry['source'])}</source>"]
    if entry.get('comment'):
        lines.append(f"            <comment>{escape(entry['comment'])}</comment>")
    if entry.get('numerus'):
        lines.append('            <translation>')
        lines += [f'                <numerusform>{escape(form)}</numerusform>' for form in text]
        lines.append('            </translation>')
    else:
        lines.append(f'            <translation>{escape(text)}</translation>')
    lines.append('        </message>')
    return '\n'.join(lines) + '\n'


def merge(entries, done):
    """Existing text stays as it is: new messages go at the end of their context, new contexts before </TS>."""
    with open(KOREAN_TS, encoding='utf-8') as f:
        text = f.read()
    context_ends = {}
    for match in re.finditer(r'    <context>\n        <name>(.*?)</name>\n.*?(    </context>\n)', text, re.S):
        name = match.group(1).replace('&lt;', '<').replace('&gt;', '>').replace('&amp;', '&')
        context_ends[name] = match.start(2)
    by_context = collections.OrderedDict()
    for entry in entries:
        by_context.setdefault(entry['context'], []).append(entry)
    inserts, new_contexts = [], []
    for name, group in by_context.items():
        body = ''.join(message_xml(e, done[str(e['id'])]) for e in group)
        if name in context_ends:
            inserts.append((context_ends[name], body))
        else:
            new_contexts.append(f'    <context>\n        <name>{escape(name)}</name>\n{body}    </context>\n')
    inserts.append((text.rindex('</TS>'), ''.join(new_contexts)))
    for position, body in sorted(inserts, key=lambda item: item[0], reverse=True):
        text = text[:position] + body + text[position:]
    with open(KOREAN_TS, 'w', encoding='utf-8') as f:
        f.write(text)
    print(f'added {len(entries)} messages ({len(new_contexts)} new contexts) to {KOREAN_TS}')


def main():
    if len(sys.argv) == 3 and sys.argv[1] == 'extract':
        return extract(sys.argv[2])
    if len(sys.argv) == 4 and sys.argv[1] in ('check', 'merge'):
        with open(sys.argv[2], encoding='utf-8') as f:
            entries = json.load(f)
        with open(sys.argv[3], encoding='utf-8') as f:
            done = json.load(f)
        errors = check(entries, done)
        if errors:
            sys.exit('\n'.join(errors))
        if sys.argv[1] == 'merge':
            return merge(entries, done)
        return print(f'OK {len(entries)} entries')
    sys.exit(__doc__)


if __name__ == '__main__':
    main()
