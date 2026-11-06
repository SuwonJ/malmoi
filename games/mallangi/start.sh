#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
mode=daily
lan_address=''
check_only=0

usage() {
    echo '사용법: bash games/mallangi/start.sh [--demo] [--lan Windows_IP] [--check]'
    echo '기본: 오늘의 단어 / localhost:8000'
    echo '--demo: 정답 바람 / 별도 테스트 저장 파일'
    echo '--lan: 기존 Windows 포트 전달 설정의 와이파이 IPv4 주소'
    echo '--check: 데이터셋과 C 게임 초기화 검사만 실행'
}

while (($#)); do
    case "$1" in
        --demo) mode=demo; shift ;;
        --lan)
            if (($# < 2)); then usage >&2; exit 2; fi
            lan_address=$2; shift 2 ;;
        --check) check_only=1; shift ;;
        --help|-h) usage; exit 0 ;;
        *) echo "알 수 없는 옵션: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if ! command -v python3 >/dev/null; then
    echo 'python3가 필요합니다.' >&2; exit 1
fi
for required in server.py storage.py; do
    if [[ ! -f "$script_dir/$required" ]]; then
        echo "필요한 파일이 없습니다: $script_dir/$required" >&2; exit 1
    fi
done

export MALLANGI_LIBRARY="${MALLANGI_LIBRARY:-$project_root/build/team/libmallangi_web.so}"
if [[ ! -f "$MALLANGI_LIBRARY" ]]; then
    echo '뜻풀이 고정 게임 라이브러리를 먼저 빌드하세요: cmake --build build/team --target mallangi_web' >&2
    exit 1
fi
# Explicit options take precedence over old environment variables.
unset MALLANGI_ANSWER
if [[ "$mode" == demo ]]; then
    export MALLANGI_ANSWER='바람'
    export MALLANGI_STATE="${MALLANGI_STATE:-$script_dir/mallangi-demo.sqlite3}"
else
    export MALLANGI_STATE="${MALLANGI_STATE:-$script_dir/mallangi-state.sqlite3}"
fi
export MALMOI_DATA="${MALMOI_DATA:-$project_root/data/full/dictionary-full.mlm}"
export MALLANGI_WORDS="${MALLANGI_WORDS:-$script_dir/daily-words.txt}"
if [[ "$mode" == daily && ! -s "$MALLANGI_WORDS" ]]; then
    echo '검증된 정답 후보 파일이 없습니다. 먼저 아래 명령을 실행해주세요.' >&2
    echo "python3 \"$script_dir/prepare_daily.py\"" >&2
    exit 1
fi
export MALLANGI_HOST=127.0.0.1
export MALLANGI_ORIGINS=''
if [[ -n "$lan_address" ]]; then
    python3 - "$lan_address" <<'PY'
import ipaddress
import sys
try:
    address = ipaddress.IPv4Address(sys.argv[1])
    if not any(address in ipaddress.IPv4Network(network) for network in ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16')):
        raise ValueError('사설 와이파이 IPv4 주소가 필요합니다.')
except ValueError as error:
    sys.exit(f'--lan 주소 오류: {error}')
PY
    export MALLANGI_HOST=0.0.0.0
    export MALLANGI_ORIGINS="http://$lan_address:8000"
fi

echo "실행 모드: $mode"
if [[ "$mode" == daily ]]; then echo "정답 후보 파일: $MALLANGI_WORDS"; fi
echo "저장 파일: $MALLANGI_STATE"
echo '데이터셋과 C 게임 초기화를 확인합니다...'
python3 - "$script_dir" <<'PY'
import ctypes
import json
import os
from pathlib import Path
import sys
try:
    dataset = Path(os.environ['MALMOI_DATA']).resolve(strict=True)
    if not dataset.is_file():
        raise ValueError('데이터셋 파일이 아닙니다.')
    engine = ctypes.CDLL(str(Path(sys.argv[1]) / 'libmallangi_web.so'))
    for symbol in ('mallangi_join', 'mallangi_saved_answer', 'mallangi_restore'):
        getattr(engine, symbol)
    engine.mallangi_create.argtypes = []
    engine.mallangi_create.restype = ctypes.c_void_p
    engine.mallangi_request.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    engine.mallangi_request.restype = ctypes.c_char_p
    engine.mallangi_destroy.argtypes = [ctypes.c_void_p]
    engine.mallangi_destroy.restype = None
    handle = engine.mallangi_create()
    if not handle:
        raise MemoryError('게임 생성 실패')
    try:
        result = json.loads(engine.mallangi_request(handle, None))
        if 'error' in result or len(result.get('cards', [])) != 3 or result.get('remaining') != 5:
            raise ValueError(f'게임 초기화 실패: {result.get("error", "카드/기회 오류")}')
    finally:
        engine.mallangi_destroy(handle)
    print('검사 통과: 데이터셋 로드 · 카드 3장 · 제출 기회 5회')
except (OSError, ValueError, AttributeError, MemoryError) as error:
    sys.exit(f'시작 전 검사 실패: {error}')
PY

if ((check_only)); then exit 0; fi
if command -v ss >/dev/null && [[ -n "$(ss -H -ltn 'sport = :8000')" ]]; then
    echo '8000번 포트를 이미 사용 중입니다. 기존 서버를 Ctrl+C로 종료해주세요.' >&2
    exit 1
fi
if [[ -n "$lan_address" ]]; then
    echo "친구 접속 주소: http://$lan_address:8000"
    echo 'Windows 포트 전달과 방화벽은 기존 설정을 사용합니다.'
else
    echo '접속 주소: http://localhost:8000'
fi
exec python3 "$script_dir/server.py"
