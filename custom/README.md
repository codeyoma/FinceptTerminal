# codeyoma fork 운영 메모

이 fork는 upstream [Fincept-Corporation/FinceptTerminal](https://github.com/Fincept-Corporation/FinceptTerminal)에 KRX 4분 예측 엔진(저장소 `krx-quant`)을 보는 화면 하나를 더한 개인용 빌드다. 엔진과 알고리즘은 이 저장소에 두지 않는다. 엔진 코드는 `krx-quant`에 한 곳으로 두고, 화면은 엔진 API로 받은 것을 보이기만 한다.

> **바뀜** (2026-10-14, codeyoma/krx-quant#194): `krx-quant`는 비공개 저장소였다 → 공개. 이유: 비공개 저장소의 GitHub Actions 시간이 바닥났고, 알고리즘과 모델은 보여도 된다고 정했다. 엔진과 화면을 나눈 구조는 그대로다.

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

> **바뀜** (2026-10-14, krx-quant 결정): merge 뒤 workflow가 시험 빌드를 바로 시작했다 → `ready to build` issue를 열고 시험 빌드는 손으로 시작한다. 이유: 빌드는 필요할 때만 손으로 돌리기로 했다(맥·윈도우 빌드가 업데이트마다 돌았다).

> **바뀜** (2026-10-15, krx-quant 결정): 시험 빌드를 손으로 시작했다(`ready to build` issue) → 다시 workflow가 merge 뒤 바로 시작한다. 이유: 이 fork는 공개 저장소라 GitHub Actions가 무료다. 한도를 쓴 것은 비공개였던 krx-quant의 CI였고, 그 저장소도 공개가 됐다. 빌드는 자동이 편하다.

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

## krx-quant 포인터와 KRX_QUANT_DISPATCH_TOKEN

krx-quant는 이 fork를 `fincept/` submodule로 담는다. 그 포인터는 언제나 `custom`의 최신 커밋을 가리킨다(codeyoma/krx-quant#186). `custom-krx-pointer` workflow가 `custom`에 push 될 때마다(PR merge, upstream 자동 반영, 손 push. 문서나 workflow만 바꾼 push도) krx-quant에 `repository_dispatch`(`fincept-custom`)를 보낸다. 그러면 krx-quant의 workflow가 `custom`의 head를 읽어 포인터를 옮긴다. 그래서 fork PR을 merge 한 뒤 krx-quant에서 포인터를 손으로 올리지 않는다.

`GITHUB_TOKEN`은 다른 저장소에 닿지 못한다. 그래서 fine-grained PAT를 `KRX_QUANT_DISPATCH_TOKEN` secret으로 등록한다.

- Repository access: `codeyoma/krx-quant`만
- Permissions: Contents 읽기·쓰기(`repository_dispatch`에 필요하다)

secret이 없거나 만료되면 `custom-krx-pointer`가 실패한다. 그래도 krx-quant가 매일 05:30 KST에 스스로 따라잡는다.

## upstream 파일 수정 원칙

- 새 코드는 새 디렉터리에 둔다 (`fincept-qt/src/screens/krx_quant/`, `fincept-qt/src/services/krx_quant/`).
- 화면 등록과 로컬 CLI LLM(아래) 때문에 고치는 upstream 파일은 아래 목록으로 제한하고, 고칠 때마다 이 표를 갱신한다. 고친 곳에는 `fork addition (codeyoma/krx-quant#N)` 주석을 단다.

> **바뀜** (2026-10-19, codeyoma/krx-quant#302): 위의 것에 더해, 대시보드의 KRX 위젯 때문에 `WidgetRegistry.cpp`, `AddWidgetDialog.cpp`, `DashboardTemplates.cpp`, `TemplatePicker.cpp`도 고친다(아래 표, 아래 "대시보드 KRX 위젯"). 이유: 위젯 목록, 카테고리 색, 템플릿 목록이 upstream 파일 안의 고정된 목록이라, 거기에 한 줄씩 갈래를 넣어야 했다. 템플릿 선택 창은 640×400 고정이라 일곱째 템플릿(셋째 줄)이 들어가면 카드가 모두 겹쳤다. 위젯 코드는 `screens/krx_quant/dashboard/`에 둔다.

> **바뀜** (2026-10-16, codeyoma/krx-quant#247): 위의 것에 더해, 업데이트마다 실패하던 Python 라이브러리 설치 때문에 `fincept-qt/src/python/PythonSetupManager.cpp`도 고친다(아래 표). 이유: upstream v4.5.1의 분석 핵심 설치 단계가 uv `-c`에 lock의 절대 경로를 넘기는데, uv는 이 값을 공백에서 나눈다. 맥의 `Application Support` 경로에서 늘 실패해, 요구 목록이 바뀌는 업데이트마다 "Python 라이브러리 설치에 실패했습니다" 알림이 떴다. upstream이 고치면 이 갈래는 뺀다.

> **바뀜** (2026-10-13, codeyoma/krx-quant#152): 위의 것에 더해, 경제 캘린더(codeyoma/krx-quant#150) 때문에 `fincept-qt/src/services/economics/MacroCalendarService.cpp`도 고친다(아래 표). 이유: 대시보드의 Economic Calendar가 읽는 Fincept 서버 엔드포인트가 크레딧을 썼다. 엔진이 같은 데이터를 모아 두었으니, 엔진 주소가 있으면 거기서 읽도록 갈래를 하나 넣었다.

> **바뀜** (2026-10-13, codeyoma/krx-quant#136): 화면 등록과 로컬 CLI LLM 때문에 고치는 upstream 파일만 → 수동 전체 플랫폼 빌드(`build-cpp.yml`) 수리도. 이유: 한국어 팩(codeyoma/krx-quant#132)을 검증하다 다섯 대상이 모두 실패하는 것을 찾았다. 고친 곳마다 `fork addition` 주석을 달았다.

> **바뀜** (2026-10-13, codeyoma/FinceptTerminal#26): 화면 등록과 로컬 CLI LLM 때문에 고치는 upstream 파일만 → 원화 환산 표시(codeyoma/krx-quant#142) 때문에 고치는 시장·관심종목·대시보드·포트폴리오·주식 리서치·설정 화면과 시세 service도(아래 표). 이유: 출처 통화가 확인된 금액만 화면에서 환산하려면 각 화면이 금액을 그리는 곳에 갈래가 필요했다. upstream 릴리스를 merge 할 때 이 파일들이 충돌할 수 있다.

> **바뀜** (2026-10-12, codeyoma/krx-quant#97): 화면 등록 때문에 고치는 upstream 파일만 → 로컬 CLI LLM provider(codeyoma/krx-quant#96) 때문에 고치는 LLM 파일 9개도. 이유: Fincept의 LLM 기능을 Fincept 크레딧이나 API key 대신 이 컴퓨터에 로그인된 Claude Code·Codex CLI로 쓰려면 provider 목록, 요청 경로, 설정 화면에 갈래가 하나씩 필요했다. 새 코드는 `src/services/krx_quant/LocalCli*`에 두고 upstream 파일에는 한두 줄짜리 갈래만 넣었다.
- Fincept SQLite migration(`src/storage/sqlite/migrations/`)은 쓰지 않는다. 데이터는 엔진이 가진다.
- 개인 빌드는 공식 버전 업데이트를 끈다. 공식 버전으로 덮어써지는 것을 막기 위해서다. 대신 이 fork의 개인 빌드 Release에서만 업데이트한다(아래 "앱 업데이트").

> **바뀜** (2026-10-12, codeyoma/krx-quant#95): "개인 빌드는 앱 내 자동 업데이트를 끈다" → 공식 버전 업데이트만 끄고 개인 빌드 Release에서 업데이트한다. 그래서 아래 표의 `UpdateService.cpp`는 업데이트 확인을 멈추는 대신 `krx_quant::AppUpdater`로 넘긴다. 이유: 새 화면 빌드마다 직접 다시 설치해야 했다(codeyoma/krx-quant#92).

| upstream 파일 | 수정 내용 |
|---|---|
| `fincept-qt/src/app/WindowFrame_Setup.cpp` | `KrxQuantScreen`·`KrxBriefingScreen`·`KrxFlowScreen`·`KrxCostScreen`·`KrxClosingScreen`·`KrxTraderScreen` include와 `krx_quant`·`krx_briefing`(codeyoma/krx-quant#304)·`krx_flow`(codeyoma/krx-quant#306)·`krx_cost`(codeyoma/krx-quant#307)·`krx_closing`(codeyoma/krx-quant#305)·`krx_trader`(codeyoma/krx-quant#397) 화면 factory 등록 |
| `fincept-qt/src/app/DockScreenRouter.cpp` | `krx_quant` 탭 제목 "KRX 4분 예측", `krx_briefing` 탭 제목 "KRX 장전 브리핑"(codeyoma/krx-quant#304), `krx_flow` 탭 제목 "KRX 수급 흐름"(codeyoma/krx-quant#306), `krx_cost` 탭 제목 "KRX 지평·비용"(codeyoma/krx-quant#307), `krx_closing` 탭 제목 "KRX 마감 흐름"(codeyoma/krx-quant#305), `krx_trader` 탭 제목 "KRX 자동 매매"(codeyoma/krx-quant#397) |
| `fincept-qt/src/ui/navigation/ToolBar.cpp` | Navigate → Trading & Portfolio 메뉴에 "KRX 4분 예측", "KRX 장전 브리핑"(codeyoma/krx-quant#304), "KRX 수급 흐름"(codeyoma/krx-quant#306), "KRX 지평·비용"(codeyoma/krx-quant#307), "KRX 마감 흐름"(codeyoma/krx-quant#305), "KRX 자동 매매"(codeyoma/krx-quant#397) |
| `fincept-qt/src/ui/navigation/CommandBar.cpp` | 명령 팔레트에 `krx_quant`, `krx_briefing`(codeyoma/krx-quant#304), `krx_flow`(codeyoma/krx-quant#306), `krx_cost`(codeyoma/krx-quant#307), `krx_closing`(codeyoma/krx-quant#305), `krx_trader`(codeyoma/krx-quant#397) (모두 "krx"로 찾는다) |
| `fincept-qt/CMakeLists.txt` | `SERVICE_SOURCES`·`SCREEN_SOURCES`에 새 파일, unity 빌드 제외 목록, `FINCEPT_DISABLE_AUTO_UPDATE` 옵션(기본 ON), 빌드 태그 `FINCEPT_CUSTOM_TAG`, 업데이트 도우미 스크립트 리소스(`:/krx/`)와 윈도우 설치 트리의 그 사본 `krx-update/update-windows.ps1`(codeyoma/krx-quant#234), 윈도우 설치 프로그램의 기본 폴더 `CPACK_IFW_TARGET_DIRECTORY`(사용자 폴더, codeyoma/krx-quant#130), 윈도우 설치 프로그램에 담는 WebEngine 파일에 V8 시작 스냅샷 `resources/v8_context_snapshot.bin`(codeyoma/krx-quant#207), 한국어 언어 팩 `translations/fincept_ko_KR.ts`의 컴파일·리소스 포함, clang-cl(Windows ARM64)에서 GCC/Clang 경고 묶음을 빼고 `-Wno-clang-cl-pch`(codeyoma/krx-quant#136) |
| `fincept-qt/tests/CMakeLists.txt` | `fincept_add_test()` 정의 바로 뒤에 `include("${CMAKE_CURRENT_SOURCE_DIR}/krx_quant.cmake")` 한 줄. KRX 테스트(`tst_krx_engine_data`, `tst_krx_board`, `tst_krx_signals`, `tst_krx_stats_view`, `tst_krx_app_release`, `tst_krx_local_cli_llm`, `tst_krx_korean`, `tst_krx_engine_calendar`, `tst_krx_kind_view`, `tst_krx_chart_look`, `tst_krx_chart_studies`, `tst_krx_chart_drawings`, `tst_krx_chart_bricks`, `tst_krx_chart_slots`, `tst_krx_live`, `tst_krx_chart_page`, `tst_krx_chart_toolbar`, `tst_krx_bar_view`, `tst_krx_dashboard_view`, `tst_krx_briefing`, `tst_krx_flow`, `tst_krx_cost`, `tst_krx_closing`, `tst_krx_trader`, `tst_krx_trader_screen`)는 새 파일 `fincept-qt/tests/krx_quant.cmake`에 등록한다 |
| `fincept-qt/src/core/i18n/LanguageManager.cpp` | 공용 언어 목록에 `ko_KR`, 언어 이름에 `한국어`를 추가한다(codeyoma/krx-quant#132). 설정 화면과 로그인 언어 선택이 이 목록을 그대로 쓴다 |
| `fincept-qt/src/core/currency/CurrencyManager.{h,cpp}`, `Currency.h`, 새 `core/currency/fx/` | KRW 목록, 출처 통화를 요구하는 환산 API, ECB 비인증 조회·캐시·갱신 신호(codeyoma/krx-quant#138, #142). 공용 인프라라 KRX 엔진 service 대신 독립 디렉터리에 둔다 |
| `fincept-qt/src/screens/settings/GeneralSection.{h,cpp}` | 통화 설명, 환율 출처·기준일·갱신 상태·버튼(#142) |
| `fincept-qt/src/services/markets/MarketDataService.{h,cpp}`, `scripts/yfinance_data.py` | 원 통화·종목 유형 보존, 메타데이터 캐시와 화면용 환산 API(#142) |
| `fincept-qt/src/screens/markets/MarketPanel.cpp`, `screens/watchlist/WatchlistScreen.cpp` | 시세 표시·숫자 정렬에 환산 적용, 원본 캐시에서 다시 그리기(#142) |
| `fincept-qt/src/screens/dashboard/DashboardScreen.cpp`, `MarketPulsePanel.cpp`, `TickerBar.cpp`, `widgets/{BaseWidget,BrokerHoldingsWidget,CryptoTickerWidget,DashboardCandleWidget,MarketQuoteStripWidget,PortfolioSummaryWidget,QuickTradeWidget,QuoteTableWidget,RiskMetricsWidget,ScreenerWidget,StockQuoteWidget,TopMoversWidget,TradeTapeWidget,WatchlistWidget}.{h,cpp}` | 시세·금액 환산과 원 주문 단위 표시, 환율/메타데이터 변경 때 다시 그리기(#142) |
| `fincept-qt/src/screens/portfolio/{PortfolioBlotter,PortfolioScreen}.{h,cpp}`, `PortfolioScreen_Handlers.cpp`, `PortfolioScreen_Layout.cpp`, `PortfolioStatusBar.{h,cpp}`, 새 `PortfolioPresentation.h` | 출처가 확인된 현재 값의 화면용 사본, 혼합 단위와 과거 기록의 원 단위 안내(#142) |
| `fincept-qt/src/screens/equity_research/{EquityAnalysisTab,EquityFinancialsTab,EquityOverviewTab,EquityResearchScreen,EquityValuationTab}.{h,cpp}`, 새 `EquityValuationUnits.h`, `services/equity/EquityResearchModels.h`, `EquityResearchService.cpp` | 가격·보고 통화 구별, 원 차트 안내, 확인된 금액 환산, 서로 다른 통화의 가치평가 비교 차단(#142) |
| `fincept-qt/src/screens/equity_trading/EquityChartPanel.{h,cpp}`, `EquityTradingScreen_Streams.cpp` | 출처 통화가 없는 포지션은 원 단위로 표시하고 계좌 통화로 추정하지 않기(#142) |
| `fincept-qt/src/screens/profile/ProfileScreen.{h,cpp}`, `screens/auth/PricingScreen.cpp` | USD 청구 금액과 환산 표시를 구별(#142) |
| `fincept-qt/tests/tst_fx_rates.cpp`, `tst_portfolio_presentation.cpp`, `tst_equity_valuation_units.cpp`, `test_market_quote_metadata.py` | 표시 환산과 원 단위 보존 회귀 검사(#142) |
| `fincept-qt/src/screens/dashboard/canvas/WidgetRegistry.cpp` | `KrxDashboard.h` include와 생성자 끝의 `krx_dashboard::register_widgets(*this)` 한 줄: KRX 위젯 4개를 등록한다(codeyoma/krx-quant#302) |
| `fincept-qt/src/screens/dashboard/canvas/AddWidgetDialog.cpp` | `accent_for_category`에 `KRX` 카테고리의 색(`ui::colors::INFO`) 한 갈래(codeyoma/krx-quant#302) |
| `fincept-qt/src/screens/dashboard/canvas/DashboardTemplates.cpp` | `KrxDashboard.h` include와 템플릿 목록 끝의 `krx_dashboard::dashboard_template()` 한 줄: `KRX` 템플릿(codeyoma/krx-quant#302) |
| `fincept-qt/src/screens/dashboard/canvas/TemplatePicker.cpp` | 고정 크기 `setFixedSize(640, 400)` → 가로만 640 고정, 세로는 최소 400에서 줄 수만큼 커진다(`setFixedWidth`, `setMinimumHeight`). `KRX` 템플릿이 셋째 줄을 만든다(codeyoma/krx-quant#302) |
| `fincept-qt/src/screens/settings/SettingsScreen.cpp` | 동적으로 번역하는 설정 메뉴·범위 제목 18개를 `QT_TRANSLATE_NOOP`으로 표시한다. `update_translations`를 돌려도 기존 번역이 지워지지 않게 한다(codeyoma/krx-quant#132) |
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
| `fincept-qt/src/services/economics/MacroCalendarService.cpp` | KRX 엔진 주소(`krx_quant/base_url`)가 있으면 Economic Calendar를 Fincept 서버 대신 엔진의 `/v1/calendar`에서 읽고, `krx_quant::calendar_widget_events`로 위젯의 형식으로 바꾼다(codeyoma/krx-quant#150) |
| `fincept-qt/src/python/PythonSetupManager.cpp` | 분석 핵심 설치(`uv pip install -r requirements-numpy2-core.txt -c <lock>`)를 venv 폴더에서 돌리고 lock은 파일 이름(`-c requirements.lock`)으로만 넘긴다. 이를 위해 단계 옵션에 작업 폴더(`StepOptions::working_dir`)를 더하고, `Running:` 로그에 그 폴더를 적는다. uv는 `-c` 값을 공백에서 나누어(`UV_CONSTRAINT` 목록) 절대 경로가 두 파일로 쪼개졌다. uv 환경의 `UV_NO_CONFIG=1` 때문에 작업 폴더가 바뀌어도 설정 파일을 읽지 않는다(codeyoma/krx-quant#247) |
| `.github/workflows/build-cpp.yml` | 수동 전체 플랫폼 빌드를 다시 통과시킨다(codeyoma/krx-quant#136): Windows ARM64는 qtwebengine 없는 Qt 모듈 목록(Qt 6.8.3에 그 대상의 qtwebengine이 없다)과 `arm64-windows` vcpkg triplet, ARM64 패키지는 ARM64 키트를 읽는 `windeployqt --qtpaths`와 Visual Studio ARM64 redist의 런타임(전에는 x64 runner의 System32에서 x64 DLL을 넣었다), Windows 패키지 둘 다 vcpkg의 OpenSSL DLL(Qt에 없어서 전에는 빠졌다)과 실행 파일·DLL의 아키텍처 검사, macOS x64(Intel)와 macOS Universal 작업은 뺐다(아래 삭제됨), 패키징의 `sync_scripts.sh`는 저장소 루트 경로(`${GITHUB_WORKSPACE}`), Linux 기본 아이콘을 만들 때 Pillow 설치, macOS 앱에는 실행에 쓰는 Python 버전 하나만 넣는다(runner의 `Python.framework`에는 여러 버전이 있고, 서명은 `Current`만 되어 검증이 실패했다). 개인 배포 `custom-release.yml`은 그대로다 |

> **바뀜** (2026-10-22, codeyoma/FinceptTerminal#103, codeyoma/krx-quant#305): 위 표의 화면 등록 네 파일(`WindowFrame_Setup.cpp`, `DockScreenRouter.cpp`, `ToolBar.cpp`, `CommandBar.cpp`)에 등록한 KRX 화면 넷 → 다섯(`krx_closing`, "KRX 마감 흐름"), `tests/CMakeLists.txt` 줄의 KRX 테스트에 `tst_krx_closing`. 이유: 새 화면 KRX 마감 흐름(아래 "KRX 마감 흐름").

> **바뀜** (2026-10-30, codeyoma/FinceptTerminal#108, codeyoma/krx-quant#397): 위 표의 화면 등록 네 파일에 등록한 KRX 화면 다섯 → 여섯(`krx_trader`, "KRX 자동 매매"), `tests/CMakeLists.txt` 줄의 KRX 테스트에 `tst_krx_trader`, `tst_krx_trader_screen`. 이유: 새 화면 KRX 자동 매매(아래 "KRX 자동 매매").

> **삭제됨** (2026-10-13, codeyoma/krx-quant#136): `build-cpp.yml`의 macOS x64(Intel) 대상과 그 결과를 arm64와 합치던 macOS Universal 작업, 릴리스 목록의 두 파일. 이유: Intel Mac은 지원하지 않기로 했다(사용자 결정). 교차 컴파일한 x64 앱은 ARM64 Homebrew OpenSSL을 링크해 실패했고, Intel runner로 고치면 빌드가 가장 오래 걸렸다. 대신: macOS는 Apple Silicon(arm64)만 빌드한다.

> **바뀜** (2026-10-12, krx-quant #94): KRX 테스트 등록을 `fincept-qt/tests/CMakeLists.txt` 끝에 붙였다 → `krx_quant.cmake`로 옮기고 `fincept_add_test()` 정의 뒤에서 include 한다. 이유: upstream도 파일 끝에 테스트를 붙여서, v4.5.0 이후 upstream `main`을 merge 하면 충돌했다. 자동 반영이 멈추지 않게 하려고(krx-quant #93).

## 한국어 언어 팩 (codeyoma/krx-quant#132)

- **설정 경로:** Settings → General → Interface language → **한국어**. 한국어로 바꾼 뒤에는 설정 → 일반 → 인터페이스 언어다. 선택 즉시 적용되고, 같은 곳에서 English로 되돌릴 수 있다.
- **저장:** 기존 `LanguageManager`가 SQLite settings의 `general.language`에 `ko_KR`를 저장한다. 다음 실행에도 유지된다. 저장된 언어가 없는 첫 실행은 기존 운영체제 언어 감지를 따르므로 한국어 환경에서는 한국어를 고른다. 이미 고른 다른 언어를 덮어쓰지 않는다.
- **팩:** `fincept-qt/translations/fincept_ko_KR.ts`. 현재 내부 공통 팩의 382개 컨텍스트, 9,608개 항목을 기준으로 번역하고, 최신 설정·탐색의 누락 문구와 동적으로 읽는 설정 메뉴도 더했다. 이전 내부 한국어 팩의 맞는 표현을 참고했다. 제품명·티커·코드·수식·치환자는 유지한다. 원문에 새 문구가 생겨 팩에 아직 없거나 `tr()`로 연결되지 않은 문구는 기존 영어 fallback을 따른다.
  - 2026-10-16: v4.5.1 원문의 `tr()` 문구 중 공통 팩에도 한국어 팩에도 없던 5,223개(337개 컨텍스트, 그중 90개는 새 컨텍스트)를 더했다. 공통 팩이 namespace 없는 `ScreenerSubTab` 컨텍스트에 둔 7개도 실행 중의 컨텍스트로 옮겨 더했다(아래 `CONTEXT_FIXES`). 팩은 475개 컨텍스트, 14,965개 항목이다. upstream은 v4.5.x에서 화면 문구 대부분을 `tr()`로 감쌌지만 공통 팩(2026-06-03 이후 그대로)은 갱신하지 않았다. 그래서 이 문구들은 다른 언어에서는 아직 영어다. 원문이 한국어인 KRX 화면의 `tr()` 275개는 더하지 않는다. 번역이 없으면 원문(한국어)이 그대로 나온다.
  - 원문에서 사라진 문구(약 900개)도 팩에 남긴다. 공통 팩에는 아직 있어서, 지우면 아래 검증이 실패한다. 그래서 한국어 팩은 `update_translations`(`-no-obsolete`)로 다시 쓰지 않고, 아래 "새 문구 추가"로 더한다.
- **새 문구 추가:** upstream 릴리스가 새 `tr()` 문구를 들여오면 `custom/tools/korean-pack/korean_pack.py`로 더한다(Qt `lupdate` 필요, 없으면 `QT_BIN=<Qt>/bin`).
  1. `python3 custom/tools/korean-pack/korean_pack.py extract <new.json>`: 원문을 스캔해 한국어 팩에 없는 문구를 id·컨텍스트·원문·쓰인 위치와 함께 JSON으로 낸다. 같은 영어의 기존 번역이 있으면 `existing_translations_elsewhere`로 붙인다.
  2. 위치를 보며 [`korean-glossary.md`](korean-glossary.md) 기준으로 번역해 `{"id": "번역"}` JSON을 만든다(복수형은 형태 하나의 목록).
  3. `korean_pack.py check <new.json> <done.json>`으로 치환자·줄 수·HTML 태그·앞뒤 공백·단축키 `&`를 검사하고, `korean_pack.py merge <new.json> <done.json>`으로 팩에 더한다. 기존 항목은 바꾸지 않고, 새 항목은 컨텍스트 끝에, 새 컨텍스트는 파일 끝에 붙인다.
  - `extract`는 `lupdate`가 끝나지 않는 `src/services/news/RssFeedProbeService.cpp`(Qt 6.8의 C++ 파서 문제, `tr()` 없음)를 빼고 스캔한다. 같은 이유로 `update_translations` target은 지금 끝나지 않는다.
  - 익명 namespace 안의 `PythonSetupManager::tr()` 같은 호출은 `lupdate`가 namespace 없는 컨텍스트(`PythonSetupManager`)로 적는다. 실행 중의 `tr()`은 클래스의 전체 이름(`fincept::python::PythonSetupManager`)을 쓰므로, `extract`가 이런 컨텍스트 3개를 전체 이름으로 바꾼다(`CONTEXT_FIXES`). 공통 팩도 같은 이유로 `ScreenerSubTab` 문구 7개를 namespace 없이 담고 있어 실행 중에는 쓰이지 않는다. 그래서 `extract`는 이 3개 컨텍스트에서 이미 번역된 항목도 전체 이름의 항목이 없으면 낸다(기존 번역은 `existing_translations_elsewhere`에 있다). `lupdate`가 `Qualifying with unknown namespace/class`를 경고하는 새 파일이 생기면 여기에 더한다.
- **용어·문체:** 국내 증권사 HTS/MTS와 KRX·DART 표기를 따른다(PER·PBR, 평가손익, 평균단가, 매입금액, 평가금액, 계좌/계정 구별, 증권사, 모의투자/실전투자, 가상자산, 샤프지수, 최대낙폭(MDD) 등). 문장은 합니다체, 안내는 `~하세요`. 새 문구를 번역하거나 기존 번역을 고칠 때는 [`korean-glossary.md`](korean-glossary.md)를 따른다.
  > **바뀜** (2026-10-13, codeyoma/FinceptTerminal#31): Codex 초벌 번역의 용어·문체 → 업계 표기 기준으로 약 1,200개 항목 정리. 이유: 일반 번역어(P/E 비율, 미실현 손익, 취득 원가, 브로커, 모의 거래, 암호화폐 등)와 번역투가 국내 금융 화면과 달랐다.
  > **바뀜** (2026-10-13, codeyoma/FinceptTerminal#31): 환산하지 않은 시세 표시 `(원 단위)`·`(원 통화)` → `(거래 통화)`, 금액은 `원래 통화`. 이유: 원화(KRW) 표시 통화가 생긴 뒤 `원`이 원화로 읽혔다.
  > **바뀜** (2026-10-16, codeyoma/FinceptTerminal#67): 메뉴와 도킹 창 제목 `Trade Viz`의 `거래 시각화` → `무역 시각화`. 이유: 이 화면은 UN Comtrade의 국가 간 무역 흐름을 보여 준다. `거래`는 매매로 읽혔고, 새로 번역한 화면 안의 문구도 `무역`을 쓴다.
- **빌드:** 다른 언어와 동일하게 `lrelease`가 `.qm`을 만들고 `:/i18n/fincept_ko_KR.qm`에 포함한다. 외부 번역 API, 다운로드, 추가 설정은 필요 없다. 기존 팩은 자동으로 다시 쓰지 않는다.
- **갱신:** 설정 메뉴는 문자열을 저장했다가 동적으로 `tr()`에 넘긴다. 이 메뉴와 범위 제목 18개에는 실제 런타임 문맥의 `QT_TRANSLATE_NOOP` 표시를 넣어, 수동 `update_translations`의 `-no-obsolete` 갱신에서도 한국어 번역이 남게 한다. 새 동적 메뉴를 더할 때도 같은 표시가 필요하다.
- **검증:** `FINCEPT_BUILD_TESTS=ON`으로 설정한 빌드에서 `cmake --build <build> --target tst_krx_korean` 뒤 `ctest --test-dir <build> -R krx_korean --output-on-failure`. 앱과 같은 경로의 실제 컴파일된 리소스, 설정 메뉴 문맥, 공통 팩 전체 키·Qt 치환자, 한국어 팩 자신의 전체 키·Qt 치환자(공통 팩에 없는 새 문구), 클래스 전체 이름 컨텍스트(`fincept::python::PythonSetupManager`, `fincept::screens::fno::ScreenerSubTab`), 복수형, 영어 fallback, 용어 기준의 대표 금융 용어(PER, 평가손익, 평균단가, 계좌, 모의투자 등)를 검사한다.

## 원화 표시 통화 (codeyoma/krx-quant#138, #142)

- **설정 경로:** Settings → General → Display currency(설정 → 일반 → 표시 통화). 언어 바로 아래에서 **₩ South Korean Won (KRW)**을 고른다. 기본값 USD를 포함해 목록의 모든 표시 통화에 같은 환산 규칙을 적용한다.
- **저장:** 기존 SQLite settings의 `general.currency`에 선택을 저장하여 재시작 때 복원한다. `general.fx_rates_v1`에는 `version=1`, `source=ECB`, `date=YYYY-MM-DD`, EUR당 통화별 값인 `rates`를 JSON으로 저장한다. DB 스키마는 바꾸지 않는다.
- **환율:** [ECB 일일 참고환율](https://www.ecb.europa.eu/stats/policy_and_exchange_rates/euro_reference_exchange_rates/html/index.en.html)의 [XML](https://www.ecb.europa.eu/stats/eurofxref/eurofxref-daily.xml)을 앱 시작과 6시간마다 읽는다. 설정에서 수동 새로고침도 할 수 있다. 원 금액 × 대상 통화의 EUR당 값 ÷ 원 통화의 EUR당 값으로 환산한다. 설정에 기준일·캐시 여부·조회 상태와 `100 USD` 환산 예시를 보인다. 주문 체결이나 결제 환율이 아닌 표시용 참고환율이다.
- **실패:** 마지막 검증 자료를 쓰되 기준일로부터 7일을 넘으면 환산하지 않는다. 1분마다 만료를 확인한다. 미래 날짜·0/음수/무한 환율·중복 통화·손상 응답을 거부하고, 실패한 조회는 유효한 캐시를 덮어쓰지 않는다. 출처 통화가 없거나 환율이 없으면 **원래 숫자와 통화/원 단위 표시**를 유지한다. 1:1 환율을 만들어 쓰지 않는다. 동일 통화 표시는 환율 없이 가능하다.
- **통화 구별:** `GBp`/`GBX`는 GBP의 1/100이다. USDT/USDC는 USD로 취급하지 않는다. KRW/JPY는 기본 소수 자릿수 0이며 아주 작은 금액은 0으로 사라지지 않도록 자릿수를 늘린다. 화면이 가격 정밀도를 명시한 경우에는 그 자릿수를 따른다. 기호가 같은 USD/CAD/AUD도 통화 코드로 구별한다.
- **시세 메타데이터:** `QuoteData`와 시세 캐시 JSON은 `currency`, `quote_type`을 원 값과 함께 보관한다. 이름이 이미 있어도 통화/유형이 없으면 조회한다. 여러 화면의 요청은 250ms 동안 모아 `quote_names` 한 번으로 묻고(upstream v4.5.1, Python 쪽은 8개씩 동시에 묻는다), 종목마다 한 세션에 한 번만 묻는다. 호출 자체가 실패하면 1분 뒤 다시 묻는다. SQLite settings의 `market_symbol_names`, `market_symbol_currencies`에 더해 `market_symbol_quote_types`를 쓴다. 회사 정보는 시세 통화와 재무제표 `financial_currency`를 구별한다. 지수·수익률·환율쌍·거래량·수량·퍼센트는 금액처럼 환산하지 않는다.

> **바뀜** (2026-10-15, upstream v4.5.1 merge, codeyoma/FinceptTerminal#36): 정보가 빠진 종목을 요청마다 따로 묻고, 1분 안에는 같은 종목을 다시 묻지 않았다 → upstream v4.5.1의 묶음 조회(250ms 모아 한 번, 종목마다 한 세션에 한 번)를 따르고, 호출이 실패했을 때만 1분 뒤 다시 묻는다. 이유: upstream이 같은 함수를 묶음 조회로 바꿔 merge 충돌이 났고, 화면마다 Python 호출을 띄우던 문제를 upstream 방식이 함께 고친다. 빠진 것을 판단하는 기준(이름·유형·통화)은 fork 규칙 그대로다.

| 화면 | 환산 범위와 원 단위 유지 범위 |
|---|---|
| 시장·관심종목·대시보드 | 확인된 통화의 가격·절대 등락·고가/저가·금액. 대시보드 캔들 가격축도 같은 환율로 표시한다. FX/지수/수익률은 원 단위다. 선물은 USD 계약이어도 지수·채권 포인트일 수 있어 원 단위를 유지한다. 암호화폐는 페어의 실제 호가 자산을 쓴다 |
| 포트폴리오 | 현재 보유종목의 통화가 포트폴리오 취득원가 통화와 일치한다고 확인되면 카드·합계·보유표·히트맵·섹터 금액을 환산한다. 혼합/미확인 합계·히트맵·섹터 금액은 숨기고 환산 불가를 표시한다. 대시보드 포트폴리오 합계는 불완전하면 비운다 |
| 포트폴리오 과거 기록·상세 분석 | 과거 스냅샷에는 통화 증거가 없어 원래 차트를 유지하고 안내한다. 거래 내역·주문·계획 입력·상세 분석은 원 단위이며 현재 환율로 과거 수익률을 재계산하지 않는다 |
| 주식 리서치 | 출처를 확인한 가격·목표가·가치평가 표시·현금흐름을 환산한다. 보고 통화가 없거나 가격 통화와 다르면 서로 다른 단위를 비교하는 가치평가 판단을 내리지 않는다. GBp 시가총액의 별도 통화를 확인할 수 없으면 환산하지 않는다. OHLC 차트와 재무제표 표/차트에는 원 통화를 명시한다 |
| 주문·브로커 | 주문 가격·수량·저장 잔고는 원 값이다. Quick Trade는 원 주문 단위와 환산 예시를 함께 표시한다. 종목 통화가 없는 브로커 데이터는 계좌 통화로 추정하지 않는다 |
| 결제 | USD 가격과 결제 기록의 환산 표시를 제공하고 실제 USD 청구액을 별도로 명시한다. 결제 요청은 바꾸지 않는다 |
| 계산기·기타 분석 | 출처 통화 계약이 없는 수동 계산기는 기존 선택 통화 단위를 사용한다. 크기가 큰 숫자라고 금액으로 추정해 환산하지 않는다. 기존 CSV 내보내기는 원 값이다 |

환율/표시 통화/시세 통화 정보가 바뀌면 원본에서 화면을 다시 그린다. 환산된 값을 원본·주문·저장소로 돌려쓰지 않으며 새 환율이 도착했다고 가격을 재구독하거나 분석을 다시 실행하지 않는다.

> **바뀜** (2026-10-15, upstream v4.5.1 merge, codeyoma/FinceptTerminal#36): 주식 리서치 가치평가 탭의 DCF·Altman·Piotroski·Beneish는 QuantLib 호출에서 앱 안 계산으로 바뀌었다(upstream). upstream이 넣은 통화 불일치 검사는 대소문자를 무시하고 보고 통화가 없으면 시세 통화로 보았는데, fork 규칙(대소문자 구별: GBp는 GBP의 1/100, 보고 통화가 없으면 비교하지 않음, `EquityValuationUnits.h`)으로 덮었다. 금액은 표시 통화로 보이고, 환율이 바뀌면 계산을 다시 하지 않고 마지막 결과를 다시 그린다. 이유: upstream이 탭을 새로 썼다. 대시보드는 upstream의 `DashboardFormat.h`(통화 기호 없는 숫자)를 쓰되, 출처 통화가 확인된 금액은 그대로 `display_price`로 환산한다.

> **바뀜** (2026-10-13, codeyoma/FinceptTerminal#26): KRW 옵션은 계산기·분석의 기호만 바꿨다 → 출처 통화가 확인되는 위의 금액 표시에 날짜가 명시된 일일 참고환율을 적용한다. 이유: 기호 교체가 아니라 실제 원화 금액 표시를 요청했다. 원본 시세·주문·저장 값은 유지한다.

검증: `ctest --test-dir <build> -R 'fx_rates|portfolio_presentation|equity_valuation_units|krx_korean' --output-on-failure`, `python3 fincept-qt/tests/test_market_quote_metadata.py`. 환율 모델은 Qt Core만으로 XML·캐시·교차환산·만료·미지원 통화를 검사한다. 포트폴리오는 원본 불변·혼합/미확인/비금액 단위·overflow를, 가치평가는 가격과 보고 통화의 비교 경계를 검사한다.

화면 이벤트 순서 회귀 검사(일반 CTest의 Core-only 규칙과 분리):

```bash
cmake -S custom/tools/fx-ui-check -B fincept-qt/build/fx-ui-check -G Ninja -DCMAKE_PREFIX_PATH=<Qt 6.8.3>
cmake --build fincept-qt/build/fx-ui-check --parallel 1
QT_QPA_PLATFORM=offscreen fincept-qt/build/fx-ui-check/base_widget_check
```

실제 `BaseWidget`/`LoadingOverlay`/테마 코드를 사용하고 시세 service의 생성과 신호만 대역으로 제공한다. 조회나 사용자 DB 없이 시세·통화 변경의 두 도착 순서, 기본 hook, 생성/소멸, 시세 합치기를 검사한다. 통화 갱신이 대기 중인 시세 그리기를 지우던 구현에서는 실패한다.

## KRX 예측 화면 (#20, #21, #68)

- 코드: `fincept-qt/src/screens/krx_quant/`(화면, 차트, 차트 위 툴바 `KrxChartToolbar`, 보조지표 패널 `KrxStudyPanel`, 그린 것의 팝업 `KrxDrawingPopup`, 종목 열의 로고·이름 `KrxSymbolCell`, 순위표, 상세 통계 창, 봉 창 `KrxBarWindow`, 알림 설정), `fincept-qt/src/services/krx_quant/`(엔진 API와 스트림 읽기, 응답 해석, 화면 상태, 신호 판정, 통계 표시 문구, 방향 말고 다른 예측 종류의 선택·패널·호버와 하루 한 번 예측의 패널·일 보기 아이콘 `KindView`, 차트 모양·날짜 구분선·종목별 차트 설정 `ChartLook`, 보조지표의 값·설정·범례·더 읽을 봉 수와 하단 칸 높이, 일별 수급 지표를 봉에 놓기 `ChartStudies`, 그리기 도구·그린 것의 저장 형식·시각과 봉 위치·자석 `ChartDrawings`, 시간축 없는 차트의 벽돌·크기·시작 `ChartBricks`, 분 보기의 빈 봉과 특수 상황 띠·호버 줄 `ChartSlots`, 봉 창의 줄과 4분 미니 차트 `BarView`).
- 화면은 그리기와 입력만 맡는다. 엔진을 따라가는 상태(답과 스트림을 합친 `Board`, 신호 판정, 스트림, 읽기 주기, 엔진 시계)는 화면이 가진 `KrxLive`가 맡는다. upstream의 화면 규약(`docs/ARCHITECTURE.md`: 화면은 캐시와 업무 로직을 갖지 않는다)을 따른 것이다. 새 싱글턴은 만들지 않는다.
- Navigate → Trading & Portfolio → **KRX 4분 예측**(또는 명령 팔레트에서 "krx")으로 연다.
- 엔진 주소는 화면 위의 **엔진 주소** 칸에 넣는다(처음에는 비어 있다). 값은 QSettings `krx_quant/base_url`에 남는다. 실제 주소는 krx-quant 저장소의 문서에 있다.

### 구성

- **모양** (codeyoma/krx-quant#103): Fincept의 다른 화면과 같은 부품이다.
  - 패널마다 Portfolio 화면식 머리줄(얇은 바, 주황 표시, 굵은 제목, 오른쪽 조작)이 있다. upstream의 `PortfolioPanelHeader.h`는 고치지 않고, 같은 모양의 helper를 `screens/krx_quant/KrxPanel.h`에 둔다.
  - 표는 Fincept 공용 표(`ui::DataTable`)다. 상세 통계 창의 표도 같다.
  - 글꼴과 밀도는 Fincept 설정(전역 스타일)을 따른다. 표의 칸 여백은 밀도대로, 줄 높이는 글꼴과 밀도에 맞춘다(DataTable의 고정 26px 대신). 칸은 한 줄이고, 넘치는 글은 줄임표로 줄인다(codeyoma/krx-quant#105에서 좁은 창의 줄바꿈을 막았다). 머리줄 제목은 앱 글꼴로, 크기는 설정 크기보다 3px 작다. 테마가 바뀌면(`ThemeManager::theme_changed`) 머리줄, 표, 셀 색을 다시 칠한다.
  - 엔진을 처음 읽는 동안은 Fincept 읽는 중 표시(`ui::LoadingOverlay`)가 본문을 덮는다. 엔진 주소를 바꿔 적용해도 다시 덮는다.
  - 화면 머리줄의 제목 옆 점이 엔진 연결이다. 초록은 정상, 빨강은 엔진이나 일부(종목 예측, 합의 구성원)를 읽지 못함, 회색은 처음 읽는 중이다. 툴팁에 이유, 마지막 갱신 시각, 엔진 시계가 있다.
  - 순위 규칙(날짜, 순위에 오르는 조건, 순서, 운 보정의 뜻, 두 번 누르면 상세 창)은 순위표 제목의 툴팁에, 알림 규칙은 신호 알림 제목의 툴팁에 있다. 순위표를 읽지 못하면 머리줄에 빨간 "읽지 못함"이 나오고 이유는 그 툴팁에 있다.

  > **삭제됨** (2026-10-13, codeyoma/krx-quant#126): 화면 맨 아래 회색 상태 줄(갱신 시각, 엔진 시계, 읽기 오류). 이유: 화면을 Fincept의 다른 화면 모양으로 맞추고 회색 설명을 없앴다(codeyoma/krx-quant#100, #103). 대신: 머리줄의 엔진 점과 그 툴팁.

  > **삭제됨** (2026-10-13, codeyoma/krx-quant#126): 순위표 아래 회색 설명(날짜, 순위 규칙, 운 보정의 뜻, 두 번 누르기, 읽기 실패), 알림 설정의 회색 규칙 설명, 신호가 오기 전의 "아직 신호가 없습니다". 이유: 위와 같다. 대신: 순위표·신호 알림 제목의 툴팁과 순위표 머리줄의 "읽지 못함". 마지막 신호 줄은 첫 신호가 온 뒤에 보인다.

  > **바뀜** (2026-10-13, codeyoma/krx-quant#126): 테두리 상자(순위표, 신호 알림), 화면 자체의 표·입력 스타일, 굵은 제목 줄 → Fincept의 패널 머리줄, 공용 표, 전역 글꼴·테마와 읽는 중 표시. 기간·채점 기준·종목·특수 상황과 차트 범위는 각 패널 머리줄의 오른쪽으로 옮겼다. 이유: Fincept의 다른 요소와 같은 디자인과 레이아웃 코드를 쓰려고(codeyoma/krx-quant#100).
- **예측 종류** (codeyoma/krx-quant#182): 화면 머리줄의 제목과 엔진 점 뒤에 "예측 종류 **방향 장벽 변동폭 시가→종가 5거래일 시가→종가**" 버튼이 있다(엔진이 장벽의 지평 사다리를 예측하면 장벽 뒤에 "장벽 15분 장벽 30분 장벽 60분"도, 아래). 차트의 보기 버튼과 같은 모양(고른 것은 주황)이고 한 번에 하나를 고른다.

  > **바뀜** (2026-10-24, codeyoma/FinceptTerminal#104): 버튼은 "방향 장벽 변동폭 시가→종가"였다 → 뒤에 "5거래일 시가→종가"가 붙는다(아래). 이유: 엔진이 `open_to_close_5d`를 예측하고 API로 준다(codeyoma/krx-quant#310).

  > **바뀜** (2026-10-18, codeyoma/FinceptTerminal#92): 버튼은 "방향 장벽 변동폭"이었다 → 뒤에 하루 한 번 예측 "시가→종가"가 붙는다(아래). 이유: 엔진이 `open_to_close`를 예측하고 API로 준다(codeyoma/krx-quant#264, #293).
  - 목록은 엔진의 `GET /v1/kinds`(krx-quant #179)로 읽는다. 엔진이 예측하지 않는 종류(그 응답이 없는 옛 엔진, 장벽을 예측하기 전(krx-quant #183)의 엔진, 시가→종가를 주기 전(krx-quant #293)의 엔진, 5거래일 시가→종가를 주기 전(krx-quant #310)의 엔진)는 흐리게 두어 고를 수 없다. 방향은 늘 고를 수 있다. 이 화면이 모르는 종류는 엔진이 예측해도 버튼이 없다. 버튼 이름은 엔진의 이름(`title`)이고, 툴팁에 그 종류의 클래스와 무엇이 바뀌는지, 흐리면 그 이유가 있다.

  > **바뀜** (2026-10-15, codeyoma/krx-quant#185): 엔진이 장벽을 예측해도 이 화면이 아직 보이지 못해 장벽 버튼은 늘 흐렸다 → 엔진이 장벽을 예측하면 고를 수 있다(아래 장벽 패널·순위표·상세 통계 창·호버). 이유: 장벽의 화면을 만들었다(codeyoma/krx-quant#178).
  - 장벽의 지평 사다리(codeyoma/krx-quant#263): 엔진이 `barrier_15m`, `barrier_30m`, `barrier_60m`(장벽 15·30·60분)을 예측하면 장벽 버튼 바로 뒤에 엔진의 순서로 그 버튼이 붙는다. 예측하지 않는 엔진에서는 흐린 버튼도 없다. 각각은 장벽처럼 보인다(장벽 패널, 순위표의 장벽 손익 열, 상세 통계 창, 호버의 U·L). 툴팁에 "봉 마감부터 H분 동안의 장벽이다."가 붙고, 패널 열의 툴팁과 호버의 목표 시각이 그 분(엔진의 `horizon_minutes`, 없으면 4)을 따른다. 장벽인지는 `EngineData`의 `is_barrier_kind`(`barrier`, 또는 `barrier_`로 시작하고 `m`으로 끝나는 이름)가 정한다. 봉 창(codeyoma/krx-quant#255)은 그대로 4분 장벽과 변동폭만 보인다.
  - 하루 한 번 예측 **시가→종가**(codeyoma/krx-quant#264): 변동폭 뒤의 버튼이다. 이름은 엔진의 `title`(`시가→종가`)이고, 엔진이 주지 않으면 흐리다(툴팁 "엔진이 이 종류를 예측하지 않는다"). 엔진의 `/v1/kinds`에서 `daily` 객체가 있고 `horizon_minutes`가 `null`인 종류다(`EngineData`의 `DailySchedule`, `is_daily`. 화면은 `horizon_minutes`를 0으로 둔다). 거래일 08:50(`daily.predict_at`)에 그날 KRX 시가 단일가(09:00)부터 종가 단일가(15:30)까지의 방향을 상승·보합·하락으로 예측하고, 그날 15:30 종가 단일가가 저장된 뒤(`daily.answer_from`, 15:45부터) 채점한다. 툴팁은 "상승·보합·하락의 확률", 예측 시각과 창, 정답 시각, 그리고 "예측 패널, 순위표, 상세 통계 창이 이 종류로 바뀌고, 일 보기 차트에 날마다의 예측과 결과가 붙는다. 분 보기의 아이콘·호버·선택 합의와 신호 알림은 방향 그대로다."이다. 이 화면이 아는 하루 한 번 종류는 이것과 아래 5거래일 시가→종가 둘이다(`gap`, `last30` 같은 다른 종류는 엔진이 주어도 버튼이 없다). 그 기준 예측은 "기준: 시가→종가 늘 보합"(`otc-flat`), "기준: 시가→종가 기후"(`otc-climate`), "기준: 어제 방향 잇기"(`otc-persist`)로 보인다. 첫 알고리즘은 엔진의 `cox`다.

    > **바뀜** (2026-10-20, codeyoma/krx-quant#317): 정답은 그날 KRX 일봉이 저장된 뒤 20:35부터였다 → 15:30 종가 단일가가 저장된 뒤 15:45부터다. 이유: 2026-09-14 KRX 애프터마켓 도입 뒤 KRX 일봉의 종가가 15:30 종가 단일가와 달랐다(#264의 `daily-check-264`). 화면은 엔진의 `daily.answer_from`을 그대로 보이므로 코드는 바뀌지 않았다.

    > **바뀜** (2026-10-24, codeyoma/FinceptTerminal#104): "이 화면이 아는 하루 한 번 종류는 이것 하나다" → 5거래일 시가→종가도 안다. 이유: 아래.
  - 하루 한 번 예측 **5거래일 시가→종가**(codeyoma/krx-quant#310): 시가→종가 뒤의 버튼이다. 이름은 엔진의 `title`이고, 엔진이 주지 않으면(krx-quant #310 전) 흐리다. 엔진의 `open_to_close_5d`, `/v1/kinds`의 `daily.sessions`가 5인 종류다(`DailySchedule`의 `sessions`, `window_sessions`. 그 필드가 없는 옛 엔진의 하루 한 번 종류는 1). 거래일 08:50에 그날 KRX 시가 단일가(09:00)부터 그날을 첫날로 센 다섯째 거래일의 종가 단일가(15:30)까지의 방향을 상승·보합·하락으로 예측한다. 정답은 창의 마지막 세션 밤, 그날 KRX 일봉이 저장된 뒤(`daily.answer_from`, 20:35부터)다. 그래서 예측은 최대 4거래일 채점을 기다리고, 이웃한 날의 창이 4세션씩 겹친다. 툴팁은 "상승·보합·하락의 확률", 예측 시각과 창(5세션), 정답 시각과 "최대 4거래일 채점을 기다린다. 이웃한 날의 창이 겹친다.", 그리고 시가→종가와 같은 무엇이 바뀌는지다. 기준 예측은 "기준: 5거래일 늘 보합"(`otc5-flat`), "기준: 5거래일 기후"(`otc5-climate`), "기준: 지난 5거래일 방향 잇기"(`otc5-persist`)로 보인다. 첫 알고리즘 `cox-5d`는 사전 검증에서 거부되어(2026-10-23) 엔진이 돌리지 않고, 사용자가 거부를 그대로 두었다(2026-10-24). 그래서 지금은 기준 예측 셋만 예측하고 채점한다. 화면은 이것을 엔진이 보이는 그대로 보인다: `GET /v1/algorithms`에 이 종류의 알고리즘이 없으면 패널 제목에 "기준 예측만"이 붙는다(아래 패널). 엔진은 거부된 알고리즘을 목록에서 뺄 뿐 그 이유를 API로 주지 않아, 화면도 이유를 적지 않는다. 엔진이 `daily.awaiting`(돌지 않는 이유)이나 `daily.review_hold`(수습 보류 이유)를 주면 그 글을 그대로 보인다(지금은 둘 다 `null`).
    - 화면은 시가→종가와 같은 부품(패널, 순위표, 상세 통계 창, 일 보기)을 쓰고, 창이 여러 세션인 데서 다음이 다르다: 창 끝(`end_day`)과 채점 대기, 다섯 세션의 종가, 배당락 표시, 겹침을 보정한 통계, 판정일(아래 각 절).
  - 고른 것은 앱 설정 `krx_quant/kind`(`direction`, `range`, `barrier`, `barrier_15m`, `open_to_close`, `open_to_close_5d` 등)에 남는다. 고른 종류를 엔진이 예측하지 않으면(옛 엔진으로 주소를 바꾼 경우) 방향을 보이되 설정은 그대로 두어, 그 종류를 예측하는 엔진으로 돌아오면 다시 그 종류다. 엔진 목록을 읽기 전과 읽지 못하는 동안도 방향이다.
  - 종류를 바꾸면 예측 패널, 순위표, 상세 통계 창(새로 여는 창)과 차트의 호버가 그 종류로 바뀐다. 종목 목록, 차트의 아이콘·선택 합의(codeyoma/krx-quant#159)·성적, 신호 알림은 늘 방향이다. 열려 있던 다른 종류의 상세 통계 창은 그 예측자의 종류로 남는다. 시가→종가(codeyoma/krx-quant#264)는 봉 마감이 없어 분 보기의 아이콘과 호버는 방향 그대로이고, 대신 일 보기에 그 종류의 날마다 아이콘·호버·성적이 붙는다(아래 **시가→종가 일 보기**).

  > **바뀜** (2026-10-18, codeyoma/FinceptTerminal#92): 차트의 아이콘과 성적은 어느 종류를 골라도 방향의 것이었다 → 시가→종가를 고른 동안 일 보기에는 그 종류의 아이콘·호버·성적이 붙는다. 분 보기는 그대로 방향이다. 이유: 하루 한 번 예측은 봉 마감이 없어 1분봉에 붙일 자리가 없고, 09:00 봉에 붙이면 4분 아이콘과 겹친다. 날마다의 예측과 결과는 일봉에서 보인다(codeyoma/krx-quant#264).
  - 방향을 고르면 화면은 이 버튼 줄 말고는 전과 같다. 무엇을 고를 수 있고 무엇을 보일지는 `KindView`의 `kind_choices`·`shown_kind`(순수 함수, `tst_krx_kind_view`)가 정한다.
- **위쪽 줄**: 엔진 상태(엔진이 도는 커밋과 시작 시각("엔진 0123abc", "10-13 06:02 시작". 커밋을 모르면 "엔진 커밋 모름", codeyoma/krx-quant#85), 장 단계, 마지막 봉 마감, 채점 대기, KIS 연결, 지난밤 학습, 스트림 연결), 매크로 줄(그날 국면과 이유, 달러선물. 지표 전체와 읽기 실패는 툴팁), 오늘의 특수 상황(VI·서킷브레이커·사이드카). 매크로가 오늘 것이 아니면 "(오늘 것이 아님)"을 붙인다.
  - 달러선물은 그날 첫 체결 뒤로는 엔진 상태의 장중 마지막 체결가, 전일 종가 대비 등락, 체결 시각이다(예: "달러선물 최근월물 1,395.2 (+0.50%, 10:29)"). 상태를 읽을 때마다(5초) 바뀐다. 매크로를 읽지 못했거나 아직 정한 국면이 없어도 보인다. 체결 전(장 전, 휴장일)에는 07:30 매크로 읽기의 값과 등락이다.
  - KIS는 "마지막 수신 N초 전"(가장 최근에 받은 피드)과 결측 피드 수(엔진이 끊김·멈춤으로 판정한 피드, 목록은 툴팁)로 보인다. 피드마다 조용한 시간은 지연이 아니다. 원래 드문 피드가 있고 장 밖에서는 모두 조용하다. 문제는 엔진이 결측으로 판정한다.
  - 지난밤 학습이 미적용이면 엔진이 준 이유를 한국어로 붙인다(08:00 교체 대기, 07:30 뒤에 확정, 모델 파일 없음, 적용 뒤 바뀜).
- **종목 목록**: 종목(로고, 회사 이름, 흐린 종목 코드), 기준가, 최종 합의(방향과 가장 큰 확률. 상승과 하락이 같은 정수 %이면 "상승=하락"). 고른 종목이 차트와 예측 패널에 나온다. 장 시작 전이나 휴장일에는 지난 거래일의 마지막 예측이 남아 있다. 그 예측은 흐리게(툴팁은 봉 마감과 "(지난 거래일)"), 패널 제목은 날짜와 "(지난 거래일)"로 보인다.
  - 종목 열(codeyoma/krx-quant#250): 토스증권(tossinvest.com)이 종목을 보이듯 원형 로고, 회사 이름, 그 뒤에 흐린 종목 코드다(예: 삼성전자 005930). 이름과 로고 주소는 엔진의 `GET /v1/symbols`(`name`, `logo_url`)가 준다. 엔진은 토스 Open API의 이름을, 없으면 KIS 종목 마스터의 이름을 준다(krx-quant `docs/engine-api.md`). 엔진이 이름을 모르면(시작 직후) 로고와 코드만 보인다. 이름도 로고 주소도 주지 않는 옛 엔진이면 로고 자리 없이 코드만이다(전과 같다). 로고는 화면이 실행 중 한 번씩 받아(10초 제한) 메모리에만 두고, 글자 높이의 원으로 자른다. 받기 전이나 받지 못하면(토스에 로고가 없는 코드는 403) 이름 첫 글자의 원(테마의 `BG_RAISED`·`TEXT_PRIMARY`)이다. 그림은 `KrxLogos`, 이름 뒤 코드는 `KrxSymbolDelegate`(`screens/krx_quant/KrxSymbolCell`)가 그린다. 행을 고르면 그 행의 코드(`Qt::UserRole`)로 종목을 고른다.

  > **바뀜** (2026-10-16, codeyoma/krx-quant#250): 종목 열은 종목 코드(005930)였다 → 로고, 회사 이름, 흐린 코드. 이유: 코드만으로는 어느 회사인지 바로 알 수 없어, 토스증권처럼 이름과 로고를 보이기로 했다.

  > **바뀜** (2026-10-16, codeyoma/krx-quant#243): 열에 "채점까지"(최신 봉 마감 예측의 채점까지 남은 시간. 지난 거래일의 예측이면 "지난 거래일", 시각이 지났으면 "채점됨")가 있었다 → 뺐다. 이유: 최신 봉 마감은 모든 종목에 같아 모든 행에 같은 값이 보였고, 새 봉 마감이 1분마다 와서 4:00에서 3:00쯤까지 내려간 뒤 다시 4:00으로 돌아갔다. 그래서 "4분마다 채점한다"로 읽혔다. 엔진은 종목마다 1분마다 겹치는 4분 창 하나를 채점한다(krx-quant `docs/design.md` 5장). 대신: 예측 패널 제목의 4분(아래), 예: "005930 · 10:30–10:34".

- **차트** (codeyoma/krx-quant#105): 고른 종목의 1분봉을 TradingView Lightweight Charts™로 그린다. 처음에는 오늘의 봉이다. 휠이나 맥 트랙패드 핀치로 줌하고(포인터에 가장 가까운 봉이 제자리에 남는다, 아래 휠 줌·트랙패드 핀치), 드래그나 Shift+휠로 좌우 스크롤하고(Shift+휠은 맥과 윈도우 모두 좌우다. 윈도우는 Shift+휠을 세로 휠로 보내 페이지가 가로 휠로 바꿔 준다, codeyoma/krx-quant#242), 머리줄의 **맞춤** 버튼을 누르면 최근 60봉 보기로 돌아간다. 1분 보기에서 봉 칸을 두 번 누르면 그 봉의 창이 열린다(아래 **봉 창**). 처음 그릴 때도 최근 60봉이고, 손으로 옮기기 전까지 최근 60봉을 따라간다: 봉이 바뀔 때마다(지표가 읽는 앞의 봉이 붙거나 새 봉이 오면) 다시 맞추고, 차트 폭이 바뀌어도 다시 맞춘다. 차트를 휠로 돌리거나 누르거나(드래그, 클릭) 터치하면 그 자리에 남고, 앞의 봉이 붙어도 같은 봉을 보인다. 맞춤(보기 바꾸기도)이나 다른 종목을 고르면 다시 따라간다. 아래 칸에 거래량 막대(오른 봉은 초록, 내린 봉은 빨강. 하단 지표 `거래량`이다, 아래 보조지표), 오른쪽 가격 축에 현재가 선이 있다. 오른쪽 아래, 시간 축 줄의 오른쪽 끝(가격 축 아래 모서리)에 `일반`과 `로그` 버튼이 있고(codeyoma/krx-quant#245), 고른 쪽이 주황 굵은 글씨다. `로그`를 누르면 가격 칸의 가격 축이 로그 눈금이 된다(Lightweight Charts `PriceScaleMode.Logarithmic`: 같은 비율의 움직임이 같은 높이). 가격 칸의 보조지표와 그린 것도 같은 축을 따르고, 하단 칸(거래량, RSI 등)은 그대로 일반이다. 고른 것은 모든 종목과 보기, 차트 모양에 하나로 앱 설정 `krx_quant/chart_log_scale`(bool, 기본 `false` 일반)에 남는다. 페이지가 누른 것을 바로 그리고 bridge(`scaled`)로 화면에 알리면 화면이 저장한다. 화면은 저장된 것을 페이지에 보낸다(`scale_changed`). 시각은 KST다. 분 보기의 "봉"은 체결이 없는 분의 빈 봉도 한 칸으로 센다(아래 빈 봉, codeyoma/krx-quant#236).

  > **바뀜** (2026-10-16, codeyoma/krx-quant#255): 차트를 두 번 누르면 머리줄의 **맞춤**처럼 최근 60봉 보기로 맞췄다 → 맞춤은 **맞춤** 버튼으로만 한다. 1분 보기에서 봉 칸을 두 번 누르면 그 봉의 창이 열린다(아래 **봉 창**). 이유: 사용자가 두 번 누르기로 그 봉의 정보를 새 창으로 보기를 원했다(2026-10-16). **맞춤** 버튼의 툴팁에서도 "차트를 두 번 눌러도 된다"를 뺐다.

  > **바뀜** (2026-10-15, codeyoma/FinceptTerminal#59): 처음 그린 봉에서 한 번만 맞췄다 → 손으로 옮기기 전까지 봉이 바뀔 때마다, 차트 폭이 바뀔 때마다 다시 맞춘다. 이유: 화면은 오늘 봉을 먼저 그리고 지표가 읽는 앞의 봉(codeyoma/krx-quant#222, #223)을 곧바로 붙인다. 한 번만 맞추면 첫 화면이 오늘 봉 몇 개의 확대로 남거나(5분 보기의 오늘 봉은 18개다), Lightweight Charts가 맞출 범위를 다음 프레임에 그때의 순번으로 적용해 그 사이 붙은 앞의 날을 보였다. 데모 엔진 캡처 6번 중 5번이 그랬다.

  > **바뀜** (2026-10-15, codeyoma/krx-quant#222): 아래 거래량 칸은 늘 있었다 → 하단 지표 `거래량`이고, 보조지표 패널이나 범례에서 끄면(칸이 없어진다) 숨길 수 있다. 처음 여는 종목은 켜져 있다. 이유: spec codeyoma/krx-quant#220에서 거래량 칸이 하단 지표가 된다. 하단 지표의 나머지와 칸 높이는 codeyoma/krx-quant#223이다.

  > **바뀜** (2026-10-15, codeyoma/krx-quant#223): 거래량 칸은 가격 칸 아래 하나뿐인 칸(차트 높이의 22%)이었다 → 하단 지표 25종 중 하나인 `거래량` 지표의 칸이다. 다른 하단 지표의 칸과 함께 켠 순서대로 쌓이고(처음 여는 종목은 거래량만 켜져 맨 위), 높이(처음 100px)는 칸 경계를 드래그해 바꾸며 종목별로 남는다(아래 하단 칸). 이유: spec codeyoma/krx-quant#220에서 지금의 거래량 칸이 `거래량` 지표가 되고, 하단 지표마다 칸 하나를 둔다.
  - 십자선(codeyoma/krx-quant#275): 가로선과 그 가격 축 라벨은 봉 위에서도 포인터 높이를 따른다(가격 칸은 그 높이의 가격, 하단 칸은 그 높이의 값). 차트를 `CrosshairMode.Normal`로 만들고, 차트 모양·테마·그리기의 `자석`을 바꿔도 그대로다. 그리기의 `자석`은 그린 것의 점만 봉의 시가·고가·저가·종가에 붙인다.

    > **바뀜** (2026-10-18, codeyoma/FinceptTerminal#84): 십자선 가로선과 라벨이 봉 위에서 그 봉의 종가(가격 칸)나 거래량 막대 끝(하단 칸)에 붙었다(Lightweight Charts 기본 `CrosshairMode.Magnet`) → 포인터 높이를 따른다. 이유: 봉 위에 올리면 선이 봉 끝점에 걸려 원하는 높이의 값을 읽지 못했다(사용자 요청, 2026-10-18).
  - 휠 줌(codeyoma/krx-quant#253): 세로가 가로보다 큰 휠은 페이지가 직접 줌한다. 포인터에 가장 가까운 칸(빈 봉 칸도 한 칸, 최신 봉 오른쪽 빈자리면 최신 봉)의 가운데가 그 자리에 남는다. 한 번에 봉 폭이 바뀌는 정도는 Lightweight Charts와 같다(100픽셀에 최대 10%). 다만 윈도우에서도 픽셀을 화면 배율로 나누지 않는다: Lightweight Charts는 윈도우의 Chromium에서 휠 픽셀을 화면 배율로 나누지만(윈도우 Chrome의 휠 값이 그만큼 크게 온다), Qt WebEngine은 휠 한 칸을 화면 배율 없이 시스템의 휠 줄 수 × 20픽셀로 바꿔 보낸다. 라이브러리는 줌 값을 다음 프레임에 반영하므로, 그 사이에 온 휠은 페이지가 요청해 둔 값에서 이어 간다.
    - 줌아웃 때 오른쪽에 최신 봉이 모자라면 보기가 밀린다: 최신 봉 뒤 빈칸이 줌 전보다, 그리고 6칸(맞춤의 `kRightOffset`)보다 넓어지지 않는다. 빈칸은 최신 봉(마지막 칸)부터 센다. 일목균형표의 선행스팬이 최신 봉 뒤로 뻗어 Lightweight Charts의 마지막 점이 그쪽에 있어도 같다.
    - 줌아웃 때 왼쪽에 읽은 봉이 모자라면 기준 봉을 밀지 않고 첫 봉이 왼쪽 끝에 닿은 데서 줌아웃이 멈춘다. 그 사이 화면이 앞의 봉을 읽고(아래 지난 날로 스크롤), 붙으면 다시 줌아웃된다. 엔진에 더 앞의 봉이 없으면(`ChartLook::first_day_whole`, 시간축 없는 모양) 첫 봉을 왼쪽 끝에 둔 채 밀린다. 화면은 이것을 데이터의 `start`로 보낸다.
    - 가로 휠(트랙패드의 옆 쓸기)과 Shift+휠, 그린 것을 끄는 동안의 휠은 Lightweight Charts가 그대로 한다(휠 100픽셀에 80픽셀 옮김). 다만 윈도우에서 배율을 쓰는 화면이면 가로 휠과 Shift+휠은 페이지가 화면 배율만큼 키워 넘긴다. Lightweight Charts가 윈도우의 Chromium에서 그만큼 나누기 때문이다(위와 같은 이유). 세로 휠에 섞여 오는 작은 가로 움직임은 버린다(트랙패드로 위아래로 쓸 때 보기가 옆으로 흐르지 않게).
    - 트랙패드 핀치(codeyoma/krx-quant#391): 맥 트랙패드의 두 손가락 벌리기·오므리기는 화면(`KrxChart`)이 웹 뷰에서 가로채 Chromium에 넘기지 않는다. 대신 포인터 자리(전역 좌표를 웹 뷰 기준으로 바꾼 것)와 그 핀치의 배율을 bridge(`pinched`)로 페이지에 보낸다. 페이지는 휠 줌처럼 포인터에 가장 가까운 칸을 제자리에 두고, 봉 폭을 손가락의 배율만큼 바꾼다(1.5배 벌리면 1.5배). 오른쪽 최신 봉 뒤 빈칸, 왼쪽 첫 봉에서 멈춤, 그린 것을 끄는 동안 줌하지 않음은 휠 줌과 같다. 핀치도 손으로 옮긴 것이라 그 뒤로는 최근 봉을 따라가지 않는다. 두 손가락 두 번 두드리기(스마트 줌)도 가로채 아무것도 하지 않는다(Chromium이 페이지를 1.2배 확대했다). 가로채기는 Qt가 핀치를 네이티브 제스처(`QNativeGestureEvent`)로 보내는 맥의 것이다. 윈도우 트랙패드의 핀치가 Ctrl+휠로 오면 휠 줌 그대로다(윈도우에서는 확인하지 않았다).

    > **바뀜** (2026-10-16, codeyoma/FinceptTerminal#76): Lightweight Charts가 포인터 자리를 기준으로 줌했다. 첫 봉이 왼쪽 끝에 닿으면 왼쪽 끝을 붙잡아 둔 탓(codeyoma/krx-quant#108)에 기준 봉이 옆으로 밀렸고, 오른쪽은 최신 봉 뒤 빈자리가 화면 거의 전체까지 넓어졌다 → 페이지가 포인터에 가장 가까운 봉을 기준으로 줌한다. 오른쪽은 최신 봉이 모자라면 밀리고, 왼쪽은 앞의 봉을 읽는 동안 멈춘다. 이유: 줌하는 동안 처음 가리킨 봉의 자리가 바뀌었다(사용자 요청, codeyoma/krx-quant#253). 오른쪽에 최신 봉이 모자라 밀리는 것은 사용자가 받아들였다.

    > **바뀜** (2026-10-16, codeyoma/FinceptTerminal#81): 윈도우에서는 휠 픽셀이 화면 배율로 나뉘었다(줌은 페이지가 Lightweight Charts를 따라, 가로 휠과 Shift+휠은 Lightweight Charts가) → 모든 OS에서 나누지 않는다. 이유: Qt WebEngine의 휠 값에는 화면 배율이 없어, 배율을 쓰는 윈도우 화면에서 휠 한 번의 줌과 옆 이동이 그 배율만큼 작았다(125%에서 0.8배, `tst_krx_chart_page`가 윈도우에서 실패했다).

    > **바뀜** (2026-10-28, codeyoma/FinceptTerminal#106): 트랙패드 핀치는 Chromium이 Ctrl+휠로 바꿔 페이지에 보냈고 페이지가 휠 줌으로 셌다. Qt가 핀치를 창에서 웹 뷰로 넘길 때 좌표를 창 기준 그대로 두어, 줌의 기준이 차트가 창 안에서 떨어진 만큼(종목 목록과 머리줄만큼) 포인터에서 비켜났다. 핀치 배율 s가 휠 100·ln(s)픽셀로 와서 1.48배 핀치가 1.04배 줌이었고, 스마트 줌은 페이지를 1.2배 확대했다 → 화면이 핀치와 스마트 줌을 가로채고, 핀치는 포인터 자리와 배율로 페이지에 보낸다. 이유: 맥북 트랙패드로 줌하면 포인터 자리가 아니라 차트의 다른 부분이 커졌다(사용자 보고, codeyoma/krx-quant#391).
  - 지난 날로 스크롤(codeyoma/krx-quant#108, #239): 보기의 왼쪽 끝 앞에 남은 봉이 한 화면 너비(보이는 봉 수, 최소 30봉)보다 적어지면 그 앞의 1분봉을 미리 읽어 앞에 붙인다(`GET …/bars?before=<첫 봉>&limit=<N>`, N은 아래 분 단위 보기). 날을 넘어 어제, 그 전 날로 이어진다. 보기는 보던 봉에 머문다. 왼쪽은 읽은 첫 봉에서 멈추고(빈 칸으로 넘어가지 않는다), 엔진에 더 앞의 봉이 없으면 더 읽지 않는다. 지난 날에도 아이콘, ✓/✗, 호버, 클릭이 된다. 장 시작 전처럼 오늘 봉이 없으면 처음부터 지난 거래일의 봉을 읽는다. 일·주·월·년 보기에서 종목을 열고 분 보기로 바꿔도 그때 읽는다(codeyoma/FinceptTerminal#64. 전에는 읽지 않아 분 차트가 비었고, 화면을 다시 열어야 보였다). 다른 종목을 고르거나 엔진을 읽지 못하면 읽어 둔 지난 봉을 버린다.
    - 봉 읽기는 봉만 읽는다. 아이콘의 예측(최종 합의와 "늘 보합": `…/predictions?algorithm=consensus-final&before=`, `…?algorithm=baseline-flat&before=`)은 1분 보기에서만 따로 읽는다. 봉을 읽기 시작할 때 이미 읽은 예측의 가장 이른 봉 마감 앞 1,000봉 마감을 같이 읽고, 그 뒤에도 읽어 둔 첫 봉까지 1,000봉 마감씩 이어 읽는다(`marks_before`). 최종 합의와 "늘 보합"은 동시에 읽고, 둘 중 덜 거슬러 간 쪽까지 읽은 것으로 친다(`finals_from_after`). 엔진이 그 앞에 예측이 없다고 하면(빈 쪽) 더 읽지 않는다. 3분 이상 보기에서 읽은 봉의 예측은 1분 보기로 올 때 읽는다.
    - 처음 차트를 읽을 때도 오늘 봉과 예측(최종 합의와 "늘 보합" 각 400봉 마감, 모델을 체크했으면 모든 예측자 60봉 마감)을 동시에 읽는다.

  > **바뀜** (2026-10-16, codeyoma/krx-quant#239): 왼쪽 끝이 첫 봉에서 30봉 안으로 오면 1분봉 400개를 읽고, 그 뒤에 그 봉들의 최종 합의, 그다음 "늘 보합"을 차례로 읽었다. 처음 차트도 봉 다음에 예측을 읽었다 → 남은 봉이 한 화면 너비보다 적으면 미리 읽고, 예측은 1분 보기에서만 봉과 같은 때에 따로 읽으며, 최종 합의와 "늘 보합"도 동시에 읽는다. 이유: 지난 날로 드래그하면 읽어 둔 봉의 끝에서 기다렸다. 한 번 더 읽는 데 왕복이 세 번 차례로 걸렸다.
  - 장중에 엔진이 재시작했으면 그 전의 오늘 봉은 20:30 백필 뒤에야 보인다(엔진 API의 `bars` 참고).
  - 실시간 봉(codeyoma/krx-quant#110): 지금 만들어지는 봉이 스트림의 `bar` 이벤트(엔진이 1초마다 살펴 바뀌었을 때만 보냄)로 1초마다 움직이고, 오른쪽 현재가 선도 같이 움직인다. 고른 종목의 것만 마지막 봉에 합친다: 같은 분이면 그 봉을 바꾸고, 새 분이면 앞 봉을 끝난 것으로 보고 뒤에 붙인다. 앞선 분이나 보이는 것보다 작은 같은 분(늦게 온 이벤트)은 버린다. N분 보기에서는 묶은 마지막 봉이 움직인다. 페이지는 마지막 봉만 다시 그린다(`update`). 끝난 봉은 새 봉 마감마다 다시 읽고, 그 위에 지금 봉을 다시 얹는다. 스트림이 다시 연결되면 처음처럼 모두 다시 읽는다. 지금 봉에는 아직 아이콘이 없다(봉 마감 뒤에 생긴다).
  - **차트 툴바**(codeyoma/krx-quant#220, #221): 차트 위 한 줄이 `[1분 ▾] 일 주 월 년 │ [차트 모양] [+ 보조지표] [그리기]`이고, 오른쪽 끝에 `[휴지통] [톱니]`가 있다. 버튼은 Fincept 포트폴리오 차트의 기간 버튼과 같은 모양(납작한 버튼, 고른 보기는 주황으로 채움, 테마를 따름)이다. 패널이 좁으면 `보조지표`·`그리기`의 글자를 숨기고 아이콘만 남긴다. 들어가는지는 지금 글자로 잰다: 도구를 고른 동안 `그리기` 대신 도구 이름이 들어가므로(아래 그리기, codeyoma/krx-quant#248), 긴 이름(예: `피보나치 속도 저항 팬`)이면 그 동안만 아이콘만 보이고, 커서로 돌아오면 글자가 다시 보인다. 더 좁으면 왼쪽 묶음이 오른쪽 끝에서 잘리고, 휴지통과 톱니는 늘 오른쪽 끝에 보인다. `보조지표`는 보조지표 패널을 연다(아래, codeyoma/krx-quant#222). `그리기`는 그리기 도구 12종과 자석의 메뉴를 열고, 휴지통은 지금 종목의 그린 것을 모두 지운다(아래 그리기, codeyoma/krx-quant#224). 툴바는 `screens/krx_quant/KrxChartToolbar`이고, 보이기만 하며 고른 것을 화면에 알린다.

    > **바뀜** (2026-10-15, codeyoma/krx-quant#222): `보조지표` 버튼은 비활성이었다(툴팁 "준비 중") → 누르면 보조지표 패널이 열린다. 이유: 보조지표 패널과 상단 지표 12종을 만들었다(spec codeyoma/krx-quant#220의 두 번째 하위 이슈).

    > **바뀜** (2026-10-15, codeyoma/krx-quant#224): `그리기`와 휴지통은 비활성이었다(툴팁 "준비 중") → `그리기`는 도구 12종과 `자석`의 메뉴를 열고, 휴지통은 지금 종목의 그린 것을 확인 뒤 모두 지운다(그린 것이 없으면 비활성). 이유: 그리기 도구를 만들었다(spec codeyoma/krx-quant#220의 네 번째 하위 이슈).
  - **종목별 차트 설정**(codeyoma/krx-quant#221): 보기와 차트 모양은 종목마다 앱 설정 `krx_quant/chart/<종목코드>/view`(분 수: 1–240, 일 1440, 주 10080, 월 44640, 년 527040)와 `krx_quant/chart/<종목코드>/style`(아래 차트 모양의 이름)에 남는다. 종목을 고르면 그 종목의 보기와 모양으로 바뀐다(`KrxLive::selected_changed`). 처음 여는 종목과 목록 밖의 값은 1분·캔들이다(`ChartLook::parse_prefs`). 켠 지표와 설정값은 `krx_quant/chart/<종목코드>/indicators`, 고정 VWAP 기준은 `…/vwap_anchor`에 남는다(codeyoma/krx-quant#222, 아래 보조지표). 하단 칸 높이는 `…/pane_heights`에 남는다(codeyoma/krx-quant#223, 아래 하단 칸). 그린 것은 `…/drawings`에 남는다(codeyoma/krx-quant#224, 아래 그리기). 시간축 없는 차트의 크기는 `…/brick_sizes`에 남는다(codeyoma/krx-quant#225, 아래 차트 모양). 자석은 모든 종목에 하나다(`krx_quant/chart_magnet`). 종목을 고르기 전에는 1분·캔들이고 남기지 않는다.

    > **바뀜** (2026-10-15, codeyoma/krx-quant#221): 보기는 모든 종목에 하나(`krx_quant/chart_view_minutes`)였다 → 종목마다 남는다. 옛 키는 더 읽지 않는다. 이유: 한 종목에 맞춘 차트가 다른 종목에 그대로 남지 않게(2026-10-15 인터뷰, codeyoma/krx-quant#220).
  - **차트 모양**(codeyoma/krx-quant#221): 툴바의 차트 모양 버튼(지금 모양의 아이콘)을 누르면 메뉴가 열린다: 바(`bar`), 캔들(`candle`), 라인(`line`), 영역(`area`), 렌코(`renko`), 카기(`kagi`), 포인트 앤 피겨(`point_figure`), 라인 브레이크(`line_break`), 하이킨아시(`heikin_ashi`), 투명캔들(`hollow`), 베이스라인(`baseline`). 괄호는 설정과 페이지에 쓰는 이름이다. 고른 것은 주황 굵은 글씨다.
    - 바는 OHLC 바, 라인과 영역은 종가 선(테마의 주황, 영역은 선 아래를 칠함), 투명캔들은 오른 봉을 테두리만, 내린 봉을 채워 그린다.
    - 하이킨아시는 봉을 하이킨아시 봉으로 그린다(종가 = 시가·고가·저가·종가의 평균, 시가 = 앞 하이킨아시 봉의 시가와 종가의 중간, 첫 봉은 제 시가와 종가의 중간, 고가·저가는 봉과 제 시가·종가를 포함). 거래량 색과 가격 축의 현재가 선은 원래 봉의 것이다. 지금 봉이 움직이면 앞 봉들부터 다시 센다.
    - 베이스라인은 기준값 위를 오름 색, 아래를 내림 색으로 칠한다. 기준값은 분 보기에서 전일 종가(가장 최근 날의 첫 봉 앞, 읽은 마지막 봉의 종가. 그 앞을 읽지 않았으면 첫 봉의 시가), 일·주·월·년 보기에서 화면 첫 봉의 종가(스크롤·줌하면 다시 정한다)다. 바꿀 수 없다.
    - **시간축 없는 차트**(codeyoma/krx-quant#225): 렌코·카기·포인트 앤 피겨·라인 브레이크는 지금 보기의 봉(1–240분봉, 일·주·월·년봉)의 종가로 벽돌을 만들어 벽돌 순서로 한 칸씩 그린다. 벽돌은 그것을 만든 봉들을 담는다: 앞 벽돌 다음 봉부터 그 벽돌을 끝낸 봉까지다. 축과 십자선에는 벽돌이 끝난 실제 시각(끝낸 봉의 시작 시각. 시간축 차트가 봉을 부르는 시각이다. 분 보기는 `10-12 09:31`, 일·주·월·년 보기는 `2026-10-12`)을 보이고, 벽돌의 거래량은 그 봉들의 거래량 합이다. 메뉴 항목의 툴팁이 그 모양을 만드는 규칙이다.
      - 렌코(`renko`): 박스 ATR(14). 첫 봉의 종가에서 시작한다. 종가가 마지막 벽돌의 위보다 박스 이상 오르면 오른 박스 수만큼 오름 벽돌을, 아래보다 박스 이상 내리면 내림 벽돌을 쌓는다. 그래서 방향이 바뀔 때는 마지막 벽돌의 끝에서 두 박스다. 한 봉이 벽돌 여럿을 끝내면 그 벽돌들이 담은 거래량을 똑같이 나눈다. 마지막 벽돌 뒤의 봉은 아직 벽돌이 아니다. 오름은 상승색, 내림은 하락색 상자(꼬리 없음)다.
      - 카기(`kagi`): 반전폭 ATR(14). 종가가 첫 종가에서 반전폭 이상 멀어지면 첫 선이 시작한다. 종가가 선의 방향으로 가는 동안 선이 늘고(끝은 가장 먼 종가), 그 끝에서 반전폭 이상 되돌면 거기서 꺾어 다음 선이 시작한다. 마지막 선은 그리는 중이다. 오르는 선이 앞 오름 선의 끝(어깨)을 넘으면 그 가격부터 굵게(양, 상승색), 내리는 선이 앞 내림 선의 끝(허리) 아래로 가면 그 가격부터 가늘게(음, 하락색) 그린다. 첫 선은 오름이면 굵게 시작한다. 선 사이는 가로선으로 잇는다.
      - 포인트 앤 피겨(`point_figure`): 박스 ATR(14), 반전 3칸. 칸은 박스 크기의 배수 가격이다. 종가가 첫 종가의 칸에서 한 칸 이상 가면 첫 열이 시작한다. 종가가 닿은 칸까지 X(오름, 상승색)나 O(내림, 하락색)를 쌓고, 마지막 칸에서 세 칸 이상 되돌면 한 칸 물러난 자리에서 다음 열을 시작한다. 마지막 열은 그리는 중이다. 칸이 4픽셀보다 작으면 X·O 대신 열을 칠한다.
      - 라인 브레이크(`line_break`): 3줄. 첫 줄은 첫 종가에서 그와 다른 첫 종가까지다. 종가가 마지막 줄의 끝을 넘으면 그 끝에서 같은 방향으로 줄을 더하고, 마지막 세 줄(그보다 적으면 모두)의 가장 낮은 값 아래로(가장 높은 값 위로) 가면 마지막 줄의 시작에서 반대 줄을 긋는다. 한 봉은 줄을 하나까지 더한다. 마지막 줄 뒤의 봉은 아직 줄이 아니다.
      - **크기:** 박스와 반전폭은 기본으로 지금 보기의 봉(읽은 봉 모두) 중 마지막 봉을 뺀 봉으로 센 ATR(14)의 마지막 값을 원 단위로 반올림한 값(1원 이상)이다. 마지막 봉은 지금 만들어지는 봉일 수 있어 뺀다. 그래서 크기는 새 봉이 시작될 때(앞 봉이 끝남), 앞의 봉을 더 읽을 때, 보기·종목을 바꿀 때만 바뀐다. 지금 봉이 움직이는 동안에는 다른 일로 차트를 다시 그려도(지표 켜기, 설정 바꾸기, 예측 갱신) 크기와 벽돌이 그대로다. 마지막 봉 앞의 봉이 14개보다 적으면 ATR이 없어 벽돌도 없다.
      - 그 모양을 고른 동안 톱니 메뉴의 `날짜 구분선` 아래에 크기 줄이 나온다: `박스`(렌코), `반전폭`(카기), `박스 (반전 3칸)`(포인트 앤 피겨)는 원 단위 숫자 칸과 `ATR(14) 384원` 체크(지금 센 ATR 값), 라인 브레이크는 `줄 수`(2–10) 칸이다. 숫자를 바꾸면(Enter, 칸을 떠날 때, 화살표) 그 값을 쓰고 체크가 풀린다. 체크하면 ATR(14)로 돌아간다. 포인트 앤 피겨의 반전 3칸은 바꾸지 않는다.
      - 설정 키: `krx_quant/chart/<종목코드>/brick_sizes`는 사용자가 정한 크기의 JSON 객체다(예: `{"line_break":4,"renko":150}`). 모양 이름이 키이고, 원(1–10,000,000의 정수)이나 줄 수(2–10의 정수)가 값이다. 없는 모양은 기본값(ATR(14), 3줄)이다. 시간축 없는 모양이 아닌 키와, 범위 밖이거나 정수가 아닌 값은 버린다. 한 종목의 모든 보기에 같은 값을 쓴다.
      - **시작:** 벽돌은 종목과 보기마다 한 번 정한 봉에서 시작한다. 그 보기의 봉을 180개(첫 화면 60봉의 세 배) 이상 읽었거나, 엔진에 더 앞의 봉이 없거나, 2,000봉에 닿은 때에 읽은 첫 봉이다. 그 전에는 앞의 봉을 읽고(지난 날 스크롤과 같은 읽기, 지표가 바라는 앞부분 대신) 벽돌을 그리지 않는다. 정한 뒤에는 그 봉이 남아 있는 한 그대로라서, 그 앞의 봉을 더 읽어도(시간축 차트에서 왼쪽으로 스크롤) 벽돌은 바뀌지 않는다. 벽돌은 시작한 곳에 따라 달라지기 때문이다(렌코는 첫 종가가 3원 다르면 모든 벽돌이 3원 옮긴다). 벽돌 차트의 왼쪽 끝은 첫 벽돌이고, 스크롤해도 앞의 봉을 읽지 않는다. 종목이나 보기를 바꾸거나, 엔진을 다시 읽어 그 봉이 없어지면 다시 정한다. 크기가 바뀌면(ATR을 다시 셈, 사용자가 바꿈) 벽돌은 모두 바뀐다.
      - 새 봉은 벽돌을 뒤에 더하거나 그리는 중인 마지막 선·열을 바꿀 뿐, 앞의 벽돌은 바꾸지 않는다. 지금 봉이 움직여 벽돌이 바뀌면 벽돌을 모두 다시 보내고, 아니면 가격 축의 현재가 선만 움직인다. 현재가 선은 하이킨아시처럼 마지막 봉의 종가다(벽돌의 끝이 아니다).
      - **숨김과 비활성:** 예측 아이콘, 호버 상자, 4분 구간, 날짜 구분선의 띠, 그린 것과 머리줄의 선택 합의 성적을 숨긴다. `그리기`(툴팁 "그리기: 시간축 없는 차트에서는 쓰지 않는다"), 휴지통(아래 그리기), 톱니 메뉴의 `날짜 구분선`(툴팁 "분봉에서만. 시간축 없는 차트에서는 쓰지 않는다.")은 비활성이다. 고른 그리기 도구는 내려놓는다. 시간축 차트로 돌아오면 모두 다시 보인다. 시간축 차트와 벽돌 차트 사이를 바꾸면 최근 60봉(벽돌)에 다시 맞춘다. 차트 제목은 "005930 · 1분봉 · 렌코"다.
      - **지표:** 벽돌의 시가·고가·저가·종가·거래량으로 센다(벽돌 하나가 봉 하나다). 계산은 시간축 차트와 같은 `ChartStudies`다. 실제 시각이 필요한 것은 이렇게 한다: VWAP(그날 첫 봉)와 고정된 VWAP(기준 시각)는 벽돌에서 그리지 않는다. 보조지표 패널에서 흐리게 비활성이고 툴팁이 "시간축 없는 차트에서는 쓰지 않는다"이다. 범례에도 없지만 켠 것, 설정, 고정 VWAP 기준은 남고, 패널의 `기준 다시 고르기`도 비활성이다. AD 라인과 OBV는 벽돌에서 따로 정한다: 첫 벽돌이 0이다. 벽돌의 시작이 고정되어 있어 앞의 봉을 더 읽어도 값이 바뀌지 않는다(시간축 차트의 0점 시각과 그 앞까지 읽기는 쓰지 않는다). 일목의 선행스팬은 마지막 벽돌 뒤의 빈 칸으로 가고(축 글자 없음), 매물대는 화면에 보이는 벽돌로 센다. 날짜를 보는 것은 이 둘 VWAP뿐이다.
      - 나누기: 변환, 크기, 시작, 설정 해석은 `services/krx_quant/ChartBricks`·`ChartLook`(순수 Qt Core 함수, `tst_krx_chart_bricks`)이다. 화면은 벽돌을 한 칸에 하나씩(1분 간격의 가짜 시각) 페이지에 보내고, 각 벽돌이 끝난 시각을 같이 보낸다. 페이지는 렌코·라인 브레이크를 꼬리 없는 캔들로, 카기와 포인트 앤 피겨를 직접 그리는 시리즈(`addCustomSeries`)로 그리고, 축 글자(`tickMarkFormatter`)와 십자선 글자(프리미티브)를 그 시각으로 바꾼다.

    > **바뀜** (2026-10-15, codeyoma/krx-quant#225): 렌코·카기·포인트 앤 피겨·라인 브레이크는 메뉴에서 흐리게 비활성이었고(툴팁 "준비 중"), 종목에 남은 그 모양은 캔들로 읽었다 → 고를 수 있고 위처럼 벽돌로 그린다. 남은 모양도 그대로 읽는다. 이유: spec codeyoma/krx-quant#220의 다섯 번째 하위 이슈.
    - 시간축 차트 7종 모두에서 아이콘, 호버, 4분 구간, 고정, 선택 합의 성적이 전과 같다(1분 보기). 시간축 없는 4종에서는 숨긴다(위).
    - 무엇을 그릴지(하이킨아시 봉, 베이스라인 기준값, 날짜 구분선의 봉, 설정 해석)는 `services/krx_quant/ChartLook`(순수 함수, `tst_krx_chart_look`)이 정하고, 페이지는 모양마다 시리즈를 바꿔 그리기만 한다.
  - **날짜 구분선**(codeyoma/krx-quant#221): 톱니 메뉴의 체크 항목 `날짜 구분선`(기본 켬)이다. 모든 종목에 하나이고 앱 설정 `krx_quant/chart_day_separator`에 남는다. 분 보기(모든 N분)에서 KST 날짜가 바뀐 뒤 첫 봉(보통 08:00 NXT 봉) 한 칸 폭을 밝은 회색 세로 띠(테마 글자색 `TEXT_PRIMARY`, 불투명도 35%)로 봉 뒤에 칠한다. 가격 칸과 하단 칸(codeyoma/krx-quant#223) 모두의 전체 높이다. 읽은 첫 봉은 09:00 이전에 시작할 때만 날의 첫 봉으로 본다(그날 중간부터 읽은 날은 칠하지 않는다). 일·주·월·년 보기에서는 항목이 비활성이고 툴팁이 "분봉에서만"이다. 시간축 없는 차트(codeyoma/krx-quant#225)에서도 비활성이고 띠를 칠하지 않는다(툴팁 "분봉에서만. 시간축 없는 차트에서는 쓰지 않는다.").
    > **바뀜** (2026-10-16, codeyoma/FinceptTerminal#74): 띠는 흐린 글자색 `TEXT_DIM`(#404040)의 22%였다 → 글자색 `TEXT_PRIMARY`(#e5e5e5)의 35%(밝은 회색). 이유: 어두운 배경에서 회색 띠가 잘 보이지 않았다(2026-10-16 사용자 요청).
  - **빈 봉**(codeyoma/krx-quant#236): 분 보기(1분과 모든 N분)에서 그날 첫 봉과 마지막 봉 사이에 체결이 없어 봉이 없는 분(N분 칸)마다 빈 봉을 둔다. 빈 봉은 한 칸을 차지하지만 캔들을 그리지 않고(Lightweight Charts의 whitespace), 거래량 칸에도 막대가 없다(범례의 거래량은 0). 그래서 거래가 멈춘 시간만큼 차트에 빈 칸이 생기고 시각이 건너뛰지 않는다.
    - 그날 첫 봉 앞과 마지막 봉 뒤(장 시작 전·장 끝 뒤), 두 날 사이(밤, 주말)는 채우지 않는다. 다만 오늘 장이 열려 있는 동안(엔진 상태 `/v1/status`의 `session`의 `open`부터 `close` 전까지, 엔진 시계 `now` 기준, codeyoma/krx-quant#238)은 오늘 마지막 봉 뒤의 칸도 엔진 시계까지 다 지난 칸마다 빈 봉으로 채운다. 1분 보기의 13:10 칸은 13:11이 되어야 찬다(N분 보기는 그 N분이 다 지나야). 그래서 진행 중인 서킷브레이커나 VI, 체결이 드문 종목의 조용한 시간도 지난 만큼 보이고, 1분(N분)에 한 칸씩 자란다. 화면은 엔진 시계의 칸이 바뀔 때마다 차트를 다시 그린다(`ChartSlots::clock_slot`). 장이 끝나면(`close`부터) 그날은 첫 봉에서 마지막 봉까지로 돌아가고, 오늘 첫 봉이 아직 없거나 장 시작 전(NXT 봉만 있을 때)이면 자라지 않는다. `session`이 없는 엔진이나 휴장일에도 자라지 않는다. 빈 봉이었던 칸에 나중에 실제 봉이 오면(다시 읽은 봉, 늦게 온 실시간 봉) 그 봉이 칸을 차지한다. 그날 안의 다른 빈 시간도 그대로 보인다: NXT 장전 08:50–09:00, 종가 동시호가 15:20–15:30, 체결이 드문 종목의 빈 분. 하루에 봉이 하나면 채울 것이 없다. 일·주·월·년 보기와 시간축 없는 차트에는 빈 봉이 없다.
    - 실시간: 거래 없는 분 뒤에 새 봉이 오면 그 사이의 빈 봉이 곧바로 생긴다(페이지가 빈 칸을 먼저 붙이고 새 봉을 그린다). 새 봉이 시계까지 채운 마지막 빈 봉의 칸이면 그 자리에서 바꾸고, 그보다 앞의 빈 봉 칸이면 차트를 다시 보낸다. 왼쪽으로 스크롤해 앞의 봉을 읽어도 같은 규칙으로 채운다(읽은 앞의 봉과 그날 그다음 봉 사이도).
    - 호버와 십자선: 빈 봉 위의 십자선은 그 칸의 시각을 보이고, 호버 상자는 "09:20–09:21 · 체결 없음"(N분 보기는 "09:15–09:20 · 체결 없음")이다. 실제 봉의 상자 첫 줄은 봉을 마감 시각으로 부르므로("09:21 봉") 빈 봉은 칸의 시작과 끝을 함께 적는다. 빈 봉에는 4분 구간도 아이콘도 없다(앉을 봉이 없다). 실제 봉의 아이콘, 호버, 4분 구간은 전과 같고, 4분 구간이 빈 칸에 걸치면 그 칸까지 칠한다.
    - 칸으로는 세고 값으로는 쓰지 않는다:
      - 보조지표는 실제 봉으로만 센다. 빈 봉 때문에 바뀌는 값은 없다. 빈 봉에는 점이 없어 선이 그 앞뒤 실제 봉을 바로 잇는다. TradingView의 기본 선(`plot.style_line`)이 값 없는 봉을 건너 잇는 것과 같다(끊는 것은 `plot.style_linebr`다). 두 선 사이 채우기(볼린저 밴드, 일목 구름)도 같다. 빈 봉 위의 범례는 선의 값과 "비움" 이유를 보이지 않는다.
      - 그리기는 시각과 봉 위치의 변환(`ChartDrawings::logical_at`·`time_at`)이 빈 칸을 센다. 그래서 멈춘 시간 안의 점은 제 칸에 놓이고, 보기를 바꿔도 같은 시각에 있다. 자석은 빈 봉 위에서 붙지 않는다(값이 없다).
      - 맞춤의 최근 60봉은 최근 60칸(빈 봉 포함, 시계까지 채운 것도)이다. 날짜 구분선은 그날의 첫 실제 봉에 칠한다(첫 봉 앞에는 빈 봉이 없다).
      - 베이스라인의 기준값, 하이킨아시 봉, 매물대는 실제 봉으로 센다.
      - 시간축 없는 차트의 벽돌은 빈 봉을 쓰지 않는다.
    - 나누기: 채우기(엔진 시계까지 포함), 실제 봉 고르기, 실시간 봉 붙이기, 띠의 자리와 호버 줄은 `services/krx_quant/ChartSlots`(순수 Qt Core 함수, `tst_krx_chart_slots`)다. 화면(`KrxChart`)은 빈 봉을 포함한 칸을 페이지에 보내고(빈 봉은 시각만), 페이지는 그리기만 한다.
  - **특수 상황 표시**(codeyoma/krx-quant#236): 분 보기에서 엔진이 아는 서킷브레이커·사이드카(시장 전체)와 그 종목의 VI 구간을 옅은 세로 띠로 봉 뒤에 칠하고(가격 칸과 하단 칸의 전체 높이), 가격 칸 위쪽에 이름표를 단다: "서킷브레이커 1단계"(단계는 엔진의 `detail`), "사이드카", VI는 KIS가 알린 종류로 "정적 VI"·"동적 VI"·"정적·동적 VI"(엔진의 `static`·`dynamic`·`both`, 종류가 없으면 "VI"). 색은 종류마다 하나다: 서킷브레이커 빨강, 사이드카 노랑, VI 파랑(테마의 하락·경고·정보 색). 이름표가 범례와 겹칠 자리면 범례 아래에 적는다.
    - 띠는 구간의 시작부터 끝까지다(시각을 봉 위치로 바꾸는 것은 그리기와 같다). 빈 봉 위에서는 그 칸들을 덮는다. 아직 끝나지 않은 구간(엔진 시계가 시작과 끝 사이)은 엔진이 예상한 끝이 아니라 엔진 시계까지 그려, 위의 빈 봉과 함께 시계를 따라 자란다(codeyoma/krx-quant#238). 마지막 봉 뒤로 이어지는 띠는 오른쪽 빈칸에 그린다. 띠 안의 봉(빈 봉 포함)의 호버 상자에 "서킷브레이커 1단계 09:06–09:36" 줄이 붙는다(초가 있으면 "동적 VI 09:40:10–09:42:20"). 실시간으로 새로 붙은 봉과 그 앞에 채운 빈 봉도 곧바로 그 줄을 갖는다(1분 보기의 예측 상자는 다음에 차트를 다시 그릴 때 온다, `ChartSlots::live_hovers`). N분 보기에서도 빈 봉과 띠 안의 봉에는 이 상자가 나온다.
    - 일·주·월·년 보기와 시간축 없는 차트에서는 그리지 않는다.
    - 톱니 메뉴의 체크 항목 `특수 상황 표시`(기본 켬, 날짜 구분선 아래)로 끄고 켠다. 모든 종목에 하나이고 앱 설정 `krx_quant/chart_special`에 남는다. 끄면 띠와 호버 줄이 없다(빈 봉은 그대로다). 일·주·월·년 보기와 시간축 없는 차트에서는 날짜 구분선처럼 비활성이다.
    - 읽기: 엔진의 `GET /v1/symbols/{s}/special?from=&to=`(KST 날짜, 둘 다 포함, krx-quant `docs/engine-api.md`)를 읽은 1분봉의 첫 날부터 엔진의 오늘까지로 묻는다. 엔진이 한 번에 60일까지만 답하므로 그보다 앞의 날에는 띠가 없다. 종목을 고르거나 앞의 봉을 더 읽어 날이 바뀌면 곧바로, 화면이 보이는 동안 60초마다 다시 읽는다. 그 응답이 없는 엔진(404)이면 띠가 없다. 읽기에 실패하면 그때까지의 띠를 두고 다음에 다시 묻는다. 엔진을 읽지 못하거나 엔진 주소를 바꾸면 지운다.
  - **보조지표**(codeyoma/krx-quant#220, #222): 툴바의 `+ 보조지표`를 누르면 버튼 아래에 두 칸 팝업(`screens/krx_quant/KrxStudyPanel`)이 열린다. 바꾼 값은 바로 차트에 반영되고 그 종목의 설정에 남는다. 팝업 밖을 누르면 닫힌다.
    - 왼쪽은 `상단 지표`(가격 칸에 겹침) 12종과 `하단 지표`(칸마다 하나, codeyoma/krx-quant#223) 25종 목록이다. 그 아래 `일별 수급` 8종(codeyoma/krx-quant#226, 아래)도 하단 지표다. 목록은 세로로 스크롤된다. 체크 원으로 켜고 끄고(켠 것은 주황 체크), 이름을 누르면 오른쪽에 그 지표가 나온다. 범례의 설정 버튼으로 열면 목록이 그 지표까지 스크롤된다. 하단 지표를 끄면 그 칸이 없어진다.
    - 오른쪽은 고른 지표의 이름, `?`(올리거나 누르면 계산 방법), 설명 한 줄, 설정이다. 설정은 기준값(종가·시가·고가·저가·(고가+저가)/2·(고가+저가+종가)/3. 기준값을 읽는 지표만), 기간·배수 같은 숫자, 선마다 색(팔레트 18색)과 굵기(1–4px)다. 숫자는 Enter나 칸을 떠날 때 반영된다. 이동평균선과 지수이동평균선은 선마다 색·기간·굵기 한 줄이고, `+ 기간 추가`로 선을 더하고(5, 10, 20, 60, 120, 200, 240, 480 중 아직 없는 첫 값, 최대 8개) X로 뺀다(첫 줄은 빠지지 않는다). 프라이스 오실레이터의 `방식`(단순·지수)은 목록 상자다. 거래량, AD 라인, OBV에는 기간이 없다(거래량은 바꿀 설정이 없다).
    - 상단 지표 12종과 기본값(괄호는 설정과 페이지에 쓰는 이름이다):
      - 이동평균선(`ma`): 단순, 5·20·60·120, 종가. 그 봉까지 N봉 기준값의 평균이다.
      - 일목균형표(`ichimoku`): 전환 9, 기준 26, 선행 52, 이동 26. 전환선·기준선은 그 기간 최고 고가와 최저 저가의 중간, 선행스팬1은 전환선과 기준선의 중간, 선행스팬2는 선행 기간의 중간이다. 두 선행스팬은 이동 기간만큼 앞에 그리므로 마지막 봉 뒤 26봉(미래)까지 간다. 그 사이 구름은 선행스팬1이 위면 선행스팬1의 색, 아래면 선행스팬2의 색으로 옅게 칠한다. 후행스팬은 종가를 26봉 뒤에 그린다. 미래 봉의 시각은 분 보기면 N분씩, 일 보기면 다음 평일, 주·월·년 보기면 다음 주, 다음 달 1일, 다음 해 1월 1일이다.
      - 볼린저 밴드(`bollinger`): 20, 2σ, 종가. 중심선은 20봉 단순 평균, 상단·하단은 중심선 ± 2 × 같은 20봉의 모표준편차다. 띠 안을 옅게 칠한다.
      - 슈퍼트렌드(`supertrend`): ATR 10, 배수 3. TradingView `ta.supertrend`와 같다. (고가+저가)/2 ± 3 × ATR(10, Wilder 평균, 첫 값은 처음 10봉 true range의 평균)로 띠를 만든다. 앞 종가가 띠 안이면 띠는 좁아지기만 하고, 종가가 선을 넘으면 추세가 바뀐다. 상승 추세는 아래 띠(초록), 하락 추세는 위 띠(빨강)로 그리고, 바뀌는 곳에서 선이 끊긴다. ATR이 생긴 첫 봉은 하락으로 시작한다.
      - 매물대분석(`volume_profile`): 화면에 보이는 봉, 가격대 24칸(설정 4–200), 오른쪽 가로 막대(가장 긴 막대가 가격 칸 폭의 4분의 1)다. 보이는 봉의 최저 저가~최고 고가를 같은 높이로 나누고, 봉마다 거래량을 그 봉의 저가~고가가 걸친 칸에 걸친 만큼 나눠 더한다(고가와 저가가 같은 봉은 종가의 칸에 모두). 매수·매도는 나누지 않는다. 스크롤·확대하면 다시 센다. 막대는 봉 뒤에 그린다.
      - 엔벨로프(`envelope`): 단순 20, ±10%, 종가. 중심선 × (1 ± 10%)다.
      - 윌리엄스 프랙탈(`fractal`): 2(5봉). 고가가 양쪽 2봉의 고가보다 모두 높으면 봉 위에 ▲(초록), 저가가 양쪽 2봉의 저가보다 모두 낮으면 봉 아래에 ▼(빨강)를 찍는다. 같은 값이 있으면 프랙탈이 아니다. 오른쪽 2봉이 생겨야 정해진다.
      - 지수이동평균선(`ema`): 5·20·60·120, 종가. 첫 값은 처음 N봉의 단순 평균이고, 그 뒤는 앞 EMA + 2/(N+1) × (기준값 − 앞 EMA)다.
      - 파라볼릭 SAR(`psar`): 시작 0.02, 증가 0.02, 최대 0.2. TradingView `ta.sar`와 같다(둘째 봉부터, 둘째 종가가 첫 종가보다 높으면 상승으로 시작). 점으로 그린다.
      - 프라이스 채널(`price_channel`): 20. 그 봉까지 20봉의 최고 고가(상단)와 최저 저가(하단)다.
      - VWAP(`vwap`): (고가+저가+종가)/3. KST 날짜가 바뀐 뒤 첫 봉(날짜 구분선과 같은 기준)부터 기준값 × 거래량의 합 ÷ 거래량의 합을 다시 센다. 그날 거래량이 아직 0이면 값이 없다. 읽은 봉에 그날이 다 있을 때만 그린다: 그보다 앞 날짜의 봉도 읽었거나, 엔진에 더 앞의 1분봉이 없을 때(앞을 더 읽었더니 빈 답)다. 읽은 첫 날은 중간부터일 수 있어 비우고, 범례의 값 자리에 "그날 첫 봉을 읽지 않아 비움"을 보인다. 그렇지 않으면 그날 값이 틀리고, 왼쪽으로 스크롤해 앞의 봉을 읽을 때마다 바뀐다. 분봉에서만 그린다. 일·주·월·년 보기에서는 목록에서 흐리게 비활성이고 툴팁이 "분봉에서만"이다. 켜 둔 채 그 보기로 바꾸면 그리지 않고 범례에도 없지만, 켠 것과 설정은 남는다.
      - 고정된 VWAP(`anchored_vwap`): (고가+저가+종가)/3, 모든 보기. 기준 없이 켜면 패널이 닫히고, 범례에 "차트를 눌러 기준 봉을 고르세요"가 나오며, 다음에 차트를 누른 곳의 봉이 기준이 된다. 기준 봉부터 VWAP를 센다. 기준 봉에 세로 점선과 아래쪽 마름모를 그리고, 그 선을 드래그해 놓은 봉으로 기준을 옮긴다(드래그하는 동안 차트는 스크롤되지 않는다). 패널의 `기준 다시 고르기`로 다시 찍을 수도 있다. 범례에서 숨긴 동안은 그 버튼이 비활성이다(툴팁 "숨긴 동안은 고를 수 없다…"): 숨긴 지표는 아무것도 그리지 않아 기준을 고르는 클릭도 받지 않는다. 기준은 시각으로 남는다. 보기를 바꾸면 그 시각이 든 봉(분 보기는 시계 기준 N분 칸, 일·주·월·년 보기는 그 날·주·달·해의 봉. 그 칸에 봉이 없으면 다음 봉)이 기준이다. 기준 봉을 아직 읽지 않았으면 그때까지 앞의 봉을 더 읽는다(아래 앞부분 값, 같은 상한). 그래도 없으면 범례에 "기준 봉을 읽지 않았다"가 나온다.
    - 하단 지표 25종과 기본값(codeyoma/krx-quant#220, #223. 괄호는 설정과 페이지에 쓰는 이름, 가이드선은 그 칸에 긋는 점선이다):
      - 거래량(`volume`): 봉마다 거래량 막대. 종가가 시가 이상이면 상승색, 아니면 하락색이다. 바꿀 설정이 없다.
      - 매스 인덱스(`mass_index`): EMA 9, 합 25, 가이드선 27·26.5. 봉마다 EMA(고가 − 저가) ÷ EMA(EMA(고가 − 저가))를 25봉 더한다.
      - 모멘텀(`momentum`): 10, 종가. 기준값 − 10봉 전 기준값이다.
      - 볼륨 오실레이터(`volume_oscillator`): 짧은 5, 긴 10. 100 × (거래량 EMA 5 − EMA 10) ÷ EMA 10이다(TradingView와 같다).
      - 볼린저 밴드 %B(`bollinger_b`): 20, 2σ, 종가, 가이드선 1·0. (기준값 − 하단) ÷ (상단 − 하단)이다. 밴드는 볼린저 밴드와 같고, 상단과 하단이 같으면 비운다.
      - 볼린저 밴드폭(`bollinger_width`): 20, 2σ, 종가. 100 × (상단 − 하단) ÷ 중심선(%)이다(TradingView BBW와 같다).
      - 스토캐스틱(`stochastic`): 14, 3, 3(슬로우), 가이드선 80·20. %K는 100 × (종가 − 14봉 최저 저가) ÷ (14봉 최고 고가 − 최저 저가)의 3봉 단순 평균, %D는 %K의 3봉 단순 평균이다. 14봉의 최고와 최저가 같은 봉은 비운다.
      - 스토캐스틱 RSI(`stochastic_rsi`): RSI 14, 스토캐스틱 14, K 3, D 3, 종가, 가이드선 80·20. RSI(14)가 14봉 최고·최저 사이 어디인지(%)의 3봉 단순 평균이 K, K의 3봉 단순 평균이 D다.
      - 윌리엄스 R(`williams_r`): 14, 가이드선 −20·−80. −100 × (14봉 최고 고가 − 종가) ÷ (최고 고가 − 최저 저가)다.
      - 이격도(`disparity`): 20, 종가, 가이드선 100. 100 × 기준값 ÷ 20봉 단순 평균이다.
      - 일중 강도 지수(`intraday_intensity`): 21, 가이드선 0. 봉마다 (2 × 종가 − 고가 − 저가) ÷ (고가 − 저가) × 거래량의 21봉 합 ÷ 21봉 거래량 합 × 100(%)이다. 고가와 저가가 같은 봉은 0을 더한다.
      - 체이킨 오실레이터(`chaikin_oscillator`): 3, 10, 가이드선 0. EMA(AD 라인, 3) − EMA(AD 라인, 10)이다.
      - 트릭스(`trix`): 18, 시그널 9, 종가, 가이드선 0. 기준값에 EMA 18을 세 번 씌운 값이 앞 봉보다 바뀐 비율 × 100이고, 시그널은 그 EMA 9다.
      - 프라이스 오실레이터(`price_oscillator`): 짧은 10, 긴 21, 단순, 종가, 가이드선 0. 100 × (10봉 평균 − 21봉 평균) ÷ 21봉 평균이다(TradingView와 같다. 짧은 평균으로 나누는 HTS도 있다). `방식`은 단순(이동평균)이나 지수(EMA)이고, 설정에는 0·1로 남는다.
      - AD 라인(`ad_line`): 설정 없음. 봉마다 (2 × 종가 − 고가 − 저가) ÷ (고가 − 저가) × 거래량을 누적한다(아래 누적 지표의 0점).
      - ADX(`adx`): 14. 아래 DMI의 ADX 선이다.
      - ATR(`atr`): 14. true range(고가 − 저가, |고가 − 앞 종가|, |저가 − 앞 종가| 중 가장 큰 것. 첫 봉은 고가 − 저가)의 Wilder 평균이다.
      - CCI(`cci`): 20, (고가+저가+종가)/3, 가이드선 100·−100. (기준값 − 20봉 단순 평균) ÷ (0.015 × 20봉 평균 편차)다. 평균 편차가 0이면 비운다.
      - DMI(`dmi`): 14. +DI(초록), −DI(빨강), ADX(주황)다. 둘째 봉부터 +DM(고가가 앞 봉보다 오른 폭이 저가가 내린 폭보다 크고 양수면 그 폭, 아니면 0), −DM(거꾸로), true range를 각각 14봉 Wilder 평균해 +DI = 100 × +DM ÷ TR, −DI = 100 × −DM ÷ TR이고, ADX는 DX = 100 × |+DI − −DI| ÷ (+DI + −DI)의 14봉 Wilder 평균이다(TradingView와 같다).
      - MACD(`macd`): 12, 26, 9, 종가, 가이드선 0. MACD = EMA 12 − EMA 26, 시그널 = MACD의 EMA 9, 히스토그램 = MACD − 시그널이다. 히스토그램은 0 이상이면 `히스토그램 +`, 아래면 `히스토그램 −`의 색 막대로 선들 뒤에 그린다.
      - MFI(`mfi`): 14, (고가+저가+종가)/3, 가이드선 80·20. 기준값 × 거래량을 기준값이 앞 봉보다 오른 봉은 양, 내린 봉은 음으로 14봉 더해 100 × 양 ÷ (양 + 음)이다. 둘 다 0이면 비운다.
      - OBV(`obv`): 설정 없음. 종가가 앞 봉보다 오르면 거래량을 더하고, 내리면 빼고, 같으면 그대로 누적한다(아래 누적 지표의 0점).
      - ROC(`roc`): 9, 종가, 가이드선 0. 100 × (기준값 − 9봉 전 기준값) ÷ 9봉 전 기준값이다.
      - RSI(`rsi`): 14, 종가, 가이드선 70·30. 기준값이 앞 봉보다 오른 폭과 내린 폭을 각각 14봉 Wilder 평균해 100 − 100 ÷ (1 + 오른 폭 평균 ÷ 내린 폭 평균)이다. 내린 폭이 없으면 100, 오른 폭이 없으면 0이다(TradingView와 같다).
      - RMI(`rmi`): 14, 모멘텀 5, 종가, 가이드선 70·30. RSI와 같되 오른 폭과 내린 폭을 기준값 − 5봉 전 기준값으로 센다.
      - EMA와 Wilder 평균은 처음 N개 값의 단순 평균에서 시작하고, 그 뒤는 EMA가 2/(N+1), Wilder가 1/N만큼 다가간다. 앞 기간이 모자란 앞부분은 비운다. 값은 거래량·OBV·AD 라인·체이킨 오실레이터·모멘텀이 정수(앞의 넷은 축에서 1.2M처럼 줄인다), ATR이 소수 1자리, %B와 트릭스가 3자리, 나머지가 2자리다.
      - **누적 지표의 0점:** AD 라인과 OBV는 어디서부터 세느냐에 따라 값이 달라진다. 그래서 VWAP처럼 날마다 다시 세지 않고, 읽은 봉에 기대지 않는 0점을 둔다. 0점은 시각이다: 분 보기는 마지막 봉 날짜의 00:00(KST), 일·주·월·년 보기는 마지막 봉 해의 1월 1일 00:00이다(`ChartStudies::cumulative_zero`). 그 시각부터의 첫 봉(보통 그날 08:00 봉, 그해 첫 거래일 봉)이 0이고, 그 뒤 봉은 그다음 봉부터 더하고, 그 앞 봉은 그 봉까지의 것을 거꾸로 뺀다(`cumulative_anchor`).
        - 그 시각부터의 봉을 모두 읽었을 때만 그린다. 그 시각보다 앞의 봉도 읽었거나, 엔진에 더 앞의 봉이 없을 때다. 그 전에는 비우고, 범례의 값 자리에 "그날 첫 봉을 읽지 않아 비움"(일·주·월·년 보기는 "그해 첫 봉을 …")을 보인다. 그렇지 않으면 해(날)를 중간부터 읽었을 때 읽은 첫 봉이 0이 되고, 왼쪽으로 스크롤해 같은 해(날)의 앞 봉을 읽을 때마다 보이는 값이 모두 바뀐다.
        - 그래서 둘 중 하나를 켜면 그 시각보다 앞의 봉까지 읽는다(아래 앞부분 값). 상한(2,000봉)에 닿아도 그 시각에 못 미치면 비운 채 "그해 첫 봉이 2,000봉보다 앞이라 비움"(분 보기는 "그날 …")을 보인다.
        - 그린 값은 앞의 봉을 더 읽어도 바뀌지 않는다. 날(분 보기)이나 해가 바뀌면 0점이 옮겨 선 전체가 한 번 오르내린다(모양은 그대로다).
        - 체이킨 오실레이터는 두 EMA의 차이라 0점과 상관없이 늘 그린다.
    - **일별 수급 8종**(codeyoma/krx-quant#220, #226): 엔진이 KIS와 토스증권에서 날마다 받는 값(`GET /v1/symbols/{s}/supply`, krx-quant `docs/engine-api.md`)을 그린다. 화면이 세지 않고, 설정은 선의 색(과 굵기)뿐이다. 목록의 `일별 수급` 칸에 있다. 괄호는 설정과 페이지에 쓰는 이름이고, 엔진 API의 필드 이름과 같다:
      - 공매도 거래량(`short_volume`): 그날 KRX의 공매도 체결 주식 수. 한 색 막대.
      - 개인 순매수량(`individual_net`), 기관 순매수량(`institution_net`), 외국인 순매수량(`foreign_net`): 그날 산 주식 수 − 판 주식 수(KRX+NXT 통합, 기관은 기관계, 외국인은 등록외국인과 기타외국인). 0에서 위(`순매수` 색, 초록)·아래(`순매도` 색, 빨강) 막대와 0의 가이드선.
      - 외국인 보유수량(`foreign_holding`), 외국인 지분율(`foreign_ratio`, 상장주식수 대비 %, 소수 2자리), 대차잔고(`lending_balance`), 신용잔고(`credit_balance`, 결제일의 잔고): 계단 선(Lightweight Charts의 `LineType.WithSteps`, 굵기 2).
      - 주식 수는 축에서 1.2M처럼 줄이고, 범례는 천 단위 쉼표의 정수다.
      - **봉에 놓기**(`ChartStudies::supply_values`): 분 보기(1–240분)는 봉마다 그 봉의 KST 날짜 값이라 그날의 봉이 모두 같은 값이고, 날이 바뀌면 계단이 진다. 일 보기는 봉마다 그날 값이다. 주·월·년 보기에서는 흐름(공매도 거래량, 개인·기관·외국인 순매수량)은 그 기간에 값이 있는 날의 합이고, 수준(외국인 보유수량·지분율, 대차잔고, 신용잔고)은 그 기간에서 값이 있는 마지막 날의 값이다(사용자가 2026-10-17에 정했다, `ChartStudies::supply_flow`). 지금 기간은 지금까지의 합과 마지막 날이다. 값이 하나도 없는 기간은 비운다(0이 아니다). 합은 읽은 값이 그 기간의 첫날 앞까지 닿을 때만 그린다(그 기간 앞의 날에 값이 있을 때). 그렇지 않으면 읽기나 엔진의 이력(10년)이 기간 중간에서 시작했을 수 있어 일부만의 합이 되므로 그 기간을 비운다. 그래서 주·월·년 보기는 첫 봉 14일 앞부터 읽는다(아래 읽기, 거래가 없는 가장 긴 연휴보다 길다). 엔진의 이력이나 한 번에 4,000일의 한도가 기간 중간에서 시작하면 그 첫 기간만 빈다. 수준은 첫 기간에도 그날까지의 마지막 값을 그린다. 값이 없는 날(엔진에 없는 날, 오늘)의 봉에는 점이 없다. 엔진은 그날 값을 장이 끝난 뒤(20:30부터, 토스 값은 다음 날 아침) 받으므로 오늘의 봉에는 값이 없다.
      - **범례:** 십자선이 없으면 마지막 봉(오늘, 값 없음) 대신 그 선의 마지막 값을 보인다. 십자선 위치에 값이 없으면 "그날 값 없음"이다.
      - **읽기:** `KrxLive`가 고른 종목의 보기에 읽은 봉의 첫 날(분 보기는 첫 1분봉, 일 보기는 첫 봉, 주·월·년 보기는 첫 봉의 14일 앞)부터 엔진의 오늘까지(한 번에 4,000일까지, 그보다 앞의 봉에는 값이 없다)를 `?from=&to=`로 묻는다. 종목·보기를 바꾸거나 앞의 봉을 더 읽어 날이 바뀌면 곧바로, 화면이 보이는 동안 5분마다 다시 읽는다. 지표를 켜지 않아도 읽는다(그 응답이 있는 엔진인지 알려고). 읽기에 실패하면 그때까지의 값을 두고 다음에 다시 묻는다. 엔진을 읽지 못하거나 종목·엔진 주소를 바꾸면 지운다. 읽는 동안 보기를 바꾸거나 앞의 봉을 더 읽어 다른 날들이 필요해졌는데 그 읽기가 실패하면, 지금 필요한 날들을 곧바로 다시 묻는다(지금 필요한 날들의 읽기가 실패하면 5분 뒤).
      - **그 응답이 없는 엔진**(404, krx-quant #226 전): 8종을 목록에서 흐리게 두어 켤 수 없고, 툴팁과 설정 칸에 "이 엔진은 일별 수급을 주지 않는다"를 보인다. 켜 둔 것은 그리지 않고 범례에도 없지만, 켠 것과 설정은 남는다(분봉의 VWAP처럼). 엔진 주소를 바꾸면 다시 묻는다.
      - 시간축 없는 차트(codeyoma/krx-quant#225)에서는 그리지 않는다(목록에서 흐림, 툴팁 "시간축 없는 차트에서는 쓰지 않는다"). 벽돌에는 날의 값을 놓을 자리가 없다.
      - 토스증권의 값은 사용자 자신의 매매에만 쓴다(토스 이용 정책). 화면 캡처는 데모 엔진의 지어낸 값으로만 찍는다.
    - **하단 칸**(codeyoma/krx-quant#223): 하단 지표마다 가격 칸 아래에 칸 하나를 켠 순서대로 쌓는다(마지막에 켠 것이 맨 아래). 끄면 그 칸이 없어지고 아래 칸들이 올라온다. 범례에서 숨긴 지표의 칸은 비운 채 남고, 범례 줄만 흐리게 남는다.
      - 칸 높이는 처음 100px이다. 칸 경계를 드래그해 바꾸고(40–1000px), 놓으면 그 종목의 `krx_quant/chart/<종목코드>/pane_heights`에 남는다. 끈 지표의 높이도 남아 다시 켜면 그 높이다.
      - 가격 칸은 남은 높이를 쓰되 200px 아래로는 줄지 않는다. 하단 칸이 많아 모자라면 차트가 패널보다 길어지고, 오른쪽 세로 스크롤 막대로 내려 본다(휠은 지금처럼 차트를 줌한다).
      - 가이드선은 그 칸의 첫 선에 회색 점선으로 긋고(축 글자는 없다), 값이 닿지 않아도 보이게 그 칸 축의 범위에 넣는다.
      - 날짜 구분선의 띠는 하단 칸에도 칠하고, 1분 보기의 호버는 하단 칸에서도 그 봉의 상자를 보인다.
    - **범례:** 칸마다 왼쪽 위(가격 칸, 하단 칸마다)에 켠 지표마다 한 줄이다. 이름, 설정값(`5 20 60 120`, `20 2`, `20 10%`. 기준값이 기본과 다르면 그 이름도), 선마다 십자선 위치의 값(십자선이 없으면 마지막 봉. 일별 수급 지표는 그 선의 마지막 값, 천 단위 쉼표, 상단 지표는 정수, 하단 지표는 위의 자리수, 선의 색)을 보인다. 값이 없는 선은 건너뛴다. VWAP의 값이 없는 날(다 읽지 않은 날)에는 그 이유를 보인다. 고정된 VWAP의 설정값 자리는 기준 시각(`10-12 09:30`)이다. 줄에 마우스를 올리면 숨기기(눈)·설정(패널을 그 지표를 고른 채 연다)·삭제(끈다) 버튼이 나온다. 숨긴 지표는 켠 채로 흐린 이름과 설정값만 남고 그리지 않는다. 좁으면 줄이 접힌다.
      - **가격 줄**(codeyoma/krx-quant#285): 가격 칸 범례의 첫 줄은 지표 줄 위에 늘 있고, 십자선 위치의 봉(십자선이 없으면 마지막 봉)의 값을 토스증권 차트처럼 보인다: `시가 216,000원 (0.00%) 고가 216,500원 (+0.23%) 저가 216,000원 (0.00%) 종가 216,500원 (+0.23%)`. 값은 천 단위 쉼표와 `원`이고, 괄호 안은 그 값의 앞 봉 종가 대비 등락률(소수 둘째 자리, 부호)이다. 앞 봉은 체결이 있는 바로 앞 봉이다(빈 봉, codeyoma/krx-quant#236은 건너뛴다). 그래서 일 보기에서는 전일 종가 대비다. 등락률은 오르면 상승색, 내리면 하락색, 0.00%면 축 글자색이고, 이름은 흐리다. 읽은 첫 봉은 앞 봉이 없어 등락률이 없다. 빈 봉에 올리면 `체결 없음`이다. 하이킨아시는 그린 평균 봉이 아니라 원래 봉의 값이고, 시간축 없는 차트(codeyoma/krx-quant#225)는 벽돌마다의 값이다. 만들어지는 봉이 움직이면 같이 바뀐다. 버튼이 없고 마우스를 받지 않는다(아래 차트가 받는다). 봉이 없으면 줄도 없다.
    - **앞부분 값:** 켠 지표(숨긴 것 포함, 그 보기에서 그리지 않는 VWAP 제외)의 앞 기간 중 가장 긴 것만큼 첫 화면(최근 60봉) 앞의 봉을 더 읽어, 첫 화면 왼쪽 끝부터 값이 나오게 한다. 앞 기간은 이동평균선이 가장 긴 기간 − 1, 볼린저·엔벨로프·프라이스 채널이 기간 − 1, 일목이 max(전환, 기준, 선행) − 1 + 이동(기본 77), 프랙탈이 기간이다. 앞의 모든 봉에 기대는 재귀 지표는 더 읽어도 보이는 값이 거의 바뀌지 않을 만큼이다: 지수이동평균선은 가장 긴 기간의 4배(그 너머에 남는 무게는 약 e^-8, 기본 480), 슈퍼트렌드는 ATR 기간의 10배(Wilder ATR과 추세가 이어진다, 기본 100), 파라볼릭 SAR는 100봉이다. 그래도 TradingView처럼 앞의 봉을 더 읽으면 조금 바뀔 수 있다. 하단 지표(codeyoma/krx-quant#223)는 계산 단계마다 더한다: EMA는 기간의 4배, Wilder 평균(RSI, RMI, 스토캐스틱 RSI의 RSI, ATR, DMI·ADX)은 기간의 8배(같이 약 e^-8), 고정 기간은 기간 − 1(차이를 보는 모멘텀·ROC·MFI는 기간), 나란한 둘은 긴 쪽이다. 기본값으로 매스 인덱스 96, 모멘텀 10, 볼륨 오실레이터 40, %B·밴드폭 19, 스토캐스틱 17, 스토캐스틱 RSI 129, 윌리엄스 R 13, 이격도 19, 일중 강도 지수 20, 체이킨 오실레이터 40, 트릭스 252, 프라이스 오실레이터 20(지수면 84), ADX·DMI 224, ATR 112, CCI 19, MACD 140, MFI 14, ROC 9, RSI 112, RMI 117이다. 테스트에서 그 앞에 1,000봉을 더 읽어도 보이는 60봉의 값은 그 범위의 0.1%보다 덜 바뀌었다(RSI가 가장 커 약 0.03%). AD 라인이나 OBV를 켜면 앞 기간 대신 위의 0점 시각(마지막 봉 날짜의 0시, 일·주·월·년 보기는 그해 1월 1일)보다 앞의 봉까지 읽는다(`ChartStudies::cumulative_start`, `KrxLive::set_history`). 이 목표는 VWAP의 것처럼 종목과 보기마다 처음 읽은 봉으로 한 번 정하고, 읽은 첫 봉으로 다시 정하지 않는다. 나머지는 0이다. VWAP를 켠 분 보기는 첫 화면(최근 60봉)의 가장 이른 날짜 0시보다 앞의 봉(그 전 날짜)까지 읽어, 화면의 날들이 다 있게 한다(`ChartStudies::first_screen_day`). 이 목표는 종목과 보기마다 첫 화면에서 한 번 정하고, 읽은 첫 봉으로 다시 정하지 않는다(그러면 읽을 때마다 뒤로 물러난다). 고정된 VWAP의 기준 봉도 같은 방식으로 읽는다. 그 보기의 봉이 60 + 앞 기간이 되고 위의 날짜·기준 봉·0점 시각까지 닿을 때까지(상한 2,000봉, 엔진 `limit`의 최대) 지난 날 스크롤과 같은 읽기(분 보기는 1분봉 400개나 100×N개 중 많은 쪽, 최대 1,000개. 일·주·월·년 보기는 400개)를 하나씩 이어 한다. 엔진에 더 앞이 없으면 멈추고, 그래도 모자란 앞부분은 비운다. 기본값(이동평균선 120)이면 1분 보기에서 179봉이다(`KrxLive::set_history`). 상한에 닿으면 그 앞은 읽지 않는다.
    - **실시간:** 지금 봉이 1초마다 움직이면(`bar` 이벤트) 지표를 다시 세어, 선마다 지금 봉이 바꾸는 점 하나(보통 마지막 봉, 선행스팬은 26봉 앞, 후행스팬은 26봉 뒤)만 페이지에서 움직인다. 프랙탈과 매물대는 다시 보낸다. 봉 마감, 더 읽기, 설정 변경 때는 모두 다시 센다.
    - 지표는 원래 봉(하이킨아시라도 원래 봉의 시가·고가·저가·종가·거래량)으로 센다. 분 보기의 빈 봉은 세지 않고, 선은 빈 봉을 건너 잇는다(위 빈 봉, codeyoma/krx-quant#236). 시간축 없는 차트만 벽돌로 센다(codeyoma/krx-quant#225, 위 차트 모양. VWAP 둘은 그리지 않고, AD 라인과 OBV는 첫 벽돌이 0이다). 계산, 저장 형식, 그릴 것과 범례, 더 읽을 봉 수는 `services/krx_quant/ChartStudies`(순수 Qt Core 함수, `tst_krx_chart_studies`)이고, upstream `ChartIndicators`는 쓰지 않는다. 페이지는 받은 선(`LineSeries`, SAR는 점), 두 선 사이 채우기, 프랙탈 마커, 매물대 막대와 고정 VWAP 기준 표시(프리미티브), 하단 칸(지표마다 `addPane`, 거래량과 MACD 히스토그램은 `HistogramSeries`, 가이드선은 `createPriceLine`), 범례만 그린다. 칸 경계를 드래그하면 하단 칸의 높이를 화면에 알린다.
    - 설정 키: `krx_quant/chart/<종목코드>/indicators`는 JSON 배열이다. 지표마다 `{"study", "on", "hidden", "source", "inputs", "lines": [{"color", "width"}]}`이고, 켠 것이 켠 순서대로 앞에 온다(하단 칸의 순서). `study`는 위 괄호의 이름(상단 12종, 하단 25종), `inputs`는 패널의 숫자 순서(이동평균선은 기간들, 프라이스 오실레이터의 `방식`은 0 단순·1 지수), `color`는 `#rrggbb`, `width`는 1–4(마커, 매물대, MACD 히스토그램은 0)다. `krx_quant/chart/<종목코드>/pane_heights`는 하단 칸 높이의 JSON 객체다(예: `{"rsi":140,"volume":80}`, CSS 픽셀, codeyoma/krx-quant#223). 드래그한 칸만 있고, 없는 칸은 100px이다. 하단 지표가 아닌 이름, 40–1000의 정수가 아닌 값은 버린다. `krx_quant/chart/<종목코드>/vwap_anchor`는 ISO 8601 시각이다(예: `2026-10-12T09:30:00+09:00`). 처음 여는 종목(키 없음)이나 JSON 배열이 아닌 값은 이동평균선(5·20·60·120, 종가)과 거래량만 켠 기본값이다. 모르는 지표는 버리고, 없는 지표는 처음 여는 종목의 기본값이다. 범위 밖의 숫자, `#rrggbb`가 아닌 색, 1–4가 아닌 굵기, 모르는 기준값은 그 칸의 기본값이다.
  - **그리기**(codeyoma/krx-quant#220, #224): 툴바의 `그리기`를 누르면 메뉴가 열린다: 추세선(`trend_line`), 반직선(`ray`), 수평선(`horizontal`), 수직선(`vertical`), 크로스(`cross`), 사각형(`rectangle`), 타원(`ellipse`), 피치포크(`pitchfork`), 피보나치 되돌림(`fib_retracement`), 피보나치 채널(`fib_channel`), 피보나치 속도 저항 팬(`fib_fan`), 텍스트(`text`). 괄호는 설정과 페이지에 쓰는 이름이다. 그다음 구분선과 체크 항목 `자석`이 있다. 고른 도구와 켠 자석은 주황 굵은 글씨다. 항목의 툴팁이 그 도구가 그리는 것이다.
    - **단축키**(codeyoma/krx-quant#241): 키보드가 KRX 화면 안에 있을 때(차트나 목록을 누른 뒤) Cmd+D(윈도우는 Ctrl+D)를 누르면 `그리기` 버튼을 누른 것처럼 메뉴가 열리고, 열린 채로 다시 누르면 닫힌다. 열린 메뉴에서 글자 하나로 도구를 고르거나 자석을 켜고 끈다(메뉴가 닫히고, 클릭과 같다): T 추세선, R 반직선, H 수평선, V 수직선, C 크로스, B 사각형, E 타원, P 피치포크, F 피보나치 되돌림, N 피보나치 채널, A 피보나치 속도 저항 팬, X 텍스트, M 자석. 글자는 메뉴 항목 오른쪽에 보인다. 한글 입력 상태에서도 같은 자리의 키로 고른다(ㅅ 자리는 T). 다른 글자는 메뉴를 그대로 두고, Esc는 닫는다. 시간축 없는 모양에서는 `그리기`가 비활성이라 열리지 않는다. `그리기` 버튼의 툴팁에 단축키(`⌘D`, `Ctrl+D`)가 있다. 키보드가 다른 패널에 있으면 열리지 않는다(같은 창의 다른 화면과 겹치지 않게).
    - **그리기:** 도구를 고르면(메뉴 클릭이든 단축키 글자든) `그리기` 버튼이 주황으로 채워지고 그 도구의 아이콘과 이름(예: `반직선`)이 된다(codeyoma/krx-quant#248). 그려지거나 Esc로 취소해 커서로 돌아가면 다시 `그리기`다. 가격 칸을 도구가 받는 수만큼 누르면(수평선·수직선·크로스·텍스트 1, 피치포크·피보나치 채널 3, 나머지 2) 그려지고, 커서로 돌아가며, 그린 것이 골라진 채 남는다. 한 점에서 다음 점까지 드래그해도 두 점이 찍힌다. 찍는 동안 다음 점은 마우스를 따라 미리 그려진다. Esc로 취소한다(도구를 고르면 차트가 키보드를 받는다). 도구를 고른 동안 가격 칸의 클릭은 그리기라 아이콘을 눌러도 고정되지 않는다. 호버 상자, 4분 구간, 하단 칸, 칸 경계 드래그, 휠 줌은 그대로다.

      > **바뀜** (2026-10-16, codeyoma/krx-quant#248): 도구를 고른 동안에도 버튼의 글자는 `그리기`였다(아이콘만 그 도구) → 글자도 그 도구의 이름이다. 이유: Cmd+D와 글자로 도구를 고른 뒤 무엇을 골랐는지 버튼에서 바로 읽으려고(사용자 요청, 2026-10-16).

    - **모양:**
      - 추세선은 두 점을 잇는 선, 반직선은 첫 점에서 둘째 점 쪽으로 가격 칸 끝까지 뻗는 선이다.
      - 수평선은 그 가격의 가로선이고 가격 축에 가격이 나온다. 수직선은 그 시각의 세로선이고 시간 축에 시각(`10-12 09:54`, 일·주·월·년 보기는 날짜)이 나온다. 크로스는 둘 다다.
      - 사각형은 두 점을 마주 보는 꼭짓점으로 하고, 타원은 그 사각형 안에 그린다. 둘 다 안쪽을 옅게 칠한다.
      - 피치포크(앤드루스): 첫 점에서 둘째·셋째 점의 가운데를 지나 뻗는 중심선, 둘째·셋째 점에서 중심선과 나란히 뻗는 두 갈래, 둘째와 셋째 점을 잇는 선이다.
      - 피보나치 되돌림: 두 점의 가로 폭에 0·0.236·0.382·0.5·0.618·0.786·1 가로선을 긋고, 선 위 왼쪽에 비율과 가격(`0.618 (78,545)`)을 적는다. 0이 둘째 점, 1이 첫 점이다(TradingView와 같다). 두 점을 잇는 점선과 옅은 칠이 있다.
      - 피보나치 채널: 첫 두 점의 선이 0, 셋째 점을 지나는 나란한 선이 1이고, 그 사이에 같은 비율의 나란한 선을 긋는다(둘의 거리는 셋째 점의 가격 차이로 잰다).
      - 피보나치 속도 저항 팬: 첫 점에서, 두 점이 이루는 상자의 둘째 점 쪽 세로변 위 0.25·0.382·0.5·0.618·0.75·1 가격 지점과 둘째 점 가격의 가로변 위 0.25–0.75 시간 지점으로 뻗는 선들이다. 상자는 점선이다. 팬이 좁아 비율 글자가 겹치면 겹치는 것은 적지 않는다.
      - 텍스트: 점의 오른쪽에 쓴다. 굵기가 글자 크기다(1–4: 12·14·17·20px).
    - **고치기:** 그린 것을 누르면 골라지고 점마다 손잡이(동그라미)가 보인다. 그린 것을 드래그하면 통째로, 손잡이를 드래그하면 그 점만 옮긴다(옮기는 동안 차트는 스크롤되지 않는다). 나중에 그린 것이 먼저 잡힌다. 사각형·타원·피보나치 되돌림과 채널은 안쪽을 눌러도 잡힌다. 다른 곳을 누르면 고른 것이 풀리고, 그 클릭은 차트의 것이다(스크롤, 아이콘 고정).
      - 더블클릭하면 그 자리에 팝업(`screens/krx_quant/KrxDrawingPopup`)이 열린다: 색(보조지표와 같은 팔레트 18색, `screens/krx_quant/KrxSwatches.h`), 굵기(1–4px, 텍스트는 크기), 선 종류(실선·점선·파선, 텍스트에는 없음), `삭제`. 바꾸면 바로 그려지고 남는다. 팝업 밖을 누르거나 Esc로 닫는다. 그린 것 위의 더블클릭은 봉 창을 열지 않는다(codeyoma/krx-quant#255).
      - 고른 것은 Delete나 Backspace로 지운다.
      - 텍스트는 찍으면 그 자리에 같은 팝업이 입력칸과 함께 열리고, 입력칸의 "텍스트"가 골라져 있어 바로 쓴다. 글쇠마다 차트에 보이고 Enter로 닫는다. 더블클릭하면 다시 고친다. 비운 채 닫으면 지운다. 200자까지다.
      - 처음 그린 것은 파랑(`#60A5FA`), 실선, 굵기 2(피보나치 셋은 1)다.
    - **자석:** 켜면 찍는 점과 드래그하는 손잡이가 그 아래 봉의 가운데와, 그 봉의 시가·고가·저가·종가 중 가장 가까운 값에 붙는다(같으면 시가, 고가, 저가, 종가 순). 하이킨아시 차트에서도 원래 봉의 값이다(지금 만들어지는 봉의 원래 값도 실시간으로 따라온다). 마지막 봉 뒤 빈칸과 통째로 옮길 때는 붙지 않는다. 모든 종목에 하나이고 앱 설정 `krx_quant/chart_magnet`(기본 끔)에 남는다. 페이지는 미리보기에 같은 규칙을 쓰고, 화면이 받은 점에 다시 적용한다(`ChartDrawings::magnet_spot`).
    - **좌표:** 점은 시각(초)과 가격이다. 그래서 보기(1–240분, 일·주·월·년)를 바꿔도 같은 시각·가격에 그린다.
      - 봉 안의 자리는 그 봉의 장 시간(평일 08:00–20:00) 중 얼마나 지났는지다. 1분봉은 시계 그대로, 일봉은 08:00이 왼쪽 끝, 14:00이 가운데, 20:00이 오른쪽 끝이고, 주·월·년봉은 그 기간의 평일 장 시간으로 나눈다. 자석으로 붙인 점은 봉의 가운데(1분봉은 그 분의 30초, 일봉은 14:00)다.
      - 밤처럼 봉 사이의 시각은 두 봉의 경계에 놓인다. 분 보기에서 그날 안의 거래 없는 분은 빈 봉이라(위 빈 봉, codeyoma/krx-quant#236) 그 칸에 놓인다.

        > **바뀜** (2026-10-16, codeyoma/krx-quant#236): 거래가 없는 분의 시각도 두 봉의 경계에 놓였다 → 분 보기에서는 빈 봉의 칸에 놓인다. 이유: 체결이 없는 분이 빈 봉으로 칸을 갖게 됐다(서킷브레이커로 30분 동안 거래가 없던 것이 차트에서 사라졌다).
      - 읽은 첫 봉 앞이나 마지막 봉 뒤는, 그 사이의 장 시간(밤과 주말은 빼고, 휴장일은 모른다)을 그 보기의 봉 길이로 센 자리다. 그래서 읽지 않은 봉 위의 점은 어림이다. 왼쪽으로 스크롤해 그 봉을 읽으면 제자리에 간다. 그린 것 때문에 앞의 봉을 더 읽지는 않는다. 지금 만들어지는 봉이 새로 붙으면 그 봉까지의 자리(밤, 주말)가 그 봉 하나가 되므로, 화면이 그린 것을 다시 보내 마지막 봉 뒤의 점이 제 시각의 자리로 옮긴다(`ChartDrawings::append_live`). 그날 안의 거래 없는 분은 그 앞에 빈 봉으로 채우므로 그 사이의 점은 옮기지 않는다(`ChartSlots::append_live_slots`).

        > **바뀜** (2026-10-16, codeyoma/krx-quant#236): 새 봉이 붙으면 그 봉까지의 거래 없는 분도 그 봉 하나가 되어 마지막 봉 뒤의 점이 옮겼다 → 분 보기에서 그날 안의 거래 없는 분은 빈 봉으로 먼저 채우고, 밤과 주말만 그 봉 하나가 된다. 이유: 위와 같다.
      - 무엇이 어디에 놓이는지는 `ChartDrawings::logical_at`·`time_at`(순수 함수)이 정한다. 페이지는 봉 위치(`[logical, price]`)로만 일하고, 화면이 시각으로 바꿔 남긴 뒤 다시 보낸다.
    - 시간축 없는 모양(codeyoma/krx-quant#225)에서는 그린 것을 숨기고(지우지 않는다) `그리기`를 비활성으로 둔다(툴팁 "그리기: 시간축 없는 차트에서는 쓰지 않는다"). 고른 도구와 팝업은 내려놓는다. 휴지통도 비활성이다(툴팁 "그린 것 모두 지우기: 시간축 없는 차트에서는 그린 것을 숨긴다"): 보이지 않는 것을 지우지 않게. 시간축 차트로 돌아오면 그린 것이 같은 시각·가격에 다시 보인다.

      > **바뀜** (2026-10-15, codeyoma/krx-quant#225): "지금은 그 모양을 고를 수 없다" → 고를 수 있고, 그 동안 휴지통도 비활성이다. 이유: 시간축 없는 4종을 그리게 됐다. 휴지통은 숨긴 그린 것을 보지 않고 지우지 않게 했다.
    - **휴지통:** 지금 종목의 그린 것을 모두 지운다. "그린 것 N개를 지울까요?"를 묻고 `Yes`일 때만 지운다. 그린 것이 없으면 비활성이고, 툴팁에 개수가 있다.
    - 종목을 바꾸면 그 종목의 그린 것으로 바뀌고, 고른 도구와 팝업은 내려놓는다. 종목을 고르기 전에는 남기지 않는다.
    - 설정 키: `krx_quant/chart/<종목코드>/drawings`는 그린 순서의 JSON 배열이다. 하나마다 `{"tool", "points": [{"time", "price"}], "color", "width", "style"}`이고, 텍스트에는 `"text"`가 더 있다. `tool`은 위 괄호의 이름, `time`은 KST의 ISO 8601(예: `2026-10-12T09:45:31+09:00`), `color`는 `#rrggbb`, `width`는 1–4, `style`은 `solid`·`dotted`·`dashed`다. 읽을 때 객체가 아닌 것, 모르는 도구, 도구가 받는 수와 점 수가 다른 것, ISO 8601이 아닌 시각이나 유한한 숫자가 아닌 가격이 있는 것, 빈 텍스트는 그 하나를 버린다. `#rrggbb`가 아닌 색, 1–4의 정수가 아닌 굵기, 모르는 선 종류는 그 도구의 처음 값이다. 배열이 아니면 그린 것이 없다.
    - 나누기: 도구, 저장 형식, 시각과 봉 위치, 자석, 도구의 글자와 누른 키의 글자(`tool_letter`, `typed_letter`, codeyoma/krx-quant#241)는 `services/krx_quant/ChartDrawings`(순수 Qt Core 함수, `tst_krx_chart_drawings`)다. Cmd+D와 메뉴의 글자는 `KrxChartToolbar`(`take_keys`, 화면 전체에 `Qt::WidgetWithChildrenShortcut`)가 받는다(`tst_krx_chart_toolbar`). 페이지는 받은 그린 것을 가격 칸의 시리즈 프리미티브(축 라벨 포함)로 그리고, 찍기·고르기·드래그·더블클릭·키를 맡아 bridge(`drawn`, `reshaped`, `erased`, `edit`, `tool_ended`)로 봉 위치를 화면에 알린다. 페이지의 봉 위치는 마지막 봉의 시각에서 잰다(Lightweight Charts의 `coordinateToLogical`은 봉 사이를 주지 않는다). 화면(`KrxQuantScreen`)이 시각으로 바꾸고 저장해 다시 보낸다.
  - 분 단위 보기(codeyoma/krx-quant#109): 툴바의 분 드롭다운 `1분 ▾`(1·3·5·10·15·30·60·120·240분)으로 N분봉 보기를 고른다. 단추 글자는 마지막으로 고른 분이고, 분 보기일 때만 주황으로 채운다. 고른 보기는 종목별로 남는다(위).

    > **바뀜** (2026-10-15, codeyoma/krx-quant#221): 차트 위 한 줄의 버튼 "분 1 3 5 10 15 30 60 120 240" → 드롭다운 하나. 이유: 버튼 9개가 한 줄을 차지해 좁은 패널에서 오른쪽 버튼이 잘렸고, 차트 모양·보조지표·그리기 버튼이 들어갈 자리가 필요했다(codeyoma/krx-quant#220).
    - 1분봉을 화면에서 N분봉으로 묶는다. 시계 기준(KST)이다: 60분봉은 09:00, 10:00…, 240분봉은 08:00, 12:00, 16:00에 시작한다. 모든 N이 하루를 나누므로 봉이 날을 넘지 않는다. 시가는 첫 1분봉의 시가, 고가·저가는 가장 높고 낮은 값, 종가는 마지막 1분봉의 종가, 거래량은 합이다. 거래가 없어 빠진 분은 건너뛰고, 1분봉이 하나도 없는 N분은 봉이 없다. 그 N분이 그날 첫 봉과 마지막 봉 사이면 차트에서는 빈 봉으로 칸을 둔다(위 빈 봉).

      > **바뀜** (2026-10-16, codeyoma/krx-quant#236): 1분봉이 하나도 없는 분(N분)은 차트에 자리가 없어 시각이 건너뛰었다 → 그날 첫 봉과 마지막 봉 사이면 빈 봉으로 칸을 둔다. 이유: 2026-10-15 장중에 서킷브레이커로 30분 동안 거래가 없었는데, 차트에서 그 30분이 사라져 매매가 멈췄던 것을 알 수 없었다(사용자 요청, 2026-10-16).
    - 3분 이상 보기에는 아이콘·클릭과 실제 봉의 예측 호버가 없다(한 봉에 봉 마감이 여럿이다). 빈 봉과 특수 상황 띠 안의 봉에는 호버 상자가 있다(위 빈 봉, 특수 상황 표시). 차트 제목은 "005930 · 15분봉"이다. 보기를 바꾸면 최근 60봉에 맞춘다.

      > **바뀜** (2026-10-16, codeyoma/krx-quant#236): 3분 이상 보기에는 호버 상자가 없었다 → 빈 봉("체결 없음")과 특수 상황 띠 안의 봉에는 있다. 이유: 멈춘 시간과 그 까닭을 N분 보기에서도 보이려고.
    - 왼쪽 끝 앞에 한 화면보다 적게 남으면 그 보기의 300봉어치 1분봉을 더 읽는다(`older_read`). 1분봉으로 최소 1,000개, 최대 20,000개(엔진이 한 번에 주는 만큼)다: 1·3분 1,000, 5분 1,500, 10분 3,000, 15분 4,500, 30분 9,000, 60분 18,000, 120·240분 20,000. 3분 이상 보기는 아이콘이 없으므로 예측을 읽지 않는다.

      > **바뀜** (2026-10-16, codeyoma/krx-quant#239): 그 보기의 100봉만큼(1분 400, 5분 500, 한 번에 최대 1000) 읽고 최종 합의도 같이 읽었다 → 300봉어치(1,000–20,000)를 읽고 예측은 읽지 않는다. 이유: 60분 이상 보기는 한 번에 16봉 이하만 와서 여러 번 이어 읽었다. 최대 1000은 엔진이 한 번에 주는 최종 합의 수였는데, 이제 예측은 봉과 따로 읽는다. 20,000개는 엔진 `bars`의 `limit` 최대값이다(krx-quant #240).
  - 일 보기(codeyoma/krx-quant#111): 분 드롭다운 뒤의 **일** 버튼. 엔진이 KIS에서 받아 저장한 일봉(`GET /v1/symbols/{s}/candles?period=day`, 수정주가)을 그린다. 고르면 그 종목의 보기에 1440(하루의 분)으로 남는다.
    - 처음에 최근 400개를 읽고, 왼쪽 끝 앞에 한 화면보다 적게 남으면(최소 30봉, codeyoma/krx-quant#239) 가장 오래된 봉의 날 앞으로 400개씩 더 읽는다. 엔진에 더 없으면 그만 읽는다. KIS에 있는 만큼(몇 년 전까지) 거슬러 간다.

      > **바뀜** (2026-10-16, codeyoma/krx-quant#239): 왼쪽 끝이 첫 봉에서 30봉 안으로 와야 더 읽었다 → 한 화면 너비 안으로 오면 읽는다(분 보기와 같은 기준). 이유: 분 보기처럼 끝에 닿기 전에 미리 읽으려고.
    - 오늘의 봉은 엔진이 주지 않는다. 화면이 오늘의 1분봉과 실시간 봉으로 만들어, 실시간으로 움직인다. 엔진의 일봉이 통합(`UN`)이면 그날의 모든 1분봉, KRX면 09:00–15:30 봉으로 만든다. KRX일 때 거래량은 통합 1분봉에서 온 근사치다.
    - 방향(과 다른 봉 마감 종류)을 고른 동안은 아이콘·호버·클릭이 없다. 차트 제목은 "005930 · 일봉"이고, 시간 축과 십자선은 날짜만 보인다.

      > **바뀜** (2026-10-18, codeyoma/FinceptTerminal#92): "아이콘·호버·클릭은 없다" → 시가→종가를 고른 동안은 있다(아래). 이유: 위 예측 종류의 바뀜과 같다.
    - **시가→종가 일 보기**(codeyoma/krx-quant#264): 예측 종류에서 시가→종가를 고른 동안 일봉마다 그날 08:50 예측의 아이콘과 호버 상자가 붙는다. 차트 제목은 "005930 · 일봉과 시가→종가 예측"이다.
      - 아이콘은 그날 예측한 알고리즘(기준 예측은 빼고, 지금은 `cox` 하나)의 세 확률을 똑같은 무게로 이름순으로 더해 평균한 것의 예측 클래스(가장 큰 확률, 동률이면 보합, 그다음 상승)다. 엔진은 하루 한 번 예측의 합의를 만들지 않아서다. 1분 보기의 아이콘과 같은 모양이다: 상승 ▲(초록), 하락 ▼(빨강), 보합 ●(회색)을 그날 봉 위에, 그 위에 결과 ✓ 맞음, ✗ 틀림, ? 채점 전(그날 15:45 전)이나 채점 불가. 서킷브레이커·사이드카가 있던 날(특수, 통계에서 빠짐)은 흐리게 그린다. 알고리즘이 하나도 예측하지 않은 날(사전 검증을 통과하기 전, 기준 예측만 있는 날)은 아이콘이 없다(호버는 있다). 순위표의 첫 열 **차트**와 선택 합의는 방향의 것이라 쓰지 않는다.
      - 호버 상자: 첫 줄은 "10-08 시가→종가 (08:50 예측) · 정답 하락 -0.85% (81,950 → 81,250) · 보합 ±0.47%"(정답 클래스, 시가 단일가 → 종가 단일가의 변화와 두 가격, 그날의 보합 폭)다. 알고리즘이 둘 이상이면 "알고리즘 평균 (2) · 상승 · 상승 37% · 보합 26% · 하락 37% · ✗" 줄이 이어지고, 그 뒤 예측자마다 한 줄(알고리즘 이름순, 기준 예측 이름순: 이름, 지금 상태(마지막 시가→종가 순위표), 예측 클래스, 세 확률, ✓/✗/?. 예측하지 않았으면 그 이유. 기준 예측 "어제 방향 잇기"의 `no_previous`는 "전 거래일 정답 없음")이다. 그날 예측이 없는 봉에는 상자가 없다. 4분 구간 같은 창은 그리지 않는다(하루는 봉 마감이 아니다).
      - 일 보기의 봉은 엔진의 통합(`UN`) 일봉이라 시가가 NXT 08:00 프리마켓의 시가다(krx-quant `docs/kis-feeds.md`). 정답은 KRX 시장 일봉의 시가 단일가와 종가 단일가로 채점하므로, 호버의 두 가격은 그 정답의 것(엔진의 `path.open`, `path.close`)이고 봉의 시가와 다를 수 있다.
      - 아이콘을 누르면 예측 패널이 그날의 예측으로 고정된다(아래 시가→종가 패널).
      - 머리줄의 성적: 보이는 날 중 정답이 나온 날에 아이콘의 예측이 맞은 비율, 같은 날들에서 "늘 보합"이 맞았을 비율, ▲·▼라고 한 날 중 맞은 비율(괄호는 그 수), 날 수다: "cox · 맞음 20% · 늘 보합 20% · ▲ 맞음 17% (12) · ▼ 맞음 25% (4) · 20일"(알고리즘이 여럿이면 "알고리즘 평균 (2)", 채점된 날이 없으면 "채점된 날 없음"). 특수한 날은 세지 않고 "특수 1일 제외"로 따로 센다. 툴팁이 설명용 성적이고 정식 판정은 순위표와 상태 판정(정답이 있는 날 60일 뒤)으로 한다는 것을 알린다.
      - 주·월·년 보기(한 봉에 날이 여럿)와 시간축 없는 차트에서는 그리지 않는다. 분 보기는 시가→종가를 골라도 방향의 아이콘·호버·선택 합의 성적 그대로다. 하루 한 번 예측은 1분봉에 붙일 봉 마감이 없고, 09:00 봉에 붙이면 같은 봉의 4분 아이콘과 겹쳐서 넣지 않았다.
      - 5거래일 시가→종가(codeyoma/krx-quant#310)도 같은 일 보기다. 그날 봉에 붙는 것은 그날 시작한 창의 예측이다. 호버 상자의 첫 줄은 머리 뒤에 창 끝이 붙는다: "10-05 5거래일 시가→종가 (08:50 예측) · 창 끝 10-12 · 정답 보합 +0.62% (80,750 → 81,250) · 보합 ±1.31%"(시가는 첫 세션, 종가는 마지막 세션의 것). 정답 전에는 "창 끝 10-16 (예정) · 채점 대기 (10-16 20:35부터, 최대 4거래일)"이다. 정답이 나오면 둘째 줄이 다섯 세션의 종가(엔진의 `path.closes`, 그 종목의 날이 없는 세션은 "—")다: "세션 종가 80,350 · 80,450 · 80,850 · 82,250 · 81,250". 배당락 낀 창은 정답 뒤에 " · 배당락 낀 창"이 붙는다(아래 패널). 지금은 알고리즘이 없어(기준 예측만) 아이콘과 성적이 없고 호버만 있다.
      - 아이콘, 호버 줄, 성적은 `KindView`의 `daily_call`·`daily_icons`·`daily_hovers`·`daily_score`·`daily_score_text`(순수 함수, `tst_krx_kind_view`)가 정한다. `KrxChart::set_data`는 일 보기에서 받은 아이콘(`day_icons`)과 상자를 그대로 페이지에 보낸다(`tst_krx_chart_page`).
    - 일·주·월·년봉을 읽지 못하면 머리줄의 "읽지 못함" 툴팁에 "일·주·월·년봉을 읽지 못했습니다"와 이유가 나온다. 그 보기가 읽는 기간의 실패만 보이고, 다음 차트 읽기(60초)에 다시 묻는다. 다른 보기로 옮긴 뒤에 끝난 읽기의 실패는 보이지 않고, 새 보기의 읽기를 바로 시작한다.
    - 엔진의 최신 일봉을 한 번 읽기 전에는 그리지 않는다. 오늘 봉 하나로 먼저 맞추면 그 한 봉의 너비가 굳어, 뒤에 읽은 몇 년치가 몇 개만 보였다(codeyoma/krx-quant#112에서 찾았다).
  - 주·월·년 보기(codeyoma/krx-quant#112): **일** 뒤의 **주 월 년** 버튼. 그 종목의 보기에 10080·44640·527040(대략의 분)으로 남는다.
    - 엔진이 저장한 끝난 주·월·년봉(`candles?period=week|month|year`, 기간의 첫날 날짜)을 읽고, 그 뒤의 기간(이번 주·달·해, 엔진이 아직 저장하지 않은 지난 기간)은 일 보기의 일봉(오늘 봉 포함)을 묶어 만든다. 주는 월요일 시작, 날짜는 KST다. 그래서 지금 봉도 오늘의 1분봉으로 실시간으로 움직인다.
    - 최신 일봉과 그 보기의 최신 봉을 둘 다 읽은 뒤 그린다. 왼쪽 끝 앞에 한 화면보다 적게 남으면(일 보기와 같은 기준) 그 기간의 봉을 400개씩 더 읽는다. 엔진 날짜가 바뀌면 둘 다 다시 읽는다.
    - 아이콘·호버·클릭은 없다. 차트 제목은 "005930 · 주봉"(월봉, 년봉)이다.

  > **바뀜** (2026-10-13, codeyoma/krx-quant#108): 오늘의 1분봉만 보였다 → 왼쪽으로 스크롤하면 지난 날의 봉과 아이콘을 이어서 읽는다. 이유: 지난 날로 스크롤(codeyoma/krx-quant#100).
  - 봉 마감마다 합의가 예측한 클래스의 아이콘을 예측을 만든 봉(그 봉 마감에 끝난 봉) 위에 둔다: 상승 ▲(초록), 하락 ▼(빨강), 보합 ●(회색). 순위표의 "차트" 칸에 체크한 모델이 없으면 최종 합의, 있으면 **선택 합의**(codeyoma/krx-quant#159)다. 아이콘 위의 글자가 결과다: ✓ 맞음, ✗ 틀림, ? 아직 채점 전이거나 채점할 수 없음. 예측 클래스는 엔진이 채점하는 것과 같다(가장 큰 확률, 동률이면 보합, 그다음 상승). 4분 구간에 특수 상황이 낀 예측은 흐리게 그린다(통계에서 빠진다).
  - 선택 합의(codeyoma/krx-quant#159): 체크한 모델 중 그 봉 마감에 예측을 낸(`ok`) 모델의 세 확률을 똑같은 무게로 이름순으로 더해 평균한다. 방향별 모델 수는 각 모델의 가장 큰 확률 방향으로 센다(엔진의 `votes`와 같은 셈). 하나도 예측하지 않은 봉에는 아이콘이 없다. 오늘의 봉은 모델 결과가 올 때마다 다시 계산하고(몇 초 안에 바뀔 수 있다), 지난 봉은 늦게 온 결과까지 넣은 사후 평균이다. 기준 예측은 한 방향 100%라, 모델과 섞으면 평균이 그쪽으로 기운다. 화면이 API로 받은 확률을 평균할 뿐 엔진의 가중치나 알고리즘 내용은 쓰지 않는다. 기준 예측은 체크해도 평균·방향별 수·`n/m`에 넣지 않는다(codeyoma/krx-quant#166). 호버에 비교 줄로만 나온다. 모델 없이 기준 예측 하나만 체크하면 그 기준의 예측을 아이콘으로 그리고(크게 그리지 않음), 기준 예측만 둘 이상이면 아이콘 없이 호버 비교 줄만 보인다.

  > **바뀜** (2026-10-14, codeyoma/krx-quant#166): 체크한 기준 예측도 선택 합의의 평균에 들어갔다 → 평균에서 빼고 비교 줄로만 보인다. 이유: 기준 예측은 한 방향에 100%라 섞으면 평균을 망친다. 같은 무게 평균은 비교군일 뿐이다(2026-10-14 인터뷰 Q8, QUANT LETTER).
  - 결과는 **봉 마감의 정답** 하나를 따른다(codeyoma/krx-quant#159): 그 (종목, 봉 마감)에서 채점된 아무 예측의 도달 정답(`outcome.touch`, codeyoma/krx-quant#170: 4분 안에 최우선 매수호가가 봉 마감 중간가 + k틱에 닿으면 상승, 아니고 최우선 매도호가가 − k틱에 닿으면 하락, 아니면 보합). 도달 정답이 없는 이전 엔진의 예측은 기준 A(봉 마감 중간가 → 4분 뒤)로 본다. 같은 봉 마감의 정답은 모든 예측자에게 같고, 최종 합의가 `no_members`여도 보인다. 그래서 체크한 모델이 없을 때도 최종 합의와 함께 "늘 보합"(기준 가격이 있으면 늘 `ok`)을 읽는다. 정답 구조체(`Truth`)와 맞음·틀림(`verdict`)은 `Board`의 순수 함수다.
  - 1분 보기에서 캔들(그 봉의 세로 구간 어디든, 거래량 칸 등 하단 칸 포함, codeyoma/krx-quant#223)에 마우스를 올리면(codeyoma/krx-quant#159) 가격 칸 아래쪽, 마우스와 반대편에 상자가 나온다. 첫 줄은 "09:31 봉 → 09:35 목표 · 정답 상승 (09:32:30 닿음)"(처음 닿은 시각. 보합이면 "정답 보합", 채점 전, 채점 불가와 이유)이다. 도달 정답이 없는 이전 엔진이면 "실제 상승 +300원"이다. 체크한 모델이 있으면 선택 합의 한 줄(방향, 세 확률, "상승 2 보합 1 하락 0", "3/3 모델", 결과, 신호면 "신호")과 체크한 모델마다 한 줄(이름, 마지막 순위표의 지금 상태, 방향, 세 확률, 결과. 그 봉에 없으면 "예측 없음", `ok`가 아니면 상태 글자)이 이어진다. 그다음은 늘 최종 합의 한 줄(방향, 세 확률, "같은 방향 3/5", 결과, 신호. `ok`가 아니면 "합의 대상 없음" 같은 상태 글자)이다. 005930의 봉에서 자동 매매가 사거나 팔았으면 그 뒤에 체결마다 한 줄이 붙는다(위 **자동 매매의 매수·매도**, codeyoma/krx-quant#398). 예측이 하나도 없는 봉은 첫 줄(결과 없이)과 "예측 없음"만이다. 색과 글꼴은 Fincept 툴팁과 같고, 긴 줄은 차트 안에서 줄바꿈된다. 상자 내용은 C++이 봉 시각을 키로 한 표(`bar_hovers`)로 만들어 바뀐 봉만 페이지에 보내고, 페이지는 십자선의 시각으로 찾아 보이기만 한다. 데이터를 다시 보내도 상자는 닫히지 않는다.

  > **바뀜** (2026-10-30, codeyoma/FinceptTerminal#107): 상자의 맨 아래는 늘 최종 합의 줄이었다 → 005930의 봉에서 자동 매매가 사거나 팔았으면 그 뒤에 체결마다 한 줄이 붙는다. 이유: 차트에서 자동 매매가 산 곳과 판 곳을 가격, 수량, 청산 이유, 순손익과 함께 보려고(codeyoma/krx-quant#398, 사용자 이야기 14).

  - 마우스를 올린 동안 그 봉의 **4분 구간**(codeyoma/krx-quant#167)을 그린다: 봉 마감부터 4분 뒤까지를 옅게 칠하고 주황 테두리를 두르고, 엔진이 기록한 도달 정답이 있으면 위 목표(초록)와 아래 목표(빨강)를 그 구간에 가로선으로 긋고, 처음 닿은 지점(시각은 초 단위로 봉 안의 위치)에 점을 찍는다. 끝나지 않은 창은 선을 점선으로 오른쪽 빈칸까지 그린다. 봉 사이의 위치(초 단위 시각)는 양쪽 봉의 x 사이에서 비례로 정한다.

  > **바뀜** (2026-10-16, codeyoma/FinceptTerminal#77): 주황 테두리는 끝나지 않은 창(최근 봉)에만 점선으로 있었다 → 어느 봉에 올려도 테두리를 두르고, 끝난 창은 실선, 끝나지 않은 창은 전처럼 점선이다. 이유: 어디에 마우스를 올려도 그 구간이 최근 봉에서처럼 눈에 띄게 하려고.

  > **바뀜** (2026-10-15, codeyoma/krx-quant#182): 4분 구간, 목표선, 닿은 점이 차트 왼쪽 끝에 붙어 그려졌다(구간이 보이지 않았다) → 그 봉 마감부터 4분 뒤의 자리에 그린다. 이유: Lightweight Charts의 `logicalToCoordinate`가 소수 위치(봉 안의 초)에 0을 돌려준다. 변동폭 호버가 같은 그리기를 쓰면서 찾았다.
  - **변동폭 호버**(codeyoma/krx-quant#182): 위의 예측 종류에서 변동폭을 고른 동안 상자는 변동폭의 것이다. 첫 줄은 "10:26 봉 → 10:30 목표 · 정답 11틱 · 큼"(실제 변동폭 r과 그 구간. 채점 전, 채점 불가와 이유, 특수 상황이면 " · 특수 상황")이고, 그 봉 마감의 변동폭 예측자마다 한 줄이 이어진다: 알고리즘(이름순), 기준 예측(이름순) 순으로 이름, 지금 상태(마지막 변동폭 순위표), 가장 큰 구간과 그 확률, "q10~q90 1~6틱", 정답 구간과 같으면 ✓, 다르면 ✗, 채점 전·채점 불가는 ?. 예측하지 않았으면 그 이유("직전 창 없음" 등)다. 4분 구간에는 정답이 나온 뒤 실제 최고 중간가(초록)와 최저 중간가(빨강)를 가로선 둘로 긋고, 그 값을 처음 받은 지점에 점을 찍는다. 아이콘과 머리줄 성적은 방향 그대로다. 상자 줄과 선은 `KindView`의 `kind_hovers`(순수 함수, `tst_krx_kind_view`)가 정한다.
  - **장벽 호버**(codeyoma/krx-quant#185): 장벽을 고른 동안 상자는 장벽의 것이다. 첫 줄은 "10:26 봉 → 10:30 목표 · 정답 위 먼저 (10:29:28 닿음) · w 6틱"(먼저 닿은 장벽과 그 시각. 어느 쪽에도 닿지 않았으면 "정답 시간 만료", 채점 전, 채점 불가와 이유(두 장벽에 같은 받은 시각에 닿으면 `ambiguous`), 특수 상황이면 " · 특수 상황". 끝에 그 봉 마감의 장벽 폭 w)이고, 그 봉 마감의 장벽 예측자마다 한 줄이 이어진다: 알고리즘(이름순), 기준 예측(이름순) 순으로 이름, 지금 상태(마지막 장벽 순위표), 예측 클래스(가장 큰 확률, 동률이면 시간 만료, 그다음 위 먼저), 세 확률("위 먼저 20% · 시간 만료 35% · 아래 먼저 45%", 방향 호버의 세 확률과 같은 모양), 정답 클래스와 같으면 ✓, 다르면 ✗, 채점 전·채점 불가는 ?. 예측하지 않았으면 그 이유다. 4분 구간에는 위 장벽 U(초록)와 아래 장벽 L(빨강)을 가로선으로 긋고, 정답이 나오면 각 장벽에 처음 닿은 지점에 점을 찍는다: U는 매수 1호가가 U 이상이 된 때와 그 매수 1호가, L은 매도 1호가가 L 이하가 된 때와 그 매도 1호가다(방향의 목표선·닿은 점과 같은 그리기). 둘 다 닿았으면 점도 둘이고, 정답은 먼저 닿은 쪽이다(첫 줄). U·L은 정답의 것, 정답 전에는 예측과 함께 온 봉 마감의 통합 중간가와 호가단위(예측 항목의 `anchor`, `tick`, codeyoma/krx-quant#210)로 센 중간가 ± w × 호가단위다. 4분이 끝나기 전에는 구간처럼 점선이다. 그 값을 주지 않는 옛 엔진이면 정답 전에는 구간만 칠한다. 상자 줄과 선은 `KindView`의 `kind_hovers`(순수 함수, `tst_krx_kind_view`)가 정한다.

  > **바뀜** (2026-10-15, codeyoma/krx-quant#210): 장벽의 U·L 선은 정답이 나온 뒤에만 그렸다(정답 전에는 구간만) → 정답 전에도 예측과 함께 온 봉 마감 중간가와 호가단위로 그린다. 이유: 엔진이 봉 마감 중간가를 정답의 경로로만 주어 4분 동안 장벽을 볼 수 없었다(codeyoma/krx-quant#185를 만들며 찾음).

  > **바뀜** (2026-10-14, codeyoma/krx-quant#170): 결과·✓/✗와 호버 첫 줄은 기준 A(봉 마감 중간가 → 4분 뒤, "실제 상승 +300원")였고, 도달 정답은 첫 줄 끝에 "도달 상승 09:32:30"처럼 따로 붙었다 → 도달 정답 하나로 ✓/✗를 매기고 첫 줄도 "정답 상승 (09:32:30 닿음)"이다. 이유: 엔진의 정답이 도달 기준으로 바뀌었다(codeyoma/krx-quant#160).

  > **바뀜** (2026-10-14, codeyoma/krx-quant#167): 그 봉에서 목표 봉까지 주황 선을 그었다(목표 봉이 없으면 점선 자리) → 4분 구간, 위·아래 목표선, 처음 닿은 점. 이유: 정답이 "4분 안에 목표 가격에 닿았나"로 바뀌므로(codeyoma/krx-quant#160) 구간과 목표 가격을 직접 보여 예측과 결과를 차트에서 대조하려고(2026-10-14 인터뷰 Q4).
  - 체크한 모델이 있으면 차트 머리줄에 "선택 합의 (3) · 맞음 52% · 늘 보합 61% · ▲ 맞음 40% (52) · ▼ 맞음 45% (60) · 310개 4거래일 · 특수 12개 제외"가 나온다(codeyoma/krx-quant#159, #166). 페이지가 알리는 보이는 시각 범위에서 4분이 다 지나 채점된 아이콘만 센다(채점 전·채점 불가 제외). 같은 봉들에서 "늘 보합"이 맞았을 비율, ▲·▼라고 한 봉 중 맞은 비율(괄호는 그 수, 없으면 "—"), 봉 수와 그 봉들의 거래일 수(KST 날짜)를 나란히 둔다. 특수 상황 봉은 세지 않고 "특수 n개 제외"로 따로 센다. 기준 예측 하나만 체크했으면 앞머리가 그 기준의 이름이다. 설명용 성적이다(툴팁: 겹치는 창은 독립 표본이 아니다). 그동안 차트 제목은 "005930 · 1분봉"이다.

  > **바뀜** (2026-10-14, codeyoma/krx-quant#166): 성적은 "맞음 · 늘 보합 (n개)"였다 → ▲·▼ 맞힘률, 거래일 수, 특수 제외 수를 더했다. 이유: 스캘핑에서는 "▲라고 했을 때 맞았나"가 쓸모 있고, 겹치는 4분 창은 독립 표본이 아니며, 특수 상황은 숨기지 않고 따로 보여야 한다(2026-10-14 인터뷰 Q9, QUANT LETTER 검증 노트).

  > **바뀜** (2026-10-14, codeyoma/krx-quant#161): 아이콘은 늘 최종 합의였고, 아이콘 위에만 마우스를 올리면 다섯 줄(봉·목표 시각, 합의 방향과 세 확률, 같은 방향 정식 모델 수, 신호 여부, 고른 기준의 실제 변화와 맞음·틀림)이 나왔다. ✓/✗는 순위표에서 고른 채점 기준을 따랐다. → 체크한 모델의 선택 합의를 그릴 수 있고, 캔들 아무 곳에서 봉의 상자가 나오며, ✓/✗는 봉 마감의 정답(기준 A) 하나다. 이유: 모든 알고리즘이 수습이면 최종 합의가 "합의 대상 없음"이라 차트에 아이콘이 없었고, 그 봉에서 모델별 판단을 함께 보려고(2026-10-14 인터뷰). 봉 마감마다 이벤트가 몰리면 차트를 다시 보내며 툴팁이 닫히던 것도 고쳤다(고른 종목의 예측일 때만, 0.25초 모아서 다시 그린다).
  - 1분 보기에서 봉 칸을 누르면(아이콘, 캔들, 빈 봉 칸 모두, codeyoma/krx-quant#107, #249) 예측 패널이 그 봉의 봉 마감(봉 시작 + 1분, 그 봉의 아이콘과 같은 봉 마감)의 판단으로 고정된다. 마지막 칸 오른쪽의 빈 곳, 끌어서 옮긴 것, 그리기 도구나 그린 것을 누른 것, 앵커 VWAP의 봉을 고르는 클릭은 고정하지 않는다. 3분 이상 보기, 일·주·월·년 보기, 시간축 없는 모양에서는 눌러도 고정하지 않는다(한 봉에 봉 마감이 여럿이거나 봉이 아니다). 페이지는 아이콘이면 그 봉 마감을(`clicked`), 아니면 그 칸의 시각을(`bar`) 알리고, `KrxChart`가 1분 보기일 때만 봉 마감으로 바꿔 `close_clicked`로 낸다. 아래 **예측 패널** 참고.

  > **바뀜** (2026-10-16, codeyoma/krx-quant#249): 아이콘을 눌러야만 고정됐다 → 1분 보기의 어느 봉 칸을 눌러도 그 봉 마감으로 고정된다. 이유: 아이콘 밖의 캔들이나 아이콘이 없는 봉(빈 봉, 예측을 아직 읽지 않은 봉)을 누르면 아무 일도 없어 패널이 늘 최신 봉 마감이었다(사용자 요청, 2026-10-16).
  - 1분 보기에서 봉 칸을 두 번 누르면(아이콘, 캔들, 빈 봉 칸 모두, codeyoma/krx-quant#255) 그 봉의 창이 열린다(아래 **봉 창**). 고정과 같은 곳에서는 열지 않는다: 마지막 칸 오른쪽의 빈 곳, 끌어서 옮긴 것, 그리기 도구를 고른 동안, 그린 것(그린 것의 두 번 누르기는 지금처럼 그 팝업이다), 앵커 VWAP의 봉을 고르는 클릭. 3분 이상 보기, 일·주·월·년 보기, 시간축 없는 모양에서도 열지 않는다. 첫 번째 누르기는 한 번 누르기와 같아 기다리지 않고 패널을 그 봉 마감으로 고정한다. 두 번 누르기는 Lightweight Charts의 것이다(0.5초 안, 5픽셀 안, 끌지 않음). 그 둘째 누르기는 누르기가 아니라서 패널은 한 번만 고정한다. 페이지는 첫 누르기가 그 칸의 시각을 알린(`clicked`·`bar`) 두 번 누르기에서만 그 칸의 시각을 알리고(`opened`), `KrxChart`가 1분 보기일 때만 봉 마감으로 바꿔 `bar_window_requested`로 낸다.
  - 신호였던 봉(합의 확률이 기준값 이상이고 같은 방향 정식 모델이 N개 이상, 엔진이 막지 않음)의 아이콘은 두 배 크고 진하다. `votes`가 없는 합의(이전 엔진, 이전 기록)는 강조하지 않는다. 알림 기준값이나 N을 바꾸면 차트도 다시 판단한다. 선택 합의는 같은 기준을 "체크한 모델 중 같은 방향 N개 이상"으로 읽는다. 체크한 모델이 N보다 적으면 크게 그려지지 않는다. 그 봉이 특수 상황이거나 같은 봉 마감의 최종 합의에 `blocked`가 있으면 크게 그리지 않는다(`no_members`는 막힘이 아니다). 선택 합의로는 알림을 보내지 않는다.
  - **자동 매매의 매수·매도**(codeyoma/krx-quant#398): 고른 종목이 trader가 매매하는 005930(`ChartModel`의 `kTraderSymbol`)이면, 1분 보기(시간축 있는 모양)에 trader의 매수 ▲를 그 체결이 들어온 1분봉 아래에, 매도 ▼를 그 봉 위에(같은 봉에 합의 아이콘이 있으면 그 아이콘 위에) "매수"·"매도" 글자와 함께 둔다. 실전 계좌의 것은 주황(테마의 강조색), 모의(trader 안의 모의 체결)와 KIS 모의투자의 것은 청록(테마의 cyan)이다. 그 봉의 호버 상자에는 최종 합의 줄 뒤에 체결마다 한 줄이 시간 순서로 붙는다: "자동 매매 매수 70,100원 × 14주 · 모의", "자동 매매 매도 70,250원 × 14주 · 익절 · 순손익 -162원 · 실전"(청산 이유는 익절·손절·시간 청산·정지 매도·하루 한도·누적 한도, 순손익은 비용을 뺀 것을 원 단위로 반올림, 모드는 모의·KIS 모의·실전). 표시를 누르면 그 봉을 누른 것이다(예측 패널이 그 봉 마감으로 고정된다). 다른 종목, 3분 이상 보기, 일·주·월·년 보기, 시간축 없는 모양, 빈 봉과 읽은 봉 밖에는 그리지 않는다. 정책에 버전이 생기기 전(codeyoma/krx-quant#399)에는 정책이 하나뿐이라 모든 매매를 그린다.

    > **바뀜** (2026-10-30, codeyoma/FinceptTerminal#108): 청산 이유는 익절·손절·시간 청산이었고, 그 밖의 것은 엔진의 이름(`pause`, `daily_limit`, `cumulative_limit`) 그대로 보였다 → 정지 매도·하루 한도·누적 한도도 한국어다(KRX 자동 매매 화면과 같은 `trader_exit_label`). 이유: 엔진이 codeyoma/krx-quant#396부터 화면의 정지와 손실 한도의 매도를 준다.

    - 엔진의 `GET /v1/symbols/005930/trader-marks?before=&limit=1000`(codeyoma/krx-quant#398)로 읽는다. 예측 아이콘처럼 봉과 따로, 최신 쪽부터 읽어 둔 첫 1분봉까지 1,000개씩 거슬러 읽는다(`trader_before`, `trader_from_after`: 쪽이 1,000개보다 적으면 그 앞에는 없다). 앞 쪽은 읽은 가장 이른 표시의 시각보다 1밀리초 뒤를 `before`로 묻는다. 엔진의 시각은 마이크로초까지이고 화면은 밀리초까지라, 그 밀리초의 표시를 다시 읽어야 빠지는 것이 없다(다시 읽은 것은 겹치지 않고 합쳐진다). 엔진은 한 시각의 표시를 두 쪽에 나누지 않는다. 한 밀리초에 한 쪽보다 많은 표시가 있어 더 거슬러 가지 못하면 거기서 멈춘다. 차트를 읽을 때마다(새 봉 마감, 스트림이 없으면 60초마다) 최신 쪽을 다시 읽어 그 뒤의 매수·매도와 열려 있던 매매의 매도를 더한다. 같은 매매의 같은 쪽은 새로 읽은 것으로 바꾼다(`join_trader_marks`). 최신 쪽이 꽉 찼는데 그 가장 이른 표시가 읽어 둔 가장 늦은 표시보다 이르지 않으면(화면을 며칠 가려 둔 사이 한 쪽보다 많이 사고팔았다, `trader_gap`), 읽어 둔 것을 버리고 그 쪽부터 첫 봉까지 다시 읽는다. 그러지 않으면 그 사이가 빈다. 그 엔드포인트가 없는 옛 엔진(404)이면 표시가 없고, 읽지 못함으로 보이지도 않는다. 다른 종목을 고르면 바로 버리고 그 종목의 것은 묻지 않는다. 엔진을 읽지 못하거나 다른 엔진 주소를 적용해도 버린다.
    - 무엇을 그릴지는 `ChartModel`의 `trader_icons`·`trader_line`·`with_trader_lines`(순수 함수, `tst_krx_chart_model`)가 정하고, 페이지는 그리기만 한다(`data.trades`, 아이콘·프랙탈 표시와 같은 `createSeriesMarkers` 위에).
  - 무엇을 그릴지와 언제 앞의 봉을 읽을지(`older_read`, 읽은 봉 붙이기 `join_bars`)는 `services/krx_quant/ChartModel`(순수 함수, `tst_krx_chart_model`)이 정하고, 페이지(`screens/krx_quant/chart/index.html`)는 그리기만 한다. 페이지는 보기가 움직일 때마다 보이는 범위(왼쪽·오른쪽 끝 위치)를 화면에 알린다(`moved`, 화면 너비는 codeyoma/krx-quant#239). 화면과 페이지는 QWebChannel로 주고받는다. 페이지와 라이브러리는 Qt 리소스(`qrc:/krx/chart/`)로 앱에 들어 있어 인터넷 없이 열린다.
  - 색과 글꼴은 Fincept 테마(오름 초록, 내림 빨강, 바탕·격자·글자)와 설정 글꼴을 따르고, 테마가 바뀌면 다시 칠한다.
  - 1분봉을 읽지 못하면 머리줄에 빨간 "읽지 못함"이 나오고 이유는 그 툴팁에 있다.
  - Qt WebEngine 없이 빌드했거나 실행 파일(QtWebEngineProcess)이 빠진 설치본이면 "차트를 열 수 없음"만 보인다.
  - 실행 파일은 있는데 렌더러가 시작하지 못하면 글 없이 검은 칸만 보인다. 그 이유는 앱이 알려 주지 않는다. 윈도우 설치본에 V8 시작 스냅샷 `resources/v8_context_snapshot.bin`이 빠지면 이렇게 된다. krx.29까지의 윈도우 빌드가 그랬다(codeyoma/krx-quant#207). 그때는 Equity Trading 차트도 같이 비고, 작업 관리자에 `QtWebEngineProcess.exe`가 하나도 없다. 설치 프로그램과 업데이트 ZIP은 이 파일을 담고, `custom-release`는 빌드 폴더와 설치 결과에 이 파일이 없으면 실패한다.
  - 출처: 차트에 TradingView 로고는 끄고(`attributionLogo: false`), 차트 머리줄 제목의 툴팁과 아래 "차트 라이브러리" 절에 밝힌다.

  > **삭제됨** (2026-10-13, codeyoma/krx-quant#134): 직접 그린 1분봉 차트(`KrxChart`의 QPainter 그림, 봉 호버 툴팁)와 범위 선택 상자(30분·1시간·2시간·하루 전체, QSettings `krx_quant/chart_minutes`). 이유: 휠 줌·드래그 스크롤·거래량이 없었다(codeyoma/krx-quant#100). 대신: Lightweight Charts 차트와 **맞춤**. 남아 있는 `krx_quant/chart_minutes` 값은 더 읽지 않는다. 아이콘 호버는 codeyoma/krx-quant#106에서 다시 붙는다.

  > **바뀜** (2026-10-13, codeyoma/krx-quant#134): 상승 ▲는 봉 아래, 하락 ▼는 봉 위였다 → 모든 아이콘이 예측을 만든 봉 위에 있고, 보합은 – 대신 ●, 결과 글자는 아이콘 위다. 채점 전에도 ?가 붙는다. 이유: 스펙(codeyoma/krx-quant#100)이 "예측을 만든 봉 위에"로 정했고, Lightweight Charts의 표시(화살표·원과 글자)로 그린다.
- **예측 패널**: 고른 종목의 최신 봉 마감 예측이다. 최종 합의, 빠른 합의, 알고리즘, 기준 예측 순이고 알고리즘과 기준 예측은 이름순이다. 합의와 기준 예측은 한국어 이름으로 보인다(`EngineData`의 `predictor_label`): 합의 (최종)·합의 (빠른), 기준: 늘 보합(`baseline-flat`), 기준: 직전 4분 방향(`baseline-persist`), 기준: 비율대로 무작위(`baseline-random`), 기준: 시간대 기후(`baseline-climate`). 다른 종류의 기준 예측도 "기준: 변동폭 시간대 기후"처럼 한국어 이름이다. 순위표, 상세 통계 창, 호버도 같은 이름이다. 열은 방향, 상승·보합·하락 확률, 기대 틱, 모델 버전이다. 상승과 하락이 같은 정수 %로 보이면 방향은 비운다. 제목은 그 예측이 보는 4분, 봉 마감부터 채점 시각까지다: "005930 · 10:30–10:34". 채점 시각을 아는 예측이 없으면 "005930 · 10:30 봉 마감"이다.

  > **바뀜** (2026-10-16, codeyoma/krx-quant#243): 제목에 봉 마감과 채점까지 남은 시간(엔진 시계 기준. "005930 · 10:30 봉 마감 · 채점까지 3:12", 그 시각이 지나면 "· 10:34에 채점")이 나왔다 → 봉 마감부터 채점 시각까지의 4분("005930 · 10:30–10:34"). 이유: 위 종목 목록의 바뀜과 같다.

  > **바뀜** (2026-10-15, codeyoma/krx-quant#210): `baseline-climate`(방향의 시간대 기후, codeyoma/krx-quant#169)는 영어 이름 그대로 보였다 → "기준: 시간대 기후". 이유: 다른 기준 예측처럼 한국어 이름을 주지 않았다(codeyoma/krx-quant#185를 만들며 찾음).

  - 1분 보기에서 차트의 봉이나 그 아이콘을 누르면(codeyoma/krx-quant#107, #249) 그 봉 마감의 판단으로 고정된다. 엔진의 `GET /v1/symbols/{s}/predictions?bar_close=`와 `/v1/algorithms`(수명 상태)를 읽는다. 행은 빠른 합의, 최종 합의(굵게), 알고리즘(이름순), 기준 예측(흐리게) 순이다. 열은 예측, 상태(알고리즘의 지금 수명 상태), 방향(가장 큰 확률, 동률이면 보합, 그다음 상승. 예측하지 않았으면 그 이유), 상승·보합·하락, 합의(최종 합의의 `pooled`로 "포함"·"제외". `pooled`가 없는 이전 기록은 "알 수 없음"), 결과(봉 마감의 정답으로 "✓ 맞음"·"✗ 틀림", "채점 전", "채점 불가")다. 체크한 모델이 있으면 맨 위에 `선택 합의` 행(굵게)이 붙는다(codeyoma/krx-quant#159).
  > **바뀜** (2026-10-16, codeyoma/krx-quant#255): "기준: 직전 4분 방향"이 직전 창을 채점할 수 없어 예측하지 않은 봉 마감(`no_previous`, krx-quant #191)의 방향 칸에 그 코드가 그대로 나왔다 → "직전 창 없음"(다른 종류의 기준 예측과 같다). 고정한 표, 호버, 봉 창이 같다. 이유: 봉 창을 만들며 영어 코드가 보이는 것을 찾았다.
  - 고정한 동안 제목은 "005930 · 10-12 10:24 봉 마감의 판단"이고, 새 봉 마감이 와도 표를 덮지 않는다. 그 봉 마감의 채점 결과는 순위표를 다시 읽을 때 함께 다시 읽는다. 머리줄 오른쪽의 **최신으로**를 누르거나 다른 종목을 고르면 최신 봉 마감으로 돌아간다. 그 봉 마감에 예측이 없으면(엔진이 예측하기 전의 봉) 표가 빈다. 읽지 못하면 표가 비고 이유는 제목의 툴팁에 있다.
  - 무엇을 보일지는 `ChartModel`의 `close_rows`(순수 함수, `tst_krx_chart_model`)가 정한다.
  - **변동폭 패널**(codeyoma/krx-quant#182): 예측 종류에서 변동폭을 고르면 패널은 고른 종목의 최신 봉 마감 변동폭 예측이다(`GET /v1/symbols/{s}/kinds/range/latest`). 행은 알고리즘(이름순), 기준 예측(이름순, 흐리게)이고, 열은 예측, 구간(가장 큰 확률의 구간. 동률이면 보통, 그다음 작음. 예측하지 않았으면 그 이유), 작음·보통·큼(엔진의 클래스 이름), q10·q50·q90(틱, 반 틱까지), 모델 버전이다. 제목은 "005930 · 변동폭 · 10:30–10:34 · k 2틱 · 2k 4틱"이다. 10:30–10:34는 그 예측이 보는 4분, 봉 마감부터 채점 시각까지다. k는 그 봉 마감 정답의 k, 정답 전에는 예측과 함께 온 봉 마감의 k(예측 항목의 `flat_ticks`, codeyoma/krx-quant#200)다. 4분 안에 가격이 호가 단위나 비용 반올림 경계를 넘어도 작음·보통·큼 확률이 쓴 k와 2k다. 그 값을 주지 않는 옛 엔진에서는 `GET /v1/symbols`의 지금 `flat_ticks`다. 열 제목의 툴팁이 구간과 분위수의 뜻을 알린다.

  > **바뀜** (2026-10-16, codeyoma/krx-quant#243): 제목의 "10:30 봉 마감 · 채점까지 3:12" → "10:30–10:34"(봉 마감부터 채점 시각까지). 이유: 위 종목 목록의 바뀜과 같다.

  - 변동폭을 고른 동안 차트의 봉이나 아이콘을 누르면 패널은 그 봉 마감의 변동폭 예측으로 고정되고 결과 열(정답 구간으로 "✓ 맞음"·"✗ 틀림", "채점 전", "채점 불가")이 붙는다. 제목은 "005930 · 변동폭 · 10-12 10:24 봉 마감의 판단 · 정답 5틱 · 큼 · k 2틱 · 2k 4틱"이다. k는 위 제목과 같은 규칙이되, 옛 엔진의 정답 전에는 지금 k를 쓰지 않고 비운다. 그 봉 마감의 예측은 호버를 위해 읽어 둔 것을 쓴다.
  - 행과 결과는 `KindView`의 `kind_rows`, k는 `bar_close_k`(순수 함수, `tst_krx_kind_view`)가 정한다.
  - **장벽 패널**(codeyoma/krx-quant#185): 장벽을 고르면 패널은 고른 종목의 최신 봉 마감 장벽 예측이다(`GET /v1/symbols/{s}/kinds/barrier/latest`). 행은 알고리즘(이름순), 기준 예측(이름순, 흐리게)이고, 열은 예측, 예측 클래스(가장 큰 확률의 클래스. 동률이면 시간 만료, 그다음 위 먼저. 예측하지 않았으면 그 이유), 위 먼저·시간 만료·아래 먼저 확률(엔진의 클래스 이름), 모델 버전이다. 열 제목의 툴팁이 각 확률의 뜻(매수 1호가가 U에 먼저, 어느 장벽에도 안 닿음, 매도 1호가가 L에 먼저)을 알린다. 제목은 "005930 · 장벽 · 10:30–10:34 · w 6틱"이다. 10:30–10:34는 그 예측이 보는 4분, 봉 마감부터 채점 시각까지다. w는 그 봉 마감의 장벽 폭(틱)으로, 정답이 나오면 정답의 w, 그 전에는 예측과 함께 온 봉 마감의 w(예측 항목의 `width_ticks`, krx-quant #183. 엔진이 봉 마감에 한 번 정해 모든 예측자에게 같다)다. 위·아래 장벽 가격도 붙는다: "w 6틱 · U 78,950 · L 77,750". U·L은 정답이 나오면 정답 경로의 것(`path.upper`, `lower`), 그 전에는 예측과 함께 온 봉 마감의 통합 중간가와 호가단위(예측 항목의 `anchor`, `tick`, codeyoma/krx-quant#210)로 센 중간가 ± w × 호가단위다. 4분 안에 가격이 움직이거나 호가단위 경계를 넘어도 그 봉 마감의 값이다. 그 값을 주지 않는 옛 엔진에서는 정답 전에 "w 6틱"만 있다. 따라가지 못한 창(`restarted`)의 정답에는 U·L이 없어 w만 있다.

  > **바뀜** (2026-10-16, codeyoma/krx-quant#243): 제목의 "10:30 봉 마감 · 채점까지 3:12" → "10:30–10:34"(봉 마감부터 채점 시각까지). 이유: 위 종목 목록의 바뀜과 같다.

  > **바뀜** (2026-10-15, codeyoma/krx-quant#210): 정답 전에는 U·L이 없었다(엔진이 봉 마감의 통합 중간가를 정답의 경로로만 주었다) → 예측과 함께 오는 봉 마감 중간가와 호가단위로 정답 전에도 U·L을 보인다. 이유: 4분 동안 장벽을 보려고(codeyoma/krx-quant#185를 만들며 찾음).

  - 장벽을 고른 동안 차트의 봉이나 아이콘을 누르면 패널은 그 봉 마감의 장벽 예측으로 고정되고 결과 열(정답 클래스로 "✓ 맞음"·"✗ 틀림", "채점 전", "채점 불가")이 붙는다. 제목은 "005930 · 장벽 · 10-12 10:26 봉 마감의 판단 · 정답 위 먼저 (10:29:28 닿음) · w 6틱 · U 78,950 · L 77,750"이다.
  - 열은 `KindView`의 `kind_panel_columns`, w는 `bar_close_width`, U·L은 `bar_close_barriers`, 제목의 글은 `barrier_text`(순수 함수, `tst_krx_kind_view`)가 정한다. 변동폭 패널의 열도 같은 함수가 정하고 전과 같다.

  > **바뀜** (2026-10-15, codeyoma/krx-quant#200): 정답 전의 k와 2k는 `GET /v1/symbols`의 지금 `flat_ticks`였고, 고정한 봉 마감에서는 정답 전에 비어 있었다 → 예측과 함께 오는 봉 마감의 k. 옛 엔진에서는 전과 같다. 이유: 4분 안에 가격이 호가 단위나 비용 반올림 경계를 넘으면 보이는 k·2k가 보이는 작음·보통·큼 확률과 맞지 않았다(codeyoma/krx-quant#199 리뷰).

  - **시가→종가 패널**(codeyoma/krx-quant#264): 시가→종가를 고르면 패널은 고른 종목의 최신 날 예측이다(`GET /v1/symbols/{s}/kinds/open_to_close/latest`). 행은 알고리즘(이름순), 기준 예측(이름순, 흐리게)이고, 열은 예측, 예측 클래스(가장 큰 확률. 동률이면 보합, 그다음 상승. 예측하지 않았으면 그 이유, 기준 예측의 `no_previous`는 "전 거래일 정답 없음"), 상승·보합·하락 확률, 결과("✓ 맞음"·"✗ 틀림", 정답 전에는 "채점 전", "채점 불가"), 모델이다. 결과 열은 고정하지 않아도 늘 있다: 그날 15:45부터 정답이 나오기 때문이다. 열 제목의 툴팁이 확률의 뜻(시가 단일가 → 종가 단일가의 변화가 +보합 폭보다 큼, ±보합 폭 안, −보합 폭보다 작음)과 채점(그날 15:30 종가 단일가, 15:45부터)을 알린다.
    - 제목은 "005930 · 10-12 시가→종가 (08:50 예측) · 보합 ±0.47% · 채점 전 (15:45부터)"이다. 날과 예측 시각(엔진의 `daily.predict_at`), 그날의 보합 폭(예측 항목의 `band`, 종가 ÷ 시가 − 1의 크기를 % 로. 정답이 나오면 정답의 `path.band`), 정답이다. 정답이 나오면(그날 15:30 종가 단일가가 저장된 뒤, 15:45부터) "정답 하락 -0.85% (81,950 → 81,250)"(정답 클래스, 시가 단일가 → 종가 단일가의 변화와 두 가격, 엔진의 `path`)이다. 서킷브레이커·사이드카가 있던 날은 " · 특수 상황", 그 종목의 VI가 있었으면 " · VI 2회"가 붙는다(VI는 특수가 아니다). 채점할 수 없으면 "채점 불가 (보합 폭 없음)"(`no band`), "(일봉 없음)"(`no candle`, 7일 동안 일봉이 오지 않음)이다. 보합 폭을 정할 날이 모자라면 보합 폭이 빠진다. 제목의 툴팁이 창, 예측·채점 시각, 보합의 뜻(그날 전의 날만으로 종목의 일 변동성에서 정해 세 클래스가 대략 1/3씩), 상태 판정(정답이 있는 날 60·120·180일째, 최근 60일)을 알린다.
    - 08:50 전이나 예측이 없는 날(엔진이 08:50 뒤에 시작한 날, 휴장일)에는 지난 거래일의 예측이 남는다. 그 행은 모두 흐리게, 제목은 "005930 · 10-08 시가→종가 (08:50 예측, 지난 거래일) · …"이다(다른 패널의 "(지난 거래일)"과 같다). 그 예측의 정답이 이미 나왔으면 결과도 보인다.
    - 일 보기의 아이콘을 누르거나 1분 보기에서 아무 봉을 누르면 패널은 그날의 시가→종가 예측으로 고정된다(그 봉의 날 08:50, `KindView`의 `daily_close_on`). 제목은 그날의 것("005930 · 10-08 시가→종가 (08:50 예측) · …")이고, 머리줄 오른쪽의 **최신으로**로 돌아간다. 그날 예측이 없으면 "005930 · 10-09 시가→종가 · 예측 없음"이다. 그날의 예측은 차트를 위해 읽어 둔 것(아래 갱신)을 쓴다.
    - 스트림의 08:50 `kind_prediction`과 정답의 `kind_answer`가 오면 패널과 일 보기를 곧바로 다시 그리고, 정답이 오면 3초 뒤 순위표를 읽는다.
    - 열은 `KindView`의 `kind_panel_columns`, 행과 결과는 `kind_rows`, 제목과 툴팁은 `daily_title`·`daily_answer_text`·`daily_band`·`band_text`·`daily_tip`(순수 함수, `tst_krx_kind_view`)이 정한다.
  - **5거래일 시가→종가 패널**(codeyoma/krx-quant#310): 시가→종가 패널과 같다(`GET /v1/symbols/{s}/kinds/open_to_close_5d/latest`, 같은 열과 행, 지난 거래일, 고정). 다른 것:
    - 제목은 머리 뒤에 창 끝이 붙는다: "005930 · 10-12 5거래일 시가→종가 (08:50 예측) · 창 끝 10-16 (예정) · 보합 ±1.34% · 채점 대기 (10-16 20:35부터, 최대 4거래일) · 기준 예측만". 창 끝은 정답 전에는 예측의 `target_at`의 날(거래 일정으로 정한 다섯째 거래일, "(예정)"), 정답이 나오면 정답의 `path.end_day`(KRX가 실제로 연 다섯째 세션. 예정에 없던 휴장이 생기면 밀린다)다(`KindView`의 `daily_end_day`, `daily_window_text`). 정답 전의 결과 열과 제목은 "채점 전" 대신 "채점 대기"다.
    - 정답은 "정답 하락 -4.40% (84,050 → 80,350)"(첫 세션 시가 → 마지막 세션 종가)이다. 둘째 세션부터 마지막 세션 사이에 배당락일이 있는 창(`path.ex_dividend`가 `true`)은 " · 배당락 낀 창"이 붙는다. KIS 수정주가가 현금배당을 빼지 않아 그 창의 정답이 배당만큼 하락 쪽으로 기울기 때문이다(엔진의 정답·학습·판정은 이 표시를 읽지 않는다). `false`와 `null`(모름)은 표시가 없다. VI 수는 다섯 세션의 것이다.
    - 제목의 툴팁 맨 위에 그 창의 다섯 세션 종가("세션 종가 83,250 · 83,050 · 82,050 · 80,950 · 80,350", `daily_closes_text`)가 있고, 그 뒤가 이 종류의 설명이다: 창(5세션), 정답 시각과 "최대 4거래일 채점을 기다리고, 종목마다 채점 대기인 창이 늘 4개 있다", 보합 폭(그날 전에 끝난 창만으로 정함), 겹침(이웃한 날의 창이 4세션씩 겹쳐 통계를 보정한다: 구간은 Driscoll–Kraay, 거래일 단위 lift는 Newey–West, 독립인 창은 대략 날 수 ÷ 5개, 보정 곡선은 보정하지 않음), 배당락 낀 창의 뜻, 상태 판정(엔진의 `daily.probation_days`, 지금 정답이 있는 날 60·120·180일째, 최근 60일).
    - **어떤 예측자가 도는지**(`daily_runs_text`, `daily_runs_lines`): `GET /v1/algorithms`에 이 종류(`kind`)의 알고리즘이 하나도 없으면 제목 끝에 " · 기준 예측만"이 붙고, 툴팁 끝에 "기준 예측만 예측하고 채점한다(…). 엔진의 알고리즘 목록에 이 종류의 알고리즘이 없다. 사전 검증에서 거부되었거나 아직 없는 알고리즘은 목록에 나오지 않는다."가 붙는다. 엔진이 `daily.awaiting`을 주면 제목에 " · 돌지 않음", 툴팁에 "아직 돌지 않는다: <이유>"이고, `daily.review_hold`를 주면 툴팁에 "알고리즘은 수습에 오르지 않는다: <이유>"다. `/v1/algorithms`를 읽기 전이나 읽지 못했으면 아무것도 붙이지 않는다. 시가→종가에도 같은 규칙이지만 `cox`가 있어 붙지 않는다.

  > **바뀜** (2026-10-15, codeyoma/krx-quant#182): 예측 패널은 늘 방향 예측이었다 → 고른 예측 종류의 예측이다. 방향을 고르면 전과 같다. 이유: 변동폭 예측을 화면에서 보려고(krx-quant #177).
- **순위표**: 기간(오늘·5일·20일), 종목(전체·종목별), 특수 상황 포함을 고른다. 통계는 도달 정답으로 센다(codeyoma/krx-quant#170). 열은 순위, 상태(수습·정식·벤치·은퇴·복제), 모델 버전, 표본, 정답률, 방향 정밀도, 커버리지, Brier, 닿음 손익(틱, 연구용), 가장 약한 lift 하한, 운 보정 통과다. 95% 구간은 툴팁에 있다. 닿음 손익 열 제목의 툴팁은 어떻게 잰 값인지(▲ 매도 1호가에 사서 목표에 닿으면 목표가, 아니면 4분 끝 매수 1호가에 팖, ▼ 주식 공매도로 반대, ● 거래 없음, 비용 뺌)와 연구용 값이라는 것을 알린다. 칸의 툴팁에는 거래 수·합계·95% 구간과, 엔진이 주면(`pnl_inverse`, codeyoma/krx-quant#272) ▼를 그 종목의 인버스 상품으로 친 값(매도세 없음, 인버스가 없는 삼성전기와 상장 전의 하락은 빠짐)의 거래 수·평균·합계가 있다(codeyoma/krx-quant#267).

  > **바뀜** (2026-10-18, codeyoma/FinceptTerminal#87): 툴팁은 ▼를 "인버스로 반대"라고 했다 → 주식 공매도로 친 값이라고 하고, 인버스 상품으로 친 값은 칸의 툴팁에 따로 보인다. 이유: 엔진의 `pnl`은 하락에 매도세를 빼는 주식 공매도 값이고(codeyoma/krx-quant#272), 인버스 상품의 값은 `pnl_inverse`다. 사용자가 열을 더하지 않고 툴팁에 보이기로 정했다(2026-10-18, codeyoma/krx-quant#267). 순위가 없는 항목, 기준 예측(흐리게) 순으로 이어진다.

  > **삭제됨** (2026-10-14, codeyoma/krx-quant#170): 채점 기준 상자(B 표시 시점·A 봉 마감)와 요청의 `basis`. 이유: 엔진이 도달 정답 하나로만 채점한다(codeyoma/krx-quant#160). 대신: 기간·종목·특수 상황만 고른다. 상세 통계 창 머리줄의 질의 글에서도 기준이 빠졌다.

  > **바뀜** (2026-10-14, codeyoma/krx-quant#170): 열 이름 "중간가 손익(틱)"(시작 중간가 → 목표 중간가) → "닿음 손익(틱, 연구용)"(호가로 사고팔고 비용을 뺀 값). 이유: 정답이 목표 가격에 닿았나로 바뀌었다.
  - 첫 열 **차트**(codeyoma/krx-quant#159): 체크 칸으로 차트의 선택 합의에 넣을 모델을 고른다. 알고리즘(상태 무관)과 기준 예측 줄에만 있고 합의 줄에는 없다. 체크 칸은 Fincept 체크 상자 모양(체크하면 주황)이다. "차트" 머리 칸을 누르면 모두 해제한다. 줄을 두 번 누르면 지금처럼 상세 통계 창이 열리고, 체크 칸을 두 번 눌러도 창은 열리지 않는다. 누른 뒤 0.3초 기다렸다가 반영하므로, 두 번 눌러 제자리면 아무것도 읽지 않는다. 선택은 QSettings `krx_quant/chart_selection`(예: `gauss,markov`)에 남고 모든 종목에 공통이다. 순위표를 다시 그리거나 기간·종목을 바꿔 줄이 빠져도 선택은 남는다.
  - **변동폭 순위표**(codeyoma/krx-quant#182): 변동폭을 고르면 `GET /v1/leaderboard?kind=range`를 같은 기간·종목·특수 상황으로 읽는다(방향은 전처럼 `kind` 없이 묻는다). 행은 변동폭 알고리즘, 변동폭 기준 예측 둘(흐리게)이다. 열은 순위, 예측, 상태, 모델, 표본, 정답률, 작음·큼 정밀도, 커버리지(행동 클래스 작음·큼으로 센다), Brier, 구간 적중률(실제 r이 q10~q90 안에 든 비율, 이상적으로 80%, Wilson 구간은 툴팁), q50 오차(틱), 가장 약한 lift 하한, 운 보정이다. 닿음 손익(방향의 것)과 첫 열 **차트**(선택 합의는 방향의 것)는 없다. 순위표 제목의 툴팁은 그 종류의 정답과 기준 예측을 말한다. 엔진이 다른 종류의 순위표를 주면(종류를 모르는 옛 엔진) "읽지 못함"이다. 열과 칸은 `StatsView`의 `kind_stats_columns`·`kind_stats_cells`(순수 함수, `tst_krx_stats_view`)가 정한다.
  - **장벽 순위표**(codeyoma/krx-quant#185): 장벽을 고르면 `GET /v1/leaderboard?kind=barrier`를 같은 조건으로 읽는다. 행은 장벽 알고리즘(`breiman` 등), 장벽 기준 예측 둘(시간대 기후, 직전 4분 장벽, 흐리게)이다. 열은 순위, 예측, 상태, 모델, 표본, 정답률, 위 먼저·아래 먼저 정밀도, 커버리지(행동 클래스 위 먼저·아래 먼저로 센다), Brier, **장벽 손익(틱, 연구용)**, 가장 약한 lift 하한(장벽 기준 예측 둘 대비), 운 보정이다. 장벽 손익은 엔진의 추가 통계(`extra`, krx-quant #183)의 거래당 평균이고, 칸의 툴팁에 거래 수와 합계, 95% 구간, 롱(위 먼저 예측)·숏(아래 먼저 예측)의 거래 수와 평균이 있다. 거래가 없으면(모두 시간 만료 예측) "—"다. 열 제목의 툴팁은 어떻게 잰 값인지(위 먼저는 봉 마감의 매도 1호가에 사서 U에 먼저 닿으면 U, L에 먼저 닿으면 그때 매수 1호가, 어느 쪽에도 안 닿으면 4분 끝 매수 1호가에 팖, 아래 먼저는 주식 공매도로 반대, 시간 만료는 거래 없음, 비용 뺌)와 연구용 값이라는 것을 알린다. 엔진이 주면 칸의 툴팁에 숏을 인버스 상품으로 친 값(전체와 숏만, `pnl_inverse`·`short_inverse`, codeyoma/krx-quant#267)도 있고, 상세 창의 장벽 손익 표에는 "숏(인버스 상품)" 줄이 더해진다. 닿음 손익과 첫 열 **차트**는 없다.
  - **시가→종가 순위표**(codeyoma/krx-quant#264): 시가→종가를 고르면 `GET /v1/leaderboard?kind=open_to_close`를 같은 기간·종목·특수 상황으로 읽는다. 행은 시가→종가 알고리즘(`cox`), 기준 예측 셋(흐리게)이다. 열은 순위, 예측, 상태, 모델, **표본(종목·일)**, 정답률, 상승·하락 정밀도, 커버리지(행동 클래스 상승·하락으로 센다), Brier, **시가→종가 손익(bp, 연구용)**, 가장 약한 lift 하한(기준 예측 셋 대비), 운 보정이다.
    - 표본은 채점된 예측 수이고 하루 한 번 예측이라 종목마다 하루에 하나다(종목 × 날). 열 제목이 "표본(종목·일)"이고 툴팁이 그렇게 알린다. 기간의 오늘·5일·20일은 예측이 있는 최근 거래일이고, 오늘의 정답은 15:45 뒤라 "오늘"은 그때까지 0이다. 기간은 다른 종류와 같은 상자라 따로 바꾸지 않는다.
    - 손익은 엔진의 추가 통계(`extra`, `open_to_close_stats`)의 거래당 평균, 시가 대비 bp다. 상승 예측은 시가 단일가에 사서 종가 단일가에 팔고, 하락은 주식 공매도로 반대로, 보합은 거래하지 않는다. 비용(매도세와 수수료 두 번)을 뺀다. 칸의 툴팁에 거래 수와 합계(bp), 95% 구간, 롱(상승)·숏(하락)의 거래 수와 평균, 하락을 그 종목의 인버스 상품으로 친 값(전체와 숏, `pnl_inverse`·`short_inverse`, 매도세 없음)이 있다. 거래가 없으면(모두 보합 예측) "—"다. 열 제목의 툴팁이 어떻게 잰 값인지와 연구용 값이라는 것을 알린다.
    - 순위표 제목의 툴팁(순위 규칙)에 "하루 한 번 예측이다. 채점 한 건은 종목 하나의 하루이고 …", "상태(수습·벤치)는 순위와 따로, 정답이 있는 날 60·120·180일째에 판정한다(최근 60일)."가 더해진다. 순위에 오르는 표본 수는 엔진의 `min_sample`(지금 200)이다.
    - "특수 상황 포함" 툴팁은 "그날 서킷브레이커·사이드카가 있던 날의 예측도 센다(기본은 뺀다). VI는 특수 상황이 아니다(그날 그 종목의 VI 수는 정답에 남는다)."다(`StatsView`의 `special_tip`). 닿음 손익과 첫 열 **차트**는 없다.
    - 엔진은 같은 모양의 손익을 장벽(틱)과 시가→종가(bp)에 준다. 화면은 합계의 키(`total_ticks`, `total_bp`)로 둘을 가른다(`EngineData`의 `BarrierStats`, `OpenToCloseStats`).
    - 열과 칸은 `StatsView`의 `kind_stats_columns`·`kind_stats_cells`·`daily_pnl_shown`·`daily_pnl_header`·`daily_pnl_tip`, 순위 규칙은 `ranking_rules`(순수 함수, `tst_krx_stats_view`)가 정한다.
  - **5거래일 시가→종가 순위표**(codeyoma/krx-quant#310): `GET /v1/leaderboard?kind=open_to_close_5d`를 같은 조건으로 읽는다. 열은 시가→종가 순위표와 같고 손익 열은 "5거래일 시가→종가 손익(bp, 연구용)"이다. 지금 행은 기준 예측 셋뿐이다(위 "기준 예측만"). 다른 것은 겹침이다:
    - 엔진은 항목마다 `overlap_sessions`(5)와 `independent_days`(예측이 있는 거래일 ÷ 5, 내림)를 준다(`Entry`의 같은 이름, 옛 엔진은 1과 없음). `overlap_sessions`가 1보다 크면 표본 칸이 "45 (독립 3)"이고 칸의 툴팁이 "겹침 보정 · 5세션 창 · 독립 창 약 3개"(`StatsView`의 `overlap_text`)다. 열 제목의 툴팁이 괄호의 뜻(서로 독립인 창이 대략 몇 개인지)을 알린다. 정답률·정밀도·커버리지·Brier·손익 칸의 95% 구간 툴팁에 "겹침 보정(Driscoll–Kraay)"이 붙는다. 엔진이 겹침을 보정한 구간을 주므로 화면은 그 값을 그대로 보이고 이름만 붙인다.
    - 순위 규칙 툴팁의 하루 한 번 줄은 "채점 한 건은 종목 하나의 5세션 창이고, 기간의 날은 예측이 있는 최근 거래일(창이 시작한 날)이다. 정답은 창의 마지막 세션 밤(20:35부터)이라 최근 4거래일의 창은 아직 비어 있다."이고, 그 뒤에 "이웃한 날의 창이 4세션씩 겹친다. 정답률·정밀도·커버리지·Brier·손익의 구간과 짝 lift는 Driscoll–Kraay, 거래일 단위 lift(순위와 상태가 보는 하한)는 Newey–West로 겹침을 보정했고, 자유도는 독립인 창의 수(대략 날 수 ÷ 5) 수준이다. 보정 곡선은 보정하지 않았다. 표본 칸의 괄호가 독립인 창의 수다."가 붙는다. 상태 줄은 엔진의 판정일(60·120·180일째, 최근 60일)이다.
    - 손익 열 제목의 툴팁은 "그날 시가 단일가에 사서 마지막 세션 종가 단일가에 판다고 친다 … 날마다 새 창을 거래한다고 쳐서 한 종목에 포지션이 여럿 겹친다. 구간은 겹침을 보정한 Driscoll–Kraay다 … −2배 인버스 상품(날마다 재조정)"이다(`daily_pnl_tip`이 종류를 받는다).
    - "특수 상황 포함" 툴팁은 "세션 중 하루라도 서킷브레이커·사이드카가 있던 창의 예측도 센다(기본은 뺀다). VI는 특수 상황이 아니다(창의 그 종목 VI 수는 정답에 남는다)."다.
    - 대시보드의 KRX 순위표 위젯에서도 이 종류를 고를 수 있고, 손익 단위는 bp다(`DashboardView`의 `pnl_unit`).

  > **바뀜** (2026-10-15, codeyoma/krx-quant#182): 순위표는 늘 방향이었다 → 고른 예측 종류의 순위표다. 방향을 고르면 전과 같다. 이유: 위와 같다.
  - 체크한 모델이 있고 1분 보기이면 차트는 모든 예측자의 예측을 읽는다: 봉 마감마다 최근 60봉 마감, 왼쪽 끝에서는 지난 봉과 같은 수. 처음 체크하거나 다른 보기에서 1분 보기로 오면, 이미 읽은 범위를 최신부터 1,000봉 마감씩 다시 읽는다(`marks_before`, `full_from_after`). 왼쪽 끝 더 읽기는 기다리지 않고 봉과 같은 때에 읽는다(codeyoma/krx-quant#239). 체크한 모델이 없거나 3분 이상 보기이면 최종 합의와 "늘 보합"만 읽는다. 모든 예측자의 기록은 고른 종목만 두고, 종목을 바꾸면 비운다.

    > **바뀜** (2026-10-16, codeyoma/krx-quant#239): 다시 읽는 동안 왼쪽 끝 더 읽기(봉)는 기다렸고, 왼쪽 끝에서는 봉과 같은 수의 모든 예측자 예측을 봉 다음에 읽었다 → 봉은 기다리지 않고, 예측은 봉과 따로 같은 때에 1,000봉 마감씩 읽는다. 이유: 모델을 체크하면 지난 날로 드래그할 때 더 오래 기다렸다.

  > **바뀜** (2026-10-14, codeyoma/krx-quant#161): "차트의 ✓/✗도 여기서 고른 채점 기준을 따른다" → 순위표의 기준은 순위표에만 쓴다. 채점 기준 상자의 툴팁도 그렇게 바꿨다. 이유: 위 차트 절과 같다.

  > **바뀜** (2026-10-13, codeyoma/krx-quant#117): 열 이름 "거래당 손익(틱)" → "중간가 손익(틱)", 열 제목에 연구용 값이라는 툴팁. 이유: 따라 했을 때의 손익으로 읽히기 쉬웠다. 엔진의 손익은 중간가에서 중간가로, 스프레드와 체결 없이 잰다(codeyoma/krx-quant#78).
- **상세 통계 창**: 순위표의 줄을 두 번 누르면 그 예측(합의와 기준 예측도)의 창이 따로 열린다. 순위표의 줄(알고리즘, 합의, 기준 예측)마다 창이 하나라서 여러 줄을 나란히 띄워 견줄 수 있다. 이미 창이 열린 줄을 다시 누르면 그 창이 앞으로 온다. 순위표와 같은 조건(기간, 채점 기준, 종목, 특수 상황)으로 엔진의 상세 통계(`/v1/algorithms/{name}/stats`)를 읽는다.
  - 창 맨 위 **학습·예측 방식**(codeyoma/krx-quant#163): 엔진이 `GET /v1/algorithms`로 알려 주는 그 알고리즘의 설명이다. 줄마다 "성격"(카드의 성격과 과학자), "예측 로직", "예측 방식"(세 확률을 만드는 방법), "실패 조건"(틀리기 쉬운 장)(codeyoma/krx-quant#164), "고유 피처", "입력 피처"(피처마다 한 줄: `이름 — 뜻 (단위 · 언제 아는지)`), "학습 방법", "학습 기간"(최근 n거래일), "모델"(버전), "학습 결과"(엔진의 학습 글과 데이터의 끝 날. 학습한 적 없으면 "학습 기록 없음 (기본값)"), "파라미터"(이름 = 값, 이름순, 숫자는 유효숫자 6자리. 엔진이 뜻을 주면 파라미터마다 한 줄에 `이름 = 값 — 뜻 (단위 · 언제 아는지)`), "상태"(지금 상태와 그 시각), "상태 이력"(시각, 바뀐 상태, 이유)이다. 글은 모두 엔진의 것이고 화면은 받아서 보이기만 한다. 이 저장소에는 알고리즘 설명을 두지 않는다(테스트는 지어낸 값). 기준 예측과 합의의 창은 엔진이 같은 응답의 `baselines`·`consensus`로 주는 카드만 보인다: "성격", "예측 로직", "예측 방식", "실패 조건", "학습 방법"(codeyoma/krx-quant#164). 새 카드 필드가 없는 이전 엔진이면 그 줄은 빠진다. 뜻 필드(`feature_meanings`, `param_meanings`)가 없는 이전 엔진이면 피처·파라미터는 이름만 쉼표로 잇고, 필드는 있는데 어떤 이름의 뜻이 없으면 그 줄은 이름만이다. 머리줄 제목의 툴팁은 학습 결과의 손실이 학습 데이터로 잰 값이라 견줄 수 없다는 것을 알린다. 창을 열 때와 순위표를 다시 읽을 때(열린 창이 있을 때만) 함께 읽는다. 엔진 주소가 바뀌면 지난 엔진의 설명은 지운다. 설명을 읽지 못하면 지난 설명을 남기지 않고 "엔진에서 설명을 읽지 못했습니다: 이유"를 보인다. 통계만 읽지 못하면 설명은 그대로다. 아래 통계와의 경계는 끌어서 높이를 바꾼다.

  > **바뀜** (2026-10-14, codeyoma/krx-quant#164): 엔진이 카드를 주지 않아 기준 예측과 합의는 "엔진이 아직 설명을 주지 않음"이었고, 피처·파라미터는 이름만 보였다 → 기준 예측·합의 카드, 예측 방식·실패 조건, 이름마다 뜻. 이유: 각 예측자가 무엇을 배우고 확률을 어떻게 만드는지 상세 창에서 다 보려고(2026-10-14 인터뷰 Q10).
  - 그 아래 **학습과 검증**(codeyoma/krx-quant#165): 알고리즘 창에만 있다(기준 예측과 합의 창에는 없다). 같은 `GET /v1/algorithms`의 값으로 줄마다 "학습 데이터"(첫날 – 끝날 · 표본 수 · 정답 출처별 날 수: 녹화 호가, 1분봉), "학습 손실"(학습 데이터로 잰 값), "지난밤 검증"(밤 · 검증일 · 표본 수), "새 모델"과 "지금 모델"(버전 · 검증 손실, 검증일로 학습한 모델이면 그렇다고), "승격"(승격함·승격하지 않음 · 엔진이 적은 이유), 수습이나 벤치이면 "수습 단계"·"벤치 단계"(시작한 날 · 센 거래일 · 다음 판정 n일째와 예정일. 새 정답으로 학습한 모델이 아직 돌지 않으면 "새 정답으로 학습한 모델을 기다림", codeyoma/krx-quant#170. 센 날에 수습 시뮬레이션의 날이 들어 있으면 "20거래일 셈 (시뮬레이션 20일 포함)"이고 기다림은 보이지 않는다, codeyoma/krx-quant#298)와 "가장 약한 lift"(기준 예측 대비 값 · 거래일 하한 · 날 수 · 신뢰 수준), 엔진이 그 알고리즘의 수습을 시뮬레이션했으면 "수습 시뮬레이션"(재생한 첫날 – 끝날 · 날 수 · 학습 방식("날마다 그 전날까지로 다시 학습", "첫날 전까지로 한 번 학습") · 바로 한 판정("20일째 판정: 정식이 됨", "통과하지 못해 수습이 이어짐", "복제"). 시뮬레이션하지 못했으면 "하지 못함: 엔진의 이유")다. 이 줄은 정식이 된 뒤에도 남는다. 시뮬레이션 예측은 엔진이 순위표와 상세 통계에 넣지 않으므로 이 줄과 단계로만 보인다. "상태 이력"에서 시뮬레이션 날을 센 판정은 이유 앞에 "시뮬레이션 판정 · "이 붙는다(엔진의 `simulated`). 엔진이 학습 값을 주지 않으면(기본값, 이전에 저장한 모델) "엔진이 주지 않음", 밤 학습 리포트가 없으면 "밤 학습 리포트 없음"이다. 머리줄 제목의 툴팁은 학습 손실과 검증 손실이 서로 다른 데이터로 잰 값이라 견주지 않는다는 것과, 단계가 마지막 판정 때의 것이라는 것을 알린다. 설명을 읽지 못하면 이 절은 숨는다.

  > **바뀜** (2026-10-19, codeyoma/krx-quant#298): 단계 줄에 시뮬레이션 날, "수습 시뮬레이션" 줄과 상태 이력의 표시를 더했다. 이유: 엔진이 수습을 최근 거래일의 시뮬레이션으로 시작해 바로 판정하게 되어, 실시간 날과 구분해 보이게 하려고.

  - 모양은 본 탭과 같다(codeyoma/krx-quant#104): 창 이름은 Fincept 창처럼 "Fincept Terminal — KRX 4분 예측 — <예측> 상세 통계", 배경은 Fincept 바탕색이다. 패널 머리줄(그 예측, lift, 보정도, 나눠 본 통계), 공용 표, 테마 변경 때 다시 칠하기도 같다. 처음 읽는 동안은 Fincept 읽는 중 표시가 덮고, 읽지 못하면 머리줄에 빨간 "읽지 못함"이 나오고 이유는 그 툴팁에 있다.
  - 뜻은 툴팁에 있다. lift 머리줄 제목은 lift가 무엇인지와, 없으면 왜 없는지(기준 예측 자신, 짝지을 채점된 예측 없음)를 알린다. "95% 구간"(봉 마감마다 센 설명용, p도), "거래일 95% 구간"(하루를 값 하나로 본 구간, 상태 판정 근거), "q", "운 보정" 열 제목에도 툴팁이 있다. 보정도 머리줄 제목은 그림이 무엇을 견주는지와, 채점된 예측이 없으면 그것을 알린다. 나눠 본 통계 머리줄 제목은 그 줄들의 lift를 함께 검정하지 않는다는 것을 알린다. 보정도 그림의 축 제목은 그대로다.

  > **바뀜** (2026-10-13, codeyoma/krx-quant#128): 상세 통계 창은 화면에 하나였고, 다른 줄을 두 번 누르면 그 줄로 바뀌었다 → 순위표의 줄마다 창이 하나다. 이유: 여러 알고리즘을 나란히 띄워 견주려고(codeyoma/krx-quant#100). 엔진은 열린 창마다 그 예측의 통계를 따로 읽는다.

  > **삭제됨** (2026-10-13, codeyoma/krx-quant#128): 창 아래의 회색 설명(lift, 95% 구간과 p, 거래일 구간, q와 운 보정, 보정도의 뜻, lift가 없는 이유)과 보정도 그림 안의 "채점된 예측이 없습니다", 그리고 회색 "읽는 중…"·실패 문구. 이유: 화면을 Fincept의 다른 화면 모양으로 맞추고 회색 설명을 없앴다(codeyoma/krx-quant#100, #104). 대신: 머리줄 제목과 열 제목의 툴팁(채점된 예측이 없으면 보정도 제목의 툴팁이 알린다), Fincept 읽는 중 표시, 머리줄의 "읽지 못함".

  > **바뀜** (2026-10-13, codeyoma/krx-quant#128): 테두리 상자(lift, 보정도)와 맨 위 조건 줄 → 패널 머리줄. 조건과 날짜는 창 머리줄의 오른쪽에 있다. 이유: 위와 같다.
  - 맨 위: 조건과 날짜, 그 예측의 순위와 통계(순위표와 같은 통계 열. 순위가 없으면 "순위 밖"), 그 아래 제외 건수("제외: 특수 상황 2 · 채점 불가 1 · 시간 초과 3 · VI 4". 없으면 "제외 없음". 특수 상황 포함 조건이면 그 예측은 빼지 않고 넣었으므로 앞에 따로 "특수 상황 포함 2 · 제외: …")
  - **기준율 대비 실력**(codeyoma/krx-quant#262): 제외 건수 아래 한 줄 표다. 엔진 상세 통계의 `overall.skill`을 보인다: 기대 정답률(같은 비율로 상승·보합·하락을 결과와 상관없이 냈을 때의 정답률), 기대 대비(위 정답률 − 기대 정답률, `%p`), kappa, 거래일 kappa(엔진의 `skill.by_day`, codeyoma/krx-quant#278: 날마다 kappa를 낸 평균과 t분포 95% 구간 `+0.162 (+0.078 – +0.246)`. 날 수와 한쪽 p는 툴팁. kappa가 있는 날이 2일보다 적거나 이전 엔진이면 "—"), Gerrity, 균형 정답률, 독립성 검정(`p 0.007 (χ² 14.23, 자유도 4)`, p가 0.001보다 작으면 `<0.001`). kappa와 Gerrity는 부호를 붙여 소수 셋째 자리다. 낼 수 없는 값은 "—"이고, 왜 없는지(한 클래스뿐, 상승이나 하락이 없음, 기대 빈도 5 미만)는 그 열의 툴팁에 있다. 열 제목과 값의 툴팁은 그 지표의 뜻이고, 머리줄 제목의 툴팁은 보합이 많은 날에는 늘 보합도 정답률이 높아 기준율과 견준다는 것과, 설명용이고 상태 판정과 순위는 lift로 한다는 것을 알린다. 방향 예측자의 창에만 있고, `skill`이 없는 이전 엔진이나 채점된 예측이 없으면 숨는다. 순위표에는 열을 더하지 않았다(2026-10-17 결정).
  - **기준 예측 대비 lift**: 기준 예측 셋 각각에 대한 lift, 95% 구간, 짝 수, p, q, 운 보정. 순위 밖이라 함께 보정하지 않은 lift는 q와 운 보정이 "—"다. 그 기간에 기준 예측과 짝지을 봉 마감이 없으면 lift도 "—"다(짝 0). 거래일 95% 구간과 날 수도 있다. 하루를 값 하나로 본 구간이고, 엔진은 상태(수습·정식·벤치)를 이것으로 정한다. 봉 마감마다 센 95% 구간과 p는 같은 날의 4분 정답이 겹쳐 실제보다 좁은 설명용이다. 날이 2일보다 적으면 "—"다.
  - **보정도**: 확률을 10% 구간으로 나눠, 가로에 구간의 평균 예측 확률, 세로에 실제로 일어난 비율과 95%(Wilson) 구간을 찍는다. 곡선은 넷이다: 세 클래스를 합친 전체(주황), 상승(초록), 보합(회색), 하락(빨강). 오른쪽 아래에 범례가 있다. 점선은 완벽한 예측이다. 예측이 없는 구간은 찍지 않는다. 점에 마우스를 올리면 곡선 이름, 구간, 예측 수, 평균 예측, 실제 비율이 나온다. 같은 자리의 점은 함께 나온다.

    > **바뀜** (2026-10-13, codeyoma/krx-quant#119): 보정도는 세 클래스를 합친 곡선 하나였다 → 클래스별 곡선도 그리고, 맨 위에 제외 건수를 보인다. 이유: 합친 곡선이 방향별 보정 오차를 숨겼고, 무엇이 빠졌는지 보이지 않았다(codeyoma/krx-quant#83).
  - **30분 시간대별, 모델 버전별, 종목별** 탭: 순위표와 같은 통계 열이다. 시간대는 예측한 봉의 마감 시각으로 쓴다("09:01–09:30"). 이 줄들의 lift는 함께 검정하지 않는다.
  - **변동성별** 탭(codeyoma/krx-quant#262): 엔진의 `by_volatility`를 낮음·중간·높음 순서로 보인다. 열은 다른 탭과 같다. 변동성은 그 봉 마감에 장벽 종류가 저장한 폭 w를 중간가의 bp로 바꾼 값이고, 종목마다 그 기간 모든 봉 마감의 w로 3분위를 자른다. 탭의 툴팁이 이것과 종목마다의 경계(`005930: 낮음 ≤ 87.4bp < 중간 ≤ 89.3bp < 높음`, 엔진의 `volatility_cuts`)를 보인다. 장벽 폭이 없으면(장벽 종류가 없는 엔진, 이전 엔진) 표가 비고 툴팁이 나누지 못했다고 알린다. 방향 예측자의 창에만 있다.
  - **변동폭 예측자의 창**(codeyoma/krx-quant#182): 엔진은 예측자의 종류로 통계를 준다(`/stats`의 `kind`). 맨 위 통계 줄과 나눠 본 탭은 변동폭 순위표의 열이다. lift는 변동폭 기준 예측 둘(시간대 기후, 직전 4분) 대비다. 보정도는 전체와 작음(회색)·보통(청록)·큼(파랑) 곡선이다. lift 표 아래에 **분위수 통계**가 있다(끌어서 높이를 바꾼다): 요약 한 줄("구간 적중률(q10~q90) 78.0% (95% 72.3–82.9%, 이상 80%) · q50 오차 2.40틱 (95% 2.10 – 2.70) · 표본 240")과 분위수마다 pinball loss(틱)와 95% 구간, 거래일 95% 구간과 날 수, 시간대 기후 대비 차이(음수가 낫다)와 그 거래일 95% 구간이다. 시간대 기후 자신은 대비가 "—"다. 머리줄 툴팁이 pinball loss와 대비의 뜻(설명용, 판정은 lift)을 알린다. 변동폭 기준 예측의 학습·예측 방식은 `GET /v1/kinds`의 카드다(`/v1/algorithms`에는 방향의 기준 예측만 있다). 문구는 `StatsView`의 `quantile_rows`·`range_summary`(`tst_krx_stats_view`)가 정한다.
  - **장벽 예측자의 창**(codeyoma/krx-quant#185): 맨 위 통계 줄과 나눠 본 탭(30분 시간대별, 모델 버전별, 종목별)은 장벽 순위표의 열(장벽 손익 포함)이다. lift는 장벽 기준 예측 둘 대비다. 보정도는 전체와 위 먼저(초록)·시간 만료(회색)·아래 먼저(빨강) 곡선이다. lift 표 아래에 **장벽 손익(틱, 연구용)**이 있다(끌어서 높이를 바꾼다): 요약 한 줄("채점 240개 중 거래 121건 (롱 60 · 숏 61) · 거래당 -3.13틱 (95% -3.86 – -2.40) · 합계 -378.4틱". 거래가 없으면 "채점 252개 · 거래 없음 (모두 시간 만료 예측)")과 전체, 롱(위 먼저), 숏(아래 먼저)마다 거래 수, 합계(틱), 거래당(틱), 95% 구간(정규근사)이다. 머리줄 툴팁은 순위표 열 제목의 것과 같다. 장벽 기준 예측의 학습·예측 방식은 `GET /v1/kinds`의 카드다. 문구는 `StatsView`의 `barrier_pnl_rows`·`barrier_summary`·`barrier_pnl_shown`(`tst_krx_stats_view`)가 정한다.
  - **시가→종가 예측자의 창**(codeyoma/krx-quant#264): 맨 위 통계 줄과 나눠 본 탭(모델 버전별, 종목별)은 시가→종가 순위표의 열(표본(종목·일), 손익 포함)이다. **30분 시간대별** 탭은 없다(예측이 모두 08:50 하나라 엔진의 `by_slot`이 늘 `08:30` 하나다). lift는 기준 예측 셋 대비이고, lift 머리줄 툴팁은 "같은 종목·날의 기준 예측과 정답(1/0) 차이의 평균", "95% 구간" 열 제목의 툴팁은 "예측(종목 × 날)마다 따로 센 설명용 구간이다. 같은 날의 종목들은 함께 움직여 실제보다 확실해 보일 수 있다.", "짝"은 "같은 종목·날에 둘 다 채점된 예측의 수"다(`StatsView`의 `lift_header_tip`, `lift_tip`). 보정도는 전체와 상승(초록)·보합(회색)·하락(빨강) 곡선이다. lift 표 아래에 **시가→종가 손익(bp, 연구용)**이 있다(장벽 손익과 같은 자리, 끌어서 높이를 바꾼다): 요약 한 줄("채점 57개 중 거래 35건 (롱 24 · 숏 11) · 거래당 -48.29bp (95% -85.50 – -11.09) · 합계 -1,690.2bp". 거래가 없으면 "채점 57개 · 거래 없음 (모두 보합 예측)")과 전체, 롱(상승), 숏(하락), 숏(인버스 상품)마다 거래 수, 합계(bp), 거래당(bp), 95% 구간이다. 머리줄 툴팁은 순위표 열 제목의 것과 같다. 기준 예측 셋의 학습·예측 방식은 `GET /v1/kinds`의 카드, `cox`의 것은 `GET /v1/algorithms`다. 문구는 `StatsView`의 `daily_pnl_rows`·`daily_pnl_summary`(`tst_krx_stats_view`)가 정한다.
  - **5거래일 시가→종가 예측자의 창**(codeyoma/krx-quant#310): 시가→종가 예측자의 창과 같다(30분 시간대별 탭 없음, 손익 절의 제목은 "5거래일 시가→종가 손익(bp, 연구용)"). 머리줄의 질의 글 끝에 " · 겹침 보정 · 5세션 창 · 독립 창 약 3개"가 붙는다(엔진의 `overlap_sessions`·`independent_days`, `overlap_text`). 맨 위 통계 줄과 나눠 본 탭의 표본·구간 칸은 순위표의 것과 같다("45 (독립 3)", "겹침 보정(Driscoll–Kraay)"). lift 표의 열 제목 툴팁은 겹침을 말한다: "95% 구간"은 "겹침을 보정한 Driscoll–Kraay 구간이다 …", "거래일 95% 구간"은 "하루를 값 하나로 보고 Newey–West로 겹침을 보정한 구간이다. 자유도는 독립인 창의 수(대략 날 수 ÷ 5) 수준이다. 상태(수습·정식·벤치)는 이것으로 정한다.", "짝"은 "… 이웃한 날의 창이 겹쳐 서로 독립이 아니다."(`lift_header_tip`). 보정도 툴팁 끝에 "겹침 보정 없음: …"이 붙는다(엔진이 보정 곡선은 보정하지 않는다, `calibration_tip`). 지금 행이 있는 예측자는 기준 예측 셋이라 lift가 없다.
  - 제외 건수의 `no_previous`(직전 창을 채점할 수 없어 기준 예측이 예측하지 않은 회차)는 "직전 창 없음"이다.

  > **바뀜** (2026-10-15, codeyoma/krx-quant#182): 제외 건수에 `no_previous`가 그 코드 그대로 나왔다 → "직전 창 없음". 이유: 변동폭의 직전 4분 기준 예측 창에서 영어 코드가 보였다. 방향의 "기준: 직전 4분 방향" 창도 같이 바뀐다.
- **봉 창** (codeyoma/krx-quant#255): 1분 보기에서 차트의 봉 칸을 두 번 누르면(위 차트 절) 그 봉(봉 마감 = 봉 시작 + 1분)의 창이 따로 열린다. 봉마다 창이 하나라서(상세 통계 창처럼) 여러 봉의 창을 나란히 띄워 견줄 수 있다. 이미 열린 봉을 다시 두 번 누르면 그 창이 앞으로 온다. 창은 연 때 고른 종목의 것이고, 다른 종목을 골라도 남는다. 화면을 닫으면 함께 닫힌다.
  - 모양은 상세 통계 창과 같다: 창 이름은 "Fincept Terminal — KRX 4분 예측 — 삼성전자 005930 · 2026-10-12 10:24 봉", 바탕은 Fincept 바탕색, 패널 머리줄, 공용 표, 테마 변경 때 다시 칠하기. 처음 읽는 동안은 Fincept 읽는 중 표시가 덮고, 읽지 못하면 머리줄에 빨간 "읽지 못함"이 나오고 이유는 그 툴팁에 있다(보이던 것은 남는다). 머리줄 오른쪽은 상태다: "채점 전 · 갱신 중", "채점됨", "채점 불가", "채점되지 않음"(채점된 예측이 없음). 절이 창보다 길면 창 안에서 스크롤한다.
  - 제목은 회사 이름, 종목 코드, 날짜, 봉 시각이다. 봉 시각은 차트의 호버처럼 봉 마감으로 부른다: "10:24 봉"은 10:23–10:24의 봉이다(호버의 "10:24 봉 → 10:28 목표"와 같다). 이름을 모르면(엔진이 이름을 주지 않음) 종목 코드만이다.
  - **봉 정보**: 봉 시각(시작–끝), 시가·고가·저가·종가, 거래량, 체결 수(지난 날의 봉은 엔진이 1분봉 파일에서 읽어 모르므로 "—"), 앞 봉 대비(원과 %, 그리고 앞 봉과 그 종가. 앞 봉은 체결이 있는 바로 앞의 봉이고, 그날 첫 봉이면 지난 거래일의 마지막 봉이다). 체결 없는 분(빈 봉)이면 "체결 없음", 아직 끝나지 않은 봉이면 "만드는 중"이다. 그 봉부터 채점 시각까지 걸친 특수 상황(VI, 서킷브레이커, 사이드카)과 그 구간("동적 VI 09:40:10–09:42:20", 차트 띠의 이름), 없으면 "없음"이다.
  - **정답 상세**: 봉 마감 통합 중간가, 호가단위, k(보합 폭), 위·아래 도달 목표(중간가 ± k틱), 도달 정답("정답 상승 (09:40:23 닿음)", 채점 전, 채점 불가와 이유, 특수 상황이면 " · 특수 상황"), 위·아래 목표에 처음 닿은 시각과 그때의 매수·매도 1호가(닿지 않았으면 "닿지 않음"), 엔진이 변동폭을 예측하면 실제 변동폭 r과 그 구간, 실제 최고·최저 중간가와 그 시각, 엔진이 장벽을 예측하면 장벽 w·U·L, 먼저 닿은 장벽과 그 시각, U·L에 처음 닿은 시각과 그때의 호가, 그리고 채점 시각이다. 중간가·호가단위·k는 정답이 나오면 정답의 것, 그 전에는 다른 종류의 예측과 함께 온 봉 마감의 값(예측 항목의 `anchor`, `tick`, `flat_ticks`, codeyoma/krx-quant#200, #210)이다. 그래서 채점 전에도 목표와 U·L을 안다. 그 값을 주지 않는 옛 엔진에서는 채점 전에 "—"다.
  - **4분 미니 차트**: 그 봉과 봉 마감부터 채점 시각까지의 1분봉 4개를 캔들로 그리고(한 분에 한 칸, 체결 없는 분은 "체결 없음"), 봉 마감부터 채점 시각까지를 옅게 칠한다. 그 4분에 위·아래 도달 목표, 장벽 U·L, 실제 최고·최저 중간가를 가로선으로 긋고, 오른쪽에 이름과 가격을 적는다(겹치면 벌려 적고 가는 선으로 잇는다). 처음 닿은 곳(초 단위)에 점을 찍는다: 위 목표는 그때의 매수 1호가, 아래 목표는 매도 1호가, U·L도 같고, 최고·최저 중간가는 그 값이다. 뜻과 색은 차트의 4분 구간 호버(codeyoma/krx-quant#167, #182, #185)와 같다: 위쪽 선과 점은 초록(오름), 아래쪽은 빨강(내림). 점은 캔들 위에서도 보이게 바탕색 테두리를 둘렀다. 정답이 나오기 전의 선은 점선이고(도달 목표는 도달 정답, U·L은 장벽 정답 전), 4분이 끝나기 전에는 칠한 구간의 테두리도 점선이다. 캔들이나 점에 마우스를 올리면 그 봉의 값이나 무엇이 언제 닿았는지가 나온다. 선과 점과 자리는 `BarView`의 `mini_chart`·`mini_x`·`mini_y`·`spread_labels`(순수 함수, `tst_krx_bar_view`)가 정한다.
  - **방향 예측**: 그 봉 마감의 모든 방향 예측이다. 고정한 패널(위 예측 패널)과 같은 `close_rows`로, 체크한 모델이 있으면 맨 위에 선택 합의(codeyoma/krx-quant#159 규칙, 굵게), 빠른·최종 합의(굵게), 알고리즘(이름순), 기준 예측(이름순, 흐리게) 순이다. 열은 예측, 상태(지금 수명 상태, `/v1/algorithms`), 상승·보합·하락, 방향, 기대 틱, 합의(최종 합의의 `pooled`로 포함·제외), 모델, 결과(봉 마감의 정답으로 맞음·틀림)다. 머리줄 오른쪽에 정답이 있다. 순위표에서 체크한 것을 바꾸면 선택 합의 줄도 바뀐다.
  - **변동폭 예측**, **장벽 예측**: 화면에서 고른 예측 종류와 상관없이 그 봉 마감의 변동폭·장벽 예측자마다 각 패널(codeyoma/krx-quant#182, #185)과 같은 줄과 결과 열이다(`kind_panel_columns`, `kind_rows`). 머리줄 오른쪽에 정답과 k·2k(변동폭), w·U·L(장벽)이 있다. 엔진이 그 종류를 예측하지 않으면 "엔진이 이 종류를 예측하지 않는다", 그 봉 마감의 예측이 없으면 "그 봉 마감의 예측 없음", 읽지 못하면 "읽지 못함: 이유"만 적는다.
  - 엔진의 기존 API만 읽는다(`KrxQuantService::read_bar`, 한 번에 함께): `GET /v1/symbols/{s}/predictions?bar_close=`, `GET /v1/algorithms`(수명 상태), `GET /v1/symbols/{s}/bars?before=<채점 시각>&limit=10`(그 봉, 뒤의 4개, 앞 봉), 엔진이 예측하는 변동폭·장벽마다 `GET /v1/symbols/{s}/kinds/{kind}/predictions?bar_close=`, `GET /v1/symbols/{s}/special?from=<그날>&to=<그날>`. 예측이나 봉을 읽지 못하면 "읽지 못함"이다. 수명 상태, 특수 상황(그 응답이 없는 옛 엔진도), 한 종류의 예측을 읽지 못하면 그것만 빈다.
  - 무엇을 보일지는 `services/krx_quant/BarView`(순수 함수, `tst_krx_bar_view`)가 정한다: `bar_title`, `bar_info_lines`, `answer_lines`, `kind_sections`, `mini_chart`, 다시 읽을지(`bar_settled`)와 상태 글(`bar_state_text`).
- **신호 알림**: 엔진의 텔레그램 신호와 같은 규칙이다(krx-quant `docs/engine-api.md` "텔레그램 신호 알림"). 엔진이 최종 합의에 막을 이유(`blocked`: VI·서킷브레이커, 기준가 없음, 목표 시각 지남)를 실어 보내면 알리지 않는다.

  > **바뀜** (2026-10-13, codeyoma/krx-quant#124): 확률·동의·만료·중복만 보고 알렸다 → 엔진이 막은 합의로는 알리지 않는다. 이유: 텔레그램 신호와 같은 판정을 따르게 하려고(codeyoma/krx-quant#88).
  - 최종 합의의 상승 확률이 기준값 이상이면 상승, 아니면 하락 확률이 기준값 이상일 때 하락이다.
  - 그 합의에 들어간 정식 알고리즘 가운데 N개 이상이 같은 방향을 예측해야 한다. 각자의 방향은 가장 큰 확률의 클래스다(동률이면 보합).
  - 같은 종목·같은 방향은 앞 신호의 채점 시각(4분 뒤)까지 다시 알리지 않는다. 채점 시각이 지난 합의(장 마감 뒤에 화면을 연 경우 등)는 알리지 않는다. 한 봉 마감은 한 번만 본다. 합의 구성원을 아직 모르면(읽기 실패 뒤 다시 읽는 중) 그 합의는 알 때까지 미뤘다가 본다.
  - 이 화면을 처음 연 뒤부터 다른 화면을 보는 동안에도 알린다. Fincept는 화면을 처음 열 때 만들고, 탭을 닫으면 없앤다. 그래서 앱을 켠 뒤 이 화면을 한 번도 열지 않았거나 탭을 닫으면 알림도 없다.
  - 알림은 Fincept 알림으로 나간다(`ToastService`: 앱 안 알림과 OS 알림). 소리를 켜면 OS 기본 알림음이 난다. **시험 알림** 버튼으로 OS 알림 권한과 소리를 확인한다.
  - 켜기(기본 켬), 기준값(50–99%, 기본 60%), N(1–20개, 기본 2개), 소리(기본 켬)는 화면에서 바꾸고 QSettings `krx_quant/signal_on`, `krx_quant/signal_probability`, `krx_quant/signal_agree`, `krx_quant/signal_sound`에 남는다.

### 차트 라이브러리

차트는 [TradingView Lightweight Charts™](https://www.tradingview.com/lightweight-charts/) v5.2.1로 그린다. Copyright (c) 2026 TradingView, Inc. https://www.tradingview.com/ . Apache License 2.0이다. 라이선스는 출처 표시와 tradingview.com 링크를 요구한다. 그래서 차트 머리줄 제목의 툴팁과 이 절에 둔다. `fincept-qt/src/screens/krx_quant/chart/`에 npm 패키지의 `lightweight-charts.standalone.production.js`를 고치지 않고 넣었고, `LICENSE`와 `NOTICE`도 같이 둔다(앱의 Qt 리소스에도 들어간다). 판을 올릴 때는 이 세 파일과 이 절, 툴팁의 문구를 함께 바꾼다.

### 갱신

- 엔진의 스트림(`/v1/stream`, WebSocket)을 따른다. 예측, 합의, 채점, KIS 상태, 알고리즘 상태가 오는 대로 반영한다. 스트림은 연결 뒤의 일만 보내므로, 연결하거나 다시 연결하면 API를 먼저 다시 읽는다. 답과 스트림이 어떤 순서로 오든 더 늦은 봉 마감을 이른 것으로, 채점된 예측을 채점 전 것으로 되돌리지 않는다.
- 끊기면 위쪽 엔진 상태 줄에 이유와 다시 연결까지 남은 초를 보이고, 1, 2, 4, …초(최대 30초) 간격으로 끝없이 다시 연결한다(엔진은 매일 아침 다시 시작한다). 15초마다 ping을 보내고 45초 동안 아무 응답이 없으면 끊고 다시 연결한다(잠자기에서 깬 노트북 등).
- 스트림이 연결되어 있으면 API는 상태만 5초마다(피드 지연), 종목·최신 예측·합의 구성원은 30초마다, 순위표는 60초마다 읽는다. 차트의 지금 만들어지는 봉은 스트림의 `bar`로 움직이고(codeyoma/krx-quant#110), 끝난 봉은 새 봉 마감이 올 때마다 다시 읽는다. 채점이 오면 3초 뒤 순위표를 바로 읽는다. 매크로는 5분마다다. 고른 종목의 특수 상황 구간(차트의 띠, codeyoma/krx-quant#236)은 60초마다와 읽은 봉의 날이 바뀔 때 읽는다. 스트림이 없으면(끊김, Qt WebSockets 없는 빌드) 상태·예측·합의 구성원·순위표를 5초마다, 차트를 새 봉 마감마다와 60초마다 읽는다. 채점과 봉을 알려줄 스트림이 없어서다. 매크로는 스트림이 있을 때와 같다.

  > **바뀜** (2026-10-13, codeyoma/krx-quant#110): 스트림이 있어도 차트를 60초마다 다시 읽었다 → 스트림이 있으면 지금 만들어지는 봉은 `bar` 이벤트로 움직이고, 차트는 새 봉 마감마다만 다시 읽는다. 60초마다 읽기는 스트림이 없을 때만 한다. 이유: 봉이 실시간으로 움직이게 했다(codeyoma/krx-quant#100).
- 예측 종류(codeyoma/krx-quant#182): 종류 목록(`GET /v1/kinds`)은 시작할 때, 엔진 주소가 바뀔 때, 스트림이 (다시) 연결될 때, 읽기 실패에서 돌아올 때 읽는다. 방향 말고 다른 종류를 고른 동안은 고른 종목의 그 종류 최신 예측(`…/kinds/{kind}/latest`)을 종목·최신 예측과 같은 주기(스트림이 있으면 30초, 없으면 5초)로, 호버용 예측(`…/kinds/{kind}/predictions`)은 차트를 읽을 때마다 최근 400봉 마감을 읽고, 차트가 지난 날로 스크롤되면 보이는 첫 봉까지 1,000봉 마감씩 더 읽는다(`kind_read`). 스트림의 `kind_prediction`·`kind_answer`(엔진 이름은 `prediction_kind`)가 오는 대로 반영하고, 정답이 오면 3초 뒤 순위표를 읽는다. 정답은 그 봉 마감의 모든 예측에 붙고, 정답보다 늦게 도착한 읽기가 정답을 지우지 않는다(`KindBoard`, `tst_krx_board`). 방향을 고른 동안은 다른 종류의 이벤트와 읽기가 없다.
- 자동 매매의 매수·매도(codeyoma/krx-quant#398): 005930을 고른 동안 차트를 읽을 때마다 최신 쪽(1,000개)을 읽고, 지난 날의 봉을 읽으면 읽은 첫 봉까지 1,000개씩 더 읽는다(위 차트 절). 스트림 메시지는 없다.
  - 시가→종가(codeyoma/krx-quant#264)와 5거래일 시가→종가(codeyoma/krx-quant#310, `…/kinds/open_to_close_5d/predictions`)의 기록은 날 단위로 읽는다(`KindView`의 `daily_read`, `daily_from_after`): 처음에 최근 250일(`…/kinds/open_to_close/predictions?limit=250`, `limit`은 날의 개수), 그 뒤 차트를 읽을 때마다 최근 5일(정답을 놓쳤을 때를 위해), 그리고 차트가 가진 가장 이른 날(1분봉이나 일 보기의 일봉 중 이른 것, `KrxLive::first_day_held`)까지 그 앞의 날을 한 번에 최대 1,000일씩 읽는다. 일 보기가 앞의 일봉을 읽으면 그만큼 더 읽고, 엔진이 그 앞에 예측이 없다고 하면(빈 쪽) 더 읽지 않는다. 08:50의 `kind_prediction`과 15:45부터의 `kind_answer`는 다른 종류처럼 스트림으로 온다(키는 그날 08:50).
- 봉 창(codeyoma/krx-quant#255): 창을 열면 그 봉을 바로 읽는다. 오늘의 봉이라 채점 전이면 창이 열린 동안 늦게 온 모델 결과와 채점 결과가 오는 대로 다시 읽는다: 스트림이 그 봉 마감의 예측·합의·채점·다른 종류의 예측과 정답을 가져오거나, 그 종목의 더 늦은 봉 마감을 가져오면(4분 중 한 분이 끝나 봉이 하나 더 있다) 1초 뒤에 읽는다. 스트림이 없으면 5초마다, 있어도 1분마다 읽는다. 다 왔으면(`bar_settled`: 4분이 지났고 방향이 채점되고(예측한 것이 없으면 그만) 엔진이 예측하는 변동폭·장벽의 정답이 다 왔음) 더 읽지 않는다. 변동폭이나 장벽을 읽지 못했으면(엔진 오류 등) 그 종류는 다시 읽는다. 지난 날의 봉은 한 번만 읽되 읽지 못한 종류가 있으면 읽을 때까지 다시 읽고, 채점 시각에서 10분이 지나면 무엇이 왔든 그만 읽는다(엔진이 따라가지 못한 창은 채점되지 않는다). 엔진 주소가 바뀌거나 엔진이 예측하는 종류가 바뀌면 다시 읽는다. 창을 닫으면 그 봉은 읽지 않는다. 화면이 가려져 있어도(창은 다른 화면 옆에 열려 있을 수 있다) 같다.
- 순위표를 읽는 중에 조건을 바꾸면, 읽기가 끝난 뒤 마지막 조건으로 다시 읽는다. 끝난 읽기의 답(실패 포함)은 버린다. 마지막 조건이 읽는 중인 것과 같으면 그 답을 쓴다(엔진이 5초보다 느려도 스트림 없이 5초마다 묻는 읽기가 답을 받는다).
- 상세 통계 창이 열려 있으면 순위표를 읽을 때마다(조건을 바꿀 때도) 열린 창마다 그 줄의 상세 통계를 같이 읽는다. 줄마다 따로 읽어서, 한 창의 읽기가 다른 창을 기다리지 않는다. 한 예측의 통계를 읽는 중에 조건을 바꾸면 위와 같이 마지막 조건으로 다시 읽는다. 창을 닫으면 그 예측은 읽지 않는다.
- 화면이 가려져 있어도 스트림은 열어 두어 신호 알림이 온다. 그동안 스트림이 없고 알림이 켜져 있으면 상태와 예측을 5초마다 읽는다. 차트와 매크로는 읽지 않고, 순위표는 스트림으로 채점이나 알고리즘 상태 변경이 온 뒤에만 읽는다. 상세 통계 창은 다른 화면 옆에 열려 있을 수 있어서, 스트림이 없으면 열린 창들의 통계를 5초마다 읽는다. 다른 KRX 패널이 엔진 주소를 바꾸면 가려진 패널도 5초 안에 새 주소로 옮긴다.
- 한 종류의 읽기가 끝나기 전에는 같은 읽기를 다시 묻지 않는다. 읽기에 실패하면 지난 예측과 차트를 지우고 이유를 보여준다(합의 구성원은 남긴다). 다시 읽히면 예측, 매크로, 순위표, 차트를 다시 읽는다.
- 엔진의 시계가 화면에 보이는 봉 마감보다 이르면(다시 띄운 데모 엔진) 그 뒤의 예측을 버리고 엔진을 따라간다.

> **바뀜** (2026-10-11, krx-quant #64): 화면이 보이는 동안 5초마다 모든 것을 API로 읽었다 → 스트림으로 받고, API는 연결·재연결 때와 위의 주기로 읽는다. 화면이 가려져도 스트림은 열어 둔다. 이유: 스트림으로 갱신하고 끊기면 다시 연결한다(#21). 다른 화면을 보는 동안에도 신호 알림이 와야 한다.

> **바뀜** (2026-10-11, krx-quant #64): 표 하나(종목마다 모든 예측) → 종목 목록과 고른 종목의 예측 패널. 알고리즘과 기준 예측은 엔진이 준 순서 → 이름순. 이유: 차트와 패널을 한 종목씩 보여주게 됐고(#21), 스트림이 알고리즘을 끝난 순서대로 가져와 줄이 매분 바뀌었다.

> **바뀜** (2026-10-12, krx-quant #74): 달러선물은 07:30 읽기만 → 장중에는 엔진 상태의 마지막 체결가와 전일 종가 대비 등락. 이유: 그날의 달러선물 흐름을 매크로 줄로 본다(krx-quant #70).

### 개발

- 엔진 없이 화면을 만들 때는 krx-quant 저장소에서 데모 엔진을 띄우고 주소를 `http://127.0.0.1:8090`으로 바꾼다. 고정된 가상 데이터다.
  - `uv run krx-quant demo-engine`: 10:30에 멈춘 아침이다. 스트림에는 연결할 때의 KIS 상태 하나만 온다.
  - `uv run krx-quant demo-engine --pace 2`: 10:00에 열고 10:30까지 1분을 2초마다 이어 재생하며 스트림을 보낸다. 차트, 패널, 채점이 움직이는 것을 볼 수 있다. 데모 아침의 최종 합의는 50%를 거의 넘지 않아 신호는 나오지 않는다. 알림은 **시험 알림**으로 본다.
  - 데모 아침에는 09:06–09:36 서킷브레이커 1단계(세 종목 모두 그 사이 체결 없음)와 000660의 동적 VI(09:40:10–09:42:20)가 있다(krx-quant #236). 빈 봉과 특수 상황 띠를 볼 때 쓴다. 다른 데모 엔진이 8090을 쓰고 있으면 `--port`로 다른 포트에 띄운다.
  - 데모 엔진에는 005930의 지어낸 자동 매매가 있다(codeyoma/krx-quant#398): 10-07·10-08 오후에 하나씩, 10-12 아침에 일곱(10:05까지 모의, 그 뒤 실전, 마지막 매매는 10:30에도 들고 있다). 데모의 호가로 모의 체결 규칙대로 채웠다. 차트의 매수·매도 표시와 색을 볼 때 쓴다. 속도를 준 데모(`--pace`)는 처음부터 아침의 모든 매매를 주고, 화면은 그 봉이 온 뒤에 그린다.
- 테스트(Qt Core만): `cmake -B build -DFINCEPT_BUILD_TESTS=ON … && cmake --build build --target tst_krx_engine_data tst_krx_board tst_krx_signals tst_krx_stats_view tst_krx_chart_model tst_krx_kind_view tst_krx_app_release tst_krx_chart_slots tst_krx_bar_view tst_krx_dashboard_view tst_krx_briefing tst_krx_flow tst_krx_cost tst_krx_closing tst_krx_trader tst_krx_trader_screen && ctest --test-dir build -R krx`.
  - CI는 경고를 오류로 빌드한다(`-Werror`, 환경 변수 `CI`가 있을 때). 로컬에서 같은 조건으로 보려면 `CI=1 cmake -B build …`로 설정한다. 그러지 않으면 구조체에 필드를 더하고 테스트의 초기화 목록을 그대로 두는 것 같은 경고를 놓친다(`custom-v4.5.0-krx.4` 맥 빌드가 이것으로 실패했다). 이 경고는 clang에서 난다. `custom` 대상 PR에서는 `custom-pr-check`가 리눅스의 clang으로 KRX 테스트를 `CI` 조건(`-Werror`)으로 빌드하고 돌린다(아래 "개인 빌드"의 PR 검사). `custom-v4.5.0-krx.30`부터 `krx.37`까지 맥 빌드가 모두 이것으로 실패해 Release가 없다(`Mark`, `Outcome`, `AlgorithmCard`의 새 필드). 리눅스 clang과 맥의 Apple clang은 버전이 달라, 드물게 맥에서만 나는 경고는 머지 뒤 맥 개인 빌드에서 드러난다.
    > **바뀜** (2026-10-20, codeyoma/FinceptTerminal#100, codeyoma/krx-quant#324): "이 fork의 PR에는 빌드 검사가 돌지 않는다. 그래서 KRX 구조체에 필드를 더하는 PR은 머지 전에 맥에서 `CI=1`로 KRX 테스트를 빌드한다" → `custom` 대상 PR마다 KRX 테스트가 리눅스 clang과 `-Werror`로 저절로 빌드되고 돈다. 이유: 이런 경고(krx.4, krx.25)와 요청 순서에 기댄 테스트(krx.34, codeyoma/FinceptTerminal#99)가 머지 뒤 개인 빌드에서야 드러났다.
  - 윈도우는 `custom` 대상 PR마다 GitHub에서 저절로 빌드된다(아래 "개인 빌드"의 PR 검사). PR이 없는 브랜치는 손으로 빌드해 볼 수 있다: Actions → custom-release → Run workflow, 그 브랜치를 고르고 tag는 비운다. 윈도우 빌드와 그 설치·업데이트 검사만 돌고, Release·태그·issue는 만들지 않는다. 빌드 번호는 어떤 Release도 쓰지 않는 `krx.0`이다.
    > **바뀜** (2026-10-20, codeyoma/FinceptTerminal#100, codeyoma/krx-quant#324): 머지 전에 에이전트가 브랜치의 윈도우 점검을 손으로 돌렸다 → PR이면 같은 점검이 저절로 돌고 PR의 체크로 보인다. 손으로 돌리기는 PR이 없는 브랜치에 남는다. 이유: MSVC 경고(`setAlphaF(0.08)`, krx.16, codeyoma/FinceptTerminal#80)가 머지 뒤 개인 빌드에서야 드러났다.
    > **바뀜** (2026-10-15, upstream v4.5.1 merge, codeyoma/FinceptTerminal#36): `build-pr.yml`은 꺼져 있었다 → upstream v4.5.1이 파일을 지웠다. 이유 기록 없음(upstream). 대신: 위처럼 머지 전에 맥에서 `CI=1`로 빌드한다.
  - `tst_krx_engine_data`: 엔진 API와 스트림 메시지 해석(일봉과 그 시장, `bar` 이벤트, 합의의 `pooled`, `/v1/algorithms`의 수명 상태, 상세 통계의 기준별 lift와 그 거래일 단위 값, 보정 곡선, 나눠 본 통계, 기준율 대비 실력과 변동성 3분위 포함), 한국어 이름(방향의 기준 예측 넷, `baseline-climate` 포함), 시각·가격 표기, 머리줄 엔진 점의 상태와 툴팁, 종목의 특수 상황 구간(`/v1/symbols/{s}/special`, 단계 `detail`, 시작 순서, 엔진의 오류)과 상태의 `detail`(codeyoma/krx-quant#236), 상태의 오늘 장 `session`(휴장일은 없음, codeyoma/krx-quant#238). codeyoma/krx-quant#255에서 1분봉의 체결 수(`trades`, 지난 날의 봉은 `null`이라 없음), 도달 정답의 호가단위와 k(`touch.tick`, `touch.k`), "기준: 직전 4분 방향"의 `no_previous`가 "직전 창 없음"인 것을 더 본다. 일별 수급(`/v1/symbols/{s}/supply`, codeyoma/krx-quant#226)의 필드와 `null`, 날짜 순서, 날짜 없는 날 버림, 엔진의 오류도 본다(지어낸 값). 자동 매매의 매수·매도(`/v1/symbols/{s}/trader-marks`, codeyoma/krx-quant#398)의 필드, 매수의 `null`, 마이크로초까지 있는 시각, 매매·쪽·시각이 없는 표시 버림, 엔진의 오류도 본다(지어낸 값).
  - `tst_krx_board`: 답과 스트림을 합친 화면 상태(최신 봉 마감, 차트 표시, 합의 구성원, 신호가 볼 예측, 실패와 되감기).
  - `tst_krx_signals`: 신호 규칙, 다시 알리지 않는 규칙, 구성원을 모를 때 미루기, 엔진의 `votes`로 판단하기.
  - `tst_krx_stats_view`: 순위표와 상세 통계 창이 보이는 문구(비율·평균과 구간, 기준별 lift 줄과 거래일 구간, 시간대, 보정도 곡선과 점, 제외 건수, 조건과 날짜, 순위표 제목 툴팁의 순위 규칙, 열 제목·lift·보정도 툴팁, 기준율 대비 실력의 칸과 툴팁, 변동성 3분위의 이름과 경계).
  - `tst_krx_chart_model`: 차트가 그릴 봉(KST를 차트의 UTC로), 아이콘의 방향(▲▼–)·결과(✓/✗/?)·흐림·붙는 봉, 맞춤 범위(최근 60봉), 호버 다섯 줄(채점 전·후, `votes` 없음, 막힌 합의), 목표 봉(있음, 오른쪽 빈칸), 신호 강조, 고정한 봉 마감의 표 행(순서, 기준 예측 묶음, 상태, 맞음·틀림, 합의 포함 여부, `pooled` 없음은 "알 수 없음"), 왼쪽 끝에서 다음에 읽을 범위(한 화면 너비 안, 최소 30봉, 첫 봉 앞 1,000봉, 봉이 없으면 지금 앞, N분 보기는 300봉어치 최대 20,000), 아이콘 예측의 다음 쪽(최신부터, 봉과 같이 앞서 읽기, 빈 쪽이면 끝, 최종 합의와 "늘 보합" 중 덜 간 쪽까지), 더 읽은 봉 붙이기(중복·순서), N분 묶기(시가·고가·저가·종가·거래량, 240분봉 08:00/12:00/16:00 시작, 날 경계, 빈 분, 다른 오프셋의 시각), 실시간 봉 합치기(같은 분, 새 분, 오래된 이벤트 버리기, 5분 보기의 마지막 봉), 일 보기(오늘 봉을 통합·KRX 기준으로 만들기, 저장된 일봉 뒤에 붙이기·바꾸기, 실시간 봉으로 움직이기, 왼쪽 끝에서 앞의 일봉 읽기), 주·월·년 보기(월요일·1일·1월 1일 시작, 해를 넘는 주, 엔진의 마지막 봉 뒤를 일봉으로 만들기, 저장된 것이 없을 때). 누른 봉의 봉 마감(봉 시작 + 1분, 엔진에 `+09:00`으로 묻는다, codeyoma/krx-quant#249)을 본다. 자동 매매의 매수·매도(codeyoma/krx-quant#398): 다시 읽은 쪽이 읽어 둔 것을 바꾸는 것과 순서(같은 시각이면 늦은 매매 먼저), 다음에 읽을 쪽(봉이 없으면 없음, 최신부터, 1밀리초 뒤부터, 첫 봉까지, 그 앞에 없음), 1,000개보다 적은 쪽이면 그 앞에 없음, 더 거슬러 가지 못한 앞 쪽에서 멈춤, 읽어 둔 것에 닿지 않는 최신 쪽(codeyoma/FinceptTerminal#107), 체결이 든 1분봉과 매수 아래·매도 위·실전 여부, 호버 줄(가격, 수량, 청산 이유, 순손익의 부호와 반올림, 모의·KIS 모의·실전), 그 봉의 상자 끝에 시간 순서로 붙는 것을 손으로 센 값으로 본다.
  - `tst_krx_chart_studies`(codeyoma/krx-quant#222): 상단 지표 12종의 값(짧은 합성 봉에서 손으로 센 값. 슈퍼트렌드와 파라볼릭 SAR는 TradingView 식을 한 봉씩 따라 센 값), 앞 기간이 모자란 앞부분은 빔, 일목 선행스팬이 마지막 봉 뒤로 가는 것, VWAP가 날짜가 바뀐 첫 봉에서 다시 시작, 읽은 첫 날이 중간부터면 그날 VWAP는 비고(범례에 이유) 다음 날부터 맞음, VWAP를 위해 첫 화면 가장 이른 날짜의 0시 앞까지 읽기(읽은 봉이 늘어도 목표는 그대로, 상한), 재귀 지표의 앞 기간(EMA 4배, 슈퍼트렌드 10배, SAR 100봉), 고정 VWAP가 기준 봉부터(분·5분·일·주 보기에서 기준 시각이 든 봉, 읽지 않은 기준), 매물대 칸 나누기, 마지막 봉 뒤 시각, 더 읽을 봉 수와 상한(2,000봉, 고정 VWAP 기준까지), 종목별 설정(처음 여는 종목의 기본값, 저장하고 다시 읽기, 켠 순서, 깨진 값), 기간 추가와 빼기, 페이지가 그릴 것과 범례(숨김, 일봉의 VWAP, 기준 고르기, 숨긴 고정 VWAP는 클릭을 기다리지 않음, 거래량 끄기). codeyoma/krx-quant#223에서 하단 지표 24종의 값(손으로 센 값. ADX·DMI, RSI·RMI, 스토캐스틱 RSI, MACD, 트릭스는 Wilder·EMA 식을 한 봉씩 따라 센 값)과 앞부분 빔, 하단 지표의 기본값·가이드선·설정값 글자, 앞 기간(단계마다 더함)과 상한, 1,000봉을 더 읽어도 재귀 지표의 보이는 값이 범위의 0.1%보다 덜 바뀜, AD 라인·OBV의 0점이 읽은 봉과 상관없는 시각이고(분·일 보기), 그 시각부터의 봉을 다 읽기 전에는 비우며(해·날을 중간부터 읽음, 같은 해·날의 앞 봉을 더 읽어도 빔, 상한에 닿음, 범례의 이유), 다 읽은 뒤에는 앞의 봉을 더 읽어도 값이 같고, 읽을 목표 시각과 그 읽기, 체이킨 오실레이터가 0점과 상관없음, 하단 칸(켠 순서, 높이, 숨기면 빈 칸, 끄면 올라옴, 다시 켜면 맨 아래, 모든 하단 지표가 일봉에서도 제 칸에 그림), 칸 높이 저장과 깨진 값, 하단 지표 설정의 저장과 `방식` 범위를 더 본다. 일별 수급 8종(codeyoma/krx-quant#226)의 이름·필드·모양(막대와 계단), 분·일·주·월·년 보기의 봉에 놓는 값(그날 값, 흐름은 기간의 합과 수준은 기간의 마지막 날 값, 값 없는 날은 더하지 않음, 값이 없는 기간은 0이 아니라 빔, 읽은 값이 첫날 앞까지 닿지 않은 첫 기간은 빔, 오늘), 페이지에 보내는 것(칸, 막대의 위·아래 색, 계단 선, 범례의 마지막 값과 "그날 값 없음"), 그 응답이 없는 엔진과 시간축 없는 차트에서 그리지 않음(흐림과 이유), 설정의 저장도 본다.
  - `tst_krx_chart_look`: 차트 모양의 이름(설정 이름과 메뉴 이름, 시간축 없는 4종도 그림, codeyoma/krx-quant#225), 종목별 설정(처음 여는 종목은 1분·캔들, 목록 밖의 보기·모르는 모양은 기본값, 남은 시간축 없는 모양은 그대로, INI 문자열 값), 하이킨아시 봉(첫 봉, 이어지는 시가, 고가·저가, 시각·거래량은 그대로), 베이스라인 기준값(분 보기의 전일 종가, 오늘만 읽었으면 첫 봉 시가, 일 보기의 화면 첫 봉, 마지막 봉 뒤), 날짜 구분선(1–240분 보기마다 날마다 첫 봉, 중간부터 읽은 날, 09:00에 여는 종목, 일 보기는 없음).
  - `tst_krx_chart_drawings`(codeyoma/krx-quant#224): 그리기 도구 12종의 이름과 받는 점 수, 선 종류, 처음 모양과 다음 id, 그린 것을 저장하고 다시 읽기(시각은 KST ISO 8601, id는 남기지 않음), 깨진 값(객체가 아닌 것, 모르는 도구, 점 수, 시각, 가격, 빈 텍스트는 버리고, 색·굵기·선 종류는 처음 값, 긴 텍스트는 자름), 자석이 고르는 값(아래 봉의 가운데와 가장 가까운 시가·고가·저가·종가, 같으면 시가, 마지막 봉 뒤와 첫 봉 앞은 그대로), 1분봉 위 시각의 자리(봉 시작과 가운데, 거래 없는 분은 경계, 마지막 봉 뒤, 첫 봉 앞은 주말과 밤을 빼고 센 장 시간)와 거꾸로, 같은 시각이 3·5·60·240분 보기에서 놓이는 자리(240분봉의 20시 뒤는 다음 거래일)와 모든 분 보기의 왕복, 일·주·월·년 보기의 자리(일봉의 14:00이 가운데, 토요일은 경계, 2026년 10월 평일 22일, 2026년 평일 261일)와 왕복, 지금 만들어지는 봉이 마지막 봉을 바꾸는지 새로 붙는지와, 거래 없는 분이나 주말 뒤에 새 봉이 붙으면 마지막 봉 뒤의 점이 그 봉으로 옮기는 것. 빈 봉을 채운 봉(codeyoma/krx-quant#236)에서는 거래 없는 분의 시각이 제 칸에 놓이고 모든 분 보기에서 왕복하는 것, 자석이 빈 봉 위에서 붙지 않는 것, 거래 없는 분 뒤에 새 봉이 와도 빈 봉이 먼저 채워져 마지막 봉 뒤의 점이 그 자리에 남는 것. 도구와 자석의 글자가 서로 다른지와, 누른 키의 글자(A–Z는 그대로, 한글 ㅅ·ㅎ·ㅁ은 맥의 키 자리·윈도우의 가상 키로 T·G·M, Esc·Return·숫자는 없음, codeyoma/krx-quant#241).
  - `tst_krx_chart_toolbar`(codeyoma/krx-quant#241, #248): 차트 위 툴바를 실제 창에 띄운다(Widgets와 Qt의 단축키가 필요하다. offscreen이거나 창이 활성이 되지 않으면 건너뛴다). 메뉴 항목에 글자가 보이는지, 화면 안에 키보드가 있을 때 Cmd+D(Ctrl+D)가 메뉴를 열고 다시 누르면 닫는지, 글자 12개가 각 도구를 고르고 메뉴를 닫는지, 없는 글자는 메뉴를 두고 Esc가 닫는지, M이 자석을 켜고 끄는지, 한글 키(ㅅ)가 자리로 추세선을 고르는지, 키보드가 화면 밖에 있거나 시간축 없는 모양이면 열리지 않는지 본다. 단축키로 고른 도구의 이름이 `그리기` 버튼의 글자가 되고 커서로 돌아가면 `그리기`인지, 긴 이름이 넘쳐 글자를 숨긴 폭에서 커서로 돌아가면 글자가 다시 보이는지(글자를 숨긴 동안에도 글자 폭으로 잰다) 본다(codeyoma/krx-quant#248).
  - 예측 종류(codeyoma/krx-quant#182, 지어낸 값): `tst_krx_engine_data`는 `/v1/kinds`, 종류의 최신 예측과 정답·경로, `kind_prediction`·`kind_answer` 이벤트, 예측과 함께 오는 봉 마감의 k(`flat_ticks`, 없거나 `null`이면 없음, codeyoma/krx-quant#200), 변동폭 순위표의 `kind`와 분위수 통계(`extra`), `/stats`의 `kind`, `/v1/symbols`의 `tick`·`flat_ticks`, 알고리즘의 종류, 장벽(codeyoma/krx-quant#185)의 봉 마감 w(`width_ticks`), 예측과 함께 오는 봉 마감 중간가와 호가단위(`anchor`, `tick`, 없거나 `null`이면 없음, codeyoma/krx-quant#210), 정답 경로(U·L, 처음 닿은 시각과 호가, 따라가지 못한 창과 중간가 없는 창), 스트림의 장벽 정답, 순위표의 장벽 손익(`extra`)과 기준 예측 이름을 읽는 것을, `tst_krx_board`는 `KindBoard`(종목별 최신, 고른 종목의 봉 마감별 기록, 정답 붙이기와 되돌리지 않기, 다른 종류의 이벤트 무시, 되감기, 장벽 정답의 경로가 그 봉 마감의 모든 예측에 붙고 w, 중간가, 호가단위는 남기)를, `tst_krx_stats_view`는 변동폭·장벽 순위표와 상세 창의 열과 칸, 분위수 표와 요약, 장벽 손익 칸·표·요약, 클래스별 보정 곡선(과 그 툴팁의 조사 "을/를"), 기준 예측의 lift 툴팁, 순위 규칙을 본다.
  - 하루 한 번 예측(codeyoma/krx-quant#264, 지어낸 값): `tst_krx_engine_data`는 `/v1/kinds`의 `daily`와 `horizon_minutes: null`(0으로 둠, 봉 마감 종류는 `daily` 없음), 예측의 `band`, 정답 경로(시가·종가·변화·보합 폭·σ̂·s·VI 수, 일봉이 오지 않은 날의 빈 경로), 스트림의 그 정답, 순위표의 `open_to_close_stats`(bp, 장벽 손익으로 읽지 않음)와 기준 예측 셋의 한국어 이름을, `tst_krx_board`는 08:50 예측과 그날 15:45부터의 정답이 `KindBoard`에 붙는 것을, `tst_krx_stats_view`는 순위표·상세 창의 열(표본(종목·일), 손익)과 칸·툴팁, 손익 표와 요약, 순위 규칙의 하루 한 번 줄, 특수 상황·lift 열 툴팁을, `tst_krx_kind_view`는 버튼(엔진이 주면 고를 수 있음, 없으면 흐림, 툴팁), 날과 그날 08:50(1분봉·UTC 시각에서도), 패널 열과 행(결과 늘, "전 거래일 정답 없음"), 제목(채점 전, 정답, 지난 거래일, 예측 없음, 보합 폭 없음), 정답 문구(특수, VI, 채점 불가 이유), 보합 폭, 알고리즘 평균과 동률 규칙, 일 보기 아이콘(봉 없는 날·알고리즘 없는 날 빠짐, 흐림, ?)과 호버 줄, 보이는 날의 성적, 날 단위 읽기를 본다. `tst_krx_chart_page`는 일 보기에 아이콘과 창 없는 상자가 그려지고 주 보기에는 없는 것을 본다(그래서 페이지의 `window.krxChart`에 `icons`를 더했다).
  - 5거래일 시가→종가(codeyoma/krx-quant#310, 지어낸 값): `tst_krx_engine_data`는 `/v1/kinds`의 `daily.sessions`(옛 엔진은 1)·`awaiting`·`review_hold`, 정답 경로의 `end_day`·`sessions`·`closes`(`null`인 세션)·`ex_dividend`(`true`, `null`), 정답 전 예측의 `target_at`, 순위표 항목의 `overlap_sessions`·`independent_days`(옛 엔진은 1과 없음)와 그 `extra`, 기준 예측 셋의 한국어 이름을, `tst_krx_kind_view`는 버튼(엔진이 주면 고를 수 있음, 옛 엔진은 흐림, 툴팁)과 `window_sessions`, 창 끝(정답 전 `target_at`의 날 "(예정)", 정답의 `end_day`), 채점 대기 문구와 결과 열, 정답의 배당락 표시와 세션 종가("—" 포함), 고정한 날의 제목, 일 보기 호버의 창 끝과 세션 종가 줄, 기준 예측만 있는 날의 아이콘 없음, 패널 툴팁(대기, 겹침, 배당락, 판정일)과 결과 열 툴팁, `kind_algorithms`와 "기준 예측만"·"돌지 않음"·수습 보류 문구를, `tst_krx_stats_view`는 표본 열 제목 툴팁, "45 (독립 3)" 칸과 그 툴팁, 구간 툴팁의 "겹침 보정(Driscoll–Kraay)", `overlap_text`, 순위 규칙의 5세션·겹침·판정일 줄, 손익 열 툴팁, 특수 상황·lift 열·보정도 툴팁을, `tst_krx_dashboard_view`는 이 종류의 손익 단위(bp)와 기준 예측 이름을 본다.
  - `tst_krx_kind_view`: 예측 종류 선택(옛 엔진에서 방향만, 장벽을 예측하지 않는 엔진에서 장벽은 흐림, 장벽을 예측하는 엔진에서 고를 수 있음, 모르는 종류는 버튼 없음), 고른 종류와 보이는 종류(설정은 그대로), 클래스 동률 규칙, 틱 표기, k와 2k(정답의 k, 정답 전에는 예측과 함께 온 봉 마감의 k, 옛 엔진이면 지금 k, codeyoma/krx-quant#200), 정답 문구(장벽은 먼저 닿은 쪽과 시각), 패널 열(변동폭은 전과 같음, 장벽은 세 확률), 장벽 w와 U·L(정답의 w와 U·L, 정답 전에는 예측의 w와 예측과 함께 온 봉 마감 중간가·호가단위로 센 U·L, 그 값이 없는 옛 엔진이면 w만, codeyoma/krx-quant#210), 변동폭·장벽 패널의 행 순서·확률·결과, 호버 줄과 4분 구간의 선(변동폭의 최고·최저 중간가, 장벽의 U·L(정답 전에도)과 각 장벽에 처음 닿은 점), 지난 봉까지 읽을 범위.
  - `tst_krx_bar_view`(codeyoma/krx-quant#255): 봉 창의 키와 제목(봉 마감으로 부름, 이름이 없으면 코드만), 봉 정보(시가·고가·저가·종가, 거래량, 체결 수와 지난 날의 "—", 앞 봉 대비의 원·%와 앞 봉(지난 거래일의 봉, 앞 봉 없음, 내림), 빈 봉의 "체결 없음"과 만드는 중인 봉, 4분에 걸친 특수 상황만), 정답 상세(채점 뒤의 모든 줄, 채점 전에 다른 종류의 예측과 함께 온 중간가·호가단위·k로 센 목표와 U·L, 방향만 예측하는 엔진, 채점 불가와 특수 상황), 변동폭·장벽 절(열, 줄 순서와 결과, 머리줄의 정답과 k·w·U·L, 채점 전, 예측하지 않는 종류, 읽지 못함, 예측 없음), 방향 줄의 기대 틱과 모델, 4분 미니 차트(다섯 칸과 빈 칸, 채점 뒤의 선 여섯과 점 넷, 채점 전의 점선, 옛 엔진의 구간만, 보일 가격 범위), 자리 계산과 이름 벌리기, 다시 읽기를 멈출 때(4분 전, 채점 기다림, 한 종류 정답 기다림, 다 옴, 10분 뒤, 예측 없음(VI), 지난 날)와 상태 글을 지어낸 값으로 본다.
  - `tst_krx_app_release`: 개인 빌드 태그 해석과 순서, Release 목록에서 업데이트할 빌드·설치 파일·sha256 고르기.
  - `tst_krx_chart_page`(codeyoma/FinceptTerminal#59): 차트 페이지를 `KrxChart`로 실제 Qt WebEngine 창에 띄운다(위 Qt Core만 쓰는 것과 달리 Widgets·WebEngine이 필요하고, `HAS_WEBENGINE`일 때만 빌드한다. offscreen이나 WebEngine 실행 파일이 없으면 건너뛴다). 오늘 봉 18개 뒤에 앞의 봉이 같은 프레임에, 또는 한 프레임 뒤에 붙어도 첫 화면이 최근 60봉에 맞는지, 휠로 옮긴 보기는 앞의 봉이 붙어도 그 봉에 남고 맞춤으로 돌아오는지 본다. 윈도우처럼 Shift를 누른 세로 휠이 줌하지 않고 보기를 옮기는지(codeyoma/krx-quant#242), 휠 줌이 포인터에 가장 가까운 봉을 제자리에 두는지, Qt가 보낸 휠(한 칸의 3분의 1)이 화면 배율과 상관없이 휠 줄 수 × 20픽셀만큼 줌하는지, Qt가 보낸 가로 휠과 Shift+휠 한 칸이 줌 없이 그 픽셀의 0.8배만큼 옮기는지(윈도우의 125% 화면에서도), 최신 봉이 오른쪽 끝에 있을 때 줌아웃하면 최신 봉 뒤 빈칸이 6칸으로 남는지, 첫 봉이 왼쪽 끝에 닿으면 줌아웃이 멈추고 엔진에 앞의 봉이 없으면 밀리는지(codeyoma/krx-quant#253), 창의 왼쪽 위에서 (300, 120) 떨어진 차트에 맥이 보내는 것처럼 창으로 트랙패드 핀치(시작, 배율 1.04 열 번, 끝)를 보내면 포인터 아래 봉이 제자리에 남고 봉 폭이 1.04^10배가 되는지, 이어 0.95 네 번 오므리면 그만큼 줄고 그 봉이 남는지, 페이지가 확대되지 않는지(`visualViewport.scale` 1), 스마트 줌이 페이지를 확대하지도 줌하지도 않는지(codeyoma/krx-quant#391), 오른쪽 아래 `일반`·`로그` 버튼이 차트 오른쪽 아래 끝에 있고, `로그`를 누르면 가격 축이 로그가 되며 화면이 그것을 받는지(이미 고른 것을 다시 누르면 아무것도 없음), 차트 모양을 바꿔도 남는지, 화면이 보낸 것으로 그리는지(codeyoma/krx-quant#245) 본다. 시간축 없는 차트(codeyoma/krx-quant#225)는 시작할 봉을 읽는 동안 벽돌이 없다가 최근 60벽돌에 맞고(렌코, 카기), 캔들로 돌아오면 최근 60봉에 맞는지, 지금 봉이 크게 움직이는 동안 지표를 켜는 등으로 다시 그려도 기본 크기(ATR)가 그대로이고 새 봉이 오면 바뀌는지 본다. 서킷브레이커의 30분(codeyoma/krx-quant#236)은 빈 칸 30개가 되고 첫 화면이 그 칸을 세어 최근 60칸에 맞는지, 빈 칸의 호버 상자가 "체결 없음"과 서킷브레이커 줄인지, 거래 없는 분 뒤의 새 봉이 그 칸을 곧바로 채우는지, 5분 보기의 빈 칸 수를 본다. 진행 중인 서킷브레이커(codeyoma/krx-quant#238)는 엔진 시계가 13:20:30, 13:25:30일 때 빈 칸이 10개, 15개인지, 다시 읽은 봉이 빈 칸을 차지하는지, 마지막 빈 칸의 실시간 봉이 그 자리에서 바뀌고 그 호버 상자에 서킷브레이커 줄이 있는지, 그 뒤 새 봉이 그 사이를 채우는지 본다. 차트(WebEngine)가 키보드를 가진 채 Cmd+D(Ctrl+D)를 누르면 페이지가 키를 가져가지 않고 툴바의 그리기 메뉴가 열리는지 본다(codeyoma/krx-quant#241, 창이 활성이 되지 않으면 건너뛴다). 1분 보기에서 아이콘 없는 봉과 빈 봉 칸을 실제 마우스로 누르면 화면이 그 봉 마감(`+09:00`)을 받는지, 끌어서 옮기거나 마지막 칸 오른쪽을 누르거나 3분 보기의 봉을 누르면 받지 않는지 본다(codeyoma/krx-quant#249). 1분 보기에서 손으로 옮긴 차트의 봉을 실제 마우스로 두 번 누르면 화면이 그 봉 마감(`+09:00`)을 봉 창으로 받고, 첫 누르기의 고정은 한 번이고, 차트가 맞춰지지 않는지, 마지막 칸 오른쪽, 그린 것(그 팝업이 열린다. 페이지 안에서 합성 이벤트로 두 번 누른다), 그리기 도구를 고른 동안, 앵커 VWAP의 봉을 고르는 동안, 3분 보기에서는 받지 않는지 본다(codeyoma/krx-quant#255). 그린 것을 짚으려고 페이지의 `window.krxChart`에 가격의 y(`y`)를 더했다. 가격 칸 범례의 가격 줄(codeyoma/krx-quant#285)은 09:00·09:01·(빈 09:02)·09:03 봉에서, 십자선이 없으면 마지막 봉의 값과 빈 봉을 건너뛴 09:01 종가 대비 등락률과 색(상승·하락·보합)인지, 만들어지는 봉이 움직이면 바뀌는지, 09:01에 올리면 09:00 종가 대비(`+0.23%`)인지, 읽은 첫 봉은 등락률이 없는지, 빈 봉은 `체결 없음`인지, 하이킨아시에서도 원래 봉의 값인지, 마우스가 나가면 마지막 봉으로 돌아오는지, 봉이 없으면 줄이 없는지 본다. 이를 읽으려고 `window.krxChart`에 `prices`(칸마다 글자와 등락률의 색 이름)를 더했다. 자동 매매의 매수·매도(codeyoma/krx-quant#398)가 같은 봉의 합의 아이콘과 함께 모두 그려지는지(합의 아이콘이 봉에 가장 가깝고, 매수는 봉 아래, 매도는 아이콘 위, 실전과 모의의 색이 다르고 아이콘의 색과도 다름), 그린 봉 밖의 것과 5분 보기에는 없는지 본다.
  - `tst_krx_chart_slots`(codeyoma/krx-quant#236): 빈 봉 채우기(1분, 3·5·60·240분, 서킷브레이커의 30칸, 첫 봉 앞·마지막 봉 뒤·날 사이는 채우지 않음, 봉이 하나인 날, 일·주 보기는 그대로), 실제 봉만 고르기, 지금 만들어지는 봉(같은 분은 바꿈, 앞선 분은 버림, 거래 없는 분 뒤의 새 봉은 그 사이를 곧바로 채움, 다음 날 첫 봉은 채우지 않음, N분 보기), 앞의 봉을 읽어 붙인 뒤의 칸(이음새도 채움, 오늘의 칸은 그대로), 보조지표가 실제 봉으로만 세는 것(빈 봉이 있어도 값이 같고, 멈춘 뒤 첫 봉의 이동평균 5는 실제 봉 다섯의 평균, 빈 봉에는 점이 없음), 그리기의 자리(멈춘 시간 안의 시각이 제 칸, 5분 보기, 자석), 특수 상황의 이름(서킷브레이커 단계, 사이드카, VI 종류의 한국어), 띠의 자리(빈 봉 위, 초 단위 VI, 마지막 봉 뒤로 이어지는 구간, 읽은 봉 앞에 끝난 것과 시각이 없는 것은 없음, 5분 보기, 일 보기와 봉 없음), 호버 줄(초가 없으면 분까지, 끝 시각은 들지 않음, 빈 봉의 칸 시각, 실제 봉 상자 뒤에 붙는 줄, 4분 구간이 없는 빈 봉 상자, 띠를 끄면 빈 봉 상자만, 5분 보기는 빈 칸과 띠 안의 봉만, 일 보기는 없음)를 본다. codeyoma/krx-quant#238에서 엔진의 장과 시계(`session_clock`, 시계의 칸은 장이 열린 동안만), 진행 중인 서킷브레이커가 1분에 한 칸씩 자라고 그 띠가 시계까지인 것(끝난 뒤에는 끝까지), 장중 조용한 종목, 장이 끝난 뒤·오늘 첫 봉 전·장 시작 전에는 자라지 않음, 5분 보기의 칸, 실제 봉이 빈 봉의 칸을 차지하는 것(다시 읽기, 마지막 칸은 그 자리에서, 앞의 칸은 다시 그림), 실시간으로 붙은 봉과 그 앞에 채운 빈 봉의 호버 줄을 더 본다.
  - `tst_krx_live`(codeyoma/FinceptTerminal#64): `KrxLive`를 이 컴퓨터에 띄운 가짜 엔진(`QTcpServer`, 상태·종목·봉·일봉·예측과, 정한 테스트에서만 일별 수급에 답하고 나머지는 404)에 붙여, 화면이 보내는 읽기를 본다(위 Qt Core만 쓰는 것과 달리 Qt Network가 필요하다. WebSockets 없이 빌드해 스트림은 붙지 않는다). 설정은 테스트가 정의한 `AppConfig`로 임시 파일에 쓰고, 앱의 엔진 주소는 건드리지 않는다. 장 시작 전에 일 보기에서 종목을 연 뒤 1분 보기로 바꾸면 지난 거래일의 봉을 읽는지, 분 보기에서 고른 종목은 그 봉을 한 번만 읽는지, 오늘 봉이 있을 때 분 보기에서 고른 종목은 그 앞의 봉을 읽지 않는지 본다. 봉 창(codeyoma/krx-quant#255)을 열면 그 봉 마감의 예측, `/v1/algorithms`, 채점 시각 전의 봉 10개, 그날의 특수 상황을 바로 읽는지, 지난 날의 봉은 한 번만, 채점 전인 오늘의 봉은 5초마다 다시 읽는지, 창을 닫으면 더 읽지 않는지 본다. 일별 수급(codeyoma/krx-quant#226)은 읽은 봉의 첫 날부터 묻는지, 404인 엔진이면 없다고 아는지, 답하는 엔진이면 그 날들(지어낸 값)을 받는지, 엔진 주소를 바꾸면 다시 모르는 것으로 돌아가는지, 주 보기는 첫 주의 14일 앞부터 묻는지, 읽는 동안 다른 날들이 필요해진 뒤 그 읽기가 실패하면 지금 필요한 날들을 곧바로 묻고 그것마저 실패하면 곧바로 다시 묻지 않는지 본다. 자동 매매의 매수·매도(codeyoma/krx-quant#398)를 005930의 차트와 함께 최신 쪽부터 첫 봉까지 1,000개씩 읽는지(지어낸 1,100개: 두 쪽. 가짜 엔진은 엔진처럼 마이크로초로 비교하고 한 시각을 나누지 않는다), 쪽 끝의 밀리초에 든 마이크로초가 다른 표시를 모두 읽는지, 화면을 다시 보일 때 읽어 둔 것에 닿지 않는 최신 쪽이면 첫 봉까지 다시 읽는지(codeyoma/FinceptTerminal#107), 그 엔드포인트가 없는 엔진(404)에서 표시 없이 실패도 아닌지, 다른 종목을 고르면 바로 버리고 그 종목의 것은 묻지 않는지 본다.
  - `tst_krx_chart_bricks`(codeyoma/krx-quant#225): 렌코·카기·포인트 앤 피겨·라인 브레이크를 합성 봉에서 손으로 센 벽돌 수, 방향, 가격, 끝난 시각, 거래량 합(한 봉이 렌코 벽돌 둘을 끝내면 나눔, 그리는 중인 카기 선과 포인트 앤 피겨 열), 카기의 굵기와 꺾이는 가격, 라인 브레이크의 줄 수, 반전 칸 수, ATR(14) 크기(마지막 봉을 뺌, 그 앞이 14봉보다 적으면 없음, 반올림, 1원 이상, 지금 봉이 넓어져도 그대로이고 새 봉이 오면 다시 셈)와 사용자가 정한 크기, 크기 설정의 저장과 깨진 값, 시작(읽는 동안 없음, 180봉, 엔진에 더 없음, 그 봉이 남는 동안 그대로, 없어지면 다시)과 앞의 봉을 더 읽어도 네 모양의 벽돌이 같은 것(시작을 고정하지 않으면 렌코 벽돌이 3원 옮기는 것과 대조), 새 봉이 앞의 벽돌을 바꾸지 않는 것, 벽돌의 칸 시각, 벽돌 위 지표(VWAP 둘은 그리지 않음, 첫 벽돌이 0인 OBV·AD 라인, 이동평균선)를 본다.
- 화면 캡처(codeyoma/krx-quant#103): `custom/tools/krx-capture`는 KRX 화면을 엔진에 붙여 PNG로 저장한다. 화면 PR에 데모 엔진 화면을 남길 때 쓴다. 앱 빌드와 따로인 CMake 프로젝트이고, 설정은 임시 폴더의 `krx-capture.ini`에 쓴다. 같은 컴퓨터의 Fincept 설정(엔진 주소, 알림 설정)은 건드리지 않는다.
  - 빌드: `cmake -S custom/tools/krx-capture -B fincept-qt/build/krx-capture -G Ninja -DCMAKE_PREFIX_PATH=<Qt 6.8.3> && cmake --build fincept-qt/build/krx-capture` (Qt Test 모듈이 필요하다: `KRX_CAPTURE_KEY`, codeyoma/krx-quant#241)
  - 실행: `fincept-qt/build/krx-capture/krx_capture http://127.0.0.1:8090 out.png [초=8] [가로=1600] [세로=1000] [예측,...]`. 그 초만큼 엔진을 읽은 뒤 찍는다. 예측 이름(`gauss` 등)을 주면 그 상세 통계 창도 열어 `out-<예측>.png`로 찍는다. 테마와 글꼴은 앱 기본값(Obsidian, Consolas 14px)이다.
  - 환경 변수 `KRX_CAPTURE_FLOW=1`을 주면 KRX 수급 흐름 화면(codeyoma/krx-quant#306)을 찍는다. `KRX_CAPTURE_SYMBOL`이 그 종목, `KRX_CAPTURE_DAY`가 그 날, `KRX_CAPTURE_HOVER=<0..1>`이 차트 위 마우스 위치(가로 비율, 그 분의 값 상자가 보인다)다.
  - 환경 변수 `KRX_CAPTURE_CLOSING=1`을 주면 KRX 마감 흐름 화면(codeyoma/krx-quant#305)을 찍는다. `KRX_CAPTURE_SYMBOL`이 그 종목이다(없으면 지난번 종목, 그것도 없으면 엔진의 첫 종목). 차트가 없어 `QT_QPA_PLATFORM=offscreen`으로 찍어도 된다. 지난날 표와 기간 요약까지 보이려면 세로를 1400쯤으로 준다.
  - 환경 변수 `KRX_CAPTURE_TRADER=1`을 주면 KRX 자동 매매 화면(codeyoma/krx-quant#397)을 찍는다. `KRX_CAPTURE_CONFIRM=entries` 또는 `=sell`을 더 주면 찍은 뒤 그 정지 버튼을 눌러 확인 창을 `<out>-confirm.png`로 찍고 취소한다(정지를 보내지 않는다). `KRX_CAPTURE_POLICY=<버전>`은 기다리는 시간의 절반에 매매 표 위에서 그 정책의 매매를 고르고, `KRX_CAPTURE_SCROLL=trades|policies`는 찍기 전에 화면을 그 패널(매매, 정책 버전)이 맨 위에 오게 굴린다(codeyoma/krx-quant#401). 머리줄은 굴러가지 않아 같이 찍힌다. 차트가 없어 확인 창 없이는 `QT_QPA_PLATFORM=offscreen`으로 찍어도 된다.
  - 환경 변수 `KRX_CAPTURE_BRIEFING=1`을 주면 KRX 4분 예측 화면 대신 KRX 장전 브리핑 화면(codeyoma/krx-quant#304)을 띄워 그 초 뒤에 찍는다. `KRX_CAPTURE_DAY=<YYYY-MM-DD>`는 그 날의 브리핑이다. 나머지 `KRX_CAPTURE_*`는 이 화면에 쓰이지 않는다.
  - 환경 변수 `KRX_CAPTURE_VIEW=<분>`을 주면 기다리는 시간의 0.3 지점(엔진을 읽고 종목이 골라진 뒤)에 툴바에서 그 보기를 고른다(codeyoma/krx-quant#109, #221). `1440`은 일 보기, `10080`·`44640`·`527040`은 주·월·년 보기다(codeyoma/krx-quant#111, #112). 고른 보기는 임시 설정에 그 종목의 것으로 남아 다음 캡처도 그 보기로 열린다.
  - 환경 변수 `KRX_CAPTURE_STYLE=<모양>`(예: `heikin_ashi`, `baseline`, `renko`)을 주면 같은 때에 그 차트 모양을 고른다(codeyoma/krx-quant#221, #225). `KRX_CAPTURE_BRICK=<크기>`는 같은 때에 지금 모양(시간축 없는 것)의 크기를 톱니 메뉴에서 바꾼 것처럼 정한다(원이나 줄 수, `0`은 기본값으로, codeyoma/krx-quant#225). 임시 설정에 그 종목의 것으로 남는다. `KRX_CAPTURE_MENU=options`로 그 크기 줄이 있는 톱니 메뉴를 찍는다. `KRX_CAPTURE_SEPARATOR=0|1`은 날짜 구분선을 끄고 켠 것으로 연다. `KRX_CAPTURE_SPECIAL=0|1`은 특수 상황 표시를 끄고 켠 것으로 연다(codeyoma/krx-quant#236).
  - 환경 변수 `KRX_CAPTURE_MENU=minutes|style|options|indicators|drawing|edit`를 주면 기다리는 시간의 0.9 지점에 툴바의 그 메뉴(분 드롭다운, 차트 모양, 톱니, 보조지표 패널, 그리기 메뉴)를 열고, 메뉴를 `out-menu.png`로 따로 찍는다(메뉴는 따로 뜨는 창이다). 보조지표 패널은 `KRX_CAPTURE_INDICATOR=<지표>`(예: `bollinger`)를 고른 채 연다(codeyoma/krx-quant#222). `edit`은 `KRX_CAPTURE_SELECT`가 가리키는 그린 것(없으면 첫째)의 팝업을 더블클릭처럼 연다(codeyoma/krx-quant#224). 끝에 그린 것의 팝업이 열려 있으면(텍스트를 찍었을 때, `KRX_CAPTURE_JS`의 더블클릭) 그것도 `out-menu.png`로 찍는다.
  - 환경 변수 `KRX_CAPTURE_INDICATORS=<지표,...>`(예: `ma,bollinger,ichimoku,volume_profile`)를 주면 0.3 지점에 그 상단 지표만 그 순서로 기본값으로 켠다(거래량은 그대로, codeyoma/krx-quant#222). 하단 지표를 하나라도 적으면(codeyoma/krx-quant#223) 하단 지표도 거래량까지 모두 끄고 적은 것만 그 순서로 켠다(예: `ma,volume,rsi,macd,stochastic,obv`). `KRX_CAPTURE_ANCHOR=<ISO 8601>`은 같은 때에 그 시각의 봉을 고정된 VWAP의 기준으로 찍는다. `KRX_CAPTURE_PANES=<지표:높이,...>`(예: `rsi:180`)는 같은 때에 하단 칸 높이를 드래그한 것처럼 정한다. 모두 임시 설정에 그 종목의 것으로 남는다. `KRX_CAPTURE_DRAG=<칸>:<픽셀>`(예: `2:-45`)은 0.6 지점에 그 하단 칸(맨 위가 1) 위 경계를 그만큼 아래로(음수면 위로) 마우스로 끈 것처럼 드래그한다(페이지의 드래그 처리와 저장까지 탄다). `KRX_CAPTURE_LEGEND=<줄>`은 0.85 지점에 가격 칸 범례의 그 줄(0부터)에 마우스를 올린 것처럼 버튼을 보인다.

    > **바뀜** (2026-10-15, codeyoma/krx-quant#221): `KRX_CAPTURE_VIEW`는 처음에 보기 버튼을 눌렀다 → 0.3 지점에 툴바에서 고른다. 이유: 분 보기가 드롭다운이 됐고, 보기가 종목별이라 종목이 골라진 뒤에 골라야 남는다.
  - 환경 변수 `KRX_CAPTURE_BACK=<횟수>`를 주면 기다리는 시간의 4분의 1부터 0.5초마다 차트를 그 횟수만큼 휠로 왼쪽(지난 날)으로 돌린 뒤 찍는다(codeyoma/krx-quant#108).
  - 환경 변수 `KRX_CAPTURE_PIN=<봉 마감, ISO 8601>`(예: `2026-10-12T10:24:00+09:00`)을 주면 기다리는 시간의 절반이 지났을 때 그 봉 마감의 봉이나 아이콘을 누른 것처럼 예측 패널을 고정하고 찍는다(codeyoma/krx-quant#107, #249). 페이지에서 봉 칸을 실제로 누르는 것은 `KRX_CAPTURE_JS`로 `window.krxChart.x(<시각>)` 자리에 mouseenter·mousemove·mousedown·mouseup을 보낸다.
  - 환경 변수 `KRX_CAPTURE_BAR=<봉 마감, ISO 8601>[,…]`(예: `2026-10-12T10:24:00+09:00`)을 주면 기다리는 시간의 절반이 지났을 때 고른 종목의 그 봉을 두 번 누른 것처럼 봉 창을 열고(codeyoma/krx-quant#255), 끝에 연 순서대로 `<out>-bar1.png`, `<out>-bar2.png`…로 찍는다. 창은 절이 다 보이게 세로로 늘려 찍는다.
  - 환경 변수 `KRX_CAPTURE_SELECTION=gauss,markov`를 주면 순위표에서 그 모델들을 체크한 것으로 연다(codeyoma/krx-quant#159). 빈 값이면 해제한다. 주지 않으면 임시 설정의 지난 값이 남는다.
  - 환경 변수 `KRX_CAPTURE_KIND=<종류>`(예: `range`, `barrier`, `open_to_close`, `open_to_close_5d`)를 주면 그 예측 종류를 고른 것으로 연다(codeyoma/krx-quant#182). `direction`이면 방향으로 돌아간다. 주지 않으면 임시 설정의 지난 값이 남는다. 엔진이 그 종류를 예측한다고 답한 뒤에 바뀐다.
  - 환경 변수 `KRX_CAPTURE_PERIOD=<today|5d|20d>`를 주면 기다리는 시간의 0.3 지점에 순위표의 기간을 그것으로 고른다(codeyoma/krx-quant#264: 시가→종가의 "오늘"은 그날 15:45까지 비어 있다). `KRX_CAPTURE_STATS_SIZE=<가로>x<세로>`(예: `1100x1100`)는 찍기 직전에 열린 상세 통계 창마다 그 크기로 바꾼다(손익 절 위의 lift 표가 보이게).
  - 환경 변수 `KRX_CAPTURE_STATS_TAB=<탭 이름>`(예: `변동성별`)을 주면 찍기 직전에 열린 상세 통계 창마다 그 이름의 탭을 고른다(codeyoma/krx-quant#262). 나눠 본 통계의 첫 탭이 아닌 것을 찍을 때 쓴다.
  - 환경 변수 `KRX_CAPTURE_SCREEN=<번호>`를 주면 그 디스플레이(`QGuiApplication::screens()`의 순서)에 창을 연다. 기본 디스플레이가 주어진 가로보다 좁으면 창이 디스플레이에 맞춰 줄어서다.
  - 그리기(codeyoma/krx-quant#224):
    - `KRX_CAPTURE_DRAWINGS=<JSON 배열>`: 0.3 지점에 그 종목의 그린 것을 이것으로 바꾼다(남아 있던 것은 지우고, 그린 것처럼 하나씩 더한다. `[]`는 모두 지운다). 형식은 위 설정 키와 같되, 점의 `time`이 정수면 읽은 마지막 1분봉에서 그만큼 앞의 봉(0은 마지막, -10은 열 봉 앞)의 가운데이고, 그때 `price`에 `"open"`·`"high"`·`"low"`·`"close"`를 쓰면 그 봉의 값이다. 예: `[{"tool":"trend_line","points":[{"time":-52,"price":"low"},{"time":-18,"price":"low"}]},{"tool":"text","points":[{"time":-56,"price":"low"}],"text":"지지선"}]`.
    - `KRX_CAPTURE_SELECT=<n>`: 0.6 지점에 n번째 그린 것(1이 처음)을 누른 것처럼 고른다(손잡이가 보인다). `KRX_CAPTURE_MAGNET=0|1`은 자석을 끄고 켠 것으로 연다. `KRX_CAPTURE_LOG=0|1`은 오른쪽 아래의 `일반`·`로그`를 고른 것으로 연다(codeyoma/krx-quant#245).
    - `KRX_CAPTURE_DRAW=<도구>@<x>,<y>[@<x>,<y>…][;<도구>@…]`: 절반 지점부터 1.5초마다 툴바에서 그 도구를 고르고 차트의 그 자리(가로·세로 비율, 가격 칸 안)를 차례로 누른다. 페이지가 클릭처럼 찍고 화면이 남긴다. 점 없이 도구만 적으면 고른 채로 둔다. 예: `trend_line@0.2,0.35@0.7,0.25;horizontal@0.5,0.45`.
    - `KRX_CAPTURE_KEY=<키>[,<키>…]`: 키(`Escape`, `Delete`, `Backspace`, `Ctrl+D`, 글자 하나)를 0.75 지점부터 0.3초마다 누른다. 키보드처럼 Qt의 단축키를 거쳐, 열린 메뉴가 있으면 메뉴에, 없으면 키보드를 가진 위젯에 간다. 그 전에 차트가 키보드를 받는다(차트를 누른 것처럼). `Ctrl+D`는 맥에서 Cmd+D이고 그리기 메뉴를 연다(codeyoma/krx-quant#241). 예: `Ctrl+D,H`는 수평선을 고른다. 키가 열어 둔 메뉴는 `<out>-menu.png`로 찍는다. 단축키는 창이 활성이어야 해서 이 변수를 주면 창을 띄울 때 앞으로 가져온다. 맥은 다른 앱이 앞에 있으면 이를 막을 수 있다: 받은 위젯 이름과 함께 stderr에 `(no active window)`가 찍히면 다시 실행한다.
      > **바뀜** (2026-10-16, codeyoma/krx-quant#241): `Escape|Delete|Backspace` 중 하나를 키보드를 가진 위젯에 바로 보냈다 → 키 여러 개를 Qt의 단축키를 거쳐 보내고, 차트가 먼저 키보드를 받는다. 이유: Cmd+D와 메뉴의 글자를 실제 앱처럼 시험하려고.
    - `KRX_CAPTURE_JS=<스크립트>`: 0.7 지점에 차트 페이지에서 그 스크립트를 돌린다. 합성 마우스·키 이벤트로 그린 것의 고르기, 드래그, 더블클릭, Delete를 시험할 때 쓴다.
  - 환경 변수 `KRX_CAPTURE_HOVER=<0–1>[,<0–1>]`을 주면 기다리는 시간의 0.8 지점에 마우스를 차트의 그만큼 가로 위치(세로 1/3, 또는 둘째 값만큼. 하단 칸을 짚을 때, codeyoma/krx-quant#223)에 올린 것처럼 해, 그 봉의 상자를 찍는다(codeyoma/krx-quant#159).
  - 환경 변수 `KRX_CAPTURE_SYMBOL=<종목코드>`(예: `000660`)를 주면 기다리는 시간의 0.2 지점에 목록에서 그 종목을 고른 것처럼 한다(codeyoma/krx-quant#236). 그 뒤의 보기·모양 변수는 그 종목의 것으로 남는다.
  - 환경 변수 `KRX_CAPTURE_AT=<ISO 8601>`(예: `2026-10-12T09:21:00+09:00`)을 주면 0.7 지점에 차트의 보기를 그 시각이 든 칸이 가운데에 오게 옮긴다(보이는 칸 수는 그대로, 따라가기는 멈춘다, codeyoma/krx-quant#236). `KRX_CAPTURE_HOVER_AT=<ISO 8601>`은 0.8 지점에 그 시각이 든 칸(세로 1/3)에 마우스를 올린 것처럼 해 그 칸의 상자(빈 봉, 띠 안의 봉)를 찍는다. 페이지가 이를 위해 `window.krxChart`(칸의 x, 보기 옮기기, 칸 목록, 상자 글자)를 둔다. `tst_krx_chart_page`도 쓴다.
  - 창이 그 초 동안 화면에 뜬다. 차트(Qt WebEngine)는 실제 창에서만 그려지기 때문이다. 차트가 필요 없으면 `QT_QPA_PLATFORM=offscreen`을 앞에 붙여 창 없이 찍는다(차트 칸은 빈다). Qt에 WebEngine이 없으면 차트 칸은 "차트를 열 수 없음"이다.

  > **바뀜** (2026-10-13, codeyoma/krx-quant#134): `QT_QPA_PLATFORM=offscreen`으로 창 없이 찍었다 → 실제 창으로 찍는다. 이유: 차트가 Qt WebEngine으로 바뀌어(codeyoma/krx-quant#105) offscreen에서는 그려지지 않는다.

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
  - Codex: `codex exec --ignore-user-config --ignore-rules --ephemeral --skip-git-repo-check --sandbox read-only --json --cd <빈 폴더> -c web_search="disabled" -c features.<이름>=false … [--model <모델>] -`. `~/.codex/config.toml`(MCP 서버, 모델·추론 설정)과 rules를 읽지 않고, 세션을 저장하지 않으며, sandbox는 읽기 전용이다. 웹 검색과, 모델에게 Codex 자체 도구를 주는 기능(셸 `shell_tool`·`unified_exec`, 앱 커넥터 `apps`, 브라우저·컴퓨터 사용, 이미지 생성, 하위 에이전트 `multi_agent`, skills, plugins, memories, hooks 등 25개)을 끈다. 목록은 `LocalCliLlm.cpp`의 `kCodexFeaturesOff`다. `--disable`이 아니라 `-c features.<이름>=false`로 끄는 것은, `--disable`은 모르는 이름이면 실행이 실패하지만 `-c`는 무시해서 Codex가 기능 이름을 바꾸거나 없애도 돌게 하려고서다. 끄지 않으면 config를 무시해도 앱 커넥터(예: Atlassian 쓰기)와 웹 검색이 켜져 있어, 모델이 Fincept 도구 대신 그것을 쓴다. `~/.codex/AGENTS.md`(전역 지시)는 끌 방법이 없어 그대로 적용된다.
  - CLI는 라운드마다 새로 만든 빈 임시 폴더(`fincept-llm-cli-XXXXXX`)에서 돌고, 끝나면 그 폴더를 지운다. 그 자리의 파일을 프로젝트 지시로 읽지 않게 하려고서다.
  - 맥의 GUI 앱은 PATH가 짧다. 그래서 앱의 PATH, 로그인 셸(`$SHELL -l`)의 PATH, `~/.local/bin`, `~/.claude/local`, `/opt/homebrew/bin`, `/usr/local/bin`, `~/.npm-global/bin`, `~/.bun/bin`, `~/.volta/bin` 순으로 찾는다. CLI가 든 폴더를 PATH 맨 앞에 두고 실행한다(Codex는 node로 돈다).
  - 윈도우: npm은 `codex`와 `claude`를 `.cmd` shim으로 깐다. shim이 부르는 node 스크립트(`node_modules\…\codex.js`)를 node로 바로 실행한다. 스크립트를 못 찾으면 shim을 `cmd.exe /d /s /c "<명령줄>"`로 실행한다(`/s`라서 공백이 든 경로도 따옴표가 그대로 남는다). 윈도우에서는 시험하지 않았다.
- **한도와 속도.** 한 라운드의 프롬프트는 200,000자까지다. 넘으면 Fincept LLM처럼 오래된 대화와 오래된 도구 결과부터 뺀다. 한 라운드는 300초가 지나면 CLI와 CLI가 띄운 프로세스까지 모두 멈추고 오류를 낸다(맥·리눅스는 프로세스 그룹, 윈도우는 job object). Codex의 node 실행기는 실제 바이너리를 자식으로 띄워서, CLI만 멈추면 그 자식이 남아 구독을 계속 쓴다. 답은 토큰 단위로 흐르지 않고 끝나면 한 번에 나온다. 라운드마다 CLI를 새로 띄워서(Claude 약 5초, Codex 약 8초에 모델 시간이 더해진다) 도구를 여러 번 쓰는 질문은 느리다.
- **주의.** Claude CLI는 환경 변수 `ANTHROPIC_API_KEY`가 있으면 구독 대신 그 키로 과금될 수 있다.
- **코드.** `fincept-qt/src/services/krx_quant/LocalCliLlm.{h,cpp}`(provider, 인자, 출력 해석, Codex 모델 캐시), `LocalCliRunner.{h,cpp}`(CLI 찾기, 실행, Settings의 확인). upstream 파일의 갈래는 위 "upstream 파일 수정 원칙" 표에 있다.
- **테스트.** `tst_krx_local_cli_llm`(Qt Core만): CLI별 인자, 각 CLI 출력의 답·사용량·오류 해석, Codex 모델 캐시, 대역 CLI로 찾기·stdin 전달·시간 초과, Settings의 확인(`--version`)과 실패 문구, 윈도우 npm shim의 node 스크립트 찾기와 `cmd.exe` 명령줄 따옴표, Codex 자체 도구 끄기, 라운드마다 새 빈 폴더, 시간 초과 때 CLI가 띄운 프로세스까지 멈추기.

## 경제 캘린더 (codeyoma/krx-quant#150)

대시보드의 **Economic Calendar** 위젯은 원래 Fincept 서버(`api.fincept.in/macro/upcoming-events?limit=25`)를 읽는다. 이 요청은 Fincept 크레딧을 쓰고, 위젯이 떠 있는 동안 5분마다(DataHub의 TTL), 로그인할 때 한 번, 새로고침을 누를 때마다 나간다.

- **엔진에서 읽기.** KRX 화면에 엔진 주소(`krx_quant/base_url`)를 넣어 두면 `MacroCalendarService`가 같은 주기로 엔진의 `GET /v1/calendar?limit=25`를 읽는다. 엔진은 15분마다 TradingView의 공개 경제 캘린더에서 미국·한국·중국·일본·유로존·독일·영국의 일정을 모은다(krx-quant `docs/design.md` 4.5의 "경제 캘린더", `docs/engine-api.md`의 `/v1/calendar`). Fincept 크레딧은 쓰지 않는다.
- **엔진 주소가 없으면** 원래대로 Fincept 서버를 읽는다.
- **인증 헤더.** 엔진 요청에는 Fincept 계정의 `X-API-Key`·`X-Session-Token`을 붙이지 않는다. `HttpClient`는 Fincept API와 호스트 이름이 같으면 포트가 달라도 이 헤더를 붙이는데, 엔진 요청은 두 헤더를 빈 값으로 덮어 지운다(`krx_quant::engine_request_headers`).
- **보이는 값.** 엔진의 답을 위젯의 필드로 바꾼다(`fincept-qt/src/services/krx_quant/EngineCalendar.{h,cpp}`).
  - 날짜와 시각은 KST다(원래 출처의 시간대와 다를 수 있다).
  - 실제(ACT)·예상(FCST)·이전(PREV)은 엔진의 숫자에 배율(`K`·`M`·`B`·`T`)과 단위를 붙인 글자다. `%`는 뒤에, 통화 기호(`$`·`€`·`£`·`¥`·`₩`)는 앞에, 다른 단위는 띄어서 뒤에 붙인다(`3.7%`, `$23.75K`, `-$12.5K`, `90.45B CNY`). 아직 모르면 `--`다.
  - 컨센서스(CONS)는 비운다. 출처의 예상치는 하나뿐이라 FCST에 보인다.
  - 중요도는 `low`·`medium`·`high`를 1·2·3(LOW·MED·HIGH)으로 보인다.
  - 툴팁의 분류(Category)는 출처의 분류 코드(`prce` 등)다. 시장 반응(sentiment)과 종목(symbol)은 없다.
- **테스트.** `tst_krx_engine_calendar`(Qt Core만): 요청 주소와 인증 헤더를 지우는 값, 엔진 답에서 위젯 행으로(KST 시각, 배율과 단위를 붙인 값, 중요도), 읽을 수 없는 답.

## 대시보드 KRX 위젯 (codeyoma/krx-quant#302)

대시보드(DASHBOARD)에 KRX 엔진 위젯 4개가 있다. KRX 4분 예측 화면을 열지 않아도 다른 시장 위젯 옆에서 지금 신호, 알고리즘 성적, 엔진 상태, 오늘 시가→종가를 본다. 위젯은 엔진 API만 읽고 보이기만 한다. 판정과 채점은 엔진에 있고, 신호 판정은 KRX 화면과 같은 `krx_quant::judge`다. 주문 기능은 없다.

- **추가.** 위젯 추가 창(ADD WIDGET)의 `KRX` 카테고리(파란색, `ui::colors::INFO`)에서 고른다. 템플릿 선택 창의 `KRX` 템플릿을 고르면 네 위젯이 놓인 대시보드가 된다(지금 대시보드는 바뀐다).
- **KRX 지금 신호**(`krx_signals`, 기본 7×4칸): 종목마다 한 줄이다.
  - 로고와 회사 이름, 흐린 코드(KRX 화면의 종목 칸과 같은 `KrxLogos`·`KrxSymbolDelegate`, 로고는 실행 중에만 메모리에 둔다), 기준가, 최종 합의의 봉 마감 → 채점 시각, 상승·보합·하락 확률(가장 큰 쪽이 굵은 방향색), 정식 알고리즘이 어느 쪽인지(`정식 9개 중 4개 상승`, 엔진의 `votes`)를 보인다.
  - **신호:** KRX 화면이 저장한 신호 규칙(`krx_quant/signal_probability` 기본 60%, `krx_quant/signal_agree` 기본 2개, `KrxAlerts::saved_rule`)으로 판정한다. `상승 신호`·`하락 신호`는 방향색이고, 채점 시각이 지나면 흐리게 남는다(툴팁 "목표 시각이 지났습니다"). 엔진이 막은 합의는 `막힘: 특수 상황`·`막힘: 기준가 없음`·`막힘: 시간 지남`, 예측하지 않은 합의는 그 이유(`합의 대상 없음` 등)를 경고색으로 보인다. "신호" 열 제목의 툴팁이 지금 규칙이다.
  - **직전 채점:** 가장 최근에 채점된 최종 합의가 맞았는지다(`10:26 상승 ✓`, `✗`, `채점 불가`). 봉 마감이 바뀔 때마다 종목마다 최근 봉 마감 6개의 최종 합의(`GET /v1/symbols/{symbol}/predictions?limit=6`, `all` 없이)를 한 종목씩 읽는다.
  - 이 위젯은 알림(데스크톱 알림, 소리)을 내지 않는다. 같은 신호가 두 번 울리지 않게 알림은 KRX 화면에서만 낸다.
- **KRX 순위표**(`krx_leaderboard`, 기본 7×5칸): 순위에 오른 예측, 그다음 아직 표본이 모자란 예측을 합쳐 8개까지, 그 아래 이겨야 하는 기준 예측(방향은 `기준: 늘 보합`, 다른 종류는 그 종류의 기준 예측 모두)을 흐리게 보인다.
  - 열은 순위, 예측, 정답률과 95% 구간(`48.0% (44.1–52.0)`), 거래일 lift 하한(엔진의 `lift.day_low`, 상태 판정이 보는 값), 상태(수습·정식·벤치 등), 표본, 손익과 인버스 손익(거래당, 연구용)이다. 사용자가 맞힘률을 먼저 보므로 정답률이 예측 이름 바로 뒤이고 손익은 맨 뒤다. 손익의 단위는 방향과 장벽이 틱, 시가→종가와 5거래일 시가→종가(codeyoma/krx-quant#310)가 bp이고, 변동폭은 손익 열이 없다.
  - 톱니 설정: 종류(엔진이 예측하는 것, `GET /v1/kinds`), 기간(오늘·5일·20일), 종목(전체 또는 종목). 대시보드 배치에 타일의 config(`{"kind", "period", "symbol"}`)로 저장된다. 읽는 동안 설정을 바꾸면 그 사이에 온 앞 질의의 답은 보이지 않고, 바로 새 질의로 다시 읽는다. 특수 상황은 늘 뺀다(`special=exclude`). 제목에 지금 질의(`방향 · 오늘 · 전체`)가 나온다.
- **KRX 엔진 상태**(`krx_engine_status`, 기본 5×4칸): `GET /v1/status`의 모드(데모면), 장 단계, 마지막 봉 마감, 채점 대기, KIS(연결, 결측 피드), 오늘 특수 상황, 지난밤 학습(날짜, 적용 여부, 승격 수), CPU 예산(사용률, 오늘 건너뛴 횟수), 엔진 커밋과 시작 시각이다.
  - 문제가 있으면 맨 위에 첫 문제 한 줄을 경고색으로 보인다. 순서는 `KIS 연결 끊김`, `KIS 피드 결측 N개`, `지난밤 학습 미적용`(08:00 교체 대기는 문제가 아니다), `CPU 예산 초과 (N%)`다. 그 줄의 값도 경고색이다.
- **KRX 오늘 시가→종가**(`krx_open_to_close`, 기본 5×4칸): 하루 한 번 예측 `open_to_close`(codeyoma/krx-quant#264)의 종목별 가장 최근 예측이다. 종목 칸은 신호 위젯과 같다(로고, 이름, 코드).
  - 예측 시각(`10-19 08:50`, 오늘 것이 아니면 `(지난 거래일)`), 알고리즘들의 세 확률 평균(기준 예측은 빼고, 차트의 표시와 같은 `daily_call`), 보합 폭, 결과(`채점 전 (15:45부터)`, 장 마감 뒤 `정답 하락 -0.85% (81,950 → 81,250) ✓`)를 보인다. 예측 시각의 툴팁은 평균한 알고리즘 수, 표의 툴팁은 이 예측의 규칙이다.
  - 엔진이 이 종류를 예측하지 않으면(`/v1/kinds`에 없거나, `/v1/kinds`가 없는 옛 엔진) "이 엔진에는 시가→종가 예측이 없습니다."를 보인다.
- **읽기.** 위젯마다 `KrxQuantService`를 하나씩 갖고, 보일 때 바로 한 번 읽은 뒤 주기마다 읽는다: 신호 5초, 순위표 60초, 상태 30초, 시가→종가 5분. 숨으면(다른 탭, 접힌 대시보드) 읽지 않는다. 종목마다 하나씩 잇는 읽기(직전 채점, 시가→종가)도 숨으면 다음 종목을 묻지 않는다. 앞의 읽기가 끝나지 않았으면 다음 주기는 건너뛴다. WebSocket 스트림은 쓰지 않는다(타일마다 연결을 따로 열지 않게). 제목 줄 끝이 마지막으로 엔진이 답한 시각이고, 새로고침 버튼은 바로 다시 읽는다.
- **엔진 주소.** KRX 화면에서 넣는 `krx_quant/base_url`을 읽는다. 비어 있으면 "KRX 엔진 주소가 없습니다. KRX 4분 예측 화면에서 엔진 주소를 넣으세요."를 보이고 읽지 않는다. 주소가 바뀌면 다음 읽기에서 그때까지의 읽기를 버리고 새 주소를 잡은 뒤(`KrxQuantService::rebind`, `restart`와 달리 스냅샷을 읽지 않는다) 그 타일의 것만 읽는다. 앞 엔진에서 읽은 것(종목 목록, 종류 정보, 직전 채점, 시가→종가 예측)은 버린다. 다른 엔진은 시가→종가를 다른 규칙으로 예측하거나 예측하지 않을 수 있다.
- **실패.** 아직 보인 값이 없으면 타일 가운데에 오류를 보인다(다음 읽기 동안에도 남고, 읽히면 사라진다). 보인 값이 있으면 그대로 두고 맨 위 한 줄(글자는 사용자 글꼴보다 3px 작게)에 `읽지 못함: <이유>`를 보인다. 일부만 못 읽었으면(종목 하나의 예측 등) `일부 읽지 못함: <이유>`다. 엔진이 답하지 않으면 `HttpClient`의 전송 시간 제한(20초)이 지나서야 오류가 나온다.
  - 연결 실패의 이유는 KRX 화면과 같은 `krx_quant::failure_text`다. `HttpClient`(upstream v4.5.1부터)는 연결 실패를 `Network error: <Qt의 이유>`로 준다. 시간 제한에 걸린 것(`Operation canceled`)은 "엔진이 답하지 않았습니다(시간 초과). 엔진 주소와 네트워크를 확인하세요.", 다른 연결 실패는 "엔진에 연결할 수 없습니다(<이유>). …"다.

    > **바뀜** (2026-10-19, codeyoma/krx-quant#302): 연결 실패는 "엔진 응답을 읽을 수 없습니다: Network error: Operation canceled"처럼 응답 해석 실패와 같은 문구였다 → 연결 실패와 시간 초과를 따로 말한다(KRX 화면도). 이유: `HttpClient`가 연결 실패에도 글자를 붙이게 바뀌어(upstream v4.5.1), 엔진이 꺼진 것을 응답을 잘못 읽은 것처럼 보였다.
- **더블클릭.** 네 위젯 모두 표의 줄을 더블클릭하면 KRX 4분 예측 화면으로 간다(`nav.switch_screen`). 대시보드의 다른 위젯처럼 한 번 클릭은 줄을 고르기만 한다. 그 종목이나 알고리즘을 고른 채 열지는 않는다. KRX 화면은 Fincept의 종목 연결(`IGroupLinked`)을 따르지 않기 때문이다. 연결하면 다른 화면의 종목(예: `AAPL`)도 KRX 화면으로 넘어온다. 알고리즘을 넘길 길도 없다.
- **문구.** KRX 화면처럼 원문이 한국어다. 위젯 이름과 설명도 한국어로 등록해 한국어 팩에 따로 넣지 않는다(영어 UI에서도 한국어로 보인다).
- **코드.** 엔진 답에서 위젯의 줄을 만드는 것은 `fincept-qt/src/services/krx_quant/DashboardView.{h,cpp}`(Qt Core만)이다. 위젯은 `fincept-qt/src/screens/krx_quant/dashboard/`: 공통 타일(`KrxEngineTile`: 주기, 엔진 주소, 실패 표시), 위젯 4개(`KrxDashboardWidgets`), 등록과 템플릿(`KrxDashboard`). `EngineData`는 순위표 항목의 `lift.day_low`(`Entry::day_low`)와 상태의 `cpu.usage`·`cpu.skipped_today`(`EngineStatus::cpu_usage`, `cpu_skipped_today`)를 새로 읽고, 404 답(옛 엔진에 없는 엔드포인트)을 `is_not_found`로 가린다. upstream 파일의 갈래는 위 "upstream 파일 수정 원칙" 표에 있다.
- **테스트.** `tst_krx_dashboard_view`(Qt Core만): 순위표 줄(순위·미순위·기준 예측, 다른 종류의 기준 예측, 표시 개수, 손익 단위), 신호 줄(살아 있는 신호, 막힌 합의, 채점 시각이 지난 신호, 구성원 없는 합의, 예측 없는 종목, 직전 채점의 ✓와 채점 불가), 상태 줄과 첫 문제의 순서, 시가→종가 줄(알고리즘 평균, 채점 전·후, 예측 없음, 지난 거래일). `tst_krx_engine_data`가 `day_low`와 `cpu`를 읽는 것, 연결 실패와 시간 초과의 문구, 404 가리기를 본다.

## KRX 장전 브리핑 (codeyoma/krx-quant#304)

화면 **KRX 장전 브리핑**은 개장 전(07:30–09:00)에 그날 시가를 가늠할 재료를 한 화면에 보인다. Navigate → Trading & Portfolio에서 연다. 명령 팔레트에서 "krx"나 "브리핑"으로 찾아도 된다. 엔진의 `GET /v1/briefing`(krx-quant `docs/engine-api.md`)을 읽어 보이기만 한다. 값은 엔진이 08:50 시가→종가 예측의 입력과 같은 규칙으로 만든다. 판단 재료이고 검증된 신호가 아니다. 주문 기능은 없다.

![장전 브리핑(데모 엔진)](https://raw.githubusercontent.com/codeyoma/FinceptTerminal/krx-screenshots/304-briefing.png)

- **머리줄.** 제목, 엔진 점(초록: 답함, 빨강: 읽지 못함, 회색: 읽는 중. 툴팁에 엔진이 답한 시각, 엔진 시계, 다음 읽기 간격), 그날 줄(`2026-10-19(월) 장전 · 국면 위험회피`. 휴장일은 `(휴장)`, 늦게 여는 날은 `수능일 10:00 개장`), 날 옮기기(◀ 전날, ▶ 다음 날, 오늘), 새로 읽기다.
- **밤사이 미국 · 국면.** SOX, 엔비디아, 마이크론, VIX의 그날 전 마지막 종가와 등락(상승 초록, 하락 빨강), 최근 5개 종가의 등락이 한 줄이다. 종가 칸의 툴팁에 날마다의 값이 있다. 아래 줄이 그날 국면과 이유다(07:30 매크로 읽기, 그날 것이 없으면 그 날짜와 함께).
- **KOSPI200 야간선물.** 전 거래일 저녁에 시작한 야간 세션의 월물, 마지막 가격과 등락(KIS 전일 대비), 고가·저가, 거래량, 마지막 체결 시각, 그리고 분마다 마지막 가격의 선이다. 녹화가 없던 밤은 "녹화 없음"이다.
- **종목.** 로고와 이름, 흐린 코드(KRX 화면의 종목 칸), 전일 KRX 종가, 장 마감 뒤(전일 장 마감부터 20:00 전 마지막 통합 1분봉), NXT 프리마켓(cox와 같은 규칙: 08:40–08:50, 없으면 08:00–08:30의 마지막 체결), KRX 예상 시가(열 제목 "KRX 예상 시가(참고)". KRX 시가 단일가의 마지막 예상가. 엔진은 거래소 구분 코드 `1`(KRX)의 것만 주고, 그 값은 08:50부터 온다. 툴팁에 시각, 예상 체결량, 거래소 구분 코드, 단일가가 끝났는지와 참고 값이라는 것. 값이 없는 칸의 툴팁은 KRX 예상가가 08:50부터 온다는 것과 같은 참고 문구), 전일 외국인·기관 순매수와 5일 합(주)이다. 가격 칸은 전일 KRX 종가 대비 %를 붙이고 오르면 초록, 내리면 빨강이다. 값이 없으면 `—`이다.
  - **KRX 예상 시가는 참고 값이다.** 개장(09:00) 전 시가 단일가의 마지막 예상가라 실제 시가와 다를 수 있다. 2026-10-12–10-20에 잰 7거래일에는 005930·000660 모두 7일 중 2일만 같았고, 다른 날은 1–5틱 달랐다(krx-quant `docs/kis-feeds.md` "피드별 메모", codeyoma/krx-quant#314). 009150은 첫 측정이 거래소 구분 코드를 거르지 않아 엔진의 예약 작업이 다시 잰다(codeyoma/krx-quant#341). 그 결과가 크게 다르면 툴팁 문구를 다시 본다. 열 제목의 툴팁과 칸마다의 툴팁(값이 없는 칸도) 끝에 이것을 적는다(`BriefingView`의 `expected_open_title`, `expected_open_note`).
    > **바뀜** (2026-10-21, codeyoma/FinceptTerminal#101, codeyoma/krx-quant#314): 열 제목 "KRX 예상 시가" → "KRX 예상 시가(참고)", 툴팁에 참고 값이라는 것과 잰 차이를 더했다. 이유: 마지막 예상가가 실제 시가와 7일 중 2일만 같았고(krx-quant 예약 작업 `probe-expected-open-314`), 사용자가 이 값을 참고로 남기기로 했다(2026-10-21).
- **08:50 시가→종가.** 종목마다 하루 한 번 알고리즘(지금은 `cox`)의 세 확률(가장 큰 쪽 굵게), 보합 폭, 결과(15:45 뒤 정답)다. 기준 예측은 넣지 않는다(입력이 없다). 그날 예측 전에는 엔진이 가장 최근 날의 것을 주고, 제목에 `(지난 거래일)`이 붙고 줄이 흐리다.
- **입력 기여.** 고른 종목(종목·예측 표의 줄을 누른다. 처음은 첫 종목)의 예측이 입력마다 받은 몫이다. 입력(한국어 이름, 툴팁은 피처 이름), 그날 값(없으면 "없음"), 표준화 값, 상승 점수·하락 점수(보합 대비 로짓에 더한 몫), 어느 쪽으로 밀었는지(상승 쪽 초록, 하락 쪽 빨강, 둘 다 내렸으면 보합 쪽)다. 몫이 큰 순서다. 알고리즘이 설명하지 않는 예측(#304 전 예측, 기본 모델)은 값만 선언 순서로 보인다.
- **오늘 일정.** 그날의 표시(선물·옵션 만기일, 옵션 만기일, 월말, 월초, 늦은 개장. 없으면 "오늘 표시 없음")와 경제 캘린더의 그날 일정(시각, 나라, 일정, 중요도, 예상·이전·실제)이다. 중요도 높음은 주황이다.
- **읽기.** 화면이 가진 `KrxBriefingLive`(`services/krx_quant/KrxBriefingLive.{h,cpp}`)가 보일 때 바로 읽고, 07:00–09:10(보이는 날이 그날일 때)에는 30초마다, 그 밖에는 5분마다 다시 읽는다. 숨으면 읽지 않는다. 종목 이름과 로고는 `/v1/symbols`를 한 번 읽는다. 다른 날로 옮기면 그날 것을 바로 읽고, 앞 날의 답이 늦게 오면 버린다. WebSocket 스트림은 쓰지 않는다.
- **엔진 주소.** KRX 4분 예측 화면에서 넣는 `krx_quant/base_url`을 읽는다. 비어 있으면 "엔진 주소가 없습니다. KRX 4분 예측 화면에서 엔진 주소를 넣으세요."를 보인다. 주소가 바뀌면 앞 엔진에서 읽은 것을 버리고 새로 읽는다.
- **실패.** 읽지 못하면 머리줄 아래에 `읽지 못함: <이유> (아래는 HH:mm:ss에 읽은 것)`을 보이고, 보인 값은 그대로 둔다. #304 전의 엔진(`/v1/briefing`이 없다, 404)도 읽지 못함이다.
- **코드.** 엔진 답의 해석은 `EngineData`의 `parse_briefing`(구조체 `Briefing` 등), 화면의 줄은 `services/krx_quant/BriefingView.{h,cpp}`(Qt Core만), 읽기 일정과 상태는 `KrxBriefingLive`, 그리기는 `screens/krx_quant/KrxBriefingScreen.{h,cpp}`다. `KrxQuantService`에 `read_briefing`과 `read_symbol_list`를 더했다. upstream 파일의 갈래는 위 "upstream 파일 수정 원칙" 표에 있다.
- **테스트.** `tst_krx_briefing`: 답의 해석(모든 덩어리, 값 없음, 엔진 오류), 화면의 줄(그날 줄, 미국 줄, 야간선물, 종목 칸과 툴팁, 예상 시가 열 제목과 참고 툴팁, 기준 예측을 뺀 예측 줄, 몫 순서의 입력 줄, 일정과 표시, 휴장·수능일), 읽기 간격(07:00–09:10과 그 밖, 다른 날, 휴장), 가짜 엔진(`QTcpServer`) 위의 `KrxBriefingLive`(날·이름 읽기, 날 옮기기, 실패에도 값 남기기, 주소 없음). 그림은 `krx-capture`의 `KRX_CAPTURE_BRIEFING=1`로 데모 엔진 위에서 확인한다(아래 "개발").

## KRX 수급 흐름 (codeyoma/krx-quant#306)

화면 **KRX 수급 흐름**은 한 종목의 그날 장중 수급을 1분 가격 위에 겹쳐 보인다. 외국인·기관 추정 가집계, 프로그램매매, 외국계 회원사를 함께 본다. Navigate → Trading & Portfolio에서 연다. 명령 팔레트에서 "krx"나 "수급"으로 찾아도 된다. 엔진의 `GET /v1/symbols/{symbol}/flow`(krx-quant `docs/engine-api.md`)를 읽어 보이기만 한다. 관찰용이고 예측 신호가 아니다. 주문 기능은 없다.

![수급 흐름(데모 엔진)](https://raw.githubusercontent.com/codeyoma/FinceptTerminal/krx-screenshots/306-flow.png)

- **머리줄.** 제목, 엔진 점(장전 브리핑과 같다), 종목(로고와 이름·코드, 고른 종목은 설정 `krx_quant/flow_symbol`에 남고 없으면 엔진의 첫 종목), 그날 줄(`2026-10-12(월) · 마지막 기록 10:30 · REST 대체 10:00–10:10`, 녹화가 없으면 `녹화 없음`), 날 옮기기(◀ ▶는 주말을 건너뛴다, 오늘), 새로 읽기다.
- **흐름 차트.** 그날 장 시각의 1분 가격(왼쪽 눈금)과 흐름 넷을 각자의 눈금으로 겹친다: 프로그램 순매수(주황, KIS 금액 단위), 외국계 회원사 순매수(파랑, 주), 추정 외국인(청록, 주), 추정 기관(노랑, 주). 머리줄의 상자로 흐름마다 켜고 끈다(상자 색이 선 색이다).
  - 흐름마다 0을 같은 색 점선으로 그린다(부호를 읽게). 특수 상황(VI, 서킷브레이커) 구간은 주황 음영이다.
  - 값이 없는 분은 선을 끊는다. 피드가 끊긴 동안을 평평한 흐름으로 그리지 않는다. 추정은 들어온 때부터 다음 구간까지 계단이고, 마지막 구간은 지금(지난날은 장 마감)까지다.
  - 오늘은 장 시작부터 지금의 30분 뒤까지, 지난날은 하루 전체를 보인다. 끌면 그 구간(5분 이상)을 확대하고, 두 번 누르면 처음 구간으로 돌아간다.
  - 마우스를 올리면 그 분의 세로선과 값 상자(시각, 가격, 프로그램 순매수, 외국계 회원사, 그때까지 들어온 추정과 입력 시각)가 나온다. REST 대체로 받은 값은 `(REST)`가 붙는다.
- **지금까지의 합.** 흐름마다 그날 마지막 값(오늘은 지금까지, 지난날은 장 마감까지)과 전 거래일 같은 시각까지의 값이다. 열 제목에 그 날과 시각이 있다(`전일(10-08) 10:30까지`). 아래 줄은 확정 일별 수급(20:30 뒤)과 추정의 마지막 값이다.
- **회원사 상위 5.** 그날 마지막 회원사 기록의 매도·매수 상위 창구와 누적 수량이다. 외국계는 주황이다. 제목에 기록 시각과 REST 여부가 있다.
- **읽기.** 화면이 가진 `KrxFlowLive`(`services/krx_quant/KrxFlowLive.{h,cpp}`)가 보일 때 바로 읽는다. 처음에는 `/v1/symbols`로 종목과 이름을 읽는다. 오늘 장중(개장부터 마감 10분 뒤까지)에는 30초마다, 그 밖에는 5분마다 다시 읽고, 숨으면 읽지 않는다. 종목이나 날을 바꾸면 바로 읽고, 앞 질의의 답이 늦게 오면 버린다.
- **엔진 주소와 실패.** 장전 브리핑과 같다. `krx_quant/base_url`을 쓰고, 읽지 못하면 머리줄 아래에 이유를 보이고 값은 남긴다. #306 전의 엔진은 읽지 못함이다.
- **코드.** 엔진 답의 해석은 `EngineData`의 `parse_flow`(구조체 `Flow` 등), 차트 선과 표의 줄은 `services/krx_quant/FlowView.{h,cpp}`(Qt Core만), 읽기 일정과 상태는 `KrxFlowLive`, 그리기는 `screens/krx_quant/KrxFlowScreen.{h,cpp}`(차트는 같은 파일의 `KrxFlowChart`, QPainter)다. `KrxQuantService`에 `read_flow`를 더했다.
- **테스트.** `tst_krx_flow`: 답의 해석, 차트 선(값 없는 분에서 끊김, 추정의 계단과 끝), 값 상자, 합과 전일 열, 확정 수급 줄, 회원사 줄(이름 없음은 번호), 그날 줄(REST 구간, 녹화 없음), 읽기 간격, 가짜 엔진 위의 `KrxFlowLive`(종목 먼저, 첫 종목, 종목 바꾸기와 설정, 날 바꾸기, 실패에도 값 남기기, 주소 없음). 그림은 `krx-capture`의 `KRX_CAPTURE_FLOW=1`로 데모 엔진 위에서 확인한다.

## KRX 지평·비용 (codeyoma/krx-quant#307)

화면 **KRX 지평·비용**은 어느 지평에서 맞힘률이 왕복 비용을 넘는지 보인다. 대칭 목표 ±W, 왕복 비용 C면 상승·하락 신호의 맞힘률이 p* = (W + C) ÷ 2W를 넘어야 본전이다. Navigate → Trading & Portfolio에서 연다. 명령 팔레트에서 "krx", "비용", "손익분기"로 찾아도 된다. 엔진의 `GET /v1/cost-map`(krx-quant `docs/engine-api.md`)을 읽어 보이기만 한다. 주문 기능은 없다.

![지평·비용(데모 엔진, 20일)](https://raw.githubusercontent.com/codeyoma/FinceptTerminal/krx-screenshots/307-cost.png)

- **머리줄.** 제목, 엔진 점(다른 KRX 화면과 같다), 종목(전체 또는 한 종목, 로고와 이름·코드), 기간(오늘/5일/20일), 특수 상황 포함, 표가 따르는 왕복 비용(시장가 왕복이 기본, 지정가 왕복), 새로 읽기다. 고른 값은 설정 `krx_quant/cost_symbol`, `cost_period`, `cost_special`, `cost_order`에 남는다. 엔진이 예측하지 않는 종목이 남아 있으면 전체로 돌아간다. 그 아래 줄은 엔진 시계, 기간, 최소 표본, 스프레드 가정이다.
- **종목별 비용.** 종목마다 카드 하나다. 기준가(연속매매 중이면 지금의 통합 중간가와 "실시간", 장 밖이면 마지막 봉 마감의 중간가), 틱과 그 bp, 매도세, 수수료(양쪽), 스프레드 1틱(가정), 지정가·시장가 왕복(bp와 틱), 정답 문턱 k, 하락을 인버스 2X로 칠 때의 왕복(기초 주식 기준, "하한": 상품의 스프레드·괴리·선물 basis가 빠졌다는 것이 툴팁에 있다. 상품이 없는 종목은 "거래 수단 없음"), 아래·위 가장 가까운 틱 구간 경계와 그 너머의 틱·k다. k가 시장가 왕복보다 작으면 k 줄이 빨강이다(4분 방향을 시장가로 드나들면 다 맞혀도 손해).
- **지평 표.** 예측 종류·지평마다 한 줄이다(방향 4분, 장벽 4·15·30·60분, 시가→종가. 엔진이 방향이 있는 종류를 더하면 줄이 저절로 생긴다). 열은 지평(툴팁에 정답 규칙과 날), 폭 W, 비용 C(지정가·시장가, 시가→종가는 bp이고 단일가라 하나), 고른 비용의 손익분기(p*와 C/W, 100%를 넘으면 "(불가능)", 시가→종가는 "해당 없음"과 이유. 툴팁에 공식과, W와 C를 읽은 봉 마감이 근사라는 것), 그리고 최종 합의, 순위 1위(합의 빼고), 가장 센 기준 예측이다.
  - 예측자 칸은 이름, 상승·하락 신호 맞힘률과 그 95% 하한, edge(맞은 몫 − 반대로 틀린 몫, C/W와 견준다)와 신호 수, 연구용 평균 손익이다. 툴팁에 판정의 근거(하한과 p*), 순위, 하락을 인버스로 친 손익이 있다. 없는 예측자는 흐리게 이유를 적는다(합의는 방향에만 있다).
  - 칸 색은 고른 비용에서의 판정이다: 초록 넘음(맞힘률의 신뢰구간 하한 > p*), 빨강 못 넘음, 회색 표본 부족(신호 수 < 순위표의 최소 표본), 색 없음은 판정 없음(p* 없음). 색은 delegate가 칠한다(전역 스타일시트가 표 항목을 칠해 항목의 배경색은 그려지지 않는다).
  - 줄을 두 번 누르면 그 예측자의 상세 통계 창(4분 예측 화면의 `KrxStatsWindow`, 같은 질의)이 열린다. 예측자 칸이면 그 예측자, 다른 칸이면 순위 1위, 없으면 합의, 그다음 기준 예측이다.
- **기간 기준율.** 4분 도달 정답(1분봉 규칙)의 종목·기간별 일수, 창 수, 상승·보합(늘 보합의 맞힘률)·하락 비율, k, 직전 창의 맞힘률이다. 제목 줄 오른쪽에 계산한 때와 읽은 1분봉의 기간이 있다. 엔진이 아직 계산하지 않았으면 그 이유를 보인다.
- **읽기.** 화면이 가진 `KrxCostLive`(`services/krx_quant/KrxCostLive.{h,cpp}`)가 보일 때 바로 읽는다. 처음에는 `/v1/symbols`를 읽고(종목 이름, 고른 종목이 엔진의 것인지), 그다음 비용 지도를 읽는다. `/v1/kinds`와 `/v1/algorithms`(상세 통계 창의 열과 설명)는 비용 지도와 함께 읽고, 실패하면 다음 읽기에 다시 묻는다. 기준가가 실시간인 종목이 있으면(연속매매) 60초마다, 아니면 10분마다 다시 읽고, 숨으면 읽지 않는다. 고르기를 바꾸면 바로 읽고, 앞 질의의 답이 늦게 오면 버린다. 열린 상세 통계 창의 통계는 창을 열 때, 고르기를 바꿀 때, 새 비용 지도마다 다시 읽고, 앞 질의의 통계는 버린다.
- **엔진 주소와 실패.** 다른 KRX 화면과 같다. `krx_quant/base_url`을 쓰고, 읽지 못하면 머리줄 아래에 이유를 보이고 값은 남긴다. #307 전의 엔진은 읽지 못함이다.
- **코드.** 엔진 답의 해석은 `EngineData`의 `parse_cost_map`(구조체 `CostMap` 등), 카드·표의 글과 색은 `services/krx_quant/CostView.{h,cpp}`(Qt Core만), 읽기 일정과 상태는 `KrxCostLive`, 그리기는 `screens/krx_quant/KrxCostScreen.{h,cpp}`다. `KrxQuantService`에 `read_cost_map`(순위표처럼 읽는 중에 다시 물으면 마지막 질의를 이어 읽는다)을 더했다.
- **테스트.** `tst_krx_cost`: 답의 해석, 카드 줄(값, k 경고, 인버스 하한과 거래 수단 없음, 틱 경계), 지평 표의 칸(지평, 폭, 비용, p*와 불가능, 해당 없음과 이유, 예측자 칸과 판정 넷), 기준율 줄과 머리 줄, 읽기 간격, 가짜 엔진 위의 `KrxCostLive`(종목 먼저, 질의 바꾸기와 설정, 실패에도 값 남기기, 모르는 종목은 전체, 실패한 `/v1/kinds` 다시 묻기, 열린 상세 창이 질의를 따르고 앞 질의의 통계를 버림, 주소 없음). 그림은 `krx-capture`의 `KRX_CAPTURE_COST=1`(`KRX_CAPTURE_PERIOD`, `KRX_CAPTURE_SYMBOL`)로 데모 엔진 위에서 확인한다.

## KRX 마감 흐름 (codeyoma/krx-quant#305)

화면 **KRX 마감 흐름**은 005930·000660을 기초로 한 단일종목 ±2배 ETF 16종이 장 마감에 노출을 순자산의 L배로 맞추려고 사고파는 양(마감 리밸런싱 수요) A × (L² − L) × r과, 그 뒤 종가 단일가와 다음날에 일어난 일을 보인다. 009150은 상품이 없는 대조군이다. Navigate → Trading & Portfolio에서 연다. 명령 팔레트에서 "krx", "마감", "리밸런싱"으로 찾아도 된다. 엔진의 `GET /v1/symbols/{symbol}/closing-flow`와 `…/closing-flow/days`(krx-quant `docs/engine-api.md`)를 읽어 보이기만 한다. 수요는 공식으로 계산한 값이고 실제 집행이 아니다. 예측 신호도 아니다. 주문 기능은 없다.

![마감 흐름(데모 엔진, 000660)](https://raw.githubusercontent.com/codeyoma/FinceptTerminal/krx-screenshots/305-closing-000660.png)

- **머리줄.** 제목, 엔진 점(다른 KRX 화면과 같다), 종목(엔진의 종목: 005930, 000660, 009150. 로고와 이름·코드. 고른 종목은 설정 `krx_quant/closing_symbol`에 남고, 없으면 엔진의 첫 종목), 그날 줄(`2026-10-12(월) · 10:30 지금 가격으로 계산`, 15:20부터 `15:20 저장값`, 장 전이면 `장 전`), "한국거래소 통계정보"(아래), 새로 읽기다.
- **오늘의 마감 수요.** 맨 위가 두 구조의 합이다: `매수 +2,327.4억원`은 초록, `매도 −2,378.7억원`은 빨강. 그 아래 줄은 현물+선물형과 선물형의 합, 단일가 비율(현물+선물형 ÷ 분모, `(상한)`: 현물+선물형은 일부를 선물로 집행할 수 있다), 전체 비율, 분모(그날 전 20거래일의 종가 단일가 체결대금 중앙값과 그 20일 중 값이 있던 날 수. 날마다의 체결대금은 녹화한 KRX 체결, 녹화가 없으면 KRX 1분봉의 단일가 봉, 그것도 없으면 통합 1분봉의 단일가 봉이다. 툴팁 끝에 그 날들이 어디서 왔는지 "그 날들: 녹화 15일 · KRX 봉 5일"(엔진의 `denominator_sources`). 없으면 엔진의 이유 `체결대금 없음`이나 `지난날 표 계산 전`), r(기준가 ÷ 전 거래일 KRX 종가 − 1과 두 가격), 계산 시각(연속매매 동안은 그 분 직전 기준가, 15:20부터는 저장한 15:20 값)이다. 수요가 없으면 맨 위에 엔진의 이유가 나온다(장 전, 기준가 없음, 전일 종가 없음, 순자산이 있는 상품 없음, 009150의 상품 없음). 아래 메모는 "공식으로 계산한 값"(실제 집행이 아니다. 금융위원회의 2026-07-28 분산 요청 뒤의 집행 시각은 모른다), "홍콩 상품·ETN 제외"(실제 수요는 더 클 수 있다), 순자산이 없어 합에서 뺀 상품이다.
  > **바뀜** (2026-10-24, codeyoma/FinceptTerminal#105, codeyoma/krx-quant#368): 분모는 "녹화한 종가 단일가 체결대금 중앙값"이고 없으면 `녹화 없음`이었다 → 녹화가 없는 날은 1분봉의 단일가 봉으로 잰 값이 들고, 툴팁에 그 날들의 출처 수가 있으며, 없으면 `체결대금 없음`(엔진의 `no_auction_value`)이다. 옛 엔진의 `no_recorded_auction`은 그대로 `녹화 없음`이다. 이유: 엔진이 녹화가 없는 날의 분모를 KRX(`J`)·통합(`UN`) 1분봉의 단일가 봉으로 채우고 이유 이름을 바꿨다(`check-closing-flow-305`, 2026-10-23 밤: 녹화가 있는 9일 모두 두 봉의 거래량 × 종가가 녹화와 1% 안에서 같았다).
- **상품.** 코드, 이름(엔진이 주는 것. 데모 엔진의 이름은 지어낸 것), 배율 L, 구조, 계수 L² − L, 전일 순자산(억원)과 그 날짜·출처(KIS 또는 "한국거래소 통계정보"), 그 상품의 수요다. 인버스(−2배)의 계수 6은 주황 굵은 글씨이고, 툴팁이 오른 날 마감 매수가 크다는 것(하락 신호를 인버스로 실행하면 그 마감 흐름에 노출된다)을 말한다. 순자산을 받지 못한 상품은 순자산이 `없음`, 수요가 `없음 (합에서 뺌)`(흐림)이다. 009150은 상품이 없어 이 패널이 없다.
- **지난날.** 날마다(새 날이 위) 날짜, 기간, 15:20 수요, 단일가 비율(상한. 툴팁 끝에 분모의 날들의 출처 "분모의 날들: 녹화 7일 · KRX 봉 7일"), 종가 단일가 움직임(bp와 틱. 툴팁에 그날 15:20까지의 흐름과 그날 체결대금과 그 출처), **체결대금**(그날 종가 단일가 체결대금의 출처, 엔진의 `auction_value_source`: `녹화`(녹화한 KRX 체결, 가격 × 수량), `KRX 봉`(KRX 1분봉의 단일가 봉, 거래량 × 종가), `통합 봉`(통합 1분봉의 단일가 봉), 값이 없으면 `—`. 녹화가 아닌 것은 흐린 글씨. 툴팁이 금액과 출처), 방향, 다음날 갭, 다음날 종가 → 종가(다음날이 아직 없으면 `—`), 특수 상황(VI·서킷브레이커·사이드카, 주황), 수요 출처(그날 저장한 `15:20 저장`, 저장한 값이 없어 그날 순자산과 1분봉의 흐름으로 계산한 `1분봉 계산`. 툴팁이 그날 순자산의 출처 "순자산 출처: 한국거래소 통계정보", "KIS", 상품마다 달랐으면 "한국거래소 통계정보 · KIS", 엔진의 `net_assets_source`)다.

  > **바뀜** (2026-10-24, codeyoma/FinceptTerminal#105, codeyoma/krx-quant#368): 열에 체결대금 출처가 없었고, 움직임 툴팁의 체결대금은 늘 "(녹화)"였으며, 수요 출처 칸에 툴팁이 없었다 → 체결대금 열을 더했고, 툴팁이 녹화·KRX 봉·통합 봉을 가르며, 수요 출처 칸의 툴팁이 그날 순자산의 출처다. 이유: 엔진이 날 줄에 `auction_value_source`의 봉 출처와 `net_assets_source`를 준다.
  - 방향은 수요와 단일가가 같은 쪽이면 초록 `같음`, 반대면 빨강 `반대`, 둘 중 하나가 움직이지 않았으면 `—`(세지 않는다)다. 수요가 없는 날은 15:20까지의 그날 흐름과 견주고 `(흐름)`이 붙는다(codeyoma/krx-quant#270과 같은 비교).
  - 비율이 없는 날은 엔진의 이유를 보인다. 그날 전 20거래일에 체결대금이 있는 날이 없으면 `체결대금 없음`이다(엔진의 `no_auction_value`. 백필 1분봉이 있는 첫날(2025-10-13) 뒤로는 모두 값이 있어 그 첫날 무렵뿐이다). 옛 엔진의 `no_recorded_auction`(2026-10-12 전의 날)은 `녹화 없음`이다. 수요가 없는 날은 `—`이고, 그날의 수요는 `순자산 없음`(009150은 `상품 없음`. 툴팁은 상장(2026-05-27) 전이거나 엔진이 그날의 순자산을 받지 못한 날이라고 말한다)이다.

    > **바뀜** (2026-10-24, codeyoma/FinceptTerminal#105, codeyoma/krx-quant#368): "2026-10-12 전의 날은 … `녹화 없음`이다(엔진의 `no_recorded_auction`)", 순자산 없음의 툴팁 "KRX Open API로 지난날을 채우기 전" → `no_auction_value`는 `체결대금 없음`, 옛 이름은 그대로 `녹화 없음`; 툴팁은 상장 전이거나 순자산을 받지 못한 날. 이유: 엔진이 녹화 전의 날을 1분봉으로 채우고 이유 이름을 바꿨으며, KRX Open API로 상장일부터 순자산을 채운다.
  - 표는 15줄을 보이고 그 아래는 표 안에서 굴린다. 제목 줄 오른쪽에 엔진이 계산한 때, 범위(기본 지난 1년), 날 수가 있다. 009150은 수요·비율·수요 출처 열이 없다.
- **기간 요약.** 기간(structure-check의 기간: 상장 전, 상장 뒤, 규제 뒤, 애프터마켓 뒤. 날이 있는 것만)과 비율 3분위(|비율|의 범위)마다 두 줄이다: 모든 날, 특수 상황이 있던 날을 뺀 날. 열은 일 수, |단일가| 중앙값(bp), 단일가 방향 일치, 다음날 갭 반대, 다음날 종가 반대(각 비율과 그 날 수)다. 표본이 20일 미만인 칸은 흐리고, 툴팁이 우연일 수 있다고 말한다. 009150은 비율이 없어 기간 줄만 있다.
- **"한국거래소 통계정보".** 보이는 값 가운데 하나라도 KRX Open API에서 온 순자산이면 머리줄 오른쪽에 주황으로 이 글이 보인다(KRX 약관, krx-quant `docs/design.md` 4.1). 오늘 답의 상품마다, 그리고 지난날 표의 날 줄마다 `net_assets_source`를 보고, KIS의 것(`kis`)이 아니면 KRX의 것으로 본다: 상품의 `krx`, 날 줄의 `krx`와 `krx+kis`(상품마다 출처가 달랐던 날)다. 엔진이 KIS 밖에서 순자산을 받는 출처는 KRX Open API뿐이다. 상품 표의 출처 칸도 "한국거래소 통계정보"(주황)가 되고, 날 줄의 수요 출처 툴팁도 그렇게 말한다(위). 날 줄이 모두 `kis`이거나 비어 있고 상품도 KIS의 것뿐이면 보이지 않는다.

  > **바뀜** (2026-10-24, codeyoma/FinceptTerminal#105, codeyoma/krx-quant#368): "엔진에서 그 출처의 이름은 아직 정해지지 않았다 … 지난날 답의 날 줄에는 순자산 출처가 없다(v1 계약)" → 엔진이 상품에 `krx`, 날 줄에 `krx`·`kis`·`krx+kis`를 준다. 화면의 규칙(KIS가 아니면 KRX)은 그대로라 코드는 그대로이고, `krx+kis`의 이름("한국거래소 통계정보 · KIS")과 테스트를 더했다. 이유: 엔진이 KRX Open API로 순자산을 받고 날 줄에 출처를 남긴다(codeyoma/krx-quant#368).
- **읽기.** 화면이 가진 `KrxClosingLive`(`services/krx_quant/KrxClosingLive.{h,cpp}`)가 보일 때 바로 읽는다. 처음에는 `/v1/symbols`로 종목과 이름을 읽는다. 수요가 그 분의 것인 동안(연속매매) 60초마다 다시 읽는다. 그 밖에는 장 전이면 09:00에(늦게 여는 날은 열릴 때까지 60초마다), 거래일 15:20 뒤에는 20:35에 그리고 지난날 표가 그날 20:30 뒤에 계산될 때까지 10분마다, 그다음에는 날이 바뀔 때(자정 직후) 읽는다. 읽지 못하면 60초 뒤에 다시 읽는다. 지난날 표(`…/closing-flow/days`, 엔진의 기본 범위)는 하루 한 번 바뀌어서 처음, 종목을 바꿀 때, 날이 바뀐 뒤 첫 읽기, 그날 저녁 계산될 때까지, 새로 읽기, 그리고 앞서 읽지 못했을 때(다음 읽기에서 다시)만 읽는다. 숨으면 읽지 않는다. 종목을 바꾸면 바로 읽고, 앞 종목의 답이 늦게 오면 버린다.
- **엔진 주소와 실패.** 다른 KRX 화면과 같다. `krx_quant/base_url`을 쓰고, 읽지 못하면 머리줄 아래에 이유를 보이고 값은 남긴다. 지난날 표를 읽지 못한 것은 `지난날:`이 붙는다. #305 전의 엔진은 읽지 못함이다.
- **코드.** 엔진 답의 해석은 `EngineData`의 `parse_closing_flow`와 `parse_closing_days`(구조체 `ClosingFlow`, `ClosingDays` 등), 카드와 표의 글·색, 읽기 간격은 `services/krx_quant/ClosingView.{h,cpp}`(Qt Core만), 읽기와 상태는 `KrxClosingLive`, 그리기는 `screens/krx_quant/KrxClosingScreen.{h,cpp}`다. `KrxQuantService`에 `read_closing_flow`와 `read_closing_days`를 더했다.
- **테스트.** `tst_krx_closing`: 두 답의 해석(값 없음과 이유, 009150, 장 전, 계산 전, 엔진 오류, codeyoma/krx-quant#368의 `denominator_sources`·`auction_value_source`·`net_assets_source`·`no_auction_value`와 그것이 없는 옛 엔진), 카드 줄(매수·매도와 부호, 구조별 합, 상한 표시, 분모와 그 날들의 출처, 체결대금 없음과 녹화 없음, r, 메모와 합에서 뺀 상품, 15:20 저장값), 지난날 줄의 체결대금 출처(녹화, KRX 봉, 통합 봉, `—`)와 그 툴팁, 순자산 출처 툴팁(`krx+kis`, `kis`, 없음), 분모의 날들, 009150의 이유와 줄, 상품 표(없음, 계수 6), 지난날 줄(새 날이 위, 녹화 없음, 순자산 없음·상품 없음, 흐름과의 비교, 특수 상황, 다음날 없음), 요약(두 줄씩, 20일 미만 흐림), "한국거래소 통계정보"를 보일 때, 읽기 간격과 지난날 표를 읽을 때, 가짜 엔진 위의 `KrxClosingLive`(종목 먼저, 다음 분에는 오늘만, 날이 바뀌면 지난날 표도, 종목 바꾸기와 설정, 읽는 중에 바꾼 종목, 실패에도 값 남기기, 읽지 못한 지난날 표를 다음 읽기에서 다시 묻기, 주소 없음). 그림은 `krx-capture`의 `KRX_CAPTURE_CLOSING=1`(`KRX_CAPTURE_SYMBOL`)로 데모 엔진 위에서 확인한다.

## KRX 자동 매매 (codeyoma/krx-quant#397, #401)

화면 **KRX 자동 매매**는 엔진의 자동 매매 trader(005930을 삼중 장벽으로 사고판다, krx-quant `docs/design.md` 14장)의 모드, 상태, 잔고, 포지션, 손익, 한도, 매매와 정책 버전을 보인다. Navigate → Trading & Portfolio에서 연다. 명령 팔레트에서 "krx", "자동", "매매"로 찾아도 된다. 엔진의 `GET /v1/trader`, `/v1/trader/days`, `/v1/trader/trades`, `/v1/trader/policies`(krx-quant `docs/engine-api.md`)를 읽어 보인다. 화면이 하는 쓰기는 정지 두 가지(`POST /v1/trader/pause`)뿐이다. 재개, 모드, 한도는 화면에서 바꿀 수 없다(서버에서만 된다).

![자동 매매(데모 엔진)](https://raw.githubusercontent.com/codeyoma/FinceptTerminal/krx-screenshots/397-trader.png)

- **머리줄.** 제목, 엔진 점(다른 KRX 화면과 같다), 모드 배지(`모의`·`KIS 모의`는 청록, `실전`은 주황: 4분 예측 화면 차트의 매수·매도 표시와 같은 색이다. 툴팁에 모드의 뜻과, 모드는 서버 `.env`로만 바뀐다는 것), 상태 배지(`도는 중` 초록, `새 진입 정지`·`정지` 노랑, `하루 한도`·`누적 한도` 빨강. 툴팁에 그 상태에서 trader가 하는 일과 다시 켜는 방법), 챔피언과 실전 관문 진행(`챔피언 정책 1 · 관문 모의 2/20일 · 하한 -2.7%`: 엔진의 `champion`과 `gate`, codeyoma/krx-quant#400. 하한은 날마다 순손익 평균의 95% 하한 ÷ 운용 상한이고, 관문을 넘으면 `· 관문 넘음`이 붙는다. 첫 챔피언 전에는 `챔피언 정책 없음`, #400 전의 엔진이면 없다. 관문을 넘으면 초록이다(codeyoma/krx-quant#401). 툴팁에 관문의 조건과, 넘어도 저절로 실전이 되지 않는다는 것. 자세한 것은 아래 "정책 버전"의 관문 줄에 있다), 상태 줄(`상태 10-12 10:30:00 · 시작 10-12 10:05 · 정지 10-08 15:02 화면(새 진입 정지)`), 새로 읽기다.
  - trader가 15초 넘게 상태를 쓰지 않았으면(엔진의 `alive`가 거짓) 상태 배지가 빨강 `응답 없음`이고, 툴팁이 마지막으로 쓴 때와 그때의 상태를 말한다. 아래 값은 그때의 것이다. trader가 상태를 쓴 적이 없으면(시작 전, 저장소 없음) `상태 없음`이고 값은 `—`다.
- **카드.** 운용 상한, 현금(주문 가능), 포지션, 오늘 순손익, 누적 순손익, 하루 한도 사용, 누적 한도 사용이다.
  - 포지션: 수량 × 평균가, 목표가·손절가, 시간 장벽과 남은 시간, 평가에 쓴 매수 1호가와 평가 손익, 칸(예측 종류 · 예측자). 남은 시간은 trader의 시계로 센다: 엔진이 준 상태의 시각에 그 답 뒤로 흐른 시간을 더한다(1초마다 다시 센다). 그래서 이 컴퓨터의 시계와 상관없다. 정지나 한도가 다음 호가에 팔게 했으면 노랑 `매도 중`이다. 포지션이 없으면 `없음`이다.
  - 오늘·누적 순손익: 실현(청산한 매매의 순손익, 수수료와 매도세를 뺌)과 그날 매매 수, 평가, 가장 낮을 때. 누적은 세기 시작한 때(그 모드의 첫 시작 또는 서버의 `krx-quant trade rearm`)도 보인다. 이익은 초록, 손실은 빨강이다.
  - 한도 사용: 지금 손익이 한도에서 쓴 몫(%)과 한도(원, 운용 상한의 %). 절반을 넘으면 노랑, 닿았으면 빨강과 닿은 때다.
- **날마다 순손익.** 한 모드의 거래일마다 청산한 매매의 순손익을 막대(이익 초록, 손실 빨강)로, 보이는 첫날부터의 합을 주황 선으로 한 눈금 위에 그린다. 손실 한도에 닿은 날은 빨간 테두리다. 날에 마우스를 올리면 그날의 순손익과 매매 수, 합, 하루 가장 낮을 때와 한도의 몫, 누적 손익과 그 가장 낮을 때, 그날 마지막 상태가 나온다. 날이 있는 모드가 둘 이상이면 머리줄에 모드 고르기가 생긴다. 처음은 trader의 지금 모드다(그 모드의 날이 없으면 가장 최근 날의 모드). 제목 아래 줄은 `모의 3일 · 순손익 합 -8,496원 · 이익 1일 · 손실 2일`처럼 요약한다.
- **정지.** 버튼 둘이다. **새 진입 정지**는 새로 사지 않게 하고 들고 있는 것은 원래 규칙(목표가, 손절가, 시간 장벽)대로 팔게 둔다. **정지하고 즉시 매도**는 새로 사지 않게 하고 들고 있는 것을 다음 호가의 매수 1호가에 바로 판다.
  - 누르면 확인 창이 먼저 뜬다. 무엇을 하는지, 들고 있는 포지션(수량, 칸, 즉시 매도면 평가 손익), 즉시 매도면 모드(실전이면 "실전 계좌의 매도 주문입니다"), 다시 켜는 것은 서버에서만 된다는 것을 적는다. 기본 버튼과 Esc는 `취소`다. 확인 창의 버튼을 눌러야만 `POST /v1/trader/pause`(`{"sell_now": false}` 또는 `{"sell_now": true}`, `Content-Type: application/json`)를 보낸다. 보낸 뒤에는 결과 줄(`정지하고 즉시 매도 요청을 보냈습니다(10:07:00). trader가 1초 안에 읽습니다.`, 실패면 빨강으로 그 이유)을 보이고 바로 다시 읽는다.

    ![정지하고 즉시 매도의 확인 창(데모 엔진)](https://raw.githubusercontent.com/codeyoma/FinceptTerminal/krx-screenshots/397-trader-confirm.png)
  - 같은 정지가 이미 걸려 있으면(새 진입 정지 중의 새 진입 정지, 즉시 매도 중의 둘 다) 버튼이 꺼지고, 툴팁이 언제 누가 걸었는지와 재개 방법을 말한다. 보낸 정지를 trader가 아직 읽지 않았으면 그것이 덮는 버튼도 꺼진다(즉시 매도는 둘 다, 새 진입 정지는 그 버튼만). 엔진의 상태를 읽기 전과 보내는 동안에도 꺼져 있다.
  - **재개 버튼은 없다.** 버튼 아래에 "재개는 서버에서만 됩니다: krx-quant trade resume (정지), krx-quant trade rearm (누적 한도)."를 적는다(krx-quant `docs/operations.md` "한도, 정지, 재개, 다시 켜기").
  - 확인 창이 떠 있는 동안에도 화면은 다시 읽는다. 그사이 다른 KRX 화면에서 엔진 주소가 바뀌어 새 엔진을 읽었으면, 확인해도 보내지 않고 그렇다고 적는다(확인은 창을 열 때 보이던 엔진에 한 것이다).
  - **최근 요청**: 엔진의 `controls`(최근 20개, 최근 것부터)를 시각, 요청(새 진입 정지, 정지하고 즉시 매도, 재개, 누적 한도 다시 켜기와 그 모드), 누가(화면, 서버), trader가 읽은 때(아직이면 노랑 `아직 읽지 않음`. trader가 멈춰 있으면 다시 뜰 때 읽는다)로 보인다.
- **매매.** 최근 매매 100개, 진입이 늦은 것부터다. 처음은 챔피언의 매매(trader가 낸 것: 챔피언의 장부가 낸 매매와 첫 챔피언 전의 매매. 한도가 세고 4분 예측 화면의 차트에 보이는 것, 엔진의 기본 `GET /v1/trader/trades`)다. 머리줄 오른쪽에서 정책 버전(`정책 2 · 도전`처럼 상태가 붙는다. 정책 버전 표와 같은 순서)을 고르면 그 정책의 매매(`?policy=2`: 도전 정책으로 모의로 돈 매매와 챔피언으로 낸 매매)를 곧바로 읽는다(codeyoma/krx-quant#401). 고른 것은 설정에 남지 않아 앱을 다시 켜면 챔피언의 매매이고, 다른 엔진을 읽어도 챔피언의 매매로 돌아간다. 고르는 동안 읽던 다른 정책의 답은 버리고, 새로 고른 정책의 것을 그 뒤에 읽는다. 머리줄 줄은 `챔피언의 매매 11개`, `정책 2의 매매 2개`다. 열은 진입(시각 · 매수가), 청산(시각 · 매도가, 진입과 다른 날이면 날짜도. 들고 있으면 노랑 `보유 중`), 수량, 청산 이유(익절 초록, 손절·하루 한도·누적 한도 빨강, 정지 매도 노랑, 시간 청산), 비용(수수료 양쪽과 매도세, 툴팁에 나눈 값. 들고 있으면 매수 수수료와 `(매수)`), 순손익(비용을 뺀 것. 들고 있으면 흐린 `평가 …`), 칸(예측 종류 · 예측자, 비용 지도의 자리: 합의, 1위, 기준), 확률(오름 확률과 문턱 θ), p*(그 칸을 고를 때 시장가 왕복의 손익분기 맞힘률), 하한(그 칸의 맞힘률 95% 하한), 모드(실전 주황, 모의·KIS 모의 청록), 정책(그 매매를 낸 정책 버전 `정책 1`. 도전 정책의 모의 매매는 청록 `정책 2 · 도전`이고 툴팁에 한도와 정지에 멈추지 않고 차트에 보이지 않는다는 것. 첫 챔피언 전의 매매는 흐린 `—`, codeyoma/krx-quant#401)다.
  - 다시 읽어 같은 줄이면 표를 건드리지 않는다. 줄이 바뀌면(들고 있는 매매의 평가, 새 매매) 다시 채우되, 아래로 굴려 보던 표는 보던 맨 위 매매가 맨 위에 남고 고른 줄도 그대로다(굴리지 않은 표는 맨 위에 새 매매가 보인다). 최근 요청 표도 같다(`krx_tables::refill`).
  - 확률·p*·하한의 툴팁에 사는 규칙과 Kelly f* → 산 비율이, 진입·청산·수량·칸의 툴팁에 매매 번호, 봉 마감, 정한 때, 기준가·틱·폭과 목표가·손절가·시간 장벽, 정책 버전이 있다. 칸 고르기(codeyoma/krx-quant#395) 전의 매매는 p*와 하한이 흐린 `—`다.

    ![정책 2(도전 정책)를 고른 매매 표(데모 엔진)](https://raw.githubusercontent.com/codeyoma/FinceptTerminal/krx-screenshots/401-trader-policy-2.png)

  > **바뀜** (2026-10-30, codeyoma/FinceptTerminal#109, codeyoma/krx-quant#401): 매매 표는 엔진의 기본 답(챔피언의 매매)만 보였고 열은 모드에서 끝났다 → 정책 버전을 골라 그 정책의 매매(`?policy=N`)도 보고, 열 끝에 정책이 있다. 이유: 도전 정책이 모의로 낸 매매를 보려고(codeyoma/krx-quant#401 "매매 표를 정책으로 고르기").

- **정책 버전 (codeyoma/krx-quant#401).** 매매 표 아래 패널이다. 정책은 버전이 있는 값 묶음(매매할 칸과 순위, 칸마다 문턱 θ와 Kelly 비율 λ)이고, 밤마다 맞춘 도전 정책이 모의로 챔피언을 이겨야 챔피언이 된다(krx-quant `docs/design.md` 14장 "정책", "도전 정책과 승격", "실전 관문"). 엔진의 `GET /v1/trader/policies`(정책 버전과 챔피언이 바뀐 기록)와 `GET /v1/trader`의 `gate`·`policies`(실전 관문, 도는 정책의 모의 기록과 승격 판정)를 읽는다. 머리줄 오른쪽은 `챔피언 정책 2 · 도전 3개 · 대기 1개 · 전 챔피언 1개 · 내려짐 1개`처럼 센다(첫 밤 정책 맞추기 전에는 그렇다고).
  - **관문 줄.** 표 위에 실전 관문을 다 적는다: `실전 관문 · 챔피언 정책 1의 모의 기록 2/20일 · 날마다 평균 -5,209원 · 95% 하한 -26,882원 (운용 상한의 -2.7%) · 못 넘음: 18일 더, 하한 ≤ 0`. 무엇이 모자라는지(날 수, 하한 ≤ 0, 2일 전이면 하한이 아직 없음)를 말하고, 넘었으면 초록 `넘음: 실전 모드는 서버에서 켭니다`다. 첫 챔피언 전에는 첫 챔피언의 모의 기록부터 센다고 적는다. #400 전의 엔진이면 없다.
  - **표.** 줄 순서는 챔피언, 도전(자리 셋), 대기, 전 챔피언, 내려짐이고, 같은 상태끼리는 새 버전부터다. 열은 정책(`정책 2`), 상태(챔피언 초록, 도전, 흐린 대기·전 챔피언·내려짐), 맞춘 때, 칸(순위대로 `장벽 4분 · breiman (1위) / 장벽 15분 · …`, 칸이 없는 정책은 `칸 없음`), θ와 λ(칸 순서대로 `65.0% / 70.0%`, `1 / 0.25`), 모의 성적(모의 날, 매매, 순손익, 날마다 평균, 하한: 날마다 순손익 평균의 95% 하한, 2일부터, 0보다 크면 초록), 판정이다.
  - 판정: 챔피언은 실전 관문(`관문 2/20일 · 못 넘음`, 넘으면 초록 `관문 넘음`). 도전 정책은 챔피언과 함께 돈 거래일과 날마다 (도전 − 챔피언) 순손익의 하한이다: `함께 1/10일 · 날 수 부족`, 오늘 밤 자리를 받았으면 `함께 0/10일 · 다음 거래일부터`, 하한이 0 이하면 노랑 `함께 12/10일 · 차이 하한 -244원 ≤ 0`, 지금 판정하면 이기면 초록 `이김 · 함께 12/10일 · 차이 하한 +63원`. 툴팁에 평균, 도전 정책 수로 Bonferroni 보정한 신뢰수준(셋이면 98.33%), 교체는 장이 끝난 뒤 밤 작업이 한다는 것, 20일까지 이기지 못하면 내려진다는 것과 남은 날이 있다. 대기는 `자리 기다림`, 전 챔피언은 `10-29 정책 2에게 짐`, 내려진 것은 `10-28 내려짐`이다.
  - 모의 성적은 도는 정책(챔피언, 도전)이면 `GET /v1/trader`의 것(상태와 함께 몇 초마다 다시 읽는다), 아니면 정책 버전의 것이다. 툴팁: 버전·칸 열에 맞춘 때와 이유, 비용 지도와 통과한 칸 수, 맞춘 날들의 모의 결과, 자리를 받은 때와 내려진 때. 칸·θ·λ 열에 칸마다 맞춘 값(p*, 하한, f* → 비율, 맞춘 날, 모의 매매·이익·순손익)과 비용 지도는 통과시켰지만 뺀 칸과 그 이유. 모의 열에 첫날과 마지막 날, 하한의 운용 상한 대비.
  - **교체 기록.** 표 아래, 최근 것부터: 챔피언이 바뀐 때(`처음 챔피언`, `챔피언 (정책 1 → 2)`와 엔진이 남긴 이유, 툴팁에 함께 돈 날·평균·하한·신뢰수준·도전 정책 수), 도전 정책이 자리를 받은 때, 내려진 때다. 같은 시각이면 챔피언 교체, 내려짐, 자리 받음 순서다.
  - 다시 읽어 같은 줄이면 두 표를 건드리지 않는다. 바뀌면 다시 채우되 굴려 보던 맨 위 줄과 고른 줄이 남는다(`krx_tables::refill`).

    ![정책 버전과 교체 기록(데모 엔진: 챔피언 정책 1, 도전 정책 2)](https://raw.githubusercontent.com/codeyoma/FinceptTerminal/krx-screenshots/401-trader-policies.png)

  > **바뀜** (2026-10-30, codeyoma/FinceptTerminal#109, codeyoma/krx-quant#401): "정책 표와 관문의 자세한 진행(정책 버전, 챔피언과 도전 정책, 모의 성적, 도전 정책의 승격 판정)은 매매 표 아래에 codeyoma/krx-quant#401이 더한다. 이 화면은 머리줄의 한 줄만 보인다" → 위 "정책 버전" 패널이 그것을 보인다. 이유: codeyoma/krx-quant#401.

- **읽기.** 화면이 가진 `KrxTraderLive`(`services/krx_quant/KrxTraderLive.{h,cpp}`)가 보일 때 상태·날마다·매매·정책 버전을 함께 읽는다. 그 뒤 상태(`/v1/trader`)는 trader가 돌고 포지션이 있으면 2초마다, 연속매매(평일 09:00–15:20 KST) 동안은 5초마다, 그 밖에는 5분마다 다시 읽는다. 화면은 거래소 일정을 몰라서 평일 휴장일에도 5초마다 읽는다. 날마다(`/v1/trader/days`, 엔진의 기본 범위인 1년), 매매(`/v1/trader/trades?limit=100`, 정책을 골랐으면 `&policy=N`)와 정책 버전(`/v1/trader/policies`, codeyoma/krx-quant#401)은 상태가 그것들이 바뀌었을 수 있다고 말할 때(모드, 상태, 시작, 그날, 그날 청산한 매매 수, 누적을 세기 시작한 때, 열린 매매, 챔피언, 도는 정책의 상태·모의 날 수·매매 수가 바뀌면), 정지를 보낸 뒤, 새로 읽기, 앞서 읽지 못했을 때(다음 상태 읽기와 함께), 그리고 5분마다만 읽는다. 매매는 정책을 고르면 곧바로도 읽는다. 숨으면 읽지 않는다. WebSocket 스트림은 쓰지 않는다.

  > **바뀜** (2026-10-30, codeyoma/FinceptTerminal#109, codeyoma/krx-quant#401): 상태 뒤에 따로 읽는 것은 날마다와 매매였고, 그것들을 다시 읽게 하는 상태의 값에 챔피언과 도는 정책이 없었다 → 정책 버전도 함께 읽고, 챔피언이나 도는 정책의 상태·모의 날 수·매매 수가 바뀌어도 다시 읽는다. 이유: 정책 버전 패널(codeyoma/krx-quant#401).

- **엔진 주소와 실패.** 다른 KRX 화면과 같다. `krx_quant/base_url`을 쓰고, 읽지 못하면 머리줄 아래에 이유를 보이고 값은 남긴다. 날마다, 매매, 정책 버전만 읽지 못한 것은 `날마다:`, `매매:`, `정책:`이 붙는다(#399 전의 엔진은 `정책:` 읽지 못함이고 나머지는 보인다). codeyoma/krx-quant#396 전의 엔진(`/v1/trader`가 없다)은 읽지 못함이고 버튼은 꺼져 있다. 확인한 뒤 보내기 전에 다른 KRX 화면에서 엔진 주소가 바뀌었으면 보내지 않고(확인한 상태와 다른 엔진이다) 새 엔진을 읽는다.
- **코드.** 엔진 답의 해석은 `EngineData`의 `parse_trader`, `parse_trader_days`, `parse_trader_trades`, `parse_trader_pause`, `parse_trader_policies`(구조체 `TraderState`, `TraderDay`, `TraderTrade`, `TraderControl`, codeyoma/krx-quant#401의 `TraderPolicies`·`TraderPolicy`·`TraderPolicyCell`·`TraderChampionChange`와 상태의 `TraderRunning`·`TraderPaper`·`TraderPromotion`, 매매의 `champion` 등)와, 모드·상태·청산 이유의 한국어(`trader_mode_label`, `trader_status_label`, `trader_exit_label`. 4분 예측 화면 차트의 매수·매도 호버도 이것을 쓴다)다. 카드·표·곡선의 글과 색, 확인 창의 글, 버튼을 누를 수 있는지, 읽기 간격, 정책 버전 표·교체 기록·관문 줄·매매의 정책 고르기(`policy_rows`, `history_rows`, `gate_line`, `trade_policy_choices`)는 `services/krx_quant/TraderView.{h,cpp}`(Qt Core만), 읽기와 정지 보내기와 상태는 `KrxTraderLive`, 그리기와 확인 창은 `screens/krx_quant/KrxTraderScreen.{h,cpp}`(곡선은 같은 파일의 `KrxTraderDaysChart`, QPainter)다. `KrxQuantService`에 `read_trader`, `read_trader_days`, `read_trader_trades`(정책을 주면 `&policy=`), `read_trader_policies`, `pause_trader`(엔진 API의 유일한 쓰기, JSON 몸)를 더했다. 재개를 보내는 코드는 없다.
- **테스트.** `tst_krx_trader`: 답의 해석(상태, 산 뒤 호가가 아직 없는 포지션, 쓴 적 없는 상태, 챔피언과 관문(#400 전의 엔진에는 없음), 날마다, 매매의 `null`과 #395 전의 매매, 엔진의 오류, 정지의 답), 머리줄(모드 셋, 상태 다섯, 응답 없음, 상태 없음, 정지 줄, 관문 줄: 진행 중, 넘음, 하한 없음, 챔피언 없음, #400 전의 엔진), 카드(포지션 있음·없음, 남은 시간과 trader의 시계, 매도 중, 한도의 노랑·빨강과 닿은 때, 쓴 적 없는 상태), 매매 표(보유 중과 평가, 청산 이유 여섯과 색, 비용·순손익과 툴팁, 칸과 자리, 확률과 θ, p*·하한 없음, 다른 날의 청산, 정책 열: 챔피언의 것, 도전 정책의 모의 매매, 첫 챔피언 전), 정책 버전(codeyoma/krx-quant#401: 답의 해석(칸·맞춘 값·뺀 칸·맞춘 것, 기다리는 정책의 빈 기록, 교체의 수치, 첫 맞추기 전, 상태의 도는 정책과 승격 판정, 매매의 `champion`), 도전 정책이 없을 때(챔피언 한 줄, 관문 판정, 처음 챔피언 한 줄, 고르기 둘)와 도전 정책 셋·대기·전 챔피언·내려짐(줄 순서, 칸 둘의 θ·λ, 도는 정책은 상태의 기록, 이김·하한 ≤ 0·날 수 부족과 Bonferroni 98.33%, 칸 없음, 진 날·내려진 날, 교체 기록의 순서와 수치, 상태 전), 관문 넘음·못 넘음(머리줄과 관문 줄의 글과 색, 날 수만·하한만 모자람, 하한 없음, 첫 챔피언 전, #400 전의 엔진)), 날마다 곡선(모드, 처음 모드, 합, 한도에 닿은 날, 요약 줄), 최근 요청, 확인 창의 글과 버튼을 누를 수 있는지(같은 정지, 읽지 않은 요청, 상태 전), 읽기 간격, 가짜 엔진(`QTcpServer`) 위의 `KrxTraderLive`(셋을 함께 읽기, 그 뒤 상태만, 매매가 바뀌면·5분 뒤·새로 읽기에 날마다와 매매도, 멈추면 읽지 않음, GET 밖의 요청 없음, 확인에 아니요면 보내지 않음, 예면 JSON `{"sell_now": true}`·`false`의 POST와 다시 읽기, 보내는 동안 다시 보내지 않음, 엔진의 거부, 실패에도 값을 남기고 다음 읽기에서 다시 묻기, #396 전의 엔진, 엔진 주소가 바뀌면 보내지 않고 새로 읽기, 확인 창이 떠 있는 동안 새 엔진을 읽었으면 확인해도 보내지 않기, 주소 없음, 정책 버전을 날마다·매매와 함께 읽고 도는 정책의 기록이나 챔피언이 바뀌면 다시 읽기, #399 전의 엔진, 고른 정책의 매매를 곧바로 `&policy=2`로 읽기, 읽는 중에 다시 고르면 앞 답을 버리고 새것을 읽기, 다른 엔진이면 챔피언의 매매로). `tst_krx_trader_screen`(Widgets, 가짜 엔진): 정지 버튼 둘이 있고 재개 버튼은 없는 것, 다시 읽어 같은 줄이면 매매 표를 건드리지 않는 것, 줄이 바뀌어도 굴려 보던 맨 위 매매와 고른 줄이 남는 것, 굴리지 않았으면 새 매매가 맨 위에 보이는 것, 정책 버전 표(도전 정책 없음과 셋: 줄과 상태, 판정과 Bonferroni 툴팁, 교체 기록, 매매 고르기의 항목, 머리줄 오른쪽 줄), 관문 넘음·못 넘음(머리줄과 관문 줄의 글과 초록, 챔피언 줄의 판정), 정책을 고르면 그 매매가 보이고 다시 읽어도 남고 챔피언의 매매로 돌아가는 것. `tst_krx_chart_model`이 차트 호버의 정지 매도·하루 한도·누적 한도를 본다.
- **그림.** `krx-capture`의 `KRX_CAPTURE_TRADER=1`로 데모 엔진 위에서 찍는다. `KRX_CAPTURE_SCROLL=policies`면 정책 버전 패널로, `=trades`면 매매 패널로 굴려 찍고, `KRX_CAPTURE_POLICY=2`면 매매 표에서 정책 2를 고른다(codeyoma/krx-quant#401). `KRX_CAPTURE_CONFIRM=entries|sell`이면 그 버튼을 눌러 확인 창도 `<out>-confirm.png`로 찍고 취소한다(보내지 않는다). 데모 엔진에는 10:30의 실전 trader(10:05 시작, 아침의 마지막 매매를 들고 있다), 모드마다 날마다 장부, 10-08 화면의 새 진입 정지와 10-12 서버의 재개, 챔피언 정책 1과 도전 정책 2(관문은 모의 2/20일)가 있다(codeyoma/krx-quant#415). 데모에 정지를 보내면 엔진처럼 쓰지만 읽을 trader가 없어 `아직 읽지 않음`으로 남는다.

## 개인 빌드

- `custom` 브랜치의 커밋에 `custom-v*` 태그(예: `custom-v4.5.0-krx.1`)를 달아 push하면 `custom-release` workflow가 맥 DMG와 윈도우 `setup.exe`를 만들어 이 fork의 Release(prerelease)에 올린다.
- Release에는 가장 새 개인 빌드 하나만 남는다. `custom-release`의 release job이 올린 뒤 다른 `custom-v*` Release를 모두 지운다. 초안(draft)은 세지도 지우지도 않는다(앱도 초안은 보지 않는다). 태그는 남긴다(`custom-auto-tag`가 태그로 다음 번호를 정한다). "가장 새"는 실행 순서가 아니라 가장 높은 버전(`sort -V`)이다. 그래서 늦게 끝난 옛 태그의 실행이 새 빌드를 지우지 않고, 자기 Release를 지운다. 옛 태그를 다시 빌드해도(Run workflow에 태그) 곧 다시 지워진다. 앱 업데이트는 Release 목록에서 가장 새 빌드를 고르므로 이것만 있으면 된다.

  > **바뀜** (2026-10-17, krx-quant 결정): 빌드마다 Release가 쌓였다(52개) → 가장 새 개인 빌드의 Release 하나만 남긴다. 이유: 옛 빌드는 쓰지 않고, 목록만 길어졌다. 2026-10-17에 `custom-v4.5.1-krx.21` 앞의 51개를 손으로 지웠다(태그는 남김).
- 서명하지 않은 빌드다. 맥은 처음 열 때 우클릭 → 열기(또는 `xattr -dr com.apple.quarantine /Applications/FinceptTerminal.app`), 윈도우는 SmartScreen에서 추가 정보 → 실행.
- 공식 버전 업데이트는 꺼져 있다(`FINCEPT_DISABLE_AUTO_UPDATE`). 새 버전은 새 태그로 만들고, 설치된 앱은 아래처럼 스스로 업데이트한다.
- upstream 릴리스를 반영한 첫 빌드는 workflow가 `custom-<tag>-krx.1`로 태그한다(위 "upstream 릴리스 반영").
- 그 뒤 `custom`에 merge(push) 되는 변경은 `custom-auto-tag` workflow가 저절로 태그한다(codeyoma/krx-quant#98). push 된 커밋에 `custom-<custom/UPSTREAM_BASE>-krx.<N+1>`을 단다. N은 그 upstream 버전에서 이미 쓴 가장 큰 번호다(`-rc` 같은 꼴은 세지 않는다). 이 태그 push로 `custom-release`가 개인 빌드를 만들어 Release에 올리고, 설치된 앱이 업데이트로 받는다. 그래서 앱을 바꾸는 merge는 손대지 않아도 개인 빌드에 들어간다. 대개 그 merge의 빌드로 들어가고, 아래처럼 더 새 커밋의 빌드에 함께 들어가기도 한다. 문서나 workflow만 바꾼 merge는 빌드하지 않는다. 손으로도 돌릴 수 있다(Actions → custom-auto-tag → Run workflow, 브랜치 `custom`).
  - 태그를 달지 않는 경우: 그 커밋에 이미 `custom-v*` 태그가 있을 때(upstream 반영은 `custom`과 `krx.1` 태그를 한 번에 push 한다. 손으로 단 태그도 그대로 된다), push가 문서나 workflow만 바꿨을 때(`custom/**`, `*.md`, `docs/**`, `fincept-qt/docs/**`, `.github/**`). 이런 변경을 빌드하려면 손으로 태그한다.
  - 실행은 한 번에 하나라서 번호가 겹치지 않는다. `queue: max`로 기다리는 실행을 모두(최대 100개) 남겨서, 연달아 merge 해도 merge 마다 빌드가 나온다. GitHub는 기다리는 실행의 순서를 보장하지 않는다. 그래서 더 새 커밋의 빌드가 이미 있으면 그 커밋은 태그하지 않는다. 오래된 코드가 더 큰 번호를 받아 앱이 그쪽으로 "업데이트" 하는 일을 막으려고서다. 같은 번호를 다른 태그가 먼저 가졌으면 태그 push가 실패하고 기존 태그는 그대로다.
  - 태그는 `SYNC_TOKEN`으로 push 한다. `GITHUB_TOKEN`으로 push 한 태그는 `custom-release`를 돌리지 않는다. `SYNC_TOKEN`이 없으면 workflow가 실패로 끝난다.
  - 손으로 태그해도 된다. 이미 태그가 달린 커밋이면 `custom-auto-tag`는 아무것도 하지 않는다.
  - `custom`의 커밋만 태그한다. 다른 브랜치에서 손으로 돌리면(Run workflow) 아무것도 하지 않는다. 빌드 번호를 쓰거나 옛 커밋을 가장 새 빌드로 올리는 일을 막으려고서다.

> **바뀜** (2026-10-14, krx-quant 결정): `custom`에 push 될 때마다 `custom-auto-tag`가 태그하고 빌드했다 → 빌드가 필요할 때 손으로 `custom-auto-tag`를 돌린다. 이유: merge 마다 맥·윈도우 빌드가 돌았고, 개인 빌드는 필요할 때만 있으면 된다.

> **바뀜** (2026-10-15, krx-quant 결정): 손으로 돌렸다 → 다시 `custom`에 push 될 때마다 태그하고 빌드한다. 이유: 공개 저장소라 Actions가 무료이고, merge 한 화면이 앱 업데이트로 바로 오는 편이 낫다.

> **바뀜** (2026-10-12, codeyoma/krx-quant#99): "그 뒤 화면 변경은 같은 upstream 버전에 번호를 올려 손으로 태그한다" → `custom`에 push 되면 `custom-auto-tag`가 다음 번호로 태그한다. 이유: merge 한 변경마다 사람이 태그를 달아야 앱 업데이트가 나왔다(codeyoma/krx-quant#98).

> **바뀜** (2026-10-12, codeyoma/krx-quant#95): 앱 내 업데이트 꺼짐 → 이 fork의 개인 빌드 Release에서 버튼 한 번으로 업데이트. 이유: 새 화면 빌드마다 DMG·setup.exe를 받아 직접 다시 설치해야 했다(codeyoma/krx-quant#92). 공식 버전 업데이트는 여전히 꺼져 있다.

- 맥 DMG를 만드는 `hdiutil create`는 runner에서 가끔 `Resource busy`로 실패하고 다시 돌리면 통과한다(krx.21). 그래서 15초 간격으로 네 번까지 시도한다(codeyoma/krx-quant#324).

### PR 검사 (codeyoma/krx-quant#324)

`custom` 대상 PR에서는 검사가 저절로 돌고, 결과가 PR의 체크로 보인다. 머지 뒤 개인 빌드에서야 드러나던 실패(MSVC 경고 krx.16, clang 경고 krx.4·krx.25, 요청 순서에 기댄 테스트 krx.34)를 머지 전에 잡으려고서다.

| 체크 | workflow | 하는 일 | 걸리는 시간 |
|---|---|---|---|
| `Windows x64 Installer` | `custom-release` | 위 "개발"의 브랜치 윈도우 점검과 같다. PR을 `custom`에 merge 한 결과를 빌드 번호 `krx.0`으로 빌드하고, 스모크 테스트, 자가 검사, 설치 파일·업데이트 도우미 검사까지 한다. | 약 40분(빌드 25분, vcpkg의 OpenSSL·zlib 설치 8분) |
| `KRX tests (Linux, clang, -Werror)` | `custom-pr-check` | 리눅스에서 KRX 테스트(`ctest -R krx`)의 대상만 빌드해 돌린다. 앱은 빌드하지 않는다. `CI` 조건이라 `-Werror`다. 맥 빌드를 막은 경고가 clang의 것이라 GCC 대신 clang으로 빌드한다. 실제 창이 필요한 `tst_krx_chart_page`, `tst_krx_chart_toolbar`는 offscreen이라 건너뛴다(개인 빌드의 맥 ctest는 돌린다). | 약 9분(테스트 대상 빌드 7분, 테스트 20초). 같은 PR의 다음 push는 그 PR의 ccache를 받아 빌드가 짧아진다 |
| `README placeholders` | `custom-pr-check` | `custom/README.md`에 PR 번호 자리표시(`#` 뒤의 `PRNUM`, `NNN`, `__PR3__` 같은 것. 실제 번호는 통과한다)가 남았으면 실패한다. 정규식은 krx-quant의 `src/krx_quant/forbidden.py` `REPOSITORY_RULES`(codeyoma/krx-quant#321)를 복사한 것이라, 바꾸면 둘을 함께 바꾼다. 바뀜·삭제됨 노트의 번호는 PR을 연 뒤에 넣는다. | 몇 초 |

- 도는 PR: `fincept-qt/**`, `custom/**`, 그 workflow 파일을 바꾼 PR. 윈도우 점검은 Markdown(`*.md`)만 바꾼 PR에서는 돌지 않고, 대신 `.github/scripts/ci_app_checks.sh`를 바꿔도 돈다. PR의 검사가 도는 동안 새 커밋을 push 하면 돌던 검사는 취소되고 새로 돈다.
- `custom-release`의 다른 job(맥 DMG, Release, land, report)은 PR에서 "skipped"로 보인다. PR 검사는 Release, 태그, issue를 만들지 않는다. 읽기 권한 토큰만 쓰고 secret은 쓰지 않아서, 다른 fork에서 온 PR도 같게 돈다(처음 기여하는 사람의 PR은 저장소 주인이 승인해야 돈다).
- 필수 검사(branch protection)로 걸어 두지는 않았다. 머지하기 전에 체크가 모두 초록인지 본다.
- 머지 뒤 개인 빌드(태그 push)는 전처럼 맥과 윈도우를 모두 빌드하고, 맥에서 모든 테스트를 돌린다.

> **바뀜** (2026-10-20, codeyoma/FinceptTerminal#100, codeyoma/krx-quant#324): `custom` 대상 PR에는 검사가 하나도 돌지 않아, 에이전트가 머지 전에 브랜치 윈도우 점검을 손으로 돌렸다 → PR마다 윈도우 점검, KRX 테스트, README 자리표시 검사가 저절로 돌고 PR의 체크로 보인다. 이유: MSVC 경고(`setAlphaF(0.08)`, krx.16, codeyoma/FinceptTerminal#80)와 요청 순서에 기댄 `tst_krx_cost`(krx.34, codeyoma/FinceptTerminal#99)가 머지 뒤 개인 빌드에서야 드러났다.

## 앱 업데이트 (codeyoma/krx-quant#92, #130)

- **빌드 태그.** `custom-release` workflow가 태그를 `-DFINCEPT_CUSTOM_TAG`로 빌드에 넘긴다. 앱은 자기가 어느 개인 빌드인지 안다. 로컬 빌드는 태그가 없어 업데이트를 확인하지 않는다.
- **확인.** 로그인 뒤 한 번, 그리고 앱이 켜져 있는 동안 6시간마다 조용히 확인한다. 메뉴의 업데이트 확인과 About 화면의 버튼은 늘 결과를 알려 준다. GitHub API(토큰 없음)로 이 fork의 Release를 읽어, 지금보다 새 `custom-v*` 빌드 가운데 이 설치가 쓰는 파일(아래)과 GitHub의 sha256 digest가 있는 가장 새 것을 고른다. 순서는 upstream 버전, 다음 `krx.N`이다. 초안 Release와 공식 태그는 보지 않는다.
- **내려받기.** 조용한 확인은 묻지 않고 백그라운드에서 내려받는다. 메뉴의 확인은 "받기"를 물은 뒤 진행 창(취소 가능)을 띄운다. 받은 파일은 sha256을 확인하고, 맞지 않거나 digest가 없으면 지운다. 백그라운드에서 실패하면 알리지 않고 다음 확인 때 다시 받는다.
- **준비되면 버튼 하나.** 확인된 빌드는 도우미 스크립트(`fincept-qt/src/services/krx_quant/update/`, 앱 리소스에 들어 있다)에 넘긴다. 도우미는 앱이 끝나기를 기다린다. 앱은 "새 개인 빌드 krx.N이 준비됐습니다"와 **지금 재시작**, **나중에**를 띄운다(다른 작업을 막지 않는 창).
  - 지금 재시작: 앱이 닫히고, 도우미가 새 빌드로 바꾼 뒤 몇 초 안에 다시 연다. 앱은 닫기 전에 재시작 표시 파일(받은 파일 이름 + `.relaunch`)을 만들고, 도우미는 그 파일이 있을 때만 앱을 다시 연다.
  - 나중에: 앱을 언제든 끌 때 바뀐다. 다음에 켜면 새 빌드다. 그사이 메뉴로 확인하면 같은 창을 다시 띄운다.
  - 기록은 임시 폴더의 `fincept-update.log`에 남는다. 윈도우에서 새 빌드를 넣지 못하면 도우미가 이유와 이 기록의 위치를 창으로 알린다. 앱은 지금 설치된 빌드로 다시 열린다.
- **이 설치가 쓰는 파일과 방식.**
  - 맥(`update-macos.sh`, `…-macos-arm64.dmg`): DMG를 붙여 설치된 `.app`을 새 것으로 바꾸고 서명을 확인한다. 옛 앱은 복사가 끝날 때까지 옆에 두었다가, 실패하면 되돌린다. macOS가 "앱 관리" 권한을 물으면 허용한다. 재시작을 눌렀는데 실패하면 옛 앱과 DMG가 열린다.
  - 윈도우, 사용자 폴더 설치(`%LOCALAPPDATA%\Programs\FinceptTerminal`, `…-windows-x64.zip`): 폴더를 통째로 바꾼다. ZIP은 설치된 파일 트리에서 설치 프로그램 자신의 파일(`FinceptMaintenanceTool.*`, `components.xml`, `InstallationLog.txt`, `network.xml`, `installer.dat`, `installerResources`)을 뺀 것이다. 도우미는 앱이 도는 동안 ZIP을 `<설치 폴더>.staging`에 풀어 둔다. 앱이 끝나면 그 폴더에서 띄운 프로세스(QtWebEngineProcess 등)와 그 폴더에 서 있는 앱의 Python(작업 폴더는 `scripts`, 실행 파일은 앱 데이터 폴더 `%LOCALAPPDATA%\com.fincept.terminal`)도 끝나기를 2분까지 기다리고, 남은 것은 멈춘다. 앱이 그 폴더에서 다시 켜져 있으면 이번 업데이트는 그만둔다. 설치 폴더를 `<설치 폴더>.old`로 옮기고(이름을 한 번에 바꾼다. 옮기지 못하면 그 폴더에 서 있거나 거기서 띄운 프로세스를 이유에 적는다) 새 트리를 그 자리에 놓은 뒤, 새 트리에 없는 것(제거 도구와 그 기록, 사용자가 넣은 파일)을 옛 폴더에서 복사해 온다. 그래서 제어판의 제거도 그대로 된다. 어느 단계든 실패하면 옛 폴더를 되돌린다. 관리자 권한이 필요 없어 UAC가 뜨지 않는다.
  - 윈도우, 다른 곳 설치(Program Files 등, `…-windows-x64-setup.exe`): 이번 한 번 사용자 폴더로 옮긴다. 앱이 끝나면 새 `setup.exe`를 사용자 폴더에 무인 설치하고(workflow의 설치 확인과 같은 옵션), 옛 설치를 그 제거 도구(`FinceptMaintenanceTool.exe purge`)로 지운다. 옛 폴더에 쓸 수 없으면(Program Files) 지울 때 UAC가 한 번 뜬다. 거절하면 옛 설치는 남고 새 설치는 그대로 쓴다. 옛 설치를 지우면 그 바로가기(시작 메뉴의 `Fincept\Fincept Terminal`, 바탕 화면의 `Fincept Terminal`)도 지워지는데, 새 설치가 같은 이름으로 만든 것일 수 있다. 그래서 지우기 전에 새 설치를 여는 바로가기를 적어 두고, 지워졌으면 다시 만든다. 옛 설치를 먼저 지우지 않는 것은, 새 설치가 실패하면 앱이 없는 상태가 되기 때문이다. 준비 창에 옮길 위치와 UAC 안내가 나온다. 그 뒤로는 위의 폴더 바꾸기다.
  - 앱은 도우미를 Windows PowerShell의 전체 경로(`%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe`, 없으면 PATH의 `powershell.exe`)로 숨은 창에서 띄운다. PATH에 없을 수 있어서다(codeyoma/krx-quant#101).
  - 도우미의 작업 폴더는 도우미가 있는 임시 폴더다. 도우미도 시작하자마자 임시 폴더로 옮겨 간다(옛 앱이 설치 폴더에서 띄웠을 때를 위해). 시작 메뉴 바로가기는 앱을 설치 폴더에서 여는데, 어떤 프로세스든 그 폴더를 작업 폴더로 두고 있으면 윈도우는 폴더를 옮기지 못한다. 다시 여는 앱은 바로가기처럼 설치 폴더에서 연다.
  - 폴더 바꾸기는 새 빌드의 도우미가 한다. 앱은 자기 리소스의 도우미를 띄우고, 그 도우미는 ZIP에 든 새 빌드의 도우미(`krx-update\update-windows.ps1`)가 자기와 다르면 받은 인수에 `-Handoff`를 붙여 그것에 넘긴다. 그래서 도우미를 고치면 다음 업데이트에 바로 쓰인다. krx.42까지는 이미 고친 빌드를 받고도 옛 도우미로 실패했다(codeyoma/krx-quant#234). 새 도우미는 옛 도우미가 넘기는 인수(`-Mode`, `-AppPid`, `-Zip`, `-Root`, `-Relaunch`, `-NoLaunch`, `-Handoff`)를 계속 받아야 한다. 넘기기는 이 변경이 든 빌드부터 일어나고, 다른 곳 설치(`setup.exe`)는 넘기지 않는다.
  - 스스로 업데이트할 수 없는 경우(설치 프로그램으로 설치하지 않은 윈도우 앱, `%LOCALAPPDATA%`를 모르는 윈도우, 쓸 수 없는 폴더의 맥 앱)에는 메뉴의 확인에서만 알리고, 설치 파일을 받아 열어 직접 설치하게 한다. 도우미를 띄우지 못하면 설치 파일이나(ZIP이면) 릴리스 페이지를 연다.
- **새 설치 위치.** 윈도우 설치 프로그램의 기본 폴더는 `%LOCALAPPDATA%\Programs\FinceptTerminal`이다(`fincept-qt/CMakeLists.txt`의 `CPACK_IFW_TARGET_DIRECTORY`, upstream은 Program Files). 마법사에서 다른 곳을 고를 수 있지만, 그러면 첫 업데이트 때 이 폴더로 옮긴다.
- **신뢰.** Release에서 직접 받는 것과 같다(이 fork의 GitHub 계정과 Release). 거기에 GitHub digest 확인을 더했다. 서명 키는 쓰지 않는다. 윈도우에서 넘겨받는 도우미도 sha256을 확인한 그 ZIP 안의 것이다.
- **CI.** `custom-release` workflow가 Release마다 윈도우 ZIP을 만들어 올리고, 두 도우미를 새로 만든 파일로 돌려 본다.
  - 윈도우: 설치 확인으로 설치한 트리에서 ZIP을 만든다. 폴더 바꾸기(옛 설치에만 있던 파일이 남는지, 바꾼 뒤에도 제거 도구가 지우는지)와 사용자 폴더로 옮기기(새 폴더에 설치되고 옛 설치가 지워지는지)를 확인한다. 두 경우 모두 바로가기로 연 앱이 띄울 때처럼 옛 설치 폴더를 작업 폴더로 해서 도우미를 띄운다. 폴더 바꾸기는 옛 도우미(이 도우미에 한 줄을 더한 것)로 시작해, ZIP의 도우미에 넘기는지도 확인한다. ZIP에 이 빌드의 도우미가 들었는지, 도우미 파일이 UTF-8 BOM을 지켰는지도 본다. Windows PowerShell 5.1은 BOM이 없는 스크립트를 ANSI 코드 페이지로 읽어, 실패를 알리는 한국어 창이 깨진다. PowerShell은 전체 경로로 띄운다. 앞의 IFW 설치 단계가 PATH를 MSYS 꼴로 `GITHUB_ENV`에 써서, 뒤의 PowerShell 단계는 PATH로 `powershell.exe`를 찾지 못한다. 그 때문에 `krx.4`–`krx.6`은 윈도우 단계에서 실패해 Release가 나오지 않았다(codeyoma/krx-quant#101).
  - 맥: 설치된 복사본을 새 DMG로 바꾼다. 재시작 표시 파일 없이 돌려 아무것도 열지 않는다.

> **바뀜** (2026-10-13, codeyoma/krx-quant#133): "지금 업데이트"를 눌러야 내려받고, 받으면 앱을 바로 닫고 설치했다 → 백그라운드에서 받고 "지금 재시작" 버튼 하나, 누르지 않으면 앱을 끌 때 바뀐다. 확인은 로그인 뒤 한 번 → 그 뒤 6시간마다도. 이유: 업데이트를 사용자가 편하게, 버튼 한 번으로(codeyoma/krx-quant#130).

> **바뀜** (2026-10-13, codeyoma/krx-quant#133): 윈도우는 설치 폴더를 제거 도구로 지우고 새 `setup.exe`를 같은 폴더에 무인 설치했다(Program Files면 UAC 두 번, 실패하면 설치 마법사가 열렸고 마법사는 비어 있지 않은 폴더에 설치하지 못했다) → 사용자 폴더 설치는 ZIP으로 폴더를 바꾸고(UAC 없음, 실패하면 되돌림), 다른 곳 설치는 첫 업데이트 때 사용자 폴더로 옮긴다. 기본 설치 폴더는 Program Files → `%LOCALAPPDATA%\Programs\FinceptTerminal`. 이유: 윈도우에서 업데이트를 누르면 설치 마법사가 뜨고 덮어쓰지 못했다(codeyoma/krx-quant#130).

> **바뀜** (2026-10-15, codeyoma/krx-quant#234): 윈도우 도우미는 앱의 작업 폴더를 물려받았다. 시작 메뉴로 열었다면 그것은 설치 폴더다 → 임시 폴더에서 돈다. 이유: 도우미가 설치 폴더를 작업 폴더로 두고 있어서 폴더 바꾸기가 "사용 중"으로 늘 실패했고, 업데이트를 눌러도 옛 빌드가 다시 열렸다. CI는 도우미를 설치 폴더 밖에서 띄워 이것을 잡지 못했다.

> **바뀜** (2026-10-16, codeyoma/krx-quant#234): 윈도우 폴더 바꾸기는 앱 리소스의 도우미, 곧 바뀌는 옛 빌드의 도우미가 했다 → ZIP에 든 새 빌드의 도우미에 넘긴다. 넣지 못하면 옛 빌드가 말없이 다시 열렸다 → 도우미가 이유를 창으로 알린다. 폴더는 `Move-Item`으로 옮겼다 → 이름을 한 번에 바꾼다(`[IO.Directory]::Move`). 앱의 Python은 기다리지 않았다 → 기다린다. 이유: krx.42 설치는 고친 도우미가 든 빌드를 받고도 옛 도우미로 계속 실패했고, 사용자는 기록을 열어 보기 전에는 그것을 몰랐다. 또 시험해 보니 하위 폴더(`scripts`)에 프로세스가 서 있으면 `Move-Item`은 파일을 하나씩 옮기다 멈추고, 다시 시도할 때 설치 폴더를 반쯤 만든 `.old` 안으로 옮겨, 설치에서 `FinceptTerminal.exe`가 사라졌다. 되돌리기는 첫 이동이 성공한 뒤의 실패만 다뤘다.

## 에이전트 설정

루트 `CLAUDE.md`는 `@custom/AGENTS.md` 한 줄만 담는다. upstream `.gitignore`가 `CLAUDE.md`를 무시하므로 `git add -f`로 추적하고, upstream이 같은 파일을 만들 일이 없어 merge 충돌이 나지 않는다. 설정 본문은 `custom/AGENTS.md`와 `custom/agents/`에 있다. 작업 ticket은 저장소 `codeyoma/krx-quant`의 이슈로 관리한다. 이 fork는 `krx-quant`의 `fincept/` submodule로 받는다(`git clone --recurse-submodules`). 단독으로 clone해도 빌드와 upstream merge는 되지만, 에이전트 문서가 참조하는 상위 폴더의 설계 문서(`../docs/design.md`)는 없다.

## upstream workflow

이 fork에서는 upstream의 예약·릴리스 workflow를 끈다(GitHub Actions 설정에서 disable). upstream의 PR 검사도 꺼져 있다: `lint`, `arch-ratchet`은 `pull_request`로 돌게 되어 있지만 disable 되어 있고, `pr-gate`는 upstream의 기여 규칙(연결된 issue, 라벨)을 보는 `pull_request_target` workflow라 이 fork에 맞지 않고, 역시 disable 되어 있다. 이 fork의 PR 검사는 아래 `custom-pr-check`와 `custom-release`의 윈도우 점검이다(위 "PR 검사"). 개인 빌드용 workflow는 `custom-` 접두사로 둔다: `custom-upstream-watch`(`main` 동기화와 upstream 릴리스 자동 반영), `custom-release`(개인 빌드, upstream merge의 시험 빌드, 브랜치와 PR의 윈도우 빌드 확인, 위 "개인 빌드"), `custom-auto-tag`(`custom`에 push 된 변경을 다음 개인 빌드로 태그, 위 "개인 빌드"), `custom-krx-pointer`(`custom`에 push 될 때마다 krx-quant에 포인터를 옮기라고 알림, 위 "krx-quant 포인터"), `custom-pr-check`(`custom` 대상 PR의 KRX 테스트와 README 자리표시 검사, 위 "PR 검사").

> **바뀜** (2026-10-20, codeyoma/FinceptTerminal#100, codeyoma/krx-quant#324): 개인 빌드용 workflow 넷 → 다섯(`custom-pr-check`). `custom-release`는 `custom` 대상 PR에서도 윈도우 점검을 한다. 이유: 위 "PR 검사"의 바뀜과 같다.

> **바뀜** (2026-10-14, codeyoma/krx-quant#188): 개인 빌드용 workflow 셋 → 넷(`custom-krx-pointer`). 이유: krx-quant의 `fincept` 포인터가 손으로 올리지 않아도 `custom`의 최신 커밋을 따라가게 하려고(codeyoma/krx-quant#186).

> **바뀜** (2026-10-12, codeyoma/krx-quant#99): 개인 빌드용 workflow 둘 → 셋(`custom-auto-tag`). 이유: 위 "개인 빌드"의 바뀜과 같다(codeyoma/krx-quant#98).

> **바뀜** (2026-10-12, krx-quant #94): `custom-upstream-watch`는 upstream 릴리스 알림 → 자동 반영. `custom-release`에 `upstream-merge/<tag>` 브랜치의 시험 빌드가 더해졌다. 이유: upstream 릴리스 자동 반영(krx-quant #93).

`custom-*` workflow는 Node.js 24에서 도는 action 판을 쓴다(codeyoma/FinceptTerminal#102): `actions/checkout@v7`, `actions/upload-artifact@v7`, `actions/download-artifact@v8`, `hendrikmuhs/ccache-action@v1.2.24`. ccache-action만 정확한 태그로 둔다. 그 action의 `v1`·`v1.2` 태그는 아직 Node.js 20에서 도는 v1.2.20을 가리킨다. `jurplel/install-qt-action@v4`는 composite이고 안의 action이 이미 Node.js 24라 그대로다. release.yml의 바뀐 내용을 `custom-release.yml`로 옮길 때 이 판을 upstream의 옛 판으로 되돌리지 않는다. upstream workflow는 merge가 충돌하지 않게 upstream의 판을 그대로 둔다. 그래서 손으로 돌리는 `build-cpp.yml`은 아직 Node.js 20 경고를 낸다.
