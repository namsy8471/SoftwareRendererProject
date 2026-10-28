# CLAUDE.md

Claude Code가 읽는 입구 파일이다. 공통 규칙(성능 판정, 명령, 작업 흐름, 코드 지도, PC Job Runner, 버린 시도)은 전부 [AGENTS.md](AGENTS.md)에 있고
아래 줄로 그대로 불러온다. Gemini CLI도 `GEMINI.md`에서 같은 파일을 불러오므로, **규칙은 AGENTS.md에만 고친다.**

@AGENTS.md

## Claude 전용 메모

- 프로젝트 메모리에 새 규칙·사용자 결정·함정을 저장하면, 같은 내용을 AGENTS.md에도 반영하는 문서 PR을 연다(Gemini는 Claude 메모리를 볼 수 없다).
- 클라우드 세션은 이 저장소에 직접 push할 수 없다. push·`gh pr`은 PC Job Runner 작업으로 한다(AGENTS.md "PC 측정은 Job Runner 큐로").
