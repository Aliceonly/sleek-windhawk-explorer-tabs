#!/usr/bin/env python3
"""Check the approved 45-key contract and the smaller, implemented Windhawk UI."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read_schema(source):
    block = source.split('constexpr SettingSpec kSettings[] = {', 1)[1].split('\n};', 1)[0]
    rows = []
    for line in block.splitlines():
        match = re.match(
            r'\s*\{L"([^"]+)", SettingKind::(\w+), L"([^"]*)", L"([^"]*)"(.*?)\},', line)
        if match:
            key, kind, default, choices, tail = match.groups()
            rows.append((key, kind, default, choices, bool(re.search(r', true\s*$', tail))))
    return rows


def read_exposed(source):
    block = source.split('// ==WindhawkModSettings==', 1)[1].split('// ==/WindhawkModSettings==', 1)[0]
    result = {}
    parent = ''
    for line in block.splitlines():
        match = re.match(r'^( *)- ([A-Za-z]\w*):(?: (.+))?$', line)
        if not match:
            continue
        indent, key, value = match.groups()
        if not indent:
            parent = '' if value is not None else key
            if value is not None:
                result[key] = value
        elif len(indent) == 2 and parent:
            result[parent + '.' + key] = value
    return result


def main():
    source = (ROOT / 'sleekwintabs.wh.cpp').read_text()
    schema = read_schema(source)
    assert len(schema) == 45, f'Expected 45 contract keys, got {len(schema)}'
    keys = {row[0] for row in schema}
    assert len(keys) == 45, 'Duplicate schema keys'
    plan = (ROOT / 'docs/implementation-plan.md').read_text().split('## D ', 1)[1].split('## E ', 1)[0]
    planned = set(re.findall(r'^\| `([\w.]+)` \|', plan, re.M))
    assert keys == planned, f'Contract drift: {keys ^ planned}'
    exposed = read_exposed(source)
    expected = {key: default for key, _, default, _, active in schema if active}
    assert exposed == expected, f'UI/default mismatch: {exposed} != {expected}'
    contract = (ROOT / 'docs/settings-contract.md').read_text()
    for key, _, _, _, active in schema:
        assert f'`{key}`' in contract, f'Undocumented key: {key}'
        if active:
            assert source.count(f'L"{key}"') >= 2, f'No direct consumer for exposed key: {key}'
    assert expected.get('duplicate.shortcut') is None, 'Do not expose an unconnected shortcut'
    assert dict((row[0], row[2]) for row in schema)['duplicate.shortcut'] == 'Ctrl+Q'
    print(f'PASS: {len(schema)} contract keys, {len(exposed)} exposed/consumed, {len(schema) - len(exposed)} staged')


if __name__ == '__main__':
    main()
