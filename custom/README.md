# codeyoma fork 운영 메모

이 fork는 upstream [Fincept-Corporation/FinceptTerminal](https://github.com/Fincept-Corporation/FinceptTerminal)에 KRX 4분 예측 엔진(비공개 저장소 `krx-quant`)을 보는 화면 하나를 더한 개인용 빌드다. 엔진과 알고리즘은 이 저장소에 두지 않는다.

## 브랜치

| 브랜치 | 용도 | 규칙 |
|---|---|---|
| `main` | upstream 미러 | 직접 커밋하지 않는다. 매일 workflow가 upstream으로 fast-forward 한다. |
| `custom` | 개인 빌드 (기본 브랜치) | 모든 개인 작업은 여기. upstream은 **릴리스 태그만** merge 한다. rebase 하지 않는다. |

`custom/UPSTREAM_BASE`에는 `custom`이 기반으로 하는 upstream 릴리스 태그를 적는다.

## upstream 릴리스 반영

새 릴리스는 사람이 손대지 않아도 `custom`과 개인 빌드에 들어간다. `custom-upstream-watch` workflow가 매일 05:00 KST에 upstream의 최신 릴리스(prerelease 제외)와 `custom/UPSTREAM_BASE`를 비교한다.

1. 새 릴리스가 있으면 runner에서 `custom/merge-upstream-release.sh <tag>`로 `custom`에 merge 하고, 결과를 `upstream-merge/<tag>` 브랜치에 push 한다.
2. 그 브랜치로 `custom-release`를 시험 빌드로 돌린다. 맥과 윈도우 빌드(스모크 테스트 포함)만 하고 Release는 만들지 않는다.
3. 두 빌드가 통과하면 `custom`의 fast-forward와 `custom-<tag>-krx.1` 태그를 한 번에(atomic) push 한다(같은 버전의 태그가 이미 있으면 다음 번호). 둘 중 하나라도 거부되면 둘 다 그대로다. 이 태그 push로 `custom-release`가 다시 돌아 개인 빌드를 Release에 올린다. 시험 빌드와 릴리스 빌드로 두 번 빌드한다. 공개 저장소라 Actions 비용은 없다.
4. 다 끝나면 `upstream-merge/<tag>` 브랜치를 지운다. 다음 날 실행이 `custom/UPSTREAM_BASE`가 새 태그인 것을 보고 그 릴리스의 issue를 닫는다.

사람이 필요하면 `custom`을 건드리지 않고 `Upstream <tag>: merge needs a hand` issue를 연다. 본문에 마무리 명령이 있다. issue가 열려 있는 동안 매일 실행은 그 릴리스를 다시 시도하지 않는다. 손으로 끝내서 `custom/UPSTREAM_BASE`가 새 태그가 되면 issue는 저절로 닫힌다. 먼저 닫으면 다음 실행이 다시 시도한다.

- merge 충돌: 충돌한 파일 목록이 issue에 있다.
- 시험 빌드 실패: merge는 `upstream-merge/<tag>` 브랜치에 남아 있다. 그 브랜치에서 고친 뒤 `custom`에 merge 하고 태그를 단다.
- 브랜치 push나 `custom` 반영 실패: 대부분 `SYNC_TOKEN` 문제다(아래).
- upstream 태그가 `vX.Y.Z` 꼴이 아님: 앱은 `custom-vX.Y.Z-krx.N` 태그만 업데이트로 받는다(아래 "앱 업데이트"). 그래서 merge 하지 않고 issue만 연다. 손으로 반영하고 앱이 읽을 수 있는 태그를 정한다.

시험 빌드 중에 `custom`이 움직이면(다른 PR merge) `custom`에 반영하지 않고 경고만 남긴다. 다음 날 실행이 새 `custom` 위에서 다시 merge 한다.

upstream 릴리스가 `.github/workflows/release.yml`을 바꿨으면 `Upstream <tag> changed release.yml` issue도 연다. `custom-release.yml`은 release.yml의 빌드 job을 복사한 것이라 바뀐 내용을 손으로 옮겨야 한다. 자동 반영은 이것을 하지 않는다. 시험 빌드는 복사한 job이 여전히 빌드되는지만 본다.

손으로 반영할 때:

```bash
git switch custom && git pull --ff-only
custom/merge-upstream-release.sh [tag]
```

스크립트는 최신(또는 지정한) upstream 릴리스 태그를 merge 하고 `custom/UPSTREAM_BASE`를 갱신한다. 충돌이 나면 멈추고 마무리 방법을 출력한다. Windows에서는 Git Bash에서 실행한다. push 한 뒤 `custom-<tag>-krx.1` 태그를 단다.

> **바뀜** (2026-10-12, krx-quant #94): 새 릴리스가 나오면 issue만 열고 merge·빌드·태그는 손으로 했다 → workflow가 merge 하고, 시험 빌드가 통과하면 `custom`에 반영하고 개인 빌드 태그까지 단다. 확인 주기는 매주 월요일 09:00 KST → 매일 05:00 KST. 이유: upstream이 릴리스를 내면 업데이트가 저절로 돌게 하려고(krx-quant #93).

## main 자동 동기화와 SYNC_TOKEN

`custom-upstream-watch`는 매일 `main`을 upstream으로 동기화하고, 위의 자동 반영을 한다. upstream이 `.github/workflows/` 파일을 바꾸면 기본 `GITHUB_TOKEN`으로는 그 변경을 push 할 수 없다. `GITHUB_TOKEN`으로 push 한 태그는 `custom-release`를 돌리지도 않는다. 그래서 fine-grained PAT를 `SYNC_TOKEN` secret으로 등록한다.

- Repository access: 이 저장소(`codeyoma/FinceptTerminal`)만
- Permissions: Contents 읽기·쓰기, Workflows 읽기·쓰기

`main` 동기화가 실패하면 "Sync of main with upstream failed" issue가 열린다. 저장소 페이지의 **Sync fork** 버튼으로 손으로 동기화할 수 있다. `custom`을 보고 있을 때 나오는 **Discard N commits**는 누르지 않는다. 개인 커밋이 지워진다.

> **바뀜** (2026-10-12, krx-quant #94): `SYNC_TOKEN`은 `main` 동기화에만 썼다 → upstream 릴리스 자동 반영(브랜치 push, `custom` 반영, 태그 push)에도 쓴다. 없으면 자동 반영은 issue로 끝난다. 이유: 자동 반영이 upstream workflow 변경과 태그 push를 다뤄야 해서다(krx-quant #93).

## upstream 파일 수정 원칙

- 새 코드는 새 디렉터리에 둔다 (`fincept-qt/src/screens/krx_quant/`, `fincept-qt/src/services/krx_quant/`).
- 화면 등록과 로컬 CLI LLM(아래) 때문에 고치는 upstream 파일은 아래 목록으로 제한하고, 고칠 때마다 이 표를 갱신한다. 고친 곳에는 `fork addition (codeyoma/krx-quant#N)` 주석을 단다.

> **바뀜** (2026-10-12, codeyoma/krx-quant#97): 화면 등록 때문에 고치는 upstream 파일만 → 로컬 CLI LLM provider(codeyoma/krx-quant#96) 때문에 고치는 LLM 파일 9개도. 이유: Fincept의 LLM 기능을 Fincept 크레딧이나 API key 대신 이 컴퓨터에 로그인된 Claude Code·Codex CLI로 쓰려면 provider 목록, 요청 경로, 설정 화면에 갈래가 하나씩 필요했다. 새 코드는 `src/services/krx_quant/LocalCli*`에 두고 upstream 파일에는 한두 줄짜리 갈래만 넣었다.
- Fincept SQLite migration(`src/storage/sqlite/migrations/`)은 쓰지 않는다. 데이터는 엔진이 가진다.
- 개인 빌드는 공식 버전 업데이트를 끈다. 공식 버전으로 덮어써지는 것을 막기 위해서다. 대신 이 fork의 개인 빌드 Release에서만 업데이트한다(아래 "앱 업데이트").

> **바뀜** (2026-10-12, codeyoma/krx-quant#95): "개인 빌드는 앱 내 자동 업데이트를 끈다" → 공식 버전 업데이트만 끄고 개인 빌드 Release에서 업데이트한다. 그래서 아래 표의 `UpdateService.cpp`는 업데이트 확인을 멈추는 대신 `krx_quant::AppUpdater`로 넘긴다. 이유: 새 화면 빌드마다 직접 다시 설치해야 했다(codeyoma/krx-quant#92).

| upstream 파일 | 수정 내용 |
|---|---|
| `fincept-qt/src/app/WindowFrame_Setup.cpp` | `KrxQuantScreen` include와 `krx_quant` 화면 factory 등록 |
| `fincept-qt/src/app/DockScreenRouter.cpp` | `krx_quant` 탭 제목 "KRX 4분 예측" |
| `fincept-qt/src/ui/navigation/ToolBar.cpp` | Navigate → Trading & Portfolio 메뉴에 "KRX 4분 예측" |
| `fincept-qt/src/ui/navigation/CommandBar.cpp` | 명령 팔레트에 `krx_quant` ("krx"로 찾는다) |
| `fincept-qt/CMakeLists.txt` | `SERVICE_SOURCES`·`SCREEN_SOURCES`에 새 파일, unity 빌드 제외 목록, `FINCEPT_DISABLE_AUTO_UPDATE` 옵션(기본 ON), 빌드 태그 `FINCEPT_CUSTOM_TAG`, 업데이트 도우미 스크립트 리소스(`:/krx/`) |
| `fincept-qt/tests/CMakeLists.txt` | `fincept_add_test()` 정의 바로 뒤에 `include("${CMAKE_CURRENT_SOURCE_DIR}/krx_quant.cmake")` 한 줄. KRX 테스트(`tst_krx_engine_data`, `tst_krx_board`, `tst_krx_signals`, `tst_krx_stats_view`, `tst_krx_app_release`, `tst_krx_local_cli_llm`)는 새 파일 `fincept-qt/tests/krx_quant.cmake`에 등록한다 |
| `fincept-qt/src/services/updater/UpdateService.cpp` | `FINCEPT_DISABLE_AUTO_UPDATE`이면 공식 업데이트 대신 개인 빌드 업데이트(`krx_quant::AppUpdater`)로 넘긴다(`check_for_updates` 본문을 `#ifdef … #else … #endif`로 감쌌다) |
| `fincept-qt/src/services/llm/ProviderCatalog.cpp` | provider 목록에 로컬 CLI provider(`claude_cli`, `codex_cli`)를 붙이고, 이름·기본 모델·키 불필요·OpenAI 호환 아님을 `krx_quant::cli_llm`에 묻는다 |
| `fincept-qt/src/services/llm/LlmService.h` | 로컬 CLI provider는 키가 필요 없고, AI Chat이 스트리밍 경로(답은 한 덩어리)로 부른다 |
| `fincept-qt/src/services/llm/LlmService.cpp` | `do_request`가 로컬 CLI provider를 endpoint 확인 전에 Fincept 평문 프롬프트 루프(`fincept_async_request`)로 보낸다. `do_streaming_request`도 Fincept처럼 `do_request`로 돌린다 |
| `fincept-qt/src/services/llm/LlmFinceptAsync.cpp` | 평문 프롬프트 루프의 한 라운드를 Fincept 서버 대신 CLI로 보낸다(`local_cli_submit`). 프롬프트 한도는 CLI면 200,000자, Fincept면 그대로 50,000자 |
| `fincept-qt/src/services/llm/LlmModelsApi.cpp` | 로컬 CLI provider의 Fetch·Test는 CLI의 `--version`이 도는지 보고 모델 목록을 준다 |
| `fincept-qt/src/screens/settings/LlmConfigSection_Providers.cpp` | 로컬 CLI provider는 API key 칸을 끄고 Base URL 칸을 "CLI path"로 보인다. 저장·Test·Fetch에 키를 요구하지 않고, 모델이 비면 `default`로 저장한다 |
| `fincept-qt/src/screens/settings/LlmConfigSection_Profiles.cpp` | LLM 프로필의 provider 목록에서 로컬 CLI provider를 뺀다(프로필은 Python 에이전트가 쓰고, Python 에이전트는 CLI를 부르지 못한다) |
| `fincept-qt/src/services/alpha_arena/ArenaModelRegistry.cpp` | Alpha Arena 모델 목록에서 로컬 CLI provider를 뺀다(Arena는 provider API를 직접 부른다) |
| `fincept-qt/src/mcp/ProviderToolFormatSelfTest.cpp` | provider별 endpoint 자가 검사에서 로컬 CLI provider를 건너뛴다(endpoint가 없다) |

> **바뀜** (2026-10-12, krx-quant #94): KRX 테스트 등록을 `fincept-qt/tests/CMakeLists.txt` 끝에 붙였다 → `krx_quant.cmake`로 옮기고 `fincept_add_test()` 정의 뒤에서 include 한다. 이유: upstream도 파일 끝에 테스트를 붙여서, v4.5.0 이후 upstream `main`을 merge 하면 충돌했다. 자동 반영이 멈추지 않게 하려고(krx-quant #93).

## KRX 예측 화면 (#20, #21, #68)

- 코드: `fincept-qt/src/screens/krx_quant/`(화면, 차트, 순위표, 상세 통계 창, 알림 설정), `fincept-qt/src/services/krx_quant/`(엔진 API와 스트림 읽기, 응답 해석, 화면 상태, 신호 판정, 통계 표시 문구).
- 화면은 그리기와 입력만 맡는다. 엔진을 따라가는 상태(답과 스트림을 합친 `Board`, 신호 판정, 스트림, 읽기 주기, 엔진 시계)는 화면이 가진 `KrxLive`가 맡는다. upstream의 화면 규약(`docs/ARCHITECTURE.md`: 화면은 캐시와 업무 로직을 갖지 않는다)을 따른 것이다. 새 싱글턴은 만들지 않는다.
- Navigate → Trading & Portfolio → **KRX 4분 예측**(또는 명령 팔레트에서 "krx")으로 연다.
- 엔진 주소는 화면 위의 **엔진 주소** 칸에 넣는다(처음에는 비어 있다). 값은 QSettings `krx_quant/base_url`에 남는다. 실제 주소는 krx-quant 저장소의 문서에 있다.

### 구성

- **위쪽 줄**: 엔진 상태(장 단계, 마지막 봉 마감, 채점 대기, KIS 연결, 지난밤 학습, 스트림 연결), 매크로 줄(그날 국면과 이유, 달러선물. 지표 전체와 읽기 실패는 툴팁), 오늘의 특수 상황(VI·서킷브레이커·사이드카). 매크로가 오늘 것이 아니면 "(오늘 것이 아님)"을 붙인다.
  - 달러선물은 그날 첫 체결 뒤로는 엔진 상태의 장중 마지막 체결가, 전일 종가 대비 등락, 체결 시각이다(예: "달러선물 최근월물 1,395.2 (+0.50%, 10:29)"). 상태를 읽을 때마다(5초) 바뀐다. 매크로를 읽지 못했거나 아직 정한 국면이 없어도 보인다. 체결 전(장 전, 휴장일)에는 07:30 매크로 읽기의 값과 등락이다.
  - KIS는 "마지막 수신 N초 전"(가장 최근에 받은 피드)과 결측 피드 수(엔진이 끊김·멈춤으로 판정한 피드, 목록은 툴팁)로 보인다. 피드마다 조용한 시간은 지연이 아니다. 원래 드문 피드가 있고 장 밖에서는 모두 조용하다. 문제는 엔진이 결측으로 판정한다.
  - 지난밤 학습이 미적용이면 엔진이 준 이유를 한국어로 붙인다(08:00 교체 대기, 07:30 뒤에 확정, 모델 파일 없음, 적용 뒤 바뀜).
- **종목 목록**: 기준가, 최종 합의(방향과 가장 큰 확률. 상승과 하락이 같은 정수 %이면 "상승=하락"), 채점까지 남은 시간. 고른 종목이 차트와 예측 패널에 나온다. 장 시작 전이나 휴장일에는 지난 거래일의 마지막 예측이 남아 있다. 그 예측은 흐리게, 채점까지 칸은 "지난 거래일"로, 패널 제목은 날짜와 "(지난 거래일)"로 보인다.
- **차트**: 고른 종목의 오늘 1분봉이다. 범위는 30분·1시간(기본)·2시간·하루 전체이고 QSettings `krx_quant/chart_minutes`에 남는다. 봉 마감마다 최종 합의가 예측한 클래스를 그 봉에 그린다: 상승은 봉 아래 ▲, 하락은 봉 위 ▼, 보합은 –. 채점되면 그 옆에 ✓(맞음)나 ✗(틀림)를, 순위표에서 고른 기준으로 채점할 수 없었으면 ?를 그린다. 예측 클래스는 엔진이 채점하는 것과 같다(가장 큰 확률, 동률이면 보합, 그다음 상승). 4분 구간에 특수 상황이 낀 예측은 흐리게 그린다(통계에서 빠진다). 봉에 마우스를 올리면 가격과 그 예측이 나온다.
- **예측 패널**: 고른 종목의 최신 봉 마감 예측이다. 최종 합의, 빠른 합의, 알고리즘, 기준 예측 순이고 알고리즘과 기준 예측은 이름순이다. 열은 방향, 상승·보합·하락 확률, 기대 틱, 모델 버전이다. 상승과 하락이 같은 정수 %로 보이면 방향은 비운다. 제목에 봉 마감과 채점까지 남은 시간(엔진 시계 기준)이 나온다.
- **순위표**: 기간(오늘·5일·20일), 채점 기준(B 표시 시점·A 봉 마감), 종목(전체·종목별), 특수 상황 포함을 고른다. 열은 순위, 상태(수습·정식·벤치·은퇴·복제), 모델 버전, 표본, 정답률, 방향 정밀도, 커버리지, Brier, 거래당 손익(틱), 가장 약한 lift 하한, 운 보정 통과다. 95% 구간은 툴팁에 있다. 순위가 없는 항목, 기준 예측(흐리게) 순으로 이어진다. 차트의 ✓/✗도 여기서 고른 채점 기준을 따른다.
- **상세 통계 창**: 순위표의 줄을 두 번 누르면 그 예측(합의와 기준 예측도)의 창이 따로 열린다. 화면에 하나이고, 다른 줄을 두 번 누르면 그 예측으로 바뀐다. 순위표와 같은 조건(기간, 채점 기준, 종목, 특수 상황)으로 엔진의 상세 통계(`/v1/algorithms/{name}/stats`)를 읽는다.
  - 맨 위: 조건과 날짜, 그 예측의 순위와 통계(순위표와 같은 통계 열. 순위가 없으면 "순위 밖")
  - **기준 예측 대비 lift**: 기준 예측 셋 각각에 대한 lift, 95% 구간, 짝 수, p, q, 운 보정. 순위 밖이라 함께 보정하지 않은 lift는 q와 운 보정이 "—"다. 그 기간에 기준 예측과 짝지을 봉 마감이 없으면 lift도 "—"다(짝 0). 거래일 95% 구간과 날 수도 있다. 하루를 값 하나로 본 구간이고, 엔진은 상태(수습·정식·벤치)를 이것으로 정한다. 봉 마감마다 센 95% 구간과 p는 같은 날의 4분 정답이 겹쳐 실제보다 좁은 설명용이다. 날이 2일보다 적으면 "—"다.
  - **보정도**: 모든 클래스 확률을 10% 구간으로 나눠, 가로에 구간의 평균 예측 확률, 세로에 실제로 일어난 비율과 95%(Wilson) 구간을 찍는다. 점선은 완벽한 예측이다. 예측이 없는 구간은 찍지 않는다. 점에 마우스를 올리면 구간, 예측 수, 평균 예측, 실제 비율이 나온다.
  - **30분 시간대별, 모델 버전별, 종목별** 탭: 순위표와 같은 통계 열이다. 시간대는 예측한 봉의 마감 시각으로 쓴다("09:01–09:30"). 이 줄들의 lift는 함께 검정하지 않는다.
- **신호 알림**: 엔진의 텔레그램 신호와 같은 규칙이다(krx-quant `docs/engine-api.md` "텔레그램 신호 알림").
  - 최종 합의의 상승 확률이 기준값 이상이면 상승, 아니면 하락 확률이 기준값 이상일 때 하락이다.
  - 그 합의에 들어간 정식 알고리즘 가운데 N개 이상이 같은 방향을 예측해야 한다. 각자의 방향은 가장 큰 확률의 클래스다(동률이면 보합).
  - 같은 종목·같은 방향은 앞 신호의 채점 시각(4분 뒤)까지 다시 알리지 않는다. 채점 시각이 지난 합의(장 마감 뒤에 화면을 연 경우 등)는 알리지 않는다. 한 봉 마감은 한 번만 본다. 합의 구성원을 아직 모르면(읽기 실패 뒤 다시 읽는 중) 그 합의는 알 때까지 미뤘다가 본다.
  - 이 화면을 처음 연 뒤부터 다른 화면을 보는 동안에도 알린다. Fincept는 화면을 처음 열 때 만들고, 탭을 닫으면 없앤다. 그래서 앱을 켠 뒤 이 화면을 한 번도 열지 않았거나 탭을 닫으면 알림도 없다.
  - 알림은 Fincept 알림으로 나간다(`ToastService`: 앱 안 알림과 OS 알림). 소리를 켜면 OS 기본 알림음이 난다. **시험 알림** 버튼으로 OS 알림 권한과 소리를 확인한다.
  - 켜기(기본 켬), 기준값(50–99%, 기본 60%), N(1–20개, 기본 2개), 소리(기본 켬)는 화면에서 바꾸고 QSettings `krx_quant/signal_on`, `krx_quant/signal_probability`, `krx_quant/signal_agree`, `krx_quant/signal_sound`에 남는다.

### 갱신

- 엔진의 스트림(`/v1/stream`, WebSocket)을 따른다. 예측, 합의, 채점, KIS 상태, 알고리즘 상태가 오는 대로 반영한다. 스트림은 연결 뒤의 일만 보내므로, 연결하거나 다시 연결하면 API를 먼저 다시 읽는다. 답과 스트림이 어떤 순서로 오든 더 늦은 봉 마감을 이른 것으로, 채점된 예측을 채점 전 것으로 되돌리지 않는다.
- 끊기면 상태 줄에 이유와 다시 연결까지 남은 초를 보이고, 1, 2, 4, …초(최대 30초) 간격으로 끝없이 다시 연결한다(엔진은 매일 아침 다시 시작한다). 15초마다 ping을 보내고 45초 동안 아무 응답이 없으면 끊고 다시 연결한다(잠자기에서 깬 노트북 등).
- 스트림이 연결되어 있으면 API는 상태만 5초마다(피드 지연), 종목·최신 예측·합의 구성원은 30초마다, 차트와 순위표는 60초마다 읽는다. 새 봉 마감이 오면 차트를, 채점이 오면 3초 뒤 순위표를 바로 읽는다. 매크로는 5분마다다. 스트림이 없으면(끊김, Qt WebSockets 없는 빌드) 상태·예측·합의 구성원·순위표를 5초마다 읽는다. 채점을 알려줄 스트림이 없어서다. 차트와 매크로는 스트림이 있을 때와 같다(차트는 새 봉 마감마다, 그리고 60초마다).
- 순위표를 읽는 중에 조건을 바꾸면, 읽기가 끝난 뒤 마지막 조건으로 다시 읽는다. 끝난 읽기의 답(실패 포함)은 버린다. 마지막 조건이 읽는 중인 것과 같으면 그 답을 쓴다(엔진이 5초보다 느려도 스트림 없이 5초마다 묻는 읽기가 답을 받는다).
- 상세 통계 창이 열려 있으면 순위표를 읽을 때마다(조건을 바꿀 때도) 그 예측의 상세 통계를 같이 읽는다. 읽는 중에 다른 예측이나 조건을 고르면 위와 같이 마지막 것으로 다시 읽는다. 창을 닫으면 읽지 않는다.
- 화면이 가려져 있어도 스트림은 열어 두어 신호 알림이 온다. 그동안 스트림이 없고 알림이 켜져 있으면 상태와 예측을 5초마다 읽는다. 차트와 매크로는 읽지 않고, 순위표는 스트림으로 채점이나 알고리즘 상태 변경이 온 뒤에만 읽는다. 상세 통계 창은 다른 화면 옆에 열려 있을 수 있어서, 스트림이 없으면 그 통계를 5초마다 읽는다. 다른 KRX 패널이 엔진 주소를 바꾸면 가려진 패널도 5초 안에 새 주소로 옮긴다.
- 한 종류의 읽기가 끝나기 전에는 같은 읽기를 다시 묻지 않는다. 읽기에 실패하면 지난 예측과 차트를 지우고 이유를 보여준다(합의 구성원은 남긴다). 다시 읽히면 예측, 매크로, 순위표, 차트를 다시 읽는다.
- 엔진의 시계가 화면에 보이는 봉 마감보다 이르면(다시 띄운 데모 엔진) 그 뒤의 예측을 버리고 엔진을 따라간다.

> **바뀜** (2026-10-11, krx-quant #64): 화면이 보이는 동안 5초마다 모든 것을 API로 읽었다 → 스트림으로 받고, API는 연결·재연결 때와 위의 주기로 읽는다. 화면이 가려져도 스트림은 열어 둔다. 이유: 스트림으로 갱신하고 끊기면 다시 연결한다(#21). 다른 화면을 보는 동안에도 신호 알림이 와야 한다.

> **바뀜** (2026-10-11, krx-quant #64): 표 하나(종목마다 모든 예측) → 종목 목록과 고른 종목의 예측 패널. 알고리즘과 기준 예측은 엔진이 준 순서 → 이름순. 이유: 차트와 패널을 한 종목씩 보여주게 됐고(#21), 스트림이 알고리즘을 끝난 순서대로 가져와 줄이 매분 바뀌었다.

> **바뀜** (2026-10-12, krx-quant #74): 달러선물은 07:30 읽기만 → 장중에는 엔진 상태의 마지막 체결가와 전일 종가 대비 등락. 이유: 그날의 달러선물 흐름을 매크로 줄로 본다(krx-quant #70).

### 개발

- 엔진 없이 화면을 만들 때는 krx-quant 저장소에서 데모 엔진을 띄우고 주소를 `http://127.0.0.1:8090`으로 바꾼다. 고정된 가상 데이터다.
  - `uv run krx-quant demo-engine`: 10:30에 멈춘 아침이다. 스트림에는 연결할 때의 KIS 상태 하나만 온다.
  - `uv run krx-quant demo-engine --pace 2`: 10:00에 열고 10:30까지 1분을 2초마다 이어 재생하며 스트림을 보낸다. 차트, 패널, 채점이 움직이는 것을 볼 수 있다. 데모 아침의 최종 합의는 50%를 거의 넘지 않아 신호는 나오지 않는다. 알림은 **시험 알림**으로 본다.
- 테스트(Qt Core만): `cmake -B build -DFINCEPT_BUILD_TESTS=ON … && cmake --build build --target tst_krx_engine_data tst_krx_board tst_krx_signals tst_krx_stats_view tst_krx_app_release && ctest --test-dir build -R krx`.
  - CI는 경고를 오류로 빌드한다(`-Werror`, 환경 변수 `CI`가 있을 때). 로컬에서 같은 조건으로 보려면 `CI=1 cmake -B build …`로 설정한다. 그러지 않으면 구조체에 필드를 더하고 테스트의 초기화 목록을 그대로 두는 것 같은 경고를 놓친다(`custom-v4.5.0-krx.4` 맥 빌드가 이것으로 실패했다).
  - `tst_krx_engine_data`: 엔진 API와 스트림 메시지 해석(상세 통계의 기준별 lift와 그 거래일 단위 값, 보정 곡선, 나눠 본 통계 포함), 한국어 이름, 시각·가격 표기.
  - `tst_krx_board`: 답과 스트림을 합친 화면 상태(최신 봉 마감, 차트 표시, 합의 구성원, 신호가 볼 예측, 실패와 되감기).
  - `tst_krx_signals`: 신호 규칙, 다시 알리지 않는 규칙, 구성원을 모를 때 미루기.
  - `tst_krx_stats_view`: 순위표와 상세 통계 창이 보이는 문구(비율·평균과 구간, 기준별 lift 줄과 거래일 구간, 시간대, 보정도 점, 조건과 날짜).
  - `tst_krx_app_release`: 개인 빌드 태그 해석과 순서, Release 목록에서 업데이트할 빌드·설치 파일·sha256 고르기.

## 로컬 CLI LLM (codeyoma/krx-quant#96)

Fincept의 LLM 기능을 Fincept 크레딧이나 API key 대신 이 컴퓨터에 로그인된 **Claude Code CLI**(`claude`)나 **Codex CLI**(`codex`)로 돌린다. 요청은 각 CLI의 구독(Claude Pro/Max, ChatGPT 플랜) 사용량에서 나간다.

- **설정.** Settings → LLM Config에서 provider **Claude Code CLI (local)**(`claude_cli`)나 **Codex CLI (local)**(`codex_cli`)를 추가하고 Save 한다. Save 한 provider가 활성 provider가 된다.
  - API key: 없다. CLI의 로그인을 쓴다. CLI에 먼저 로그인한다(`claude`를 한 번 실행, `codex login`).
  - Model: `default`는 CLI의 기본 모델이다(모델 인자를 넘기지 않는다). Claude는 CLI의 별칭(`opus`, `sonnet`, `haiku`)이나 모델 id를 쓴다. Codex는 Fetch가 CLI의 모델 캐시(`$CODEX_HOME/models_cache.json`, 기본 `~/.codex`)에서 숨김이 아닌 모델을 우선순위대로 보인다.
  - CLI path(다른 provider의 Base URL 칸): 비우면 자동으로 찾는다. 실행 파일이나 그것이 든 폴더를 넣으면 그것을 쓴다. 값은 LLM 설정의 `base_url`에 남는다.
  - Test와 Fetch는 CLI의 `--version`이 도는지만 본다. 로그인은 확인하지 않는다. 로그인이 안 되어 있으면 첫 대화에서 CLI의 오류가 그대로 나온다.
- **쓰는 곳.** `LlmService`를 부르는 곳: AI Chat 화면, 떠 있는 채팅 버블, workflow의 agent 노드. 쓰지 않는 곳: Python 에이전트(Agents, Teams)와 그것이 쓰는 LLM 프로필, Alpha Arena. 이들은 provider API를 직접 불러서 프로필과 Arena 목록에 로컬 CLI provider가 나오지 않는다. 대시보드 위젯 등 LLM을 쓰지 않는 화면은 상관없다.
- **동작.** LLM 한 라운드마다 CLI를 한 번 실행한다. 프롬프트는 stdin으로 주고 답은 JSON으로 받는다. CLI에는 자기 도구가 없다. Fincept의 도구(시세, 화면 이동, 리포트 등)는 Fincept LLM과 같은 평문 프롬프트 루프로 돈다: 도구 목록을 프롬프트에 넣고, 모델이 `<tool_call>` 블록으로 답하면 Fincept가 실행해 결과를 붙여 다시 부른다.
  - Claude: `claude -p --safe-mode --tools "" --no-session-persistence --output-format json --system-prompt <Fincept 안내> [--model <모델>]`. CLAUDE.md, hooks, MCP 서버, skills, plugins를 읽지 않고, 세션을 저장하지 않는다.
  - Codex: `codex exec --ignore-user-config --ignore-rules --ephemeral --skip-git-repo-check --sandbox read-only --disable shell_tool --disable unified_exec --json --cd <빈 폴더> [--model <모델>] -`. `~/.codex/config.toml`(MCP 서버, 모델·추론 설정)과 rules를 읽지 않고, 세션을 저장하지 않는다. 셸 도구는 끄고 sandbox는 읽기 전용이다. `~/.codex/AGENTS.md`(전역 지시)는 끌 방법이 없어 그대로 적용된다.
  - CLI는 임시 폴더의 빈 폴더 `fincept-llm-cli`에서 돈다. 그 자리의 프로젝트 파일을 지시로 읽지 않게 하려고서다.
  - 맥의 GUI 앱은 PATH가 짧다. 그래서 앱의 PATH, 로그인 셸(`$SHELL -l`)의 PATH, `~/.local/bin`, `~/.claude/local`, `/opt/homebrew/bin`, `/usr/local/bin`, `~/.npm-global/bin`, `~/.bun/bin`, `~/.volta/bin` 순으로 찾는다. CLI가 든 폴더를 PATH 맨 앞에 두고 실행한다(Codex는 node로 돈다).
  - 윈도우: npm은 `codex`와 `claude`를 `.cmd` shim으로 깐다. shim이 부르는 node 스크립트(`node_modules\…\codex.js`)를 node로 바로 실행한다. 스크립트를 못 찾으면 shim을 `cmd.exe /d /s /c "<명령줄>"`로 실행한다(`/s`라서 공백이 든 경로도 따옴표가 그대로 남는다). 윈도우에서는 시험하지 않았다.
- **한도와 속도.** 한 라운드의 프롬프트는 200,000자까지다. 넘으면 Fincept LLM처럼 오래된 대화와 오래된 도구 결과부터 뺀다. 한 라운드는 300초가 지나면 멈추고 오류를 낸다. 답은 토큰 단위로 흐르지 않고 끝나면 한 번에 나온다. 라운드마다 CLI를 새로 띄워서(Claude 약 5초, Codex 약 8초에 모델 시간이 더해진다) 도구를 여러 번 쓰는 질문은 느리다.
- **주의.** Claude CLI는 환경 변수 `ANTHROPIC_API_KEY`가 있으면 구독 대신 그 키로 과금될 수 있다.
- **코드.** `fincept-qt/src/services/krx_quant/LocalCliLlm.{h,cpp}`(provider, 인자, 출력 해석, Codex 모델 캐시), `LocalCliRunner.{h,cpp}`(CLI 찾기, 실행, Settings의 확인). upstream 파일의 갈래는 위 "upstream 파일 수정 원칙" 표에 있다.
- **테스트.** `tst_krx_local_cli_llm`(Qt Core만): CLI별 인자, 각 CLI 출력의 답·사용량·오류 해석, Codex 모델 캐시, 대역 CLI로 찾기·stdin 전달·시간 초과, Settings의 확인(`--version`)과 실패 문구, 윈도우 npm shim의 node 스크립트 찾기와 `cmd.exe` 명령줄 따옴표.

## 개인 빌드

- `custom` 브랜치의 커밋에 `custom-v*` 태그(예: `custom-v4.5.0-krx.1`)를 달아 push하면 `custom-release` workflow가 맥 DMG와 윈도우 `setup.exe`를 만들어 이 fork의 Release(prerelease)에 올린다.
- 서명하지 않은 빌드다. 맥은 처음 열 때 우클릭 → 열기(또는 `xattr -dr com.apple.quarantine /Applications/FinceptTerminal.app`), 윈도우는 SmartScreen에서 추가 정보 → 실행.
- 공식 버전 업데이트는 꺼져 있다(`FINCEPT_DISABLE_AUTO_UPDATE`). 새 버전은 새 태그로 만들고, 설치된 앱은 아래처럼 스스로 업데이트한다.
- upstream 릴리스를 반영한 첫 빌드는 workflow가 `custom-<tag>-krx.1`로 태그한다(위 "upstream 릴리스 반영"). 그 뒤 화면 변경은 같은 upstream 버전에 번호를 올려 손으로 태그한다(`custom-<tag>-krx.2`, …).

> **바뀜** (2026-10-12, codeyoma/krx-quant#95): 앱 내 업데이트 꺼짐 → 이 fork의 개인 빌드 Release에서 버튼 한 번으로 업데이트. 이유: 새 화면 빌드마다 DMG·setup.exe를 받아 직접 다시 설치해야 했다(codeyoma/krx-quant#92). 공식 버전 업데이트는 여전히 꺼져 있다.

## 앱 업데이트 (codeyoma/krx-quant#92)

- **빌드 태그.** `custom-release` workflow가 태그를 `-DFINCEPT_CUSTOM_TAG`로 빌드에 넘긴다. 앱은 자기가 어느 개인 빌드인지 안다. 로컬 빌드는 태그가 없어 업데이트를 확인하지 않는다.
- **확인.** 로그인 뒤 한 번 조용히 확인하고, 새 빌드가 있을 때만 묻는다. 메뉴의 업데이트 확인과 About 화면의 버튼은 늘 결과를 알려 준다. GitHub API(토큰 없음)로 이 fork의 Release를 읽어, 지금보다 새 `custom-v*` 빌드 가운데 이 플랫폼의 설치 파일(`…-macos-arm64.dmg`, `…-windows-x64-setup.exe`)과 GitHub의 sha256 digest가 있는 가장 새 것을 고른다. 순서는 upstream 버전, 다음 `krx.N`이다. 초안 Release와 공식 태그는 보지 않는다.
- **지금 업데이트.** 설치 파일을 내려받아 sha256을 확인한다. 맞지 않거나 digest가 없으면 지우고 멈춘다. 확인되면 도우미 스크립트(`fincept-qt/src/services/krx_quant/update/`, 앱 리소스에 들어 있다)를 띄우고 앱을 닫는다. 도우미는 앱이 끝나기를 기다린 뒤 새 빌드를 같은 자리에 설치하고 연다. 기록은 임시 폴더의 `fincept-update.log`에 남는다.
  - 맥(`update-macos.sh`): DMG를 붙여 설치된 `.app`을 새 것으로 바꾸고 서명을 확인한다. 옛 앱은 복사가 끝날 때까지 옆에 두었다가, 실패하면 되돌린다. macOS가 "앱 관리" 권한을 물으면 허용한다. 막히면 옛 앱이 그대로 남고 DMG가 열린다.
  - 윈도우(`update-windows.ps1`): 설치 폴더의 `FinceptMaintenanceTool.exe purge`로 지우고, 새 `setup.exe`를 같은 폴더에 무인 설치한다(workflow의 설치 확인과 같은 옵션). 설치 폴더가 Program Files면 UAC 확인이 지울 때와 설치할 때 두 번 뜬다. 옛 설치로 되돌리지는 못한다. 두 번째 확인을 거절하거나 설치가 실패하면 앱이 없는 상태가 되고, 설치 마법사가 열리니 그것으로 다시 설치한다.
  - 자동 설치가 안 되는 경우(설치 프로그램으로 설치하지 않은 앱, 쓸 수 없는 폴더, 도우미 실패)에는 설치 파일을 열어 직접 설치하게 한다.
- **설치 중.** "지금 업데이트"를 누르면 내려받는 동안 진행 창(취소 가능)이 뜬다. 설치 전에 앱이 닫힌다는 안내가 나오고, 확인을 누르면 앱이 닫힌다. 그 뒤 1–2분 동안은 화면이 없다(윈도우는 숨은 창에서 설치한다).
- **신뢰.** Release에서 직접 받는 것과 같다(이 fork의 GitHub 계정과 Release). 거기에 GitHub digest 확인을 더했다. 서명 키는 쓰지 않는다.
- **CI.** `custom-release` workflow가 두 도우미를 새로 만든 설치 파일로 돌려 본다. 윈도우는 설치한 앱 위에 다시 설치하고, 맥은 설치된 복사본을 새 DMG로 바꾼다.

## 에이전트 설정

루트 `CLAUDE.md`는 `@custom/AGENTS.md` 한 줄만 담는다. upstream `.gitignore`가 `CLAUDE.md`를 무시하므로 `git add -f`로 추적하고, upstream이 같은 파일을 만들 일이 없어 merge 충돌이 나지 않는다. 설정 본문은 `custom/AGENTS.md`와 `custom/agents/`에 있다. 작업 ticket은 비공개 저장소 `codeyoma/krx-quant`의 이슈로 관리한다. 이 fork는 비공개 저장소 `krx-quant`의 `fincept/` submodule로 받는다(`git clone --recurse-submodules`). 단독으로 clone해도 빌드와 upstream merge는 되지만, 에이전트 문서가 참조하는 상위 폴더의 설계 문서(`../docs/design.md`)는 없다.

## upstream workflow

이 fork에서는 upstream의 예약·릴리스 workflow를 끈다(GitHub Actions 설정에서 disable). 개인 빌드용 workflow는 `custom-` 접두사로 둔다: `custom-upstream-watch`(`main` 동기화와 upstream 릴리스 자동 반영), `custom-release`(개인 빌드와 upstream merge의 시험 빌드, 위 "개인 빌드").

> **바뀜** (2026-10-12, krx-quant #94): `custom-upstream-watch`는 upstream 릴리스 알림 → 자동 반영. `custom-release`에 `upstream-merge/<tag>` 브랜치의 시험 빌드가 더해졌다. 이유: upstream 릴리스 자동 반영(krx-quant #93).
