#!/bin/bash
# 사용법: malmoi-main 폴더에서  bash gameSH/build.sh
set -e
cd "$(dirname "$0")/.."

# 사전 파일이 없으면 조각 합치기 (최초 1회)
if [ ! -f data/full.mlm ]; then
    echo "사전 파일 합치는 중..."
    cat data/full/part-*.mlmchunk > data/full.mlm
fi

gcc -std=c11 -O2 -Wall -Wextra -Iinclude \
    gameSH/*.c \
    src/malmoi.c src/vecdb.c src/bridge.c \
    src/internal/search_backend.c src/internal/lexical.c \
    -lm -o wordgame_sii

echo "빌드 완료 → ./wordgame_sii"
