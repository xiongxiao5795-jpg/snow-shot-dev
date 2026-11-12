#!/usr/bin/env python3
"""Use CMake's forced prefix header instead of its binary PCH in fork CI.

Only rewrite CMake's Clang PCH-consumer arguments. PCH generation, explicit
source opt-outs, unity batches and all other compiler arguments are unchanged.
"""

import os
from pathlib import Path
import re
import sys


def rewrite(arguments):
    result = []
    headers = set()
    i = 0
    pattern = re.compile(r'^cmake_pch(?:\.objcxx)?\.hxx\.pch$')
    while i < len(arguments):
        size = 0
        pch = None
        if arguments[i:i + 2] == ['-Xclang', '-include-pch'] and i + 3 < len(arguments):
            if arguments[i + 2] == '-Xclang':
                pch, size = arguments[i + 3], 4
        elif arguments[i] == '-include-pch' and i + 1 < len(arguments):
            pch, size = arguments[i + 1], 2
        if pch and pattern.fullmatch(Path(pch).name):
            header = pch[:-4]
            if not Path(header).is_file():
                raise ValueError(f'CMake PCH prefix does not exist: {header}')
            result.extend(['-include', header])
            headers.add(header)
            i += size
        else:
            result.append(arguments[i])
            i += 1

    # Some CMake/Clang versions also force-include the same generated header.
    # Keep exactly one ordinary -include so ccache can track the header safely.
    cleaned = []
    seen = set()
    i = 0
    while i < len(result):
        header = None
        size = 0
        if result[i:i + 2] == ['-Xclang', '-include'] and i + 3 < len(result):
            if result[i + 2] == '-Xclang':
                header, size = result[i + 3], 4
        elif result[i] == '-include' and i + 1 < len(result):
            header, size = result[i + 1], 2
        if header in headers:
            if header not in seen:
                cleaned.extend(['-include', header])
                seen.add(header)
            i += size
        else:
            cleaned.append(result[i])
            i += 1
    return cleaned


def main():
    if len(sys.argv) < 2:
        raise ValueError('Compiler command is missing')
    os.execvp('ccache', ['ccache', *rewrite(sys.argv[1:])])


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError) as error:
        print(f'macOS compiler cache launcher: {error}', file=sys.stderr)
        sys.exit(1)
