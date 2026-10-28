# GEMINI.md

Gemini CLI가 읽는 입구 파일이다. 공통 규칙(성능 판정, 명령, 작업 흐름, 코드 지도, PC Job Runner, 버린 시도)은 전부 [AGENTS.md](AGENTS.md)에 있고
아래 줄(Gemini CLI의 `@파일` 가져오기)로 그대로 불러온다. Claude Code도 `CLAUDE.md`에서 같은 파일을 불러오므로, **규칙은 AGENTS.md에만 고친다.**

@./AGENTS.md

## Gemini 전용 메모

- `/memory add`로 저장한 내용은 내 PC의 `~/.gemini/GEMINI.md`에만 남아 Claude가 볼 수 없다. 이 저장소에 관한 규칙·결정·함정은 AGENTS.md에 적어 PR로 올린다.
- 불러온 내용이 맞게 들어왔는지는 `/memory show`로 확인한다. AGENTS.md를 고친 뒤에는 `/memory refresh`.
- Gemini CLI는 PC에서 돌므로 `git push`, `gh pr create`를 직접 할 수 있다(`gh`는 namsy8471로 로그인됨). 벤치·프로파일만은 Job Runner 큐로 넣는다.
