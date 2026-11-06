"""Build a vetted answer pool using the same C dataset as the game."""
import argparse
import ctypes
import os
from pathlib import Path
import tempfile
import unicodedata

ROOT = Path(__file__).resolve().parent


def prepare(source, output):
    os.environ.setdefault('MALMOI_DATA', str(ROOT.parent.parent / 'data/full/dictionary-full.mlm'))
    library = Path(os.environ.get('MALLANGI_LIBRARY', str(ROOT.parents[1] / 'build/team/libmallangi_web.so'))).resolve()
    os.environ.setdefault('MALMOI_NATIVE_INDEX', str(library.parent / 'runtime/index'))
    os.environ.setdefault('MALMOI_KIWI_MODEL', str(library.parent / 'runtime/model/models/cong/base'))
    engine = ctypes.CDLL(str(library))
    engine.mallangi_validate_daily.argtypes = [ctypes.c_char_p]
    engine.mallangi_validate_daily.restype = ctypes.c_int
    engine.malmoi_error_string.argtypes = [ctypes.c_int]
    engine.malmoi_error_string.restype = ctypes.c_char_p
    words = []
    seen = set()
    for raw in source.read_text(encoding='utf-8-sig').splitlines():
        word = unicodedata.normalize('NFC', raw.strip())
        if not word or word.startswith('#'):
            continue
        key = word.replace('-', '').replace('^', '')
        if key in seen:
            continue
        seen.add(key)
        if not key or len(word.encode('utf-8')) >= 64 or any(c.isspace() or not c.isprintable() for c in word):
            print(f'[제외] {word!r}: 단어 형식 오류', flush=True)
            continue
        status = engine.mallangi_validate_daily(word.encode('utf-8'))
        if status == 0:
            words.append(word)
            print(f'[통과] {word}', flush=True)
        elif status in (1, 5, 6, 9):  # argument/range/not-found/zero-vector
            reason = engine.malmoi_error_string(status).decode('utf-8')
            print(f'[제외] {word}: {reason}', flush=True)
        else:
            raise RuntimeError(f'C 엔진 오류 {status}: 기존 후보 파일은 유지합니다.')
    if len(words) < 6:
        raise ValueError('유효한 후보가 6개 미만입니다. 기존 후보 파일을 유지합니다.')
    if len(words) > 1024:
        raise ValueError('후보는 최대 1024개입니다. 기존 후보 파일을 유지합니다.')
    # Atomic replacement: a failed validation never truncates the existing pool.
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', newline='\n',
                                         dir=output.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write('\n'.join(words) + '\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, output)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()
    print(f'완료: {len(words)}개 후보 → {output}', flush=True)
    return words


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=ROOT / 'daily-seeds.txt')
    parser.add_argument('--output', type=Path, default=ROOT / 'daily-words.txt')
    args = parser.parse_args()
    try:
        prepare(args.input, args.output)
    except (OSError, ValueError, RuntimeError, AttributeError) as error:
        parser.exit(1, f'후보 생성 실패: {error}\n')
