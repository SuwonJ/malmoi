PYTHON ?= research/.venv/bin/python
RESEARCH_PYTHON = $(abspath $(PYTHON))

.PHONY: help setup setup-advanced test smoke experiment

help:
	@echo 'make setup          Python 기본 실험 환경 설치'
	@echo 'make setup-advanced 형태소·ONNX 모델 실험 의존성 추가'
	@echo 'make test           단위 테스트 (모델 다운로드 없음)'
	@echo 'make smoke          예제 사전으로 검색·평가 전체 흐름 실행'
	@echo 'make experiment     로컬 전체 사전으로 어휘 검색 비교'

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
