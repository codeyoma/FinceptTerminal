# 한국어 언어 팩 용어·문체 기준

`fincept-qt/translations/fincept_ko_KR.ts`를 고치거나 새 문구를 번역할 때 따르는 기준이다. 목표는 국내 증권사 HTS/MTS(키움 영웅문, 미래에셋, 삼성증권, 토스증권)와 KRX·DART에서 쓰는 표기, 그리고 자연스러운 한국어 소프트웨어 문구다. 이미 자연스럽고 맞는 번역은 취향만으로 바꾸지 않는다.

## 지켜야 할 것

- 치환자 `%1`…`%99`, `%L1`, `%n`의 종류와 개수를 그대로 둔다. 순서는 바꿔도 된다.
- HTML 태그, 줄바꿈 수, 앞쪽 글머리표(`■`, `•`, `⚠` 등), 앞뒤 공백, 단축키, URL, 이메일, 경로, 백틱 안의 코드, 제품·브랜드명, 티커, 통화 코드는 그대로 둔다.
- 복수형(`numerus`) 항목은 형태 수를 유지한다.
- 뜻이 모호하면 `fincept-qt/src`에서 그 문자열이 쓰이는 곳을 확인한다. 같은 영어라도 화면에 따라 번역이 다르다(예: `OPEN`은 시가·열기·장중·접수됨).

## 문체

- 문장은 합니다체(`~합니다`, `~했습니다`), 안내는 `~하세요`, 확인 질문은 `~하시겠습니까?`. 해요체(`~해요`, `~할까요?`), `당신`, `~하십시오`는 쓰지 않는다.
- 라벨·열 제목·버튼·메뉴·탭은 명사형으로 짧게 쓴다(`저장`, `새로고침`, `평가손익`). 상태는 `연결됨`, `완료`, `대기 중`, `불러오는 중…`.
- 영어 대문자 제목은 일반 한국어로 옮긴다. 약어는 대문자 그대로 둔다. `PYTHON`, `DBNOMICS`처럼 대문자로 쓴 제품명은 본래 표기(`Python`, `DBnomics`)로 쓴다.
- 번역투를 피한다: `~에 대한 접근을 제공합니다` → `~를 이용할 수 있습니다`, `성공적으로 ~되었습니다` → `~했습니다`, `~를 가지고 있습니다` → `~가 있습니다`, 불필요한 `~하는 것`, `~에 대한`, `~를 위한`.
- 개수는 한국어 어순과 단위로 쓴다: `거래 %1건`, `종목 %1개`, `%1일`.
- 업계에서 붙여 쓰는 복합어는 붙여 쓴다: 종목코드, 종목명, 현재가, 전일대비, 등락률, 거래량, 거래대금, 시가총액, 매수호가, 매도호가, 호가창, 호가잔량, 평균단가, 매입금액, 평가금액, 평가손익, 실현손익, 수익률, 주문가능금액, 관심종목, 보유종목, 미체결, 체결내역, 증거금, 미결제약정, 내재변동성, 행사가격, 재무상태표, 손익계산서, 현금흐름표, 매출액, 영업이익, 당기순이익, 최대낙폭, 거래정지.

## 용어

### 시세·주문

| 영어 | 한국어 | 비고 |
|---|---|---|
| Buy / Sell | 매수 / 매도 | |
| Bid / Ask | 매수호가 / 매도호가 | Bid size / Ask size → 매수잔량 / 매도잔량 |
| Order book / Depth | 호가창 / 호가잔량 | |
| Last, Last price, LTP | 현재가 | "가장 최근 항목"의 뜻이면 최근 |
| Open / High / Low / Close (가격) | 시가 / 고가 / 저가 / 종가 | |
| Prev close | 전일 종가 | |
| Change / Chg / Chg % | 전일대비 또는 등락 / 등락률 | 경제지표·비중의 변화는 변동 |
| Volume / Turnover | 거래량 / 거래대금 | |
| Market cap | 시가총액 | |
| Trades (체결 목록) | 체결, 체결내역 | 백테스트 통계의 trades는 거래(횟수) |
| Fill / Partially filled | 체결 / 부분 체결 | |
| Open orders, Pending orders | 미체결 주문 | 승인 대기열(Action Center)은 승인 대기 주문 |
| Cancel / Modify | 취소 / 정정 | |
| Market / Limit / Stop order | 시장가 / 지정가 / 스톱 주문 | |
| Auctions (미국 장 시작·마감 단일가) | 동시호가 | 국채 입찰은 입찰 |
| Position | 포지션 | |
| Holdings | 보유종목 | 가상자산·혼합이면 보유 자산 |
| Avg price / Avg cost | 평균단가 | |
| Cost basis / Invested | 매입금액 | |
| Market value / Current value | 평가금액 | |
| Unrealized / Realized P&L | 평가손익 / 실현손익 | P&L → 손익, Day P&L → 당일 손익 |
| Return | 수익률 | |
| Balance | 잔고 | 크레딧은 크레딧 잔액 |
| Buying power | 주문가능금액 | |
| Account | 계좌(증권·거래 계좌) / 계정(로그인·Fincept·온체인 계정) | |
| Broker | 증권사 | MT4 등 외환 브로커는 브로커 |
| Deposit / Withdraw | 입금 / 출금 | |
| Commission / Fees | 수수료 | |
| Margin / Leverage / Liquidation | 증거금 / 레버리지 / 청산(강제 청산) | |
| Long / Short | 롱 / 숏 | 주식 공매도는 공매도 |
| Watchlist / Screener | 관심종목 / 스크리너 | |
| Symbol, Ticker | 종목코드 | 가상자산·통화쌍은 심볼 |
| Gainers / Losers / Movers | 상승 종목 / 하락 종목 / 급등락 종목 | Top gainers → 상승률 상위 |
| Pre-market / After-hours | 프리마켓 / 애프터마켓 | KRX 세션은 장전 시간외 / 장후 시간외 |
| Market hours / Market open | 거래시간 / 장중 | |
| Trading halt | 거래정지 | 작업 중단의 halt는 중지 |
| Paper / Live trading | 모의투자 / 실전투자 | |
| Native (종목의 원래 시세 통화) | 거래 통화 | 환산하지 않은 금액은 원래 통화·원래 금액. `원 단위`는 원화로 읽혀서 쓰지 않는다 |

### 차트·기술적 분석

| 영어 | 한국어 |
|---|---|
| Candle / N-minute bar / Daily | 캔들 / N분봉 / 일봉 |
| Timeframe, Interval | 주기 |
| 1D 1W 1M 3M 6M YTD 1Y MAX | 1일 1주 1개월 3개월 6개월 연초 이후 1년 전체 |
| Indicator | 지표(차트 위에서는 보조지표) |
| Moving average | 이동평균 |
| Support / Resistance | 지지선 / 저항선(고객 지원의 Support는 지원) |
| Overbought / Oversold | 과매수 / 과매도 |
| Bullish / Bearish / Neutral | 강세 / 약세 / 중립 |

### 재무(K-IFRS·DART 표기)

| 영어 | 한국어 |
|---|---|
| Balance sheet / Income statement / Cash flow | 재무상태표 / 손익계산서 / 현금흐름표 |
| Revenue / Gross profit / Operating income / Net income | 매출액 / 매출총이익 / 영업이익 / 당기순이익(좁은 열은 매출, 순이익) |
| Total assets / liabilities / equity | 자산총계 / 부채총계 / 자본총계 |
| P/E, Fwd P/E, P/B, P/S | PER, 선행 PER, PBR, PSR |
| D/E | 부채비율 |
| Dividend yield / Payout ratio | 배당수익률 / 배당성향 |
| Shares outstanding / Float | 발행주식수 / 유통주식수 |
| Price target / Rating | 목표주가 / 투자의견(매수·중립·매도, 비중확대·비중축소) |
| Valuation | 밸류에이션(제목) / 가치평가(행위) |
| Peers / Filings | 동종업체 / 공시 |

### 리스크·포트폴리오

| 영어 | 한국어 |
|---|---|
| Risk (제목·지표 묶음) | 리스크(문장 속에서는 위험도 쓴다) |
| Sharpe / Sortino / Calmar / Treynor ratio | 샤프지수 / 소르티노지수 / 칼마지수 / 트레이너지수 |
| Max drawdown | 최대낙폭(MDD) |
| Value at risk / Expected shortfall | VaR / 기대손실(ES) |
| Win rate / Profit factor | 승률 / 프로핏 팩터 |
| Exposure / Allocation / Weight | 익스포저 / 배분, 자산배분 / 비중(최적화 모수는 가중치) |
| Rebalance / Benchmark / Hedge | 리밸런싱 / 벤치마크 / 헤지 |
| Risk-free rate | 무위험이자율 |

### 파생·채권·외환·가상자산

| 영어 | 한국어 |
|---|---|
| Call / Put / Strike / Expiry | 콜 / 풋 / 행사가격(좁은 열은 행사가) / 만기 |
| Implied volatility / Greeks | 내재변동성 / 그릭스(로는 로(Rho)) |
| ATM / ITM / OTM | 등가격 / 내가격 / 외가격 |
| Open interest | 미결제약정 |
| Perpetual / Funding rate / Mark price | 무기한 선물 / 펀딩비 / 마크 가격 |
| Yield / Coupon / Duration | 금리·수익률 / 표면금리 / 듀레이션 |
| Crypto | 가상자산 |
| Wallet / Gas fee / Staking | 지갑 / 가스비 / 스테이킹 |
| Index (시장지수) | 지수(배열·데이터 인덱스는 인덱스) |

### 소프트웨어

Provider → 공급자, Credentials → 인증 정보, Workspace → 작업 공간, Refresh → 새로고침, Export / Import → 내보내기 / 가져오기, Category → 카테고리, Sign up → 회원가입.
