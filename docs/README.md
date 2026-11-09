# 교과서 사이트 (docs/)

GitHub Pages(Jekyll)로 배포되는 "CPU로 그리는 렌더러" 교과서입니다. 저장소 Settings → Pages에서 Source를 "Deploy from a branch", 브랜치 `main`, 폴더 `/docs`로 고르면 `https://namsy8471.github.io/SoftwareRendererProject/`에 올라갑니다.

- 장 본문: `chNN/index.md` (front matter의 `demos:`가 불러올 데모 스크립트)
- 목차: `_data/toc.yml` (url이 없는 장은 사이드바에 회색으로 표시)
- 데모: `assets/demos/common.js`(공통) + `assets/demos/chNN.js`. 본문의 `<div class="demo" data-demo="이름">`에 붙습니다.
- 로컬 미리보기: `gem install jekyll kramdown-parser-gfm && jekyll serve -s docs`
