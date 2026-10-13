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

> **바뀜** (2026-10-13, codeyoma/krx-quant#136): 화면 등록과 로컬 CLI LLM 때문에 고치는 upstream 파일만 → 수동 전체 플랫폼 빌드(`build-cpp.yml`) 수리도. 이유: 한국어 팩(codeyoma/krx-quant#132)을 검증하다 다섯 대상이 모두 실패하는 것을 찾았다. 고친 곳마다 `fork addition` 주석을 달았다.

> **바뀜** (2026-10-13, codeyoma/FinceptTerminal#26): 화면 등록과 로컬 CLI LLM 때문에 고치는 upstream 파일만 → 원화 환산 표시(codeyoma/krx-quant#142) 때문에 고치는 시장·관심종목·대시보드·포트폴리오·주식 리서치·설정 화면과 시세 service도(아래 표). 이유: 출처 통화가 확인된 금액만 화면에서 환산하려면 각 화면이 금액을 그리는 곳에 갈래가 필요했다. upstream 릴리스를 merge 할 때 이 파일들이 충돌할 수 있다.

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
| `fincept-qt/CMakeLists.txt` | `SERVICE_SOURCES`·`SCREEN_SOURCES`에 새 파일, unity 빌드 제외 목록, `FINCEPT_DISABLE_AUTO_UPDATE` 옵션(기본 ON), 빌드 태그 `FINCEPT_CUSTOM_TAG`, 업데이트 도우미 스크립트 리소스(`:/krx/`), 윈도우 설치 프로그램의 기본 폴더 `CPACK_IFW_TARGET_DIRECTORY`(사용자 폴더, codeyoma/krx-quant#130), 한국어 언어 팩 `translations/fincept_ko_KR.ts`의 컴파일·리소스 포함, clang-cl(Windows ARM64)에서 GCC/Clang 경고 묶음을 빼고 `-Wno-clang-cl-pch`(codeyoma/krx-quant#136) |
| `fincept-qt/tests/CMakeLists.txt` | `fincept_add_test()` 정의 바로 뒤에 `include("${CMAKE_CURRENT_SOURCE_DIR}/krx_quant.cmake")` 한 줄. KRX 테스트(`tst_krx_engine_data`, `tst_krx_board`, `tst_krx_signals`, `tst_krx_stats_view`, `tst_krx_app_release`, `tst_krx_local_cli_llm`, `tst_krx_korean`)는 새 파일 `fincept-qt/tests/krx_quant.cmake`에 등록한다 |
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
| `.github/workflows/build-cpp.yml` | 수동 전체 플랫폼 빌드를 다시 통과시킨다(codeyoma/krx-quant#136): Windows ARM64는 qtwebengine 없는 Qt 모듈 목록(Qt 6.8.3에 그 대상의 qtwebengine이 없다)과 `arm64-windows` vcpkg triplet, macOS x64(Intel)와 macOS Universal 작업은 뺐다(아래 삭제됨), 패키징의 `sync_scripts.sh`는 저장소 루트 경로(`${GITHUB_WORKSPACE}`), Linux 기본 아이콘을 만들 때 Pillow 설치, macOS 앱에는 실행에 쓰는 Python 버전 하나만 넣는다(runner의 `Python.framework`에는 여러 버전이 있고, 서명은 `Current`만 되어 검증이 실패했다). 개인 배포 `custom-release.yml`은 그대로다 |

> **삭제됨** (2026-10-13, codeyoma/krx-quant#136): `build-cpp.yml`의 macOS x64(Intel) 대상과 그 결과를 arm64와 합치던 macOS Universal 작업, 릴리스 목록의 두 파일. 이유: Intel Mac은 지원하지 않기로 했다(사용자 결정). 교차 컴파일한 x64 앱은 ARM64 Homebrew OpenSSL을 링크해 실패했고, Intel runner로 고치면 빌드가 가장 오래 걸렸다. 대신: macOS는 Apple Silicon(arm64)만 빌드한다.

> **바뀜** (2026-10-12, krx-quant #94): KRX 테스트 등록을 `fincept-qt/tests/CMakeLists.txt` 끝에 붙였다 → `krx_quant.cmake`로 옮기고 `fincept_add_test()` 정의 뒤에서 include 한다. 이유: upstream도 파일 끝에 테스트를 붙여서, v4.5.0 이후 upstream `main`을 merge 하면 충돌했다. 자동 반영이 멈추지 않게 하려고(krx-quant #93).

## 한국어 언어 팩 (codeyoma/krx-quant#132)

- **설정 경로:** Settings → General → Interface language → **한국어**. 한국어로 바꾼 뒤에는 설정 → 일반 → 인터페이스 언어다. 선택 즉시 적용되고, 같은 곳에서 English로 되돌릴 수 있다.
- **저장:** 기존 `LanguageManager`가 SQLite settings의 `general.language`에 `ko_KR`를 저장한다. 다음 실행에도 유지된다. 저장된 언어가 없는 첫 실행은 기존 운영체제 언어 감지를 따르므로 한국어 환경에서는 한국어를 고른다. 이미 고른 다른 언어를 덮어쓰지 않는다.
- **팩:** `fincept-qt/translations/fincept_ko_KR.ts`. 현재 내부 공통 팩의 382개 컨텍스트, 9,608개 항목을 기준으로 번역하고, 최신 설정·탐색의 누락 문구와 동적으로 읽는 설정 메뉴도 더했다. 이전 내부 한국어 팩의 맞는 표현을 참고했다. 제품명·티커·코드·수식·치환자는 유지한다. 원문에 새 문구가 생겨 팩에 아직 없거나 `tr()`로 연결되지 않은 문구는 기존 영어 fallback을 따른다.
- **용어·문체:** 국내 증권사 HTS/MTS와 KRX·DART 표기를 따른다(PER·PBR, 평가손익, 평균단가, 매입금액, 평가금액, 계좌/계정 구별, 증권사, 모의투자/실전투자, 가상자산, 샤프지수, 최대낙폭(MDD) 등). 문장은 합니다체, 안내는 `~하세요`. 새 문구를 번역하거나 기존 번역을 고칠 때는 [`korean-glossary.md`](korean-glossary.md)를 따른다.
  > **바뀜** (2026-10-13, codeyoma/FinceptTerminal#31): Codex 초벌 번역의 용어·문체 → 업계 표기 기준으로 약 1,200개 항목 정리. 이유: 일반 번역어(P/E 비율, 미실현 손익, 취득 원가, 브로커, 모의 거래, 암호화폐 등)와 번역투가 국내 금융 화면과 달랐다.
  > **바뀜** (2026-10-13, codeyoma/FinceptTerminal#31): 환산하지 않은 시세 표시 `(원 단위)`·`(원 통화)` → `(거래 통화)`, 금액은 `원래 통화`. 이유: 원화(KRW) 표시 통화가 생긴 뒤 `원`이 원화로 읽혔다.
- **빌드:** 다른 언어와 동일하게 `lrelease`가 `.qm`을 만들고 `:/i18n/fincept_ko_KR.qm`에 포함한다. 외부 번역 API, 다운로드, 추가 설정은 필요 없다. 기존 팩은 자동으로 다시 쓰지 않는다.
- **갱신:** 설정 메뉴는 문자열을 저장했다가 동적으로 `tr()`에 넘긴다. 이 메뉴와 범위 제목 18개에는 실제 런타임 문맥의 `QT_TRANSLATE_NOOP` 표시를 넣어, 수동 `update_translations`의 `-no-obsolete` 갱신에서도 한국어 번역이 남게 한다. 새 동적 메뉴를 더할 때도 같은 표시가 필요하다.
- **검증:** `FINCEPT_BUILD_TESTS=ON`으로 설정한 빌드에서 `cmake --build <build> --target tst_krx_korean` 뒤 `ctest --test-dir <build> -R krx_korean --output-on-failure`. 앱과 같은 경로의 실제 컴파일된 리소스, 설정 메뉴 문맥, 공통 팩 전체 키·Qt 치환자, 복수형, 영어 fallback, 용어 기준의 대표 금융 용어(PER, 평가손익, 평균단가, 계좌, 모의투자 등)를 검사한다.

## 원화 표시 통화 (codeyoma/krx-quant#138, #142)

- **설정 경로:** Settings → General → Display currency(설정 → 일반 → 표시 통화). 언어 바로 아래에서 **₩ South Korean Won (KRW)**을 고른다. 기본값 USD를 포함해 목록의 모든 표시 통화에 같은 환산 규칙을 적용한다.
- **저장:** 기존 SQLite settings의 `general.currency`에 선택을 저장하여 재시작 때 복원한다. `general.fx_rates_v1`에는 `version=1`, `source=ECB`, `date=YYYY-MM-DD`, EUR당 통화별 값인 `rates`를 JSON으로 저장한다. DB 스키마는 바꾸지 않는다.
- **환율:** [ECB 일일 참고환율](https://www.ecb.europa.eu/stats/policy_and_exchange_rates/euro_reference_exchange_rates/html/index.en.html)의 [XML](https://www.ecb.europa.eu/stats/eurofxref/eurofxref-daily.xml)을 앱 시작과 6시간마다 읽는다. 설정에서 수동 새로고침도 할 수 있다. 원 금액 × 대상 통화의 EUR당 값 ÷ 원 통화의 EUR당 값으로 환산한다. 설정에 기준일·캐시 여부·조회 상태와 `100 USD` 환산 예시를 보인다. 주문 체결이나 결제 환율이 아닌 표시용 참고환율이다.
- **실패:** 마지막 검증 자료를 쓰되 기준일로부터 7일을 넘으면 환산하지 않는다. 1분마다 만료를 확인한다. 미래 날짜·0/음수/무한 환율·중복 통화·손상 응답을 거부하고, 실패한 조회는 유효한 캐시를 덮어쓰지 않는다. 출처 통화가 없거나 환율이 없으면 **원래 숫자와 통화/원 단위 표시**를 유지한다. 1:1 환율을 만들어 쓰지 않는다. 동일 통화 표시는 환율 없이 가능하다.
- **통화 구별:** `GBp`/`GBX`는 GBP의 1/100이다. USDT/USDC는 USD로 취급하지 않는다. KRW/JPY는 기본 소수 자릿수 0이며 아주 작은 금액은 0으로 사라지지 않도록 자릿수를 늘린다. 화면이 가격 정밀도를 명시한 경우에는 그 자릿수를 따른다. 기호가 같은 USD/CAD/AUD도 통화 코드로 구별한다.
- **시세 메타데이터:** `QuoteData`와 시세 캐시 JSON은 `currency`, `quote_type`을 원 값과 함께 보관한다. 이름이 이미 있어도 통화/유형이 없으면 다시 조회한다. SQLite settings의 `market_symbol_names`, `market_symbol_currencies`에 더해 `market_symbol_quote_types`를 쓴다. 회사 정보는 시세 통화와 재무제표 `financial_currency`를 구별한다. 지수·수익률·환율쌍·거래량·수량·퍼센트는 금액처럼 환산하지 않는다.

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

- 코드: `fincept-qt/src/screens/krx_quant/`(화면, 차트, 순위표, 상세 통계 창, 알림 설정), `fincept-qt/src/services/krx_quant/`(엔진 API와 스트림 읽기, 응답 해석, 화면 상태, 신호 판정, 통계 표시 문구).
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
- **위쪽 줄**: 엔진 상태(엔진이 도는 커밋과 시작 시각("엔진 0123abc", "10-13 06:02 시작". 커밋을 모르면 "엔진 커밋 모름", codeyoma/krx-quant#85), 장 단계, 마지막 봉 마감, 채점 대기, KIS 연결, 지난밤 학습, 스트림 연결), 매크로 줄(그날 국면과 이유, 달러선물. 지표 전체와 읽기 실패는 툴팁), 오늘의 특수 상황(VI·서킷브레이커·사이드카). 매크로가 오늘 것이 아니면 "(오늘 것이 아님)"을 붙인다.
  - 달러선물은 그날 첫 체결 뒤로는 엔진 상태의 장중 마지막 체결가, 전일 종가 대비 등락, 체결 시각이다(예: "달러선물 최근월물 1,395.2 (+0.50%, 10:29)"). 상태를 읽을 때마다(5초) 바뀐다. 매크로를 읽지 못했거나 아직 정한 국면이 없어도 보인다. 체결 전(장 전, 휴장일)에는 07:30 매크로 읽기의 값과 등락이다.
  - KIS는 "마지막 수신 N초 전"(가장 최근에 받은 피드)과 결측 피드 수(엔진이 끊김·멈춤으로 판정한 피드, 목록은 툴팁)로 보인다. 피드마다 조용한 시간은 지연이 아니다. 원래 드문 피드가 있고 장 밖에서는 모두 조용하다. 문제는 엔진이 결측으로 판정한다.
  - 지난밤 학습이 미적용이면 엔진이 준 이유를 한국어로 붙인다(08:00 교체 대기, 07:30 뒤에 확정, 모델 파일 없음, 적용 뒤 바뀜).
- **종목 목록**: 기준가, 최종 합의(방향과 가장 큰 확률. 상승과 하락이 같은 정수 %이면 "상승=하락"), 채점까지 남은 시간. 고른 종목이 차트와 예측 패널에 나온다. 장 시작 전이나 휴장일에는 지난 거래일의 마지막 예측이 남아 있다. 그 예측은 흐리게, 채점까지 칸은 "지난 거래일"로, 패널 제목은 날짜와 "(지난 거래일)"로 보인다.
- **차트** (codeyoma/krx-quant#105): 고른 종목의 1분봉을 TradingView Lightweight Charts™로 그린다. 처음에는 오늘의 봉이다. 휠로 줌, 드래그로 스크롤하고, 차트를 두 번 누르거나 머리줄의 **맞춤**을 누르면 최근 60봉 보기로 돌아간다. 처음 그릴 때도 최근 60봉이다. 아래 칸에 거래량 막대(오른 봉은 초록, 내린 봉은 빨강), 오른쪽 가격 축에 현재가 선이 있다. 시각은 KST다.
  - 지난 날로 스크롤(codeyoma/krx-quant#108): 보기의 왼쪽 끝이 첫 봉에서 30봉 안으로 오면 그 앞의 400봉(`GET …/bars?before=<첫 봉>&limit=400`)과 그 봉들의 최종 합의(`…/predictions?algorithm=consensus-final&before=`)를 읽어 앞에 붙인다. 날을 넘어 어제, 그 전 날로 이어진다. 보기는 보던 봉에 머문다. 왼쪽은 읽은 첫 봉에서 멈추고(빈 칸으로 넘어가지 않는다), 엔진에 더 앞의 봉이 없으면 더 읽지 않는다. 지난 날에도 아이콘, ✓/✗, 호버, 클릭이 된다. 장 시작 전처럼 오늘 봉이 없으면 처음부터 지난 거래일의 봉을 읽는다. 다른 종목을 고르거나 엔진을 읽지 못하면 읽어 둔 지난 봉을 버린다.
  - 장중에 엔진이 재시작했으면 그 전의 오늘 봉은 20:30 백필 뒤에야 보인다(엔진 API의 `bars` 참고).
  - 실시간 봉(codeyoma/krx-quant#110): 지금 만들어지는 봉이 스트림의 `bar` 이벤트(엔진이 1초마다 살펴 바뀌었을 때만 보냄)로 1초마다 움직이고, 오른쪽 현재가 선도 같이 움직인다. 고른 종목의 것만 마지막 봉에 합친다: 같은 분이면 그 봉을 바꾸고, 새 분이면 앞 봉을 끝난 것으로 보고 뒤에 붙인다. 앞선 분이나 보이는 것보다 작은 같은 분(늦게 온 이벤트)은 버린다. N분 보기에서는 묶은 마지막 봉이 움직인다. 페이지는 마지막 봉만 다시 그린다(`update`). 끝난 봉은 새 봉 마감마다 다시 읽고, 그 위에 지금 봉을 다시 얹는다. 스트림이 다시 연결되면 처음처럼 모두 다시 읽는다. 지금 봉에는 아직 아이콘이 없다(봉 마감 뒤에 생긴다).
  - 분 단위 보기(codeyoma/krx-quant#109): 차트 위 한 줄의 버튼 "분 1 3 5 10 15 30 60 120 240"으로 N분봉 보기를 고른다. Fincept 포트폴리오 차트의 기간 버튼과 같은 모양(납작한 체크 버튼, 고른 것은 주황으로 채움, 테마를 따름)이다. 고른 보기는 앱 설정 `krx_quant/chart_view_minutes`에 남아 다시 켜도 그대로다(없거나 목록 밖이면 1분). 패널이 좁으면 오른쪽 버튼이 잘린다.
    - 1분봉을 화면에서 N분봉으로 묶는다. 시계 기준(KST)이다: 60분봉은 09:00, 10:00…, 240분봉은 08:00, 12:00, 16:00에 시작한다. 모든 N이 하루를 나누므로 봉이 날을 넘지 않는다. 시가는 첫 1분봉의 시가, 고가·저가는 가장 높고 낮은 값, 종가는 마지막 1분봉의 종가, 거래량은 합이다. 거래가 없어 빠진 분은 건너뛰고, 1분봉이 하나도 없는 N분은 봉이 없다.
    - 3분 이상 보기에는 아이콘·호버·클릭이 없다(한 봉에 봉 마감이 여럿이다). 차트 제목은 "005930 · 15분봉"이다. 보기를 바꾸면 최근 60봉에 맞춘다.
    - 왼쪽 끝에 닿으면 그 보기의 100봉만큼 1분봉을 더 읽는다(1분은 400, 5분은 500, 한 번에 최대 1000. 엔진이 한 번에 주는 최종 합의가 1000개까지라서). 넓은 보기는 여러 번 이어 읽는다.

  > **바뀜** (2026-10-13, codeyoma/krx-quant#108): 오늘의 1분봉만 보였다 → 왼쪽으로 스크롤하면 지난 날의 봉과 아이콘을 이어서 읽는다. 이유: 지난 날로 스크롤(codeyoma/krx-quant#100).
  - 봉 마감마다 최종 합의가 예측한 클래스의 아이콘을 예측을 만든 봉(그 봉 마감에 끝난 봉) 위에 둔다: 상승 ▲(초록), 하락 ▼(빨강), 보합 ●(회색). 아이콘 위의 글자가 결과다: ✓ 맞음, ✗ 틀림, ? 아직 채점 전이거나 순위표에서 고른 기준으로 채점할 수 없음. 예측 클래스는 엔진이 채점하는 것과 같다(가장 큰 확률, 동률이면 보합, 그다음 상승). 4분 구간에 특수 상황이 낀 예측은 흐리게 그린다(통계에서 빠진다).
  - 아이콘에 마우스를 올리면(codeyoma/krx-quant#106) 가격 칸 아래쪽, 마우스와 반대편에 다섯 줄이 나온다: 봉 시각과 목표 시각("09:31 봉 → 09:35 목표"), 합의 방향과 세 확률, 같은 방향 정식 모델 수/전체(엔진의 `votes`. 없으면 "알 수 없음"), 신호였는지(화면의 알림 기준값과 N으로 판단, 엔진이 막은 합의는 그 이유), 채점됐으면 고른 기준의 실제 4분 중간가 변화(원)와 맞음·틀림. 색과 글꼴은 Fincept 툴팁과 같다.
  - 마우스를 올린 동안 그 봉에서 목표 봉(목표 시각에 끝나는 봉)까지 주황 선을 긋는다. 목표 봉이 아직 없으면 오른쪽 빈칸의 그 자리에 점선 상자와 점선을 그린다.
  - 아이콘을 누르면(codeyoma/krx-quant#107) 예측 패널이 그 봉 마감의 판단으로 고정된다. 아래 **예측 패널** 참고.
  - 신호였던 봉(합의 확률이 기준값 이상이고 같은 방향 정식 모델이 N개 이상, 엔진이 막지 않음)의 아이콘은 두 배 크고 진하다. `votes`가 없는 합의(이전 엔진, 이전 기록)는 강조하지 않는다. 알림 기준값이나 N을 바꾸면 차트도 다시 판단한다.
  - 무엇을 그릴지와 언제 앞의 봉을 읽을지(`older_read`, 읽은 봉 붙이기 `join_bars`)는 `services/krx_quant/ChartModel`(순수 함수, `tst_krx_chart_model`)이 정하고, 페이지(`screens/krx_quant/chart/index.html`)는 그리기만 한다. 페이지는 보기가 움직일 때마다 왼쪽 끝 위치를 화면에 알린다. 화면과 페이지는 QWebChannel로 주고받는다. 페이지와 라이브러리는 Qt 리소스(`qrc:/krx/chart/`)로 앱에 들어 있어 인터넷 없이 열린다.
  - 색과 글꼴은 Fincept 테마(오름 초록, 내림 빨강, 바탕·격자·글자)와 설정 글꼴을 따르고, 테마가 바뀌면 다시 칠한다.
  - 1분봉을 읽지 못하면 머리줄에 빨간 "읽지 못함"이 나오고 이유는 그 툴팁에 있다.
  - Qt WebEngine 없이 빌드했거나 실행 파일(QtWebEngineProcess)이 빠진 설치본이면 "차트를 열 수 없음"만 보인다.
  - 출처: 차트에 TradingView 로고는 끄고(`attributionLogo: false`), 차트 머리줄 제목의 툴팁과 아래 "차트 라이브러리" 절에 밝힌다.

  > **삭제됨** (2026-10-13, codeyoma/krx-quant#134): 직접 그린 1분봉 차트(`KrxChart`의 QPainter 그림, 봉 호버 툴팁)와 범위 선택 상자(30분·1시간·2시간·하루 전체, QSettings `krx_quant/chart_minutes`). 이유: 휠 줌·드래그 스크롤·거래량이 없었다(codeyoma/krx-quant#100). 대신: Lightweight Charts 차트와 **맞춤**. 남아 있는 `krx_quant/chart_minutes` 값은 더 읽지 않는다. 아이콘 호버는 codeyoma/krx-quant#106에서 다시 붙는다.

  > **바뀜** (2026-10-13, codeyoma/krx-quant#134): 상승 ▲는 봉 아래, 하락 ▼는 봉 위였다 → 모든 아이콘이 예측을 만든 봉 위에 있고, 보합은 – 대신 ●, 결과 글자는 아이콘 위다. 채점 전에도 ?가 붙는다. 이유: 스펙(codeyoma/krx-quant#100)이 "예측을 만든 봉 위에"로 정했고, Lightweight Charts의 표시(화살표·원과 글자)로 그린다.
- **예측 패널**: 고른 종목의 최신 봉 마감 예측이다. 최종 합의, 빠른 합의, 알고리즘, 기준 예측 순이고 알고리즘과 기준 예측은 이름순이다. 열은 방향, 상승·보합·하락 확률, 기대 틱, 모델 버전이다. 상승과 하락이 같은 정수 %로 보이면 방향은 비운다. 제목에 봉 마감과 채점까지 남은 시간(엔진 시계 기준)이 나온다.
  - 차트의 아이콘을 누르면(codeyoma/krx-quant#107) 그 봉 마감의 판단으로 고정된다. 엔진의 `GET /v1/symbols/{s}/predictions?bar_close=`와 `/v1/algorithms`(수명 상태)를 읽는다. 행은 빠른 합의, 최종 합의(굵게), 알고리즘(이름순), 기준 예측(흐리게) 순이다. 열은 예측, 상태(알고리즘의 지금 수명 상태), 방향(가장 큰 확률, 동률이면 보합, 그다음 상승. 예측하지 않았으면 그 이유), 상승·보합·하락, 합의(최종 합의의 `pooled`로 "포함"·"제외". `pooled`가 없는 이전 기록은 "알 수 없음"), 결과(순위표에서 고른 채점 기준으로 "✓ 맞음"·"✗ 틀림", "채점 전", "채점 불가")다.
  - 고정한 동안 제목은 "005930 · 10-12 10:24 봉 마감의 판단"이고, 새 봉 마감이 와도 표를 덮지 않는다. 그 봉 마감의 채점 결과는 순위표를 다시 읽을 때 함께 다시 읽는다. 머리줄 오른쪽의 **최신으로**를 누르거나 다른 종목을 고르면 최신 봉 마감으로 돌아간다. 읽지 못하면 표가 비고 이유는 제목의 툴팁에 있다.
  - 무엇을 보일지는 `ChartModel`의 `close_rows`(순수 함수, `tst_krx_chart_model`)가 정한다.
- **순위표**: 기간(오늘·5일·20일), 채점 기준(B 표시 시점·A 봉 마감), 종목(전체·종목별), 특수 상황 포함을 고른다. 열은 순위, 상태(수습·정식·벤치·은퇴·복제), 모델 버전, 표본, 정답률, 방향 정밀도, 커버리지, Brier, 중간가 손익(틱), 가장 약한 lift 하한, 운 보정 통과다. 95% 구간은 툴팁에 있다. 중간가 손익 열 제목의 툴팁은 연구용 값이라는 것(스프레드와 체결을 반영하지 않음)을 알린다. 순위가 없는 항목, 기준 예측(흐리게) 순으로 이어진다. 차트의 ✓/✗도 여기서 고른 채점 기준을 따른다.

  > **바뀜** (2026-10-13, codeyoma/krx-quant#117): 열 이름 "거래당 손익(틱)" → "중간가 손익(틱)", 열 제목에 연구용 값이라는 툴팁. 이유: 따라 했을 때의 손익으로 읽히기 쉬웠다. 엔진의 손익은 중간가에서 중간가로, 스프레드와 체결 없이 잰다(codeyoma/krx-quant#78).
- **상세 통계 창**: 순위표의 줄을 두 번 누르면 그 예측(합의와 기준 예측도)의 창이 따로 열린다. 순위표의 줄(알고리즘, 합의, 기준 예측)마다 창이 하나라서 여러 줄을 나란히 띄워 견줄 수 있다. 이미 창이 열린 줄을 다시 누르면 그 창이 앞으로 온다. 순위표와 같은 조건(기간, 채점 기준, 종목, 특수 상황)으로 엔진의 상세 통계(`/v1/algorithms/{name}/stats`)를 읽는다.
  - 모양은 본 탭과 같다(codeyoma/krx-quant#104): 창 이름은 Fincept 창처럼 "Fincept Terminal — KRX 4분 예측 — <예측> 상세 통계", 배경은 Fincept 바탕색이다. 패널 머리줄(그 예측, lift, 보정도, 나눠 본 통계), 공용 표, 테마 변경 때 다시 칠하기도 같다. 처음 읽는 동안은 Fincept 읽는 중 표시가 덮고, 읽지 못하면 머리줄에 빨간 "읽지 못함"이 나오고 이유는 그 툴팁에 있다.
  - 뜻은 툴팁에 있다. lift 머리줄 제목은 lift가 무엇인지와, 없으면 왜 없는지(기준 예측 자신, 짝지을 채점된 예측 없음)를 알린다. "95% 구간"(봉 마감마다 센 설명용, p도), "거래일 95% 구간"(하루를 값 하나로 본 구간, 상태 판정 근거), "q", "운 보정" 열 제목에도 툴팁이 있다. 보정도 머리줄 제목은 그림이 무엇을 견주는지와, 채점된 예측이 없으면 그것을 알린다. 나눠 본 통계 머리줄 제목은 그 줄들의 lift를 함께 검정하지 않는다는 것을 알린다. 보정도 그림의 축 제목은 그대로다.

  > **바뀜** (2026-10-13, codeyoma/krx-quant#128): 상세 통계 창은 화면에 하나였고, 다른 줄을 두 번 누르면 그 줄로 바뀌었다 → 순위표의 줄마다 창이 하나다. 이유: 여러 알고리즘을 나란히 띄워 견주려고(codeyoma/krx-quant#100). 엔진은 열린 창마다 그 예측의 통계를 따로 읽는다.

  > **삭제됨** (2026-10-13, codeyoma/krx-quant#128): 창 아래의 회색 설명(lift, 95% 구간과 p, 거래일 구간, q와 운 보정, 보정도의 뜻, lift가 없는 이유)과 보정도 그림 안의 "채점된 예측이 없습니다", 그리고 회색 "읽는 중…"·실패 문구. 이유: 화면을 Fincept의 다른 화면 모양으로 맞추고 회색 설명을 없앴다(codeyoma/krx-quant#100, #104). 대신: 머리줄 제목과 열 제목의 툴팁(채점된 예측이 없으면 보정도 제목의 툴팁이 알린다), Fincept 읽는 중 표시, 머리줄의 "읽지 못함".

  > **바뀜** (2026-10-13, codeyoma/krx-quant#128): 테두리 상자(lift, 보정도)와 맨 위 조건 줄 → 패널 머리줄. 조건과 날짜는 창 머리줄의 오른쪽에 있다. 이유: 위와 같다.
  - 맨 위: 조건과 날짜, 그 예측의 순위와 통계(순위표와 같은 통계 열. 순위가 없으면 "순위 밖"), 그 아래 제외 건수("제외: 특수 상황 2 · 채점 불가 1 · 시간 초과 3 · VI 4". 없으면 "제외 없음". 특수 상황 포함 조건이면 그 예측은 빼지 않고 넣었으므로 앞에 따로 "특수 상황 포함 2 · 제외: …")
  - **기준 예측 대비 lift**: 기준 예측 셋 각각에 대한 lift, 95% 구간, 짝 수, p, q, 운 보정. 순위 밖이라 함께 보정하지 않은 lift는 q와 운 보정이 "—"다. 그 기간에 기준 예측과 짝지을 봉 마감이 없으면 lift도 "—"다(짝 0). 거래일 95% 구간과 날 수도 있다. 하루를 값 하나로 본 구간이고, 엔진은 상태(수습·정식·벤치)를 이것으로 정한다. 봉 마감마다 센 95% 구간과 p는 같은 날의 4분 정답이 겹쳐 실제보다 좁은 설명용이다. 날이 2일보다 적으면 "—"다.
  - **보정도**: 확률을 10% 구간으로 나눠, 가로에 구간의 평균 예측 확률, 세로에 실제로 일어난 비율과 95%(Wilson) 구간을 찍는다. 곡선은 넷이다: 세 클래스를 합친 전체(주황), 상승(초록), 보합(회색), 하락(빨강). 오른쪽 아래에 범례가 있다. 점선은 완벽한 예측이다. 예측이 없는 구간은 찍지 않는다. 점에 마우스를 올리면 곡선 이름, 구간, 예측 수, 평균 예측, 실제 비율이 나온다. 같은 자리의 점은 함께 나온다.

    > **바뀜** (2026-10-13, codeyoma/krx-quant#119): 보정도는 세 클래스를 합친 곡선 하나였다 → 클래스별 곡선도 그리고, 맨 위에 제외 건수를 보인다. 이유: 합친 곡선이 방향별 보정 오차를 숨겼고, 무엇이 빠졌는지 보이지 않았다(codeyoma/krx-quant#83).
  - **30분 시간대별, 모델 버전별, 종목별** 탭: 순위표와 같은 통계 열이다. 시간대는 예측한 봉의 마감 시각으로 쓴다("09:01–09:30"). 이 줄들의 lift는 함께 검정하지 않는다.
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
- 스트림이 연결되어 있으면 API는 상태만 5초마다(피드 지연), 종목·최신 예측·합의 구성원은 30초마다, 순위표는 60초마다 읽는다. 차트의 지금 만들어지는 봉은 스트림의 `bar`로 움직이고(codeyoma/krx-quant#110), 끝난 봉은 새 봉 마감이 올 때마다 다시 읽는다. 채점이 오면 3초 뒤 순위표를 바로 읽는다. 매크로는 5분마다다. 스트림이 없으면(끊김, Qt WebSockets 없는 빌드) 상태·예측·합의 구성원·순위표를 5초마다, 차트를 새 봉 마감마다와 60초마다 읽는다. 채점과 봉을 알려줄 스트림이 없어서다. 매크로는 스트림이 있을 때와 같다.

  > **바뀜** (2026-10-13, codeyoma/krx-quant#110): 스트림이 있어도 차트를 60초마다 다시 읽었다 → 스트림이 있으면 지금 만들어지는 봉은 `bar` 이벤트로 움직이고, 차트는 새 봉 마감마다만 다시 읽는다. 60초마다 읽기는 스트림이 없을 때만 한다. 이유: 봉이 실시간으로 움직이게 했다(codeyoma/krx-quant#100).
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
- 테스트(Qt Core만): `cmake -B build -DFINCEPT_BUILD_TESTS=ON … && cmake --build build --target tst_krx_engine_data tst_krx_board tst_krx_signals tst_krx_stats_view tst_krx_chart_model tst_krx_app_release && ctest --test-dir build -R krx`.
  - CI는 경고를 오류로 빌드한다(`-Werror`, 환경 변수 `CI`가 있을 때). 로컬에서 같은 조건으로 보려면 `CI=1 cmake -B build …`로 설정한다. 그러지 않으면 구조체에 필드를 더하고 테스트의 초기화 목록을 그대로 두는 것 같은 경고를 놓친다(`custom-v4.5.0-krx.4` 맥 빌드가 이것으로 실패했다).
  - `tst_krx_engine_data`: 엔진 API와 스트림 메시지 해석(`bar` 이벤트, 합의의 `pooled`, `/v1/algorithms`의 수명 상태, 상세 통계의 기준별 lift와 그 거래일 단위 값, 보정 곡선, 나눠 본 통계 포함), 한국어 이름, 시각·가격 표기, 머리줄 엔진 점의 상태와 툴팁.
  - `tst_krx_board`: 답과 스트림을 합친 화면 상태(최신 봉 마감, 차트 표시, 합의 구성원, 신호가 볼 예측, 실패와 되감기).
  - `tst_krx_signals`: 신호 규칙, 다시 알리지 않는 규칙, 구성원을 모를 때 미루기, 엔진의 `votes`로 판단하기.
  - `tst_krx_stats_view`: 순위표와 상세 통계 창이 보이는 문구(비율·평균과 구간, 기준별 lift 줄과 거래일 구간, 시간대, 보정도 곡선과 점, 제외 건수, 조건과 날짜, 순위표 제목 툴팁의 순위 규칙, 열 제목·lift·보정도 툴팁).
  - `tst_krx_chart_model`: 차트가 그릴 봉(KST를 차트의 UTC로), 아이콘의 방향(▲▼–)·결과(✓/✗/?)·흐림·붙는 봉, 맞춤 범위(최근 60봉), 호버 다섯 줄(채점 전·후, `votes` 없음, 막힌 합의), 목표 봉(있음, 오른쪽 빈칸), 신호 강조, 고정한 봉 마감의 표 행(순서, 기준 예측 묶음, 상태, 맞음·틀림, 합의 포함 여부, `pooled` 없음은 "알 수 없음"), 왼쪽 끝에서 다음에 읽을 범위(30봉 안, 첫 봉 앞 400봉, 봉이 없으면 지금 앞, N분 보기는 100봉만큼 최대 1000), 더 읽은 봉 붙이기(중복·순서), N분 묶기(시가·고가·저가·종가·거래량, 240분봉 08:00/12:00/16:00 시작, 날 경계, 빈 분, 다른 오프셋의 시각), 실시간 봉 합치기(같은 분, 새 분, 오래된 이벤트 버리기, 5분 보기의 마지막 봉).
  - `tst_krx_app_release`: 개인 빌드 태그 해석과 순서, Release 목록에서 업데이트할 빌드·설치 파일·sha256 고르기.
- 화면 캡처(codeyoma/krx-quant#103): `custom/tools/krx-capture`는 KRX 화면을 엔진에 붙여 PNG로 저장한다. 화면 PR에 데모 엔진 화면을 남길 때 쓴다. 앱 빌드와 따로인 CMake 프로젝트이고, 설정은 임시 폴더의 `krx-capture.ini`에 쓴다. 같은 컴퓨터의 Fincept 설정(엔진 주소, 알림 설정)은 건드리지 않는다.
  - 빌드: `cmake -S custom/tools/krx-capture -B fincept-qt/build/krx-capture -G Ninja -DCMAKE_PREFIX_PATH=<Qt 6.8.3> && cmake --build fincept-qt/build/krx-capture`
  - 실행: `fincept-qt/build/krx-capture/krx_capture http://127.0.0.1:8090 out.png [초=8] [가로=1600] [세로=1000] [예측,...]`. 그 초만큼 엔진을 읽은 뒤 찍는다. 예측 이름(`gauss` 등)을 주면 그 상세 통계 창도 열어 `out-<예측>.png`로 찍는다. 테마와 글꼴은 앱 기본값(Obsidian, Consolas 14px)이다.
  - 환경 변수 `KRX_CAPTURE_VIEW=<분>`을 주면 처음에 그 분 단위 보기 버튼을 누른다(codeyoma/krx-quant#109). 고른 보기는 임시 설정에 남아 다음 캡처도 그 보기로 열린다.
  - 환경 변수 `KRX_CAPTURE_BACK=<횟수>`를 주면 기다리는 시간의 4분의 1부터 0.5초마다 차트를 그 횟수만큼 휠로 왼쪽(지난 날)으로 돌린 뒤 찍는다(codeyoma/krx-quant#108).
  - 환경 변수 `KRX_CAPTURE_PIN=<봉 마감, ISO 8601>`(예: `2026-10-12T10:24:00+09:00`)을 주면 기다리는 시간의 절반이 지났을 때 그 봉 마감의 아이콘을 누른 것처럼 예측 패널을 고정하고 찍는다(codeyoma/krx-quant#107).
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

## 개인 빌드

- `custom` 브랜치의 커밋에 `custom-v*` 태그(예: `custom-v4.5.0-krx.1`)를 달아 push하면 `custom-release` workflow가 맥 DMG와 윈도우 `setup.exe`를 만들어 이 fork의 Release(prerelease)에 올린다.
- 서명하지 않은 빌드다. 맥은 처음 열 때 우클릭 → 열기(또는 `xattr -dr com.apple.quarantine /Applications/FinceptTerminal.app`), 윈도우는 SmartScreen에서 추가 정보 → 실행.
- 공식 버전 업데이트는 꺼져 있다(`FINCEPT_DISABLE_AUTO_UPDATE`). 새 버전은 새 태그로 만들고, 설치된 앱은 아래처럼 스스로 업데이트한다.
- upstream 릴리스를 반영한 첫 빌드는 workflow가 `custom-<tag>-krx.1`로 태그한다(위 "upstream 릴리스 반영").
- 그 뒤 `custom`에 merge(push) 되는 변경은 `custom-auto-tag` workflow가 저절로 태그한다(codeyoma/krx-quant#98). push 된 커밋에 `custom-<custom/UPSTREAM_BASE>-krx.<N+1>`을 단다. N은 그 upstream 버전에서 이미 쓴 가장 큰 번호다(`-rc` 같은 꼴은 세지 않는다). 이 태그 push로 `custom-release`가 개인 빌드를 만들어 Release에 올리고, 설치된 앱이 업데이트로 받는다. 그래서 앱을 바꾸는 merge는 손대지 않아도 개인 빌드에 들어간다. 대개 그 merge의 빌드로 들어가고, 아래처럼 더 새 커밋의 빌드에 함께 들어가기도 한다. 문서나 workflow만 바꾼 merge는 빌드하지 않는다.
  - 태그를 달지 않는 경우: 그 커밋에 이미 `custom-v*` 태그가 있을 때(upstream 반영은 `custom`과 `krx.1` 태그를 한 번에 push 한다. 손으로 단 태그도 그대로 된다), push가 문서나 workflow만 바꿨을 때(`custom/**`, `*.md`, `docs/**`, `fincept-qt/docs/**`, `.github/**`). 이런 변경을 빌드하려면 손으로 태그한다.
  - 실행은 한 번에 하나라서 번호가 겹치지 않는다. `queue: max`로 기다리는 실행을 모두(최대 100개) 남겨서, 연달아 merge 해도 merge 마다 빌드가 나온다. GitHub는 기다리는 실행의 순서를 보장하지 않는다. 그래서 더 새 커밋의 빌드가 이미 있으면 그 커밋은 태그하지 않는다. 오래된 코드가 더 큰 번호를 받아 앱이 그쪽으로 "업데이트" 하는 일을 막으려고서다. 같은 번호를 다른 태그가 먼저 가졌으면 태그 push가 실패하고 기존 태그는 그대로다.
  - 태그는 `SYNC_TOKEN`으로 push 한다. `GITHUB_TOKEN`으로 push 한 태그는 `custom-release`를 돌리지 않는다. `SYNC_TOKEN`이 없으면 workflow가 실패로 끝난다.
  - 손으로 태그해도 된다. 이미 태그가 달린 커밋이면 `custom-auto-tag`는 아무것도 하지 않는다.
  - `custom`의 커밋만 태그한다. 다른 브랜치에서 손으로 돌리면(Run workflow) 아무것도 하지 않는다. 빌드 번호를 쓰거나 옛 커밋을 가장 새 빌드로 올리는 일을 막으려고서다.

> **바뀜** (2026-10-12, codeyoma/krx-quant#99): "그 뒤 화면 변경은 같은 upstream 버전에 번호를 올려 손으로 태그한다" → `custom`에 push 되면 `custom-auto-tag`가 다음 번호로 태그한다. 이유: merge 한 변경마다 사람이 태그를 달아야 앱 업데이트가 나왔다(codeyoma/krx-quant#98).

> **바뀜** (2026-10-12, codeyoma/krx-quant#95): 앱 내 업데이트 꺼짐 → 이 fork의 개인 빌드 Release에서 버튼 한 번으로 업데이트. 이유: 새 화면 빌드마다 DMG·setup.exe를 받아 직접 다시 설치해야 했다(codeyoma/krx-quant#92). 공식 버전 업데이트는 여전히 꺼져 있다.

## 앱 업데이트 (codeyoma/krx-quant#92, #130)

- **빌드 태그.** `custom-release` workflow가 태그를 `-DFINCEPT_CUSTOM_TAG`로 빌드에 넘긴다. 앱은 자기가 어느 개인 빌드인지 안다. 로컬 빌드는 태그가 없어 업데이트를 확인하지 않는다.
- **확인.** 로그인 뒤 한 번, 그리고 앱이 켜져 있는 동안 6시간마다 조용히 확인한다. 메뉴의 업데이트 확인과 About 화면의 버튼은 늘 결과를 알려 준다. GitHub API(토큰 없음)로 이 fork의 Release를 읽어, 지금보다 새 `custom-v*` 빌드 가운데 이 설치가 쓰는 파일(아래)과 GitHub의 sha256 digest가 있는 가장 새 것을 고른다. 순서는 upstream 버전, 다음 `krx.N`이다. 초안 Release와 공식 태그는 보지 않는다.
- **내려받기.** 조용한 확인은 묻지 않고 백그라운드에서 내려받는다. 메뉴의 확인은 "받기"를 물은 뒤 진행 창(취소 가능)을 띄운다. 받은 파일은 sha256을 확인하고, 맞지 않거나 digest가 없으면 지운다. 백그라운드에서 실패하면 알리지 않고 다음 확인 때 다시 받는다.
- **준비되면 버튼 하나.** 확인된 빌드는 도우미 스크립트(`fincept-qt/src/services/krx_quant/update/`, 앱 리소스에 들어 있다)에 넘긴다. 도우미는 앱이 끝나기를 기다린다. 앱은 "새 개인 빌드 krx.N이 준비됐습니다"와 **지금 재시작**, **나중에**를 띄운다(다른 작업을 막지 않는 창).
  - 지금 재시작: 앱이 닫히고, 도우미가 새 빌드로 바꾼 뒤 몇 초 안에 다시 연다. 앱은 닫기 전에 재시작 표시 파일(받은 파일 이름 + `.relaunch`)을 만들고, 도우미는 그 파일이 있을 때만 앱을 다시 연다.
  - 나중에: 앱을 언제든 끌 때 바뀐다. 다음에 켜면 새 빌드다. 그사이 메뉴로 확인하면 같은 창을 다시 띄운다.
  - 기록은 임시 폴더의 `fincept-update.log`에 남는다.
- **이 설치가 쓰는 파일과 방식.**
  - 맥(`update-macos.sh`, `…-macos-arm64.dmg`): DMG를 붙여 설치된 `.app`을 새 것으로 바꾸고 서명을 확인한다. 옛 앱은 복사가 끝날 때까지 옆에 두었다가, 실패하면 되돌린다. macOS가 "앱 관리" 권한을 물으면 허용한다. 재시작을 눌렀는데 실패하면 옛 앱과 DMG가 열린다.
  - 윈도우, 사용자 폴더 설치(`%LOCALAPPDATA%\Programs\FinceptTerminal`, `…-windows-x64.zip`): 폴더를 통째로 바꾼다. ZIP은 설치된 파일 트리에서 설치 프로그램 자신의 파일(`FinceptMaintenanceTool.*`, `components.xml`, `InstallationLog.txt`, `network.xml`, `installer.dat`, `installerResources`)을 뺀 것이다. 도우미는 앱이 도는 동안 ZIP을 `<설치 폴더>.staging`에 풀어 둔다. 앱이 끝나면 그 폴더에서 띄운 프로세스(QtWebEngineProcess 등)도 끝나기를 2분까지 기다리고, 남은 것은 멈춘다. 앱이 그 폴더에서 다시 켜져 있으면 이번 업데이트는 그만둔다. 설치 폴더를 `<설치 폴더>.old`로 옮기고 새 트리를 그 자리에 놓은 뒤, 새 트리에 없는 것(제거 도구와 그 기록, 사용자가 넣은 파일)을 옛 폴더에서 복사해 온다. 그래서 제어판의 제거도 그대로 된다. 어느 단계든 실패하면 옛 폴더를 되돌린다. 관리자 권한이 필요 없어 UAC가 뜨지 않는다.
  - 윈도우, 다른 곳 설치(Program Files 등, `…-windows-x64-setup.exe`): 이번 한 번 사용자 폴더로 옮긴다. 앱이 끝나면 새 `setup.exe`를 사용자 폴더에 무인 설치하고(workflow의 설치 확인과 같은 옵션), 옛 설치를 그 제거 도구(`FinceptMaintenanceTool.exe purge`)로 지운다. 옛 폴더에 쓸 수 없으면(Program Files) 지울 때 UAC가 한 번 뜬다. 거절하면 옛 설치는 남고 새 설치는 그대로 쓴다. 옛 설치를 지우면 그 바로가기(시작 메뉴의 `Fincept\Fincept Terminal`, 바탕 화면의 `Fincept Terminal`)도 지워지는데, 새 설치가 같은 이름으로 만든 것일 수 있다. 그래서 지우기 전에 새 설치를 여는 바로가기를 적어 두고, 지워졌으면 다시 만든다. 옛 설치를 먼저 지우지 않는 것은, 새 설치가 실패하면 앱이 없는 상태가 되기 때문이다. 준비 창에 옮길 위치와 UAC 안내가 나온다. 그 뒤로는 위의 폴더 바꾸기다.
  - 앱은 도우미를 Windows PowerShell의 전체 경로(`%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe`, 없으면 PATH의 `powershell.exe`)로 숨은 창에서 띄운다. PATH에 없을 수 있어서다(codeyoma/krx-quant#101).
  - 스스로 업데이트할 수 없는 경우(설치 프로그램으로 설치하지 않은 윈도우 앱, `%LOCALAPPDATA%`를 모르는 윈도우, 쓸 수 없는 폴더의 맥 앱)에는 메뉴의 확인에서만 알리고, 설치 파일을 받아 열어 직접 설치하게 한다. 도우미를 띄우지 못하면 설치 파일이나(ZIP이면) 릴리스 페이지를 연다.
- **새 설치 위치.** 윈도우 설치 프로그램의 기본 폴더는 `%LOCALAPPDATA%\Programs\FinceptTerminal`이다(`fincept-qt/CMakeLists.txt`의 `CPACK_IFW_TARGET_DIRECTORY`, upstream은 Program Files). 마법사에서 다른 곳을 고를 수 있지만, 그러면 첫 업데이트 때 이 폴더로 옮긴다.
- **신뢰.** Release에서 직접 받는 것과 같다(이 fork의 GitHub 계정과 Release). 거기에 GitHub digest 확인을 더했다. 서명 키는 쓰지 않는다.
- **CI.** `custom-release` workflow가 Release마다 윈도우 ZIP을 만들어 올리고, 두 도우미를 새로 만든 파일로 돌려 본다.
  - 윈도우: 설치 확인으로 설치한 트리에서 ZIP을 만든다. 폴더 바꾸기(옛 설치에만 있던 파일이 남는지, 바꾼 뒤에도 제거 도구가 지우는지)와 사용자 폴더로 옮기기(새 폴더에 설치되고 옛 설치가 지워지는지)를 확인한다. PowerShell은 전체 경로로 띄운다. 앞의 IFW 설치 단계가 PATH를 MSYS 꼴로 `GITHUB_ENV`에 써서, 뒤의 PowerShell 단계는 PATH로 `powershell.exe`를 찾지 못한다. 그 때문에 `krx.4`–`krx.6`은 윈도우 단계에서 실패해 Release가 나오지 않았다(codeyoma/krx-quant#101).
  - 맥: 설치된 복사본을 새 DMG로 바꾼다. 재시작 표시 파일 없이 돌려 아무것도 열지 않는다.

> **바뀜** (2026-10-13, codeyoma/krx-quant#133): "지금 업데이트"를 눌러야 내려받고, 받으면 앱을 바로 닫고 설치했다 → 백그라운드에서 받고 "지금 재시작" 버튼 하나, 누르지 않으면 앱을 끌 때 바뀐다. 확인은 로그인 뒤 한 번 → 그 뒤 6시간마다도. 이유: 업데이트를 사용자가 편하게, 버튼 한 번으로(codeyoma/krx-quant#130).

> **바뀜** (2026-10-13, codeyoma/krx-quant#133): 윈도우는 설치 폴더를 제거 도구로 지우고 새 `setup.exe`를 같은 폴더에 무인 설치했다(Program Files면 UAC 두 번, 실패하면 설치 마법사가 열렸고 마법사는 비어 있지 않은 폴더에 설치하지 못했다) → 사용자 폴더 설치는 ZIP으로 폴더를 바꾸고(UAC 없음, 실패하면 되돌림), 다른 곳 설치는 첫 업데이트 때 사용자 폴더로 옮긴다. 기본 설치 폴더는 Program Files → `%LOCALAPPDATA%\Programs\FinceptTerminal`. 이유: 윈도우에서 업데이트를 누르면 설치 마법사가 뜨고 덮어쓰지 못했다(codeyoma/krx-quant#130).

## 에이전트 설정

루트 `CLAUDE.md`는 `@custom/AGENTS.md` 한 줄만 담는다. upstream `.gitignore`가 `CLAUDE.md`를 무시하므로 `git add -f`로 추적하고, upstream이 같은 파일을 만들 일이 없어 merge 충돌이 나지 않는다. 설정 본문은 `custom/AGENTS.md`와 `custom/agents/`에 있다. 작업 ticket은 비공개 저장소 `codeyoma/krx-quant`의 이슈로 관리한다. 이 fork는 비공개 저장소 `krx-quant`의 `fincept/` submodule로 받는다(`git clone --recurse-submodules`). 단독으로 clone해도 빌드와 upstream merge는 되지만, 에이전트 문서가 참조하는 상위 폴더의 설계 문서(`../docs/design.md`)는 없다.

## upstream workflow

이 fork에서는 upstream의 예약·릴리스 workflow를 끈다(GitHub Actions 설정에서 disable). 개인 빌드용 workflow는 `custom-` 접두사로 둔다: `custom-upstream-watch`(`main` 동기화와 upstream 릴리스 자동 반영), `custom-release`(개인 빌드와 upstream merge의 시험 빌드, 위 "개인 빌드"), `custom-auto-tag`(`custom`에 push 된 변경을 다음 개인 빌드로 태그, 위 "개인 빌드").

> **바뀜** (2026-10-12, codeyoma/krx-quant#99): 개인 빌드용 workflow 둘 → 셋(`custom-auto-tag`). 이유: 위 "개인 빌드"의 바뀜과 같다(codeyoma/krx-quant#98).

> **바뀜** (2026-10-12, krx-quant #94): `custom-upstream-watch`는 upstream 릴리스 알림 → 자동 반영. `custom-release`에 `upstream-merge/<tag>` 브랜치의 시험 빌드가 더해졌다. 이유: upstream 릴리스 자동 반영(krx-quant #93).
