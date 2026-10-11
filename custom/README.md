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
| `fincept-qt/tests/CMakeLists.txt` | `tst_krx_engine_data`, `tst_krx_board`, `tst_krx_signals` 테스트 등록 |
| `fincept-qt/src/services/updater/UpdateService.cpp` | `FINCEPT_DISABLE_AUTO_UPDATE`이면 업데이트 확인을 하지 않는다(`check_for_updates` 본문을 `#ifdef … #else … #endif`로 감쌌다) |

## KRX 예측 화면 (#20, #21)

- 코드: `fincept-qt/src/screens/krx_quant/`(화면, 차트), `fincept-qt/src/services/krx_quant/`(엔진 API와 스트림 읽기, 응답 해석, 화면 상태, 신호 판정).
- Navigate → Trading & Portfolio → **KRX 4분 예측**(또는 명령 팔레트에서 "krx")으로 연다.
- 엔진 주소는 화면 위의 **엔진 주소** 칸에 넣는다(처음에는 비어 있다). 값은 QSettings `krx_quant/base_url`에 남는다. 실제 주소는 krx-quant 저장소의 문서에 있다.

### 구성

- **위쪽 줄**: 엔진 상태(장 단계, 마지막 봉 마감, 채점 대기, KIS 연결과 피드 지연·결측 피드 수, 지난밤 학습, 스트림 연결), 매크로 줄(그날 국면과 이유, 달러선물. 지표 전체와 읽기 실패는 툴팁), 오늘의 특수 상황(VI·서킷브레이커·사이드카). 매크로가 오늘 것이 아니면 "(오늘 것이 아님)"을 붙인다.
- **종목 목록**: 기준가, 최종 합의(방향과 가장 큰 확률. 상승과 하락이 같은 정수 %이면 "상승=하락"), 채점까지 남은 시간. 고른 종목이 차트와 예측 패널에 나온다.
- **차트**: 고른 종목의 오늘 1분봉이다. 범위는 30분·1시간(기본)·2시간·하루 전체이고 QSettings `krx_quant/chart_minutes`에 남는다. 봉 마감마다 최종 합의가 예측한 클래스를 그 봉에 그린다: 상승은 봉 아래 ▲, 하락은 봉 위 ▼, 보합은 –. 채점되면 그 옆에 ✓(맞음)나 ✗(틀림)를, 순위표에서 고른 기준으로 채점할 수 없었으면 ?를 그린다. 예측 클래스는 엔진이 채점하는 것과 같다(가장 큰 확률, 동률이면 보합, 그다음 상승). 4분 구간에 특수 상황이 낀 예측은 흐리게 그린다(통계에서 빠진다). 봉에 마우스를 올리면 가격과 그 예측이 나온다.
- **예측 패널**: 고른 종목의 최신 봉 마감 예측이다. 최종 합의, 빠른 합의, 알고리즘, 기준 예측 순이고 알고리즘과 기준 예측은 이름순이다. 열은 방향, 상승·보합·하락 확률, 기대 틱, 모델 버전이다. 상승과 하락이 같은 정수 %로 보이면 방향은 비운다. 제목에 봉 마감과 채점까지 남은 시간(엔진 시계 기준)이 나온다.
- **순위표**: 기간(오늘·5일·20일), 채점 기준(B 표시 시점·A 봉 마감), 종목(전체·종목별), 특수 상황 포함을 고른다. 열은 순위, 상태(수습·정식·벤치·은퇴·복제), 모델 버전, 표본, 정답률, 방향 정밀도, 커버리지, Brier, 거래당 손익(틱), 가장 약한 lift 하한, 운 보정 통과다. 95% 구간은 툴팁에 있다. 순위가 없는 항목, 기준 예측(흐리게) 순으로 이어진다. 차트의 ✓/✗도 여기서 고른 채점 기준을 따른다.
- **신호 알림**: 엔진의 텔레그램 신호와 같은 규칙이다(krx-quant `docs/engine-api.md` "텔레그램 신호 알림").
  - 최종 합의의 상승 확률이 기준값 이상이면 상승, 아니면 하락 확률이 기준값 이상일 때 하락이다.
  - 그 합의에 들어간 정식 알고리즘 가운데 N개 이상이 같은 방향을 예측해야 한다. 각자의 방향은 가장 큰 확률의 클래스다(동률이면 보합).
  - 같은 종목·같은 방향은 앞 신호의 채점 시각(4분 뒤)까지 다시 알리지 않는다. 채점 시각이 지난 합의(장 마감 뒤에 화면을 연 경우 등)는 알리지 않는다. 한 봉 마감은 한 번만 본다.
  - 알림은 Fincept 알림으로 나간다(`ToastService`: 앱 안 알림과 OS 알림). 소리를 켜면 OS 기본 알림음이 난다. **시험 알림** 버튼으로 OS 알림 권한과 소리를 확인한다.
  - 켜기(기본 켬), 기준값(50–99%, 기본 60%), N(1–20개, 기본 2개), 소리(기본 켬)는 화면에서 바꾸고 QSettings `krx_quant/signal_on`, `krx_quant/signal_probability`, `krx_quant/signal_agree`, `krx_quant/signal_sound`에 남는다.

### 갱신

- 엔진의 스트림(`/v1/stream`, WebSocket)을 따른다. 예측, 합의, 채점, KIS 상태, 알고리즘 상태가 오는 대로 반영한다. 스트림은 연결 뒤의 일만 보내므로, 연결하거나 다시 연결하면 API를 먼저 다시 읽는다. 답과 스트림이 어떤 순서로 오든 더 늦은 봉 마감을 이른 것으로, 채점된 예측을 채점 전 것으로 되돌리지 않는다.
- 끊기면 상태 줄에 이유와 다시 연결까지 남은 초를 보이고, 1, 2, 4, …초(최대 30초) 간격으로 끝없이 다시 연결한다(엔진은 매일 아침 다시 시작한다). 15초마다 ping을 보내고 45초 동안 아무 응답이 없으면 끊고 다시 연결한다(잠자기에서 깬 노트북 등).
- 스트림이 연결되어 있으면 API는 상태만 5초마다(피드 지연), 종목·최신 예측·합의 구성원은 30초마다, 차트와 순위표는 60초마다 읽는다. 새 봉 마감이 오면 차트를, 채점이 오면 3초 뒤 순위표를 바로 읽는다. 매크로는 5분마다다. 스트림이 없으면(끊김, Qt WebSockets 없는 빌드) 5초마다 다 읽는다.
- 화면이 가려져 있어도 스트림은 열어 두어 신호 알림이 온다. 그동안 스트림이 없고 알림이 켜져 있으면 상태와 예측을 5초마다 읽는다. 차트, 순위표, 매크로는 읽지 않는다.
- 한 종류의 읽기가 끝나기 전에는 같은 읽기를 다시 묻지 않는다. 읽기에 실패하면 지난 예측과 차트를 지우고 이유를 보여준다. 다시 읽히면 매크로, 순위표, 차트도 다시 읽는다.

> **바뀜** (2026-10-11, krx-quant #64): 화면이 보이는 동안 5초마다 모든 것을 API로 읽었다 → 스트림으로 받고, API는 연결·재연결 때와 위의 주기로 읽는다. 화면이 가려져도 스트림은 열어 둔다. 이유: 스트림으로 갱신하고 끊기면 다시 연결한다(#21). 다른 화면을 보는 동안에도 신호 알림이 와야 한다.

> **바뀜** (2026-10-11, krx-quant #64): 표 하나(종목마다 모든 예측) → 종목 목록과 고른 종목의 예측 패널. 알고리즘과 기준 예측은 엔진이 준 순서 → 이름순. 이유: 차트와 패널을 한 종목씩 보여주게 됐고(#21), 스트림이 알고리즘을 끝난 순서대로 가져와 줄이 매분 바뀌었다.

### 개발

- 엔진 없이 화면을 만들 때는 krx-quant 저장소에서 데모 엔진을 띄우고 주소를 `http://127.0.0.1:8090`으로 바꾼다. 고정된 가상 데이터다.
  - `uv run krx-quant demo-engine`: 10:30에 멈춘 아침이다. 스트림에는 연결할 때의 KIS 상태 하나만 온다.
  - `uv run krx-quant demo-engine --pace 2`: 10:00에 열고 10:30까지 1분을 2초마다 이어 재생하며 스트림을 보낸다. 차트, 패널, 채점이 움직이는 것을 볼 수 있다. 데모 아침의 최종 합의는 50%를 거의 넘지 않아 신호는 나오지 않는다. 알림은 **시험 알림**으로 본다.
- 테스트(Qt Core만): `cmake -B build -DFINCEPT_BUILD_TESTS=ON … && cmake --build build --target tst_krx_engine_data tst_krx_board tst_krx_signals && ctest --test-dir build -R krx`.
  - `tst_krx_engine_data`: 엔진 API와 스트림 메시지 해석.
  - `tst_krx_board`: 답과 스트림을 합친 화면 상태(최신 봉 마감, 차트 표시, 합의 구성원, 신호가 볼 예측).
  - `tst_krx_signals`: 신호 규칙과 다시 알리지 않는 규칙.

## 개인 빌드

- `custom` 브랜치의 커밋에 `custom-v*` 태그(예: `custom-v4.5.0-krx.1`)를 달아 push하면 `custom-release` workflow가 맥 DMG와 윈도우 `setup.exe`를 만들어 이 fork의 Release(prerelease)에 올린다.
- 서명하지 않은 빌드다. 맥은 처음 열 때 우클릭 → 열기(또는 `xattr -dr com.apple.quarantine /Applications/FinceptTerminal.app`), 윈도우는 SmartScreen에서 추가 정보 → 실행.
- 앱 내 자동 업데이트는 꺼져 있다(`FINCEPT_DISABLE_AUTO_UPDATE`). 새 버전은 새 태그로 만든다.

## 에이전트 설정

루트 `CLAUDE.md`는 `@custom/AGENTS.md` 한 줄만 담는다. upstream `.gitignore`가 `CLAUDE.md`를 무시하므로 `git add -f`로 추적하고, upstream이 같은 파일을 만들 일이 없어 merge 충돌이 나지 않는다. 설정 본문은 `custom/AGENTS.md`와 `custom/agents/`에 있다. 작업 ticket은 비공개 저장소 `codeyoma/krx-quant`의 이슈로 관리한다. 이 fork는 비공개 저장소 `krx-quant`의 `fincept/` submodule로 받는다(`git clone --recurse-submodules`). 단독으로 clone해도 빌드와 upstream merge는 되지만, 에이전트 문서가 참조하는 상위 폴더의 설계 문서(`../docs/design.md`)는 없다.

## upstream workflow

이 fork에서는 upstream의 예약·릴리스 workflow를 끈다(GitHub Actions 설정에서 disable). 개인 빌드용 workflow는 `custom-` 접두사로 둔다: `custom-upstream-watch`(upstream 릴리스 알림), `custom-release`(개인 빌드, 위 "개인 빌드").
