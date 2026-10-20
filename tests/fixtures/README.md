# 공통 코어 fixture

이 폴더의 데이터·뜻풀이는 로더 개발을 위해 직접 작성했다. 외부 사전·모델의 원문이나 벡터를 복사하지 않았다. `synthetic: true`이며 검색 품질·한국어 임베딩 성능을 주장하는 데 사용하지 않는다.

- `small.mlm`: 8단어·9뜻풀이의 공통 binary golden fixture. C만 빌드해 바로 읽을 수 있다.
- `small.mlm.json`: 데이터/모델 식별자와 SHA-256.
- `small.mlm.jsonl`, `small.mlm.spaces.json`: 동일 파일을 재생성하는 입력.
- `dev-1000.expected.json`: 생성기로 만든 1,000단어·1,001뜻풀이 세트의 예상 크기와 해시. 모든 CI 환경에서 동일해야 한다.

```bash
python3 tools/make_fixture.py --count 8 --output tests/fixtures/small.mlm --export-input
make fixture
```

1,000단어 세트와 입력은 `build/fixtures/dev-1000.mlm*`에 생성된다. 앞부분에는 한글·구분 기호 표제어, 동음이의어 `눈` 두 항목, 같은 word ID를 공유하는 두 뜻풀이, 0벡터를 넣었다. 나머지는 `예제단어NNNN`이다. 차원은 8이며 word/sense 모델 공간은 구분한다. `먼지잼`과 `이슬비`의 예제 벡터 코사인은 약 0.8이다.
