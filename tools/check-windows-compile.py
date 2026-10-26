#!/usr/bin/env python3
"""Cross-compile Windows code to COFF; does not link or run Explorer/Windhawk."""
import argparse
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', required=True, type=Path,
                        help='Directory containing official windhawk_{api,api_internal,utils}.h')
    parser.add_argument('--zig', default='zig', help='Zig executable')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    sdk = args.sdk.resolve()
    for name in ['windhawk_api.h', 'windhawk_api_internal.h', 'windhawk_utils.h']:
        if not (sdk / name).is_file():
            parser.error(f'Missing official header: {sdk / name}')
    source = (root / 'sleekwintabs.wh.cpp').read_text()
    version = re.search(r'^// @version\s+(\S+)', source, re.M).group(1)
    with tempfile.TemporaryDirectory(prefix='sleekwintabs-windows-') as temporary:
        command = [args.zig, 'c++', '-target', 'x86_64-windows-gnu', '-std=c++23',
                   '-DWH_MOD', '-DWH_MOD_ID=L"local@sleek-wintabs"',
                   f'-DWH_MOD_VERSION=L"{version}"', '-DUNICODE', '-D_UNICODE',
                   '-I' + str(sdk), '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-private-field', '-c', str(root / 'sleekwintabs.wh.cpp'),
                   '-o', str(Path(temporary) / 'sleekwintabs.obj')]
        print(shlex.join(command), flush=True)
        subprocess.run(command, check=True, cwd=root)
        data = (Path(temporary) / 'sleekwintabs.obj').read_bytes()
        if data[:2] != b'\x64\x86':
            raise RuntimeError('Expected an AMD64 COFF object')
        print('PASS: AMD64 COFF object; NOT a linked Windhawk mod or Windows runtime test')


if __name__ == '__main__':
    main()
