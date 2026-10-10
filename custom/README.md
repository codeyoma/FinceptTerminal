# codeyoma fork 운영 메모

이 fork는 upstream [Fincept-Corporation/FinceptTerminal](https://github.com/Fincept-Corporation/FinceptTerminal)에 KRX 4분 예측 엔진(비공개 저장소 `krx-quant`)을 보는 화면 하나를 더한 개인용 빌드다. 엔진과 알고리즘은 이 저장소에 두지 않는다.

## 브랜치

| 브랜치 | 용도 | 규칙 |
|---|---|---|
| `main` | upstream 미러 | 직접 커밋하지 않는다. 매주 workflow가 upstream으로 fast-forward 한다. |
| `custom` | 개인 빌드 (기본 브랜치) | 모든 개인 작업은 여기. upstream은 **릴리스 태그만** merge 한다. rebase 하지 않는다. |

`custom/UPSTREAM_BASE`에는 `custom`이 기반으로 하는 upstream 릴리스 태그를 적는다.

## upstream 릴리스 반영

새 릴리스가 나오면 `custom-upstream-watch` workflow가 issue를 연다. 반영 절차:

```bash
git checkout custom
custom/merge-upstream-release.sh
```

스크립트는 최신 upstream 릴리스 태그를 merge 하고 `custom/UPSTREAM_BASE`를 갱신한다. 충돌이 나면 멈추고 마무리 방법을 출력한다. Windows에서는 Git Bash에서 실행한다.

## main 자동 동기화와 SYNC_TOKEN

workflow는 기본 `GITHUB_TOKEN`으로 `main`을 동기화한다. upstream이 `.github/workflows/` 파일을 바꾼 주에는 이 토큰으로 동기화할 수 없어서 실패 issue가 열린다. 그때는 저장소 페이지의 **Sync fork** 버튼을 누르면 된다. 자동으로 처리하려면 fine-grained PAT(이 저장소만, Contents와 Workflows 읽기·쓰기)를 만들어 `SYNC_TOKEN` secret으로 등록한다.

## upstream 파일 수정 원칙

- 새 코드는 새 디렉터리에 둔다 (`fincept-qt/src/screens/krx_quant/`, `fincept-qt/src/services/krx_quant/`).
- 화면 등록 때문에 고치는 upstream 파일은 아래 목록으로 제한하고, 고칠 때마다 이 표를 갱신한다.
- Fincept SQLite migration(`src/storage/sqlite/migrations/`)은 쓰지 않는다. 데이터는 엔진이 가진다.
- 개인 빌드는 앱 내 자동 업데이트를 끈다. 공식 버전으로 덮어써지는 것을 막기 위해서다.

| upstream 파일 | 수정 내용 |
|---|---|
| `fincept-qt/src/app/WindowFrame_Setup.cpp` | `KrxQuantScreen` include와 `krx_quant` 화면 factory 등록 |
| `fincept-qt/src/app/DockScreenRouter.cpp` | `krx_quant` 탭 제목 "KRX 4분 예측" |
| `fincept-qt/src/ui/navigation/ToolBar.cpp` | Navigate → Trading & Portfolio 메뉴에 "KRX 4분 예측" |
| `fincept-qt/src/ui/navigation/CommandBar.cpp` | 명령 팔레트에 `krx_quant` ("krx"로 찾는다) |
| `fincept-qt/CMakeLists.txt` | `SERVICE_SOURCES`·`SCREEN_SOURCES`에 새 파일, unity 빌드 제외 목록, `FINCEPT_DISABLE_AUTO_UPDATE` 옵션(기본 ON) |
| `fincept-qt/tests/CMakeLists.txt` | `tst_krx_engine_data` 테스트 등록 |
| `fincept-qt/src/services/updater/UpdateService.cpp` | `FINCEPT_DISABLE_AUTO_UPDATE`이면 업데이트 확인을 하지 않는다(`check_for_updates` 본문을 `#ifdef … #else … #endif`로 감쌌다) |

## KRX 예측 화면 (#20)

- 코드: `fincept-qt/src/screens/krx_quant/`(화면), `fincept-qt/src/services/krx_quant/`(엔진 API 읽기, 응답 해석).
- Navigate → Trading & Portfolio → **KRX 4분 예측**(또는 명령 팔레트에서 "krx")으로 연다. 화면이 보이는 동안 5초마다 엔진의 `/v1/status`, `/v1/symbols`, `/v1/symbols/<종목>/latest`를 읽는다. 읽기가 끝나기 전에는 다시 묻지 않는다. 실패하면 지난 예측을 지우고 이유를 보여준다.
- 엔진 주소는 화면 위의 **엔진 주소** 칸에 넣는다(처음에는 비어 있다). 값은 QSettings `krx_quant/base_url`에 남는다. 실제 주소는 krx-quant 저장소의 문서에 있다.
- 엔진 없이 화면을 만들 때는 krx-quant 저장소에서 `uv run krx-quant demo-engine`을 띄우고 주소를 `http://127.0.0.1:8090`으로 바꾼다. 고정된 가상 데이터다.
- 응답 해석 테스트: `cmake -B build -DFINCEPT_BUILD_TESTS=ON … && cmake --build build --target tst_krx_engine_data && ctest --test-dir build -R krx`.

## 개인 빌드

- `custom` 브랜치의 커밋에 `custom-v*` 태그(예: `custom-v4.5.0-krx.1`)를 달아 push하면 `custom-release` workflow가 맥 DMG와 윈도우 `setup.exe`를 만들어 이 fork의 Release(prerelease)에 올린다.
- 서명하지 않은 빌드다. 맥은 처음 열 때 우클릭 → 열기(또는 `xattr -dr com.apple.quarantine /Applications/FinceptTerminal.app`), 윈도우는 SmartScreen에서 추가 정보 → 실행.
- 앱 내 자동 업데이트는 꺼져 있다(`FINCEPT_DISABLE_AUTO_UPDATE`). 새 버전은 새 태그로 만든다.

## 에이전트 설정

루트 `CLAUDE.md`는 `@custom/AGENTS.md` 한 줄만 담는다. upstream `.gitignore`가 `CLAUDE.md`를 무시하므로 `git add -f`로 추적하고, upstream이 같은 파일을 만들 일이 없어 merge 충돌이 나지 않는다. 설정 본문은 `custom/AGENTS.md`와 `custom/agents/`에 있다. 작업 ticket은 비공개 저장소 `codeyoma/krx-quant`의 이슈로 관리한다. 이 fork는 비공개 저장소 `krx-quant`의 `fincept/` submodule로 받는다(`git clone --recurse-submodules`). 단독으로 clone해도 빌드와 upstream merge는 되지만, 에이전트 문서가 참조하는 상위 폴더의 설계 문서(`../docs/design.md`)는 없다.

## upstream workflow

이 fork에서는 upstream의 예약·릴리스 workflow를 끈다(GitHub Actions 설정에서 disable). 개인 빌드용 workflow는 `custom-` 접두사로 둔다: `custom-upstream-watch`(upstream 릴리스 알림), `custom-release`(개인 빌드, 위 "개인 빌드").
