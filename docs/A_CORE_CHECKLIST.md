# A 완료 현황: 공통 데이터 형식·벡터 로더·로컬 실험 반입

담당: 장수원 · [작업 이슈 #1](https://github.com/SuwonJ/malmoi/issues/1)

A의 개발 기반을 구현했다. B·C·D는 [공통 코어 사용법](CORE_HANDOFF.md)의 명령과 예제로 독립 개발을 시작한다.

| 단계 | 상태 | 산출물·검증 |
| --- | --- | --- |
| Python 연구 반입 | 완료 | `research/`, 출처/소스 해시, Python 단위 테스트 10개·예제 실험 CI |
| 기존 C 자산 확인 | 완료 | 현재 프로젝트·Documents에서 대상 헤더/소스 미발견. 기존 연구 작업 트리는 보존 |
| 공통 ID·문자·벡터 규칙 | 구현 기준 v1 | word/sense ID 분리, 원본 ID 보존, NFC 검색 키, 독립 모델 공간 |
| 파일 형식·공용 헤더 | 완료 | `docs/DATA_FORMAT.md`, `include/vecdb.h`, `docs/INTERFACES.md` |
| Python 작성기·fixture | 완료 | `tools/prepare.py`, `tools/make_fixture.py`, 8단어 golden 및 1,000단어·1,001뜻풀이 개발 세트 |
| C 로더·계산·해제 | 완료 | `src/vecdb.c`, 정적 코어 라이브러리, 읽기 view·모델 공간 검사·내적/코사인 |
| 호환성·메모리 검사 | 구현·자동화 완료 | Python/C 호환성 12개, C 50회 로드/해제, 손상 파일, ASan/UBSan, OS별 CI |
| 팀 전달 | 완료 | B/C/D 호출 예제·독립 make 타깃·게임 후보 목록·새 clone 실행 명령 |

## 지금 실행할 명령

```bash
make core-test
make demo-b demo-c demo-d
make sanitize CC=clang
```

C 테스트는 Python 표준 라이브러리만 필요하다. Python 연구 테스트는 `make setup` 후 `make test`로 별도 실행한다. 직접 작성한 fixture는 실제 검색 품질 평가에 사용하지 않는다.

## 호환성 결정

- 기존 v0의 DB 열기/닫기·단일 조회 시그니처는 유지했다. 동음이의어를 조용히 하나로 고르지 않고 별도 상태 코드와 복수 조회를 추가했다.
- 같은 데이터셋 안에서 word/sense 각각 0 기반 ID를 사용한다. 데이터 변경으로 ID가 달라질 수 있어 파일 해시/데이터 버전과 함께 저장한다.
- 파일은 고정 폭 little-endian, float32, UTF-8이다. C struct를 그대로 저장하지 않는다. 파이프라인이 생성한 검색 키를 C에서 바이트 비교한다.
- 차원뿐 아니라 모델 revision·풀링·정규화·질의/문서 규칙을 포함한 공간 descriptor 전체가 일치해야 비교한다.
- C11/GCC/Clang/MSVC 빌드 경로를 제공하며, Windows 경로는 UTF-8→UTF-16으로 변환한다. Unix/POSIX 전용 mmap·strdup에 의존하지 않는다.

## 팀 통합 시 확인할 것

별도 보유한 C 헤더는 이번 확인 범위에서 발견되지 않았다. 팀원이 이미 쓰고 있는 코드가 있다면 `include/vecdb.h`와 대조하고 호출부를 함께 수정한다. 이 구현이 팀원 전원의 사전 검토를 받았다고 주장하지 않는다.

실제 사전·모델의 재배포 범위는 제공자 라이선스에 따라 결정한다. 개발용 1,000단어는 synthetic이므로 외부 사전/모델 없이 같은 바이트로 생성할 수 있다. 실제 벡터는 전처리기 입력의 `word_vector`/`sense_vector`와 공간 descriptor로 연결하며, 없는 벡터를 임의 값으로 보정하지 않는다.

게임·검색 알고리즘 자체, 실제 모델 선택, 사람의 질의 판정, 세 기능 CLI 통합은 각각 B·C·D 및 후속 통합 작업이다. A 코어의 테스트 통과를 서비스 완성이나 검색 품질 검증으로 해석하지 않는다.
