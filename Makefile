PYTHON ?= research/.venv/bin/python
RESEARCH_PYTHON = $(abspath $(PYTHON))
PYTHON3 ?= python3
CC ?= cc
AR ?= ar
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?= -Iinclude
LDLIBS ?= -lm
EXAMPLE_LDFLAGS ?=
BUILD_DIR ?= build/core
EXEEXT ?=
ifeq ($(OS),Windows_NT)
EXEEXT = .exe
EXAMPLE_LDFLAGS = -municode
endif

CORE_TEST = $(BUILD_DIR)/test_vecdb$(EXEEXT)
INSPECT = $(BUILD_DIR)/inspect$(EXEEXT)
FIXTURE = build/fixtures/dev-1000.mlm

.PHONY: help setup setup-advanced test smoke experiment

help:
	@echo 'make setup          Python 기본 실험 환경 설치'
	@echo 'make setup-advanced 형태소·ONNX 모델 실험 의존성 추가'
	@echo 'make test           단위 테스트 (모델 다운로드 없음)'
	@echo 'make smoke          예제 사전으로 검색·평가 전체 흐름 실행'
	@echo 'make experiment     로컬 전체 사전으로 어휘 검색 비교'
	@echo 'make core-test      C 로더·Python 호환성 테스트 (Python 표준 라이브러리만 사용)'
	@echo 'make demo-b/c/d     각 담당자용 C API 호출 예제'
	@echo 'make sanitize       C Address/UndefinedBehaviorSanitizer 검사'

setup:
	python3 -m venv research/.venv
	$(RESEARCH_PYTHON) -m pip install -e ./research

setup-advanced:
	$(RESEARCH_PYTHON) -m pip install -e './research[advanced]'

test:
	cd research && $(RESEARCH_PYTHON) -m unittest discover -s tests -v

smoke:
	cd research && $(RESEARCH_PYTHON) -m src.experiment_suite --data tests/fixtures/dictionary.jsonl --benchmark tests/fixtures/benchmark.json --dimensions --repeats 1 --output ../build/smoke

experiment:
	cd research && $(RESEARCH_PYTHON) -m src.experiment_suite

.PHONY: core fixture core-test demo-b demo-c demo-d sanitize check

$(BUILD_DIR):
	$(PYTHON3) -c "from pathlib import Path; Path('$(BUILD_DIR)').mkdir(parents=True, exist_ok=True)"

$(BUILD_DIR)/%$(EXEEXT): examples/%.c src/vecdb.c include/vecdb.h examples/demo_utils.h examples/cli_args.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/vecdb.c $< $(LDFLAGS) $(EXAMPLE_LDFLAGS) $(LDLIBS) -o $@

$(CORE_TEST): tests/test_vecdb.c src/vecdb.c include/vecdb.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/vecdb.c $< $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/vecdb.o: src/vecdb.c include/vecdb.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/libmalmoi.a: $(BUILD_DIR)/vecdb.o
	$(AR) rcs $@ $<

core: $(BUILD_DIR)/libmalmoi.a $(INSPECT) $(CORE_TEST)

fixture:
	$(PYTHON3) tools/make_fixture.py --count 1000 --output $(FIXTURE) --export-input

core-test: core fixture
	$(CORE_TEST) $(FIXTURE)
	$(PYTHON3) tests/test_compatibility.py --inspect $(INSPECT)

demo-b: $(BUILD_DIR)/word_demo$(EXEEXT) fixture
	$(BUILD_DIR)/word_demo$(EXEEXT) $(FIXTURE)

demo-c: $(BUILD_DIR)/sense_demo$(EXEEXT) fixture
	$(BUILD_DIR)/sense_demo$(EXEEXT) $(FIXTURE)

demo-d: $(BUILD_DIR)/game_demo$(EXEEXT) fixture
	$(BUILD_DIR)/game_demo$(EXEEXT) $(FIXTURE)

sanitize:
	$(MAKE) core-test BUILD_DIR=build/sanitize CFLAGS="-std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer" LDFLAGS="-fsanitize=address,undefined"

check: test core-test
