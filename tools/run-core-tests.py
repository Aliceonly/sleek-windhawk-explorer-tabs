#!/usr/bin/env python3
"""Build and run the real single-file policy core without Windows dependencies."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true', help='Enable ASan and UBSan')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    compiler = shlex.split(os.environ.get('CXX', 'clang++'))
    with tempfile.TemporaryDirectory(prefix='sleekwintabs-tests-') as temporary:
        output = Path(temporary) / 'core-tests'
        command = compiler + ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic']
        if args.sanitize:
            command += ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        else:
            command += ['-O2']
        command += [str(root / 'tests/core-tests.cpp'), '-o', str(output)]
        subprocess.run(command, check=True, cwd=root)
        subprocess.run([str(output)], check=True, cwd=root)


if __name__ == '__main__':
    main()
