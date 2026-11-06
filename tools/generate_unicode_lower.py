"""Offline: freeze CPython's Unicode lower/case properties for native queries.

No runtime dependency. Run with the same Python version as the index producer.
"""
import ctypes
import unicodedata
from pathlib import Path


def ranges(predicate):
    result = []
    first = last = None
    for cp in range(0x110000):
        if predicate(cp):
            if first is None:
                first = cp
            last = cp
        elif first is not None:
            result.append((first, last))
            first = None
    if first is not None:
        result.append((first, last))
    return result


def main():
    result = ['/* Generated Unicode ' + unicodedata.unidata_version + ' data; tools/generate_unicode_lower.py. */']
    result += ['typedef struct { uint32_t cp, lower[3]; unsigned length; } LowerMapping;',
               'static const LowerMapping lower_mappings[] = {']
    for cp in range(0x110000):
        lowered = chr(cp).lower()
        if lowered != chr(cp):
            values = [ord(x) for x in lowered]
            if len(values) > 3:
                raise ValueError('lowercase expansion exceeds native capacity')
            result.append('{%d,{%s},%d},' % (cp, ','.join(map(str, values)), len(values)))
    result += ['};', 'typedef struct { uint32_t first, last; } CaseRange;']
    for name, symbol in [('cased', '_PyUnicode_IsCased'), ('ignorable', '_PyUnicode_IsCaseIgnorable')]:
        fn = getattr(ctypes.pythonapi, symbol)
        fn.argtypes = [ctypes.c_uint32]
        fn.restype = ctypes.c_int
        result.append('static const CaseRange ' + name + '_ranges[] = {')
        result += ['{%d,%d},' % r for r in ranges(fn)]
        result.append('};')
    Path(__file__).resolve().parents[1].joinpath('src/internal/unicode_lower_data.h').write_text('\n'.join(result) + '\n')


if __name__ == '__main__':
    main()
