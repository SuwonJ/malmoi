# 기존 로컬 코드와 실험을 안전하게 가져오기

원격 저장소는 이 협업 문서로 초기화되었다. **로컬에 이미 Git 이력/코드가 있다면 새 저장소로 덮어쓰지 않는다.**

## 공통 사전 점검

\`\`\`bash
git status --short
git branch -vv
git remote -v
\`\`\`

미커밋 변경이 있다면 먼저 별도 커밋 또는 백업을 만든다. 작업 폴더를 삭제하거나 \`git reset --hard\`, \`git push --force\`를 사용하지 않는다.

## A. 로컬이 이미 Git 저장소인 경우

로컬 프로젝트 디렉터리에서:

\`\`\`bash
git branch backup/before-team-import
git remote -v
# origin이 없으면 다음을 실행
git remote add origin https://github.com/SuwonJ/malmoi.git
git fetch origin
git switch -c chore/import-local
git merge origin/main --allow-unrelated-histories
\`\`\`

두 저장소가 다른 이력으로 출발했다면 \`--allow-unrelated-histories\`가 필요하다. 충돌이 있다면 로컬 소스를 무조건 원격 버전으로 교체하지 말고, README·문서·코드 내용을 보고 수동 해결한다.

병합이 완료되면:

\`\`\`bash
git push -u origin chore/import-local
\`\`\`

GitHub에서 PR을 만들어 \`main\`으로 병합한다. 동일 이름 브랜치가 이미 있으면 다른 이름을 사용한다.

## B. 로컬에 Git 이력이 없는 경우

\`\`\`bash
git clone https://github.com/SuwonJ/malmoi.git malmoi-team
cd malmoi-team
git switch -c chore/import-local
\`\`\`

기존 작업 폴더에서 **소스와 텍스트 실험 문서**를 골라 복사한다. 원본 JSON, 임베딩, 대형 인덱스 등은 \`.gitignore\`를 확인하고 커밋에서 제외한다.

\`\`\`bash
git add .
git status
git commit -m "chore: import existing local experiments and source"
git push -u origin chore/import-local
\`\`\`

## 반입 후 할 일

- 기존 실험 파일의 실제 위치와 현재 README의 '예정 구성'이 일치하는지 검토.
- 기존 \`.h\`와 [인터페이스 초안](INTERFACES.md)을 비교하여 합의안을 확정.
- 데이터 파일 포맷을 적고 작은 fixture를 배포.
- 기존 Python 연구 파이프라인을 보존한 채 C의 구현/실험/보고서를 연결.

특히 다른 팀원의 작업이 시작된 뒤에는 공용 함수명을 독단적으로 바꾸지 않는다.
