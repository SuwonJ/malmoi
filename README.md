# malmoi

MLM 사전 데이터를 읽어 설명 검색·유사어 목록·단어 유사도 점수를 제공하는 C11 모듈입니다. 실행에는 Python·ONNX·모델 파일·네트워크가 필요하지 않습니다.

```bash
make
./build/core/malmoi_cli query "작은 물방울로 조용히 내리는 비"
./build/core/malmoi_cli word "먼지잼"
./build/core/malmoi_cli pair "먼지잼" "이슬비"
```

`data/dictionary.mlm`은 직접 작성한 작은 synthetic 예제입니다. 실제 사전 정확도를 나타내지 않습니다. 실제 MLM은 담당자가 `MALMOI_DATA` 환경 변수로 지정합니다. 팀원 함수에는 모델 선택 인자가 없습니다.

[API 설명](docs/API.md) · [공개 헤더](include/malmoi.h) · [C 호출 예제](examples/malmoi_cli.c)

- `include/`: 공개 헤더
- `src/`: C 로더·검색 구현
- `examples/`: 모듈 호출 예제
- `data/`: 즉시 실행용 작은 MLM
- `tests/`, `tools/`: 검사·MLM 작성기

설명 검색은 뜻풀이의 문자 1–3그램 코사인이고, 단어 검색·단어 점수는 MLM의 저장 벡터 코사인입니다. E5 의미 검색과 점수 체계가 다릅니다.

빌드는 C11 컴파일러와 make, 테스트(`make test`)는 Python 표준 라이브러리만 사용합니다. 배포 시에는 운영체제에 맞춰 빌드한 실행 파일과 MLM만 전달합니다.
