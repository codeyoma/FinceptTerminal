#include "services/krx_quant/ChartStudies.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>
#include <QRegularExpression>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace fincept::krx_quant {

namespace {

constexpr qint64 kDaySeconds = 24 * 3600;

/// The KST date of a chart time (chart_time is KST read as UTC), as days since the epoch.
qint64 day_of(qint64 time) {
    return time >= 0 ? time / kDaySeconds : (time - kDaySeconds + 1) / kDaySeconds;
}

QDate date_of(qint64 time) {
    return QDate(1970, 1, 1).addDays(day_of(time));
}

qint64 time_of(const QDate& date) {
    return QDate(1970, 1, 1).daysTo(date) * kDaySeconds;
}

StudyInput input(const QString& title, double value, double min, double max, double step = 1, int decimals = 0) {
    return StudyInput{title, value, min, max, step, decimals, {}};
}

StudyInput period(double value, const QString& title = QStringLiteral("기간")) {
    return input(title, value, 1, kMaxHistoryBars);
}

/// A lower indicator's period (krx-quant #223): up to 500 bars.
StudyInput span(double value, const QString& title = QStringLiteral("기간")) {
    return input(title, value, 1, 500);
}

/// The colours the moving averages' added lines take, after their own four.
const QStringList& extra_colors() {
    static const QStringList colors{"#A3E635", "#F87171", "#818CF8", "#FDE68A", "#5EEAD4", "#FDA4AF"};
    return colors;
}

QList<StudySpec> make_specs() {
    QList<StudySpec> specs;
    auto add = [&specs](Study study, const char* key, const QString& title, const QString& about, const QString& help) {
        StudySpec spec;
        spec.study = study;
        spec.key = QString::fromLatin1(key);
        spec.title = title;
        spec.about = about;
        spec.help = help;
        specs << spec;
        return &specs.last();
    };
    auto* ma =
        add(Study::MovingAverage, "ma", QStringLiteral("이동평균선"), QStringLiteral("기간 동안 기준값의 단순 평균."),
            QStringLiteral("봉마다 그 봉까지 N봉의 기준값을 더해 N으로 나눈다. 앞에 N봉이 안 되는 봉은 비운다. "
                           "기간 추가로 선을 더하고 X로 뺀다(첫 줄은 남는다)."));
    ma->periods = true;
    ma->source = Source::Close;
    ma->inputs = {period(5), period(20), period(60), period(120)};
    ma->lines = {{"5", "#F472B6", 1}, {"20", "#F59E0B", 1}, {"60", "#22C55E", 1}, {"120", "#38BDF8", 1}};

    auto* ichimoku = add(Study::Ichimoku, "ichimoku", QStringLiteral("일목균형표"),
                         QStringLiteral("전환선·기준선과 미래로 그린 구름(선행스팬)으로 추세와 지지·저항을 본다."),
                         QStringLiteral("전환선은 전환 기간의 최고 고가와 최저 저가의 중간, 기준선은 기준 기간의 중간, "
                                        "선행스팬1은 전환선과 기준선의 중간, 선행스팬2는 선행 기간의 중간이다. 두 "
                                        "선행스팬은 이동 기간만큼 앞(미래)에 그리고 그 사이를 칠한다(선행스팬1이 위면 "
                                        "선행스팬1의 색). 후행스팬은 종가를 이동 기간만큼 뒤에 그린다."));
    ichimoku->inputs = {period(9, QStringLiteral("전환")), period(26, QStringLiteral("기준")),
                        period(52, QStringLiteral("선행")), period(26, QStringLiteral("이동"))};
    for (auto& i : ichimoku->inputs)
        i.max = 500;
    ichimoku->lines = {{QStringLiteral("전환선"), "#3B82F6", 1},
                       {QStringLiteral("기준선"), "#EF4444", 1},
                       {QStringLiteral("후행스팬"), "#22C55E", 1},
                       {QStringLiteral("선행스팬1"), "#86EFAC", 1},
                       {QStringLiteral("선행스팬2"), "#FCA5A5", 1}};

    auto* bollinger = add(Study::Bollinger, "bollinger", QStringLiteral("볼린저 밴드"),
                          QStringLiteral("이동평균선 위아래로 표준편차만큼 벌린 띠. 변동성이 크면 넓어진다."),
                          QStringLiteral("중심선은 기준값의 N봉 단순 평균, 상단·하단은 중심선 ± 배수 × 같은 N봉의 "
                                         "표준편차(모표준편차)다."));
    bollinger->source = Source::Close;
    bollinger->inputs = {period(20), input(QStringLiteral("배수"), 2, 0.1, 10, 0.1, 1)};
    bollinger->lines = {{QStringLiteral("중심선"), "#F59E0B", 1},
                        {QStringLiteral("상단"), "#38BDF8", 1},
                        {QStringLiteral("하단"), "#38BDF8", 1}};

    auto* supertrend =
        add(Study::Supertrend, "supertrend", QStringLiteral("슈퍼트렌드"),
            QStringLiteral("ATR로 만든 추세선. 선이 가격 아래면 상승 추세, 위면 하락 추세."),
            QStringLiteral("(고가+저가)/2 ± 배수 × ATR로 위아래 띠를 만들고, 종가가 띠 안에 있는 동안 띠는 "
                           "좁아지기만 한다. 상승 추세에는 아래 띠, 하락 추세에는 위 띠를 그리고, 종가가 "
                           "선을 넘으면 추세가 바뀐다."));
    supertrend->inputs = {period(10, QStringLiteral("ATR 기간")), input(QStringLiteral("배수"), 3, 0.1, 20, 0.1, 1)};
    supertrend->inputs[0].max = 500;
    supertrend->lines = {{QStringLiteral("상승 추세"), "#22C55E", 2}, {QStringLiteral("하락 추세"), "#EF4444", 2}};

    auto* profile =
        add(Study::VolumeProfile, "volume_profile", QStringLiteral("매물대분석"),
            QStringLiteral("화면에 보이는 구간의 거래량을 가격대별로 나눠 오른쪽에 가로 막대로 보인다."),
            QStringLiteral("보이는 봉의 최저 저가부터 최고 고가까지를 같은 높이의 칸으로 나누고, 봉마다 "
                           "거래량을 그 봉의 저가~고가가 걸친 칸에 걸친 만큼 나눠 더한다. 매수·매도는 나누지 "
                           "않는다. 스크롤·확대하면 다시 센다."));
    profile->inputs = {input(QStringLiteral("칸 수"), 24, 4, 200)};
    profile->lines = {{QStringLiteral("막대"), "#94A3B8", 0}};

    auto* envelope = add(Study::Envelope, "envelope", QStringLiteral("엔벨로프"),
                         QStringLiteral("이동평균선 위아래로 일정 비율만큼 벌린 띠."),
                         QStringLiteral("중심선은 기준값의 N봉 단순 평균, 상단·하단은 중심선 × (1 ± 폭%)이다."));
    envelope->source = Source::Close;
    envelope->inputs = {period(20), input(QStringLiteral("폭(%)"), 10, 0.1, 100, 0.5, 1)};
    envelope->lines = {{QStringLiteral("중심선"), "#F59E0B", 1},
                       {QStringLiteral("상단"), "#A78BFA", 1},
                       {QStringLiteral("하단"), "#A78BFA", 1}};

    auto* fractal = add(Study::Fractal, "fractal", QStringLiteral("윌리엄스 프랙탈"),
                        QStringLiteral("양옆 봉들보다 높은 고점과 낮은 저점에 표시한다."),
                        QStringLiteral("고가가 양쪽 N봉(기본 2, 모두 5봉)의 고가보다 높은 봉 위에 ▲, 저가가 양쪽 N봉의 "
                                       "저가보다 낮은 봉 아래에 ▼를 찍는다. 오른쪽에 N봉이 더 생겨야 정해진다."));
    fractal->inputs = {input(QStringLiteral("기간"), 2, 1, 20)};
    fractal->lines = {{QStringLiteral("위 프랙탈"), "#22C55E", 0}, {QStringLiteral("아래 프랙탈"), "#EF4444", 0}};

    auto* ema = add(Study::ExponentialAverage, "ema", QStringLiteral("지수이동평균선"),
                    QStringLiteral("최근 값에 더 큰 무게를 주는 이동평균."),
                    QStringLiteral("EMA = 앞 EMA + 2/(N+1) × (기준값 − 앞 EMA). 첫 값은 처음 N봉의 단순 평균이고, "
                                   "그 앞은 비운다. 기간 추가로 선을 더하고 X로 뺀다(첫 줄은 남는다)."));
    ema->periods = true;
    ema->source = Source::Close;
    ema->inputs = {period(5), period(20), period(60), period(120)};
    ema->lines = {{"5", "#C084FC", 1}, {"20", "#FB923C", 1}, {"60", "#2DD4BF", 1}, {"120", "#60A5FA", 1}};

    auto* sar = add(Study::ParabolicSar, "psar", QStringLiteral("파라볼릭 SAR"),
                    QStringLiteral("추세를 따라가며 빨라지는 점. 점이 가격 반대편으로 넘어가면 추세가 바뀐다."),
                    QStringLiteral("SAR = 앞 SAR + 가속 × (극값 − 앞 SAR). 가속은 시작값에서 새 극값마다 증가값만큼 "
                                   "늘고 최대값에서 멈춘다. 가격이 SAR를 넘으면 추세가 바뀌고, SAR는 앞 추세의 "
                                   "극값에서 다시 시작한다."));
    sar->inputs = {input(QStringLiteral("시작"), 0.02, 0.001, 1, 0.01, 3),
                   input(QStringLiteral("증가"), 0.02, 0.001, 1, 0.01, 3),
                   input(QStringLiteral("최대"), 0.2, 0.001, 1, 0.01, 3)};
    sar->lines = {{QStringLiteral("점"), "#A78BFA", 1}};

    auto* channel = add(Study::PriceChannel, "price_channel", QStringLiteral("프라이스 채널"),
                        QStringLiteral("N봉 동안의 최고 고가와 최저 저가를 잇는 채널."),
                        QStringLiteral("상단은 그 봉까지 N봉의 최고 고가, 하단은 N봉의 최저 저가다."));
    channel->inputs = {period(20)};
    channel->lines = {{QStringLiteral("상단"), "#2DD4BF", 1}, {QStringLiteral("하단"), "#FB7185", 1}};

    auto* vwap = add(Study::Vwap, "vwap", QStringLiteral("VWAP"),
                     QStringLiteral("거래량으로 가중한 그날의 평균가. 분봉에서만 그린다."),
                     QStringLiteral("날짜가 바뀐 뒤 첫 봉(날짜 구분선의 봉)부터 기준값 × 거래량의 합을 거래량의 합으로 "
                                    "나눈다. 일·주·월·년 보기에서는 그리지 않는다(켠 것과 설정은 남는다)."));
    vwap->minute_only = true;
    vwap->source = Source::HLC3;
    vwap->lines = {{QStringLiteral("VWAP"), "#FACC15", 2}};

    auto* anchored = add(Study::AnchoredVwap, "anchored_vwap", QStringLiteral("고정된 VWAP"),
                         QStringLiteral("고른 기준 봉부터 센 거래량 가중 평균가."),
                         QStringLiteral("켜면 차트를 한 번 눌러 기준 봉을 고른다. 기준 봉부터 기준값 × 거래량의 합을 "
                                        "거래량의 합으로 나눈다. 기준 표시를 드래그해 옮긴다. 기준은 시각으로 종목마다 "
                                        "남고, 보기를 바꾸면 그 시각이 든 봉이 기준이다."));
    anchored->source = Source::HLC3;
    anchored->lines = {{QStringLiteral("고정 VWAP"), "#E879F9", 2}};

    auto* volume =
        add(Study::Volume, "volume", QStringLiteral("거래량"),
            QStringLiteral("봉마다 거래량 막대. 오른 봉은 상승색, 내린 봉은 하락색."),
            QStringLiteral("가격 아래 칸에 봉마다 거래량을 막대로 그린다. 종가가 시가 이상이면 상승색이다."));
    volume->upper = false;
    volume->volume_scale = true;

    // The lower indicators (krx-quant #223): each in a pane of its own below the prices.
    auto lower = [&add](Study study, const char* key, const QString& title, const QString& about, const QString& help,
                        QList<double> guides, int decimals = 2) {
        StudySpec* spec = add(study, key, title, about, help);
        spec->upper = false;
        spec->guides = std::move(guides);
        spec->decimals = decimals;
        return spec;
    };
    auto* mass = lower(Study::MassIndex, "mass_index", QStringLiteral("매스 인덱스"),
                       QStringLiteral("고가−저가 폭의 EMA 비율을 더해 추세가 뒤집힐 때를 본다."),
                       QStringLiteral("봉마다 고가 − 저가의 EMA(EMA 기간)를 그 EMA의 EMA로 나누고, 합 기간 동안 "
                                      "더한다. 27을 넘었다가 26.5 아래로 내려오면 반전 신호로 본다."),
                       {27, 26.5});
    mass->inputs = {span(9, QStringLiteral("EMA 기간")), span(25, QStringLiteral("합 기간"))};
    mass->lines = {{QStringLiteral("매스 인덱스"), "#F59E0B", 1}};

    auto* momentum = lower(Study::Momentum, "momentum", QStringLiteral("모멘텀"),
                           QStringLiteral("기준값이 N봉 전보다 얼마나 올랐나."),
                           QStringLiteral("기준값 − N봉 전 기준값이다."), {}, 0);
    momentum->source = Source::Close;
    momentum->inputs = {span(10)};
    momentum->lines = {{QStringLiteral("모멘텀"), "#38BDF8", 1}};

    auto* vo = lower(Study::VolumeOscillator, "volume_oscillator", QStringLiteral("볼륨 오실레이터"),
                     QStringLiteral("짧은 거래량 평균이 긴 거래량 평균보다 몇 % 많은가."),
                     QStringLiteral("100 × (짧은 기간 거래량 EMA − 긴 기간 거래량 EMA) ÷ 긴 기간 거래량 EMA다."), {});
    vo->inputs = {span(5, QStringLiteral("짧은 기간")), span(10, QStringLiteral("긴 기간"))};
    vo->lines = {{QStringLiteral("볼륨 오실레이터"), "#2DD4BF", 1}};

    auto* percent_b = lower(Study::PercentB, "bollinger_b", QStringLiteral("볼린저 밴드 %B"),
                            QStringLiteral("가격이 볼린저 밴드 안 어디에 있나. 하단이 0, 상단이 1."),
                            QStringLiteral("(기준값 − 하단) ÷ (상단 − 하단). 밴드는 볼린저 밴드와 같다(N봉 단순 평균 ± "
                                           "배수 × 모표준편차). 상단과 하단이 같으면 비운다."),
                            {1, 0}, 3);
    percent_b->source = Source::Close;
    percent_b->inputs = {span(20), input(QStringLiteral("배수"), 2, 0.1, 10, 0.1, 1)};
    percent_b->lines = {{QStringLiteral("%B"), "#38BDF8", 1}};

    auto* width = lower(Study::BandWidth, "bollinger_width", QStringLiteral("볼린저 밴드폭"),
                        QStringLiteral("볼린저 밴드의 폭을 중심선에 대한 %로 본다. 좁으면 변동성이 작다."),
                        QStringLiteral("100 × (상단 − 하단) ÷ 중심선. 밴드는 볼린저 밴드와 같다."), {});
    width->source = Source::Close;
    width->inputs = {span(20), input(QStringLiteral("배수"), 2, 0.1, 10, 0.1, 1)};
    width->lines = {{QStringLiteral("밴드폭"), "#38BDF8", 1}};

    auto* stochastic =
        lower(Study::Stochastic, "stochastic", QStringLiteral("스토캐스틱"),
              QStringLiteral("N봉 범위 안에서 종가가 어디 있나(%). 슬로우 스토캐스틱."),
              QStringLiteral("%K는 100 × (종가 − N봉 최저 저가) ÷ (N봉 최고 고가 − 최저 저가)를 %K 평활 "
                             "봉 동안 단순 평균한 것, %D는 %K를 %D 평활 봉 동안 단순 평균한 것이다. 최고와 "
                             "최저가 같은 봉은 비운다."),
              {80, 20});
    stochastic->inputs = {span(14, QStringLiteral("%K 기간")), span(3, QStringLiteral("%K 평활")),
                          span(3, QStringLiteral("%D 평활"))};
    stochastic->lines = {{QStringLiteral("%K"), "#38BDF8", 1}, {QStringLiteral("%D"), "#F59E0B", 1}};

    auto* stoch_rsi =
        lower(Study::StochasticRsi, "stochastic_rsi", QStringLiteral("스토캐스틱 RSI"),
              QStringLiteral("RSI가 제 N봉 범위 안 어디에 있나(%)."),
              QStringLiteral("RSI(RSI 기간)가 스토캐스틱 기간 동안의 최고·최저 사이 어디인지(%)를 K 봉 동안 "
                             "단순 평균한 것이 K, K를 D 봉 동안 단순 평균한 것이 D다."),
              {80, 20});
    stoch_rsi->source = Source::Close;
    stoch_rsi->inputs = {span(14, QStringLiteral("RSI 기간")), span(14, QStringLiteral("스토캐스틱 기간")),
                         span(3, QStringLiteral("K")), span(3, QStringLiteral("D"))};
    stoch_rsi->lines = {{QStringLiteral("K"), "#38BDF8", 1}, {QStringLiteral("D"), "#F59E0B", 1}};

    auto* williams =
        lower(Study::WilliamsR, "williams_r", QStringLiteral("윌리엄스 R"),
              QStringLiteral("종가가 N봉 최고 고가에서 얼마나 내려와 있나(0~−100)."),
              QStringLiteral("−100 × (N봉 최고 고가 − 종가) ÷ (N봉 최고 고가 − 최저 저가). −20 위는 과매수, "
                             "−80 아래는 과매도로 본다."),
              {-20, -80});
    williams->inputs = {span(14)};
    williams->lines = {{QStringLiteral("%R"), "#C084FC", 1}};

    auto* disparity = lower(Study::Disparity, "disparity", QStringLiteral("이격도"),
                            QStringLiteral("기준값이 이동평균에서 얼마나 떨어져 있나(%). 100이 평균이다."),
                            QStringLiteral("100 × 기준값 ÷ N봉 단순 평균."), {100});
    disparity->source = Source::Close;
    disparity->inputs = {span(20)};
    disparity->lines = {{QStringLiteral("이격도"), "#FB923C", 1}};

    auto* intensity =
        lower(Study::IntradayIntensity, "intraday_intensity", QStringLiteral("일중 강도 지수"),
              QStringLiteral("봉 안에서 종가가 어디서 끝났는지를 거래량으로 가중해 매집과 분산을 본다(%)."),
              QStringLiteral("봉마다 (2 × 종가 − 고가 − 저가) ÷ (고가 − 저가) × 거래량을 N봉 더해 N봉 거래량의 합으로 "
                             "나누고 100을 곱한다. 고가와 저가가 같은 봉은 0을 더한다."),
              {0});
    intensity->inputs = {span(21)};
    intensity->lines = {{QStringLiteral("일중 강도"), "#2DD4BF", 1}};

    auto* chaikin = lower(Study::ChaikinOscillator, "chaikin_oscillator", QStringLiteral("체이킨 오실레이터"),
                          QStringLiteral("AD 라인의 짧은 EMA와 긴 EMA의 차이."),
                          QStringLiteral("EMA(AD 라인, 짧은 기간) − EMA(AD 라인, 긴 기간). AD 라인이 어디서 0이든 값은 "
                                         "같다."),
                          {0}, 0);
    chaikin->inputs = {span(3, QStringLiteral("짧은 기간")), span(10, QStringLiteral("긴 기간"))};
    chaikin->lines = {{QStringLiteral("체이킨"), "#F472B6", 1}};
    chaikin->volume_scale = true;

    auto* trix = lower(Study::Trix, "trix", QStringLiteral("트릭스"),
                       QStringLiteral("기준값의 삼중 지수이동평균이 봉마다 바뀌는 비율(%)."),
                       QStringLiteral("기준값에 EMA(기간)를 세 번 씌운 값이 앞 봉보다 바뀐 비율 × 100. 시그널은 그 "
                                      "EMA(시그널 기간)다."),
                       {0}, 3);
    trix->source = Source::Close;
    trix->inputs = {span(18), span(9, QStringLiteral("시그널"))};
    trix->lines = {{QStringLiteral("트릭스"), "#38BDF8", 1}, {QStringLiteral("시그널"), "#F59E0B", 1}};

    auto* po = lower(Study::PriceOscillator, "price_oscillator", QStringLiteral("프라이스 오실레이터"),
                     QStringLiteral("짧은 이동평균이 긴 이동평균보다 몇 % 높은가."),
                     QStringLiteral("100 × (짧은 기간 평균 − 긴 기간 평균) ÷ 긴 기간 평균. 방식은 단순(이동평균)이나 "
                                    "지수(EMA)다."),
                     {0});
    po->source = Source::Close;
    StudyInput kind = input(QStringLiteral("방식"), 0, 0, 1);
    kind.choices = {QStringLiteral("단순"), QStringLiteral("지수")};
    po->inputs = {span(10, QStringLiteral("짧은 기간")), span(21, QStringLiteral("긴 기간")), kind};
    po->lines = {{QStringLiteral("프라이스 오실레이터"), "#22C55E", 1}};

    const QString anchored_help = QStringLiteral(
        " 값은 마지막 봉 날짜의 첫 봉(일·주·월·년 보기는 그 해의 첫 봉)에서 0이고, 그 앞은 거꾸로 센다. 그래서 앞의 "
        "봉을 더 읽어도 값이 바뀌지 않는다.");
    auto* ad = lower(
        Study::AdLine, "ad_line", QStringLiteral("AD 라인"),
        QStringLiteral("봉 안에서 종가의 자리로 거래량을 가중해 누적한 매집·분산 선."),
        QStringLiteral("봉마다 (2 × 종가 − 고가 − 저가) ÷ (고가 − 저가) × 거래량을 누적한다.") + anchored_help, {}, 0);
    ad->volume_scale = true;
    ad->lines = {{QStringLiteral("AD"), "#60A5FA", 1}};

    auto* adx = lower(Study::Adx, "adx", QStringLiteral("ADX"),
                      QStringLiteral("추세의 세기. 방향은 보지 않고, 클수록 추세가 강하다."),
                      QStringLiteral("Wilder의 DMI에서 DX = 100 × |+DI − −DI| ÷ (+DI + −DI)를 N봉 Wilder 평균한 "
                                     "것이다(+DI, −DI는 DMI와 같다)."),
                      {});
    adx->inputs = {span(14)};
    adx->lines = {{QStringLiteral("ADX"), "#F59E0B", 1}};

    auto* atr = lower(Study::Atr, "atr", QStringLiteral("ATR"), QStringLiteral("봉이 움직이는 폭의 평균."),
                      QStringLiteral("true range(고가 − 저가, |고가 − 앞 종가|, |저가 − 앞 종가| 중 가장 큰 것)의 N봉 "
                                     "Wilder 평균이다."),
                      {}, 1);
    atr->inputs = {span(14)};
    atr->lines = {{QStringLiteral("ATR"), "#A78BFA", 1}};

    auto* cci = lower(Study::Cci, "cci", QStringLiteral("CCI"),
                      QStringLiteral("기준값이 평균에서 평균 편차의 몇 배 떨어졌나. ±100 밖은 과열로 본다."),
                      QStringLiteral("(기준값 − N봉 단순 평균) ÷ (0.015 × N봉 평균 편차). 평균 편차가 0이면 비운다."),
                      {100, -100});
    cci->source = Source::HLC3;
    cci->inputs = {span(20)};
    cci->lines = {{QStringLiteral("CCI"), "#FACC15", 1}};

    auto* dmi = lower(Study::Dmi, "dmi", QStringLiteral("DMI"),
                      QStringLiteral("+DI와 −DI로 추세의 방향을, ADX로 그 세기를 본다."),
                      QStringLiteral("+DM(고가가 앞 봉보다 오른 폭이 저가가 내린 폭보다 크고 양수면 그 폭, 아니면 "
                                     "0)과 −DM(거꾸로), true range를 각각 N봉 Wilder 평균해 +DI = 100 × +DM ÷ TR, −DI "
                                     "= 100 × −DM ÷ TR이다. ADX는 DX = 100 × |+DI − −DI| ÷ (+DI + −DI)의 N봉 Wilder "
                                     "평균이다."),
                      {});
    dmi->inputs = {span(14)};
    dmi->lines = {{QStringLiteral("+DI"), "#22C55E", 1},
                  {QStringLiteral("−DI"), "#EF4444", 1},
                  {QStringLiteral("ADX"), "#F59E0B", 1}};

    auto* macd = lower(Study::Macd, "macd", QStringLiteral("MACD"),
                       QStringLiteral("짧은 EMA와 긴 EMA의 차이와 그 시그널. 히스토그램은 둘의 차이."),
                       QStringLiteral("MACD = EMA(짧은 기간) − EMA(긴 기간), 시그널 = MACD의 EMA(시그널 기간), "
                                      "히스토그램 = MACD − 시그널."),
                       {0});
    macd->source = Source::Close;
    macd->inputs = {span(12, QStringLiteral("짧은 기간")), span(26, QStringLiteral("긴 기간")),
                    span(9, QStringLiteral("시그널"))};
    macd->lines = {{QStringLiteral("MACD"), "#38BDF8", 1},
                   {QStringLiteral("시그널"), "#F59E0B", 1},
                   {QStringLiteral("히스토그램 +"), "#22C55E", 0},
                   {QStringLiteral("히스토그램 −"), "#EF4444", 0}};

    auto* mfi = lower(Study::Mfi, "mfi", QStringLiteral("MFI"),
                      QStringLiteral("거래량을 넣은 RSI. 80 위는 과매수, 20 아래는 과매도로 본다."),
                      QStringLiteral("봉마다 기준값 × 거래량(돈의 흐름)을 기준값이 앞 봉보다 오르면 양, 내리면 음으로 "
                                     "나눠 N봉 더하고, 100 × 양 ÷ (양 + 음)이다."),
                      {80, 20});
    mfi->source = Source::HLC3;
    mfi->inputs = {span(14)};
    mfi->lines = {{QStringLiteral("MFI"), "#E879F9", 1}};

    auto* obv =
        lower(Study::Obv, "obv", QStringLiteral("OBV"),
              QStringLiteral("종가가 오른 봉의 거래량은 더하고 내린 봉의 거래량은 빼 누적한다."),
              QStringLiteral("종가가 앞 봉보다 오르면 거래량을 더하고, 내리면 빼고, 같으면 그대로다.") + anchored_help,
              {}, 0);
    obv->volume_scale = true;
    obv->lines = {{QStringLiteral("OBV"), "#60A5FA", 1}};

    auto* roc = lower(Study::Roc, "roc", QStringLiteral("ROC"), QStringLiteral("기준값이 N봉 전보다 몇 % 바뀌었나."),
                      QStringLiteral("100 × (기준값 − N봉 전 기준값) ÷ N봉 전 기준값."), {0});
    roc->source = Source::Close;
    roc->inputs = {span(9)};
    roc->lines = {{QStringLiteral("ROC"), "#2DD4BF", 1}};

    auto* rsi =
        lower(Study::Rsi, "rsi", QStringLiteral("RSI"),
              QStringLiteral("오른 폭과 내린 폭의 비로 과매수·과매도를 본다. 70 위, 30 아래."),
              QStringLiteral("봉마다 기준값이 앞 봉보다 오른 폭과 내린 폭을 각각 N봉 Wilder 평균해, RSI = 100 − "
                             "100 ÷ (1 + 오른 폭 평균 ÷ 내린 폭 평균)이다. 내린 폭이 없으면 100, 오른 폭이 "
                             "없으면 0이다."),
              {70, 30});
    rsi->source = Source::Close;
    rsi->inputs = {span(14)};
    rsi->lines = {{QStringLiteral("RSI"), "#C084FC", 1}};

    auto* rmi =
        lower(Study::Rmi, "rmi", QStringLiteral("RMI"),
              QStringLiteral("RSI를 앞 봉 대신 모멘텀 봉 전과 견줘 센다. 70 위, 30 아래."),
              QStringLiteral("RSI와 같되, 오른 폭과 내린 폭을 기준값 − 모멘텀 봉 전 기준값으로 센다."), {70, 30});
    rmi->source = Source::Close;
    rmi->inputs = {span(14), span(5, QStringLiteral("모멘텀"))};
    rmi->lines = {{QStringLiteral("RMI"), "#A78BFA", 1}};
    return specs;
}

QString number_text(double value, int decimals) {
    QString text = QString::number(value, 'f', decimals);
    if (text.contains('.')) {
        while (text.endsWith('0'))
            text.chop(1);
        if (text.endsWith('.'))
            text.chop(1);
    }
    return text;
}

bool valid_color(const QString& color) {
    static const QRegularExpression hex(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    return hex.match(color).hasMatch();
}

/// The highest high (``high``) or lowest low of ``period`` bars ending at ``i``; none before.
std::optional<double> extreme(const QList<ChartBar>& bars, qsizetype i, int period, bool high) {
    if (period < 1 || i + 1 < period)
        return std::nullopt;
    double out = high ? bars[i].high : bars[i].low;
    for (qsizetype j = i - period + 1; j < i; ++j)
        out = high ? std::max(out, bars[j].high) : std::min(out, bars[j].low);
    return out;
}

std::optional<double> midpoint(const QList<ChartBar>& bars, qsizetype i, int period) {
    const auto hi = extreme(bars, i, period, true);
    const auto lo = extreme(bars, i, period, false);
    if (!hi || !lo)
        return std::nullopt;
    return (*hi + *lo) / 2;
}

int whole(double value) {
    return static_cast<int>(std::lround(value));
}

Values to_values(const QList<double>& values) {
    Values out;
    out.reserve(values.size());
    for (const double v : values)
        out << v;
    return out;
}

/// The simple average of the last ``period`` values; none while any of them is none.
Values average_of(const Values& values, int period) {
    Values out(values.size());
    if (period < 1)
        return out;
    double sum = 0;
    int run = 0; // values in a row up to here
    for (qsizetype i = 0; i < values.size(); ++i) {
        if (!values[i]) {
            sum = 0;
            run = 0;
            continue;
        }
        sum += *values[i];
        if (++run > period)
            sum -= *values[i - period];
        if (run >= period)
            out[i] = sum / period;
    }
    return out;
}

/// The sum of the last ``period`` values; none while any of them is none.
Values sum_of(const Values& values, int period) {
    Values out = average_of(values, period);
    for (auto& v : out)
        if (v)
            *v *= period;
    return out;
}

/// An EMA (2 / (period + 1)) or Wilder's average (``wilder``: 1 / period) of ``values``, starting
/// at the simple average of the first ``period`` of them in a row. A none on the way is none; the
/// average carries over it.
Values smoothed(const Values& values, int period, bool wilder) {
    Values out(values.size());
    if (period < 1)
        return out;
    const double alpha = wilder ? 1.0 / period : 2.0 / (period + 1);
    std::optional<double> average;
    double sum = 0;
    int run = 0;
    for (qsizetype i = 0; i < values.size(); ++i) {
        if (!values[i]) {
            if (!average) {
                sum = 0;
                run = 0;
            }
            continue;
        }
        if (average) {
            *average += alpha * (*values[i] - *average);
            out[i] = average;
            continue;
        }
        sum += *values[i];
        if (++run == period) {
            average = sum / period;
            out[i] = average;
        }
    }
    return out;
}

/// The highest (``high``) or lowest of the last ``period`` values; none while any is none.
Values extreme_of(const Values& values, int period, bool high) {
    Values out(values.size());
    if (period < 1)
        return out;
    for (qsizetype i = period - 1; i < values.size(); ++i) {
        std::optional<double> e;
        for (qsizetype j = i - period + 1; j <= i; ++j) {
            if (!values[j]) {
                e.reset();
                break;
            }
            e = !e ? *values[j] : high ? std::max(*e, *values[j]) : std::min(*e, *values[j]);
        }
        out[i] = e;
    }
    return out;
}

/// ``a`` less ``b`` where both are.
Values difference(const Values& a, const Values& b) {
    Values out(a.size());
    for (qsizetype i = 0; i < a.size() && i < b.size(); ++i)
        if (a[i] && b[i])
            out[i] = *a[i] - *b[i];
    return out;
}

/// Where ``value`` lies from ``low`` (0) to ``high`` (100); none when they are one.
std::optional<double> place(double value, const std::optional<double>& low, const std::optional<double>& high) {
    if (!low || !high || *high <= *low)
        return std::nullopt;
    return 100 * (value - *low) / (*high - *low);
}

/// Each bar's (2 × close − high − low) / (high − low) × volume: where it closed within its range,
/// weighed by its volume (0 for a bar whose high is its low).
QList<double> money_flow_volumes(const QList<ChartBar>& bars) {
    QList<double> out;
    out.reserve(bars.size());
    for (const auto& b : bars)
        out << (b.high > b.low ? (2 * b.close - b.high - b.low) / (b.high - b.low) * b.volume : 0.0);
    return out;
}

/// A cumulative line of ``steps`` (each bar's) that is 0 at ``anchor``: after it the steps of the
/// bars up to each, before it less those of the bars after each up to it. A bar's own step counts
/// from the bar after the anchor on; the first bar's never does, so it may need no bar before.
Values anchored_sum(const QList<double>& steps, qsizetype anchor) {
    Values out(steps.size());
    if (steps.isEmpty())
        return out;
    anchor = std::clamp<qsizetype>(anchor, 0, steps.size() - 1);
    double total = 0;
    out[anchor] = 0.0;
    for (qsizetype i = anchor + 1; i < steps.size(); ++i)
        out[i] = total += steps[i];
    total = 0;
    for (qsizetype i = anchor - 1; i >= 0; --i)
        out[i] = total -= steps[i + 1];
    return out;
}

} // namespace

QString source_key(Source source) {
    switch (source) {
        case Source::Close:
            return QStringLiteral("close");
        case Source::Open:
            return QStringLiteral("open");
        case Source::High:
            return QStringLiteral("high");
        case Source::Low:
            return QStringLiteral("low");
        case Source::HL2:
            return QStringLiteral("hl2");
        case Source::HLC3:
            return QStringLiteral("hlc3");
    }
    return QStringLiteral("close");
}

std::optional<Source> parse_source(const QString& key) {
    for (const Source source : kSources)
        if (source_key(source) == key)
            return source;
    return std::nullopt;
}

QString source_title(Source source) {
    switch (source) {
        case Source::Close:
            return QStringLiteral("종가");
        case Source::Open:
            return QStringLiteral("시가");
        case Source::High:
            return QStringLiteral("고가");
        case Source::Low:
            return QStringLiteral("저가");
        case Source::HL2:
            return QStringLiteral("(고가+저가)/2");
        case Source::HLC3:
            return QStringLiteral("(고가+저가+종가)/3");
    }
    return {};
}

double price_of(const ChartBar& bar, Source source) {
    switch (source) {
        case Source::Close:
            return bar.close;
        case Source::Open:
            return bar.open;
        case Source::High:
            return bar.high;
        case Source::Low:
            return bar.low;
        case Source::HL2:
            return (bar.high + bar.low) / 2;
        case Source::HLC3:
            return (bar.high + bar.low + bar.close) / 3;
    }
    return bar.close;
}

QList<double> source_values(const QList<ChartBar>& bars, Source source) {
    QList<double> out;
    out.reserve(bars.size());
    for (const auto& b : bars)
        out << price_of(b, source);
    return out;
}

const StudySpec& study_spec(Study study) {
    static const QList<StudySpec> specs = make_specs();
    for (const auto& spec : specs)
        if (spec.study == study)
            return spec;
    return specs.first();
}

QString study_key(Study study) {
    return study_spec(study).key;
}

std::optional<Study> parse_study(const QString& key) {
    for (const Study study : kStudies)
        if (study_key(study) == key)
            return study;
    return std::nullopt;
}

QString study_title(Study study) {
    return study_spec(study).title;
}

bool StudySettings::operator==(const StudySettings& other) const {
    if (study != other.study || on != other.on || hidden != other.hidden || source != other.source ||
        inputs != other.inputs || lines.size() != other.lines.size())
        return false;
    for (qsizetype i = 0; i < lines.size(); ++i)
        if (lines[i].color != other.lines[i].color || lines[i].width != other.lines[i].width)
            return false;
    return true;
}

StudySettings default_settings(Study study) {
    const StudySpec& spec = study_spec(study);
    StudySettings s;
    s.study = study;
    s.source = spec.source;
    for (const auto& i : spec.inputs)
        s.inputs << i.value;
    s.lines = spec.lines;
    return s;
}

bool study_drawn(Study study, int view, bool bricks) {
    if (bricks)
        return study != Study::Vwap && study != Study::AnchoredVwap;
    return !(study_spec(study).minute_only && candle_view(view));
}

QString study_unavailable(Study study, int view, bool bricks) {
    if (study_drawn(study, view, bricks))
        return {};
    return bricks ? QStringLiteral("시간축 없는 차트에서는 쓰지 않는다") : QStringLiteral("분봉에서만");
}

QString settings_text(const StudySettings& settings) {
    const StudySpec& spec = study_spec(settings.study);
    QStringList parts;
    for (qsizetype i = 0; i < settings.inputs.size(); ++i) {
        const StudyInput& in = spec.periods ? spec.inputs.first() : spec.inputs.value(i);
        if (!in.choices.isEmpty()) { // by its name: 단순, 지수
            parts << in.choices.value(whole(settings.inputs[i]), in.choices.first());
            continue;
        }
        QString text = number_text(settings.inputs[i], in.decimals);
        if (in.title.endsWith(QStringLiteral("(%)"))) // the envelope's width, "폭(%)"
            text += '%';
        parts << text;
    }
    if (settings.source && settings.source != spec.source)
        parts << source_title(*settings.source);
    return parts.join(' ');
}

bool add_period(StudySettings& settings) {
    const StudySpec& spec = study_spec(settings.study);
    if (!spec.periods || settings.inputs.size() >= kMaxPeriods)
        return false;
    double next = 0;
    for (const double candidate : {5, 10, 20, 60, 120, 200, 240, 480})
        if (!settings.inputs.contains(candidate)) {
            next = candidate;
            break;
        }
    if (next == 0)
        next = std::min<double>(kMaxHistoryBars, settings.inputs.last() * 2);
    QStringList palette;
    for (const auto& line : spec.lines)
        palette << line.color;
    palette << extra_colors();
    QString color = palette.first();
    for (const QString& c : palette) {
        const bool used = std::any_of(settings.lines.begin(), settings.lines.end(), [&c](const StudyLine& line) {
            return line.color.compare(c, Qt::CaseInsensitive) == 0;
        });
        if (!used) {
            color = c;
            break;
        }
    }
    settings.inputs << next;
    settings.lines << StudyLine{number_text(next, 0), color, 1};
    return true;
}

bool remove_period(StudySettings& settings, int index) {
    if (!study_spec(settings.study).periods || index < 1 || index >= settings.inputs.size())
        return false;
    settings.inputs.removeAt(index);
    if (index < settings.lines.size())
        settings.lines.removeAt(index);
    return true;
}

const StudySettings& StudySet::of(Study study) const {
    for (const auto& s : studies)
        if (s.study == study)
            return s;
    static const QList<StudySettings> defaults = [] {
        QList<StudySettings> all;
        for (const Study s : kStudies)
            all << default_settings(s);
        return all;
    }();
    return defaults[static_cast<qsizetype>(study)];
}

StudySettings& StudySet::of(Study study) {
    for (auto& s : studies)
        if (s.study == study)
            return s;
    studies << default_settings(study);
    return studies.last();
}

void StudySet::turn(Study study, bool on) {
    StudySettings settings = of(study);
    if (settings.on == on)
        return;
    settings.on = on;
    if (!on)
        settings.hidden = false; // turned on again, it shows
    studies.removeIf([study](const StudySettings& s) { return s.study == study; });
    // After those on: last among them when turned on, first among the others when turned off.
    const auto first_off = std::find_if(studies.begin(), studies.end(), [](const StudySettings& s) { return !s.on; });
    studies.insert(first_off - studies.begin(), settings);
}

StudySet default_studies() {
    StudySet set;
    for (const Study study : {Study::MovingAverage, Study::Volume}) {
        StudySettings s = default_settings(study);
        s.on = true;
        set.studies << s;
    }
    for (const Study study : kStudies)
        if (study != Study::MovingAverage && study != Study::Volume)
            set.studies << default_settings(study);
    return set;
}

QString studies_value(const StudySet& set) {
    QJsonArray all;
    for (const auto& s : set.studies) {
        QJsonObject one{{"study", study_key(s.study)}, {"on", s.on}, {"hidden", s.hidden}};
        if (s.source)
            one.insert("source", source_key(*s.source));
        QJsonArray inputs;
        for (const double v : s.inputs)
            inputs.append(v);
        one.insert("inputs", inputs);
        QJsonArray lines;
        for (const auto& line : s.lines)
            lines.append(QJsonObject{{"color", line.color}, {"width", line.width}});
        one.insert("lines", lines);
        all.append(one);
    }
    return QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact));
}

StudySet parse_studies(const QVariant& kept) {
    if (!kept.isValid())
        return default_studies();
    const QJsonDocument doc = QJsonDocument::fromJson(kept.toString().toUtf8());
    if (!doc.isArray())
        return default_studies();
    const StudySet fresh = default_studies();
    StudySet set;
    for (const QJsonValue& value : doc.array()) {
        const QJsonObject object = value.toObject();
        const auto study = parse_study(object.value("study").toString());
        if (!study || std::any_of(set.studies.begin(), set.studies.end(),
                                  [&study](const StudySettings& s) { return s.study == *study; }))
            continue;
        const StudySpec& spec = study_spec(*study);
        StudySettings s = default_settings(*study);
        s.on = object.value("on").toBool();
        s.hidden = s.on && object.value("hidden").toBool();
        if (spec.source)
            if (const auto source = parse_source(object.value("source").toString()))
                s.source = source;
        const QJsonArray inputs = object.value("inputs").toArray();
        const auto fits = [](const StudyInput& in, const QJsonValue& v) {
            if (!v.isDouble())
                return false;
            const double d = v.toDouble();
            return d >= in.min && d <= in.max && (in.decimals > 0 || d == std::floor(d));
        };
        if (spec.periods) {
            QList<double> periods;
            for (const QJsonValue& v : inputs)
                if (fits(spec.inputs.first(), v) && periods.size() < kMaxPeriods)
                    periods << v.toDouble();
            if (!periods.isEmpty()) {
                s.inputs = periods;
                s.lines.clear();
                for (qsizetype i = 0; i < periods.size(); ++i) {
                    const QString color = i < spec.lines.size()
                                              ? spec.lines[i].color
                                              : extra_colors()[(i - spec.lines.size()) % extra_colors().size()];
                    s.lines << StudyLine{number_text(periods[i], 0), color, 1};
                }
            }
        } else {
            for (qsizetype i = 0; i < spec.inputs.size() && i < inputs.size(); ++i)
                if (fits(spec.inputs[i], inputs[i]))
                    s.inputs[i] = inputs[i].toDouble();
        }
        const QJsonArray lines = object.value("lines").toArray();
        for (qsizetype i = 0; i < s.lines.size() && i < lines.size(); ++i) {
            const QJsonObject line = lines[i].toObject();
            if (const QString color = line.value("color").toString(); valid_color(color))
                s.lines[i].color = color;
            const int width = line.value("width").toInt(-1);
            if (s.lines[i].width > 0 && width >= 1 && width <= 4)
                s.lines[i].width = width;
        }
        set.studies << s;
    }
    for (const auto& s : fresh.studies) // not kept (unknown to the version that kept them): fresh
        if (std::none_of(set.studies.begin(), set.studies.end(),
                         [&s](const StudySettings& k) { return k.study == s.study; }))
            set.studies << s;
    std::stable_partition(set.studies.begin(), set.studies.end(), [](const StudySettings& s) { return s.on; });
    return set;
}

QString pane_heights_value(const PaneHeights& heights) {
    QJsonObject all;
    for (auto it = heights.cbegin(); it != heights.cend(); ++it)
        all.insert(study_key(it.key()), it.value());
    return QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact));
}

PaneHeights parse_pane_heights(const QVariant& kept) {
    PaneHeights out;
    const QJsonObject all = QJsonDocument::fromJson(kept.toString().toUtf8()).object();
    for (auto it = all.begin(); it != all.end(); ++it) {
        const auto study = parse_study(it.key());
        const double height = it.value().toDouble(-1);
        if (study && !study_spec(*study).upper && it.value().isDouble() && height == std::floor(height) &&
            height >= kMinPaneHeight && height <= kMaxPaneHeight)
            out.insert(*study, static_cast<int>(height));
    }
    return out;
}

Values moving_average(const QList<double>& values, int period) {
    Values out(values.size());
    if (period < 1)
        return out;
    double sum = 0;
    for (qsizetype i = 0; i < values.size(); ++i) {
        sum += values[i];
        if (i >= period)
            sum -= values[i - period];
        if (i + 1 >= period)
            out[i] = sum / period;
    }
    return out;
}

Values exponential_average(const QList<double>& values, int period) {
    Values out(values.size());
    if (period < 1 || values.size() < period)
        return out;
    double sum = 0;
    for (qsizetype i = 0; i < period; ++i)
        sum += values[i];
    double ema = sum / period;
    out[period - 1] = ema;
    const double alpha = 2.0 / (period + 1);
    for (qsizetype i = period; i < values.size(); ++i) {
        ema += alpha * (values[i] - ema);
        out[i] = ema;
    }
    return out;
}

Values average_true_range(const QList<ChartBar>& bars, int period) {
    Values out(bars.size());
    if (period < 1 || bars.size() < period)
        return out;
    QList<double> ranges;
    ranges.reserve(bars.size());
    for (qsizetype i = 0; i < bars.size(); ++i) {
        const auto& b = bars[i];
        double range = b.high - b.low;
        if (i > 0)
            range = std::max({range, std::abs(b.high - bars[i - 1].close), std::abs(b.low - bars[i - 1].close)});
        ranges << range;
    }
    double atr = 0;
    for (qsizetype i = 0; i < period; ++i)
        atr += ranges[i];
    atr /= period;
    out[period - 1] = atr;
    for (qsizetype i = period; i < bars.size(); ++i) {
        atr = (atr * (period - 1) + ranges[i]) / period;
        out[i] = atr;
    }
    return out;
}

Band bollinger_band(const QList<double>& values, int period, double deviations) {
    Band band{moving_average(values, period), Values(values.size()), Values(values.size())};
    for (qsizetype i = 0; i < values.size(); ++i) {
        if (!band.middle[i])
            continue;
        const double mean = *band.middle[i];
        double squares = 0;
        for (qsizetype j = i - period + 1; j <= i; ++j)
            squares += (values[j] - mean) * (values[j] - mean);
        const double sd = std::sqrt(squares / period);
        band.upper[i] = mean + deviations * sd;
        band.lower[i] = mean - deviations * sd;
    }
    return band;
}

Band envelope_band(const QList<double>& values, int period, double percent) {
    Band band{moving_average(values, period), Values(values.size()), Values(values.size())};
    for (qsizetype i = 0; i < values.size(); ++i) {
        if (!band.middle[i])
            continue;
        band.upper[i] = *band.middle[i] * (1 + percent / 100);
        band.lower[i] = *band.middle[i] * (1 - percent / 100);
    }
    return band;
}

Band price_channel(const QList<ChartBar>& bars, int period) {
    Band band{Values(bars.size()), Values(bars.size()), Values(bars.size())};
    for (qsizetype i = 0; i < bars.size(); ++i) {
        band.upper[i] = extreme(bars, i, period, true);
        band.lower[i] = extreme(bars, i, period, false);
    }
    return band;
}

Ichimoku ichimoku_cloud(const QList<ChartBar>& bars, int conversion, int base, int lead, int displacement) {
    const qsizetype n = bars.size();
    const qsizetype shift = std::max(0, displacement);
    Ichimoku out{Values(n), Values(n), Values(n), Values(n + shift), Values(n + shift)};
    for (qsizetype i = 0; i < n; ++i) {
        out.conversion[i] = midpoint(bars, i, conversion);
        out.base[i] = midpoint(bars, i, base);
        if (i + shift < n)
            out.lagging[i] = bars[i + shift].close;
        if (out.conversion[i] && out.base[i])
            out.lead_a[i + shift] = (*out.conversion[i] + *out.base[i]) / 2;
        out.lead_b[i + shift] = midpoint(bars, i, lead);
    }
    return out;
}

Supertrend supertrend_lines(const QList<ChartBar>& bars, int atr_period, double multiplier) {
    const qsizetype n = bars.size();
    Supertrend out{Values(n), Values(n)};
    const Values atr = average_true_range(bars, atr_period);
    std::optional<double> lower_before; // the bands as they ended on the bar before (none: no ATR yet)
    std::optional<double> upper_before;
    std::optional<double> trend_before; // the supertrend on the bar before
    for (qsizetype i = 0; i < n; ++i) {
        if (!atr[i])
            continue;
        const auto& b = bars[i];
        const double middle = (b.high + b.low) / 2;
        double upper = middle + multiplier * *atr[i];
        double lower = middle - multiplier * *atr[i];
        // The bands tighten only while the close before stayed inside them. The first bar with an
        // ATR takes them as they are (TradingView's nz() of none, without its 0 on the very first bar).
        if (lower_before && upper_before) {
            const double close_before = bars[i - 1].close;
            if (!(lower > *lower_before || close_before < *lower_before))
                lower = *lower_before;
            if (!(upper < *upper_before || close_before > *upper_before))
                upper = *upper_before;
        }
        int direction = 1; // 1: down, −1: up (TradingView's)
        if (i == 0 || !atr[i - 1])
            direction = 1;
        else if (trend_before && upper_before && *trend_before == *upper_before)
            direction = b.close > upper ? -1 : 1;
        else
            direction = b.close < lower ? 1 : -1;
        const double trend = direction == -1 ? lower : upper;
        (direction == -1 ? out.up : out.down)[i] = trend;
        lower_before = lower;
        upper_before = upper;
        trend_before = trend;
    }
    return out;
}

QList<Fractal> williams_fractals(const QList<ChartBar>& bars, int period) {
    QList<Fractal> out;
    if (period < 1)
        return out;
    for (qsizetype i = period; i + period < bars.size(); ++i) {
        bool up = true;
        bool down = true;
        for (qsizetype j = i - period; j <= i + period; ++j) {
            if (j == i)
                continue;
            up = up && bars[j].high < bars[i].high;
            down = down && bars[j].low > bars[i].low;
        }
        if (up)
            out << Fractal{i, true};
        if (down)
            out << Fractal{i, false};
    }
    return out;
}

Values parabolic_sar(const QList<ChartBar>& bars, double start, double step, double maximum) {
    Values out(bars.size());
    double result = 0;
    double extreme_point = 0;
    double acceleration = start;
    bool below = false; // the SAR is under the prices: an uptrend
    for (qsizetype i = 1; i < bars.size(); ++i) {
        const auto& b = bars[i];
        bool first = false;
        if (i == 1) {
            below = b.close > bars[0].close;
            extreme_point = below ? b.high : b.low;
            result = below ? bars[0].low : bars[0].high;
            first = true;
            acceleration = start;
        }
        result += acceleration * (extreme_point - result);
        if (below) {
            if (result > b.low) {
                first = true;
                below = false;
                result = std::max(b.high, extreme_point);
                extreme_point = b.low;
                acceleration = start;
            }
        } else if (result < b.high) {
            first = true;
            below = true;
            result = std::min(b.low, extreme_point);
            extreme_point = b.high;
            acceleration = start;
        }
        if (!first) {
            if (below && b.high > extreme_point) {
                extreme_point = b.high;
                acceleration = std::min(acceleration + step, maximum);
            } else if (!below && b.low < extreme_point) {
                extreme_point = b.low;
                acceleration = std::min(acceleration + step, maximum);
            }
        }
        if (below) {
            result = std::min(result, bars[i - 1].low);
            if (i > 1)
                result = std::min(result, bars[i - 2].low);
        } else {
            result = std::max(result, bars[i - 1].high);
            if (i > 1)
                result = std::max(result, bars[i - 2].high);
        }
        out[i] = result;
    }
    return out;
}

Values mass_index(const QList<ChartBar>& bars, int ema_period, int sum_period) {
    Values ranges;
    ranges.reserve(bars.size());
    for (const auto& b : bars)
        ranges << b.high - b.low;
    const Values single = smoothed(ranges, ema_period, false);
    const Values twice = smoothed(single, ema_period, false);
    Values ratio(bars.size());
    for (qsizetype i = 0; i < bars.size(); ++i)
        if (single[i] && twice[i] && *twice[i] != 0)
            ratio[i] = *single[i] / *twice[i];
    return sum_of(ratio, sum_period);
}

Values momentum(const QList<double>& values, int period) {
    Values out(values.size());
    for (qsizetype i = std::max(1, period); i < values.size(); ++i)
        out[i] = values[i] - values[i - period];
    return out;
}

Values volume_oscillator(const QList<ChartBar>& bars, int short_period, int long_period) {
    Values volumes;
    volumes.reserve(bars.size());
    for (const auto& b : bars)
        volumes << b.volume;
    const Values fast = smoothed(volumes, short_period, false);
    const Values slow = smoothed(volumes, long_period, false);
    Values out(bars.size());
    for (qsizetype i = 0; i < bars.size(); ++i)
        if (fast[i] && slow[i] && *slow[i] != 0)
            out[i] = 100 * (*fast[i] - *slow[i]) / *slow[i];
    return out;
}

Values percent_b(const QList<double>& values, int period, double deviations) {
    const Band band = bollinger_band(values, period, deviations);
    Values out(values.size());
    for (qsizetype i = 0; i < values.size(); ++i)
        if (band.upper[i] && band.lower[i] && *band.upper[i] > *band.lower[i])
            out[i] = (values[i] - *band.lower[i]) / (*band.upper[i] - *band.lower[i]);
    return out;
}

Values band_width(const QList<double>& values, int period, double deviations) {
    const Band band = bollinger_band(values, period, deviations);
    Values out(values.size());
    for (qsizetype i = 0; i < values.size(); ++i)
        if (band.middle[i] && *band.middle[i] != 0)
            out[i] = 100 * (*band.upper[i] - *band.lower[i]) / *band.middle[i];
    return out;
}

SignalLines stochastic(const QList<ChartBar>& bars, int period, int k_smooth, int d_smooth) {
    Values raw(bars.size());
    for (qsizetype i = 0; i < bars.size(); ++i)
        raw[i] = place(bars[i].close, extreme(bars, i, period, false), extreme(bars, i, period, true));
    SignalLines out;
    out.line = average_of(raw, k_smooth);
    out.signal = average_of(out.line, d_smooth);
    return out;
}

SignalLines stochastic_rsi(const QList<double>& values, int rsi_period, int period, int k_smooth, int d_smooth) {
    const Values rsi = relative_strength(values, rsi_period);
    const Values low = extreme_of(rsi, period, false);
    const Values high = extreme_of(rsi, period, true);
    Values raw(values.size());
    for (qsizetype i = 0; i < values.size(); ++i)
        if (rsi[i])
            raw[i] = place(*rsi[i], low[i], high[i]);
    SignalLines out;
    out.line = average_of(raw, k_smooth);
    out.signal = average_of(out.line, d_smooth);
    return out;
}

Values williams_r(const QList<ChartBar>& bars, int period) {
    Values out(bars.size());
    for (qsizetype i = 0; i < bars.size(); ++i)
        if (const auto at = place(bars[i].close, extreme(bars, i, period, false), extreme(bars, i, period, true)))
            out[i] = *at - 100; // −100 × (high − close) / (high − low)
    return out;
}

Values disparity(const QList<double>& values, int period) {
    const Values average = moving_average(values, period);
    Values out(values.size());
    for (qsizetype i = 0; i < values.size(); ++i)
        if (average[i] && *average[i] != 0)
            out[i] = 100 * values[i] / *average[i];
    return out;
}

Values intraday_intensity(const QList<ChartBar>& bars, int period) {
    Values volumes;
    volumes.reserve(bars.size());
    for (const auto& b : bars)
        volumes << b.volume;
    const Values weighed = sum_of(to_values(money_flow_volumes(bars)), period);
    const Values total = sum_of(volumes, period);
    Values out(bars.size());
    for (qsizetype i = 0; i < bars.size(); ++i)
        if (weighed[i] && total[i] && *total[i] > 0)
            out[i] = 100 * *weighed[i] / *total[i];
    return out;
}

std::optional<qint64> cumulative_zero(const QList<ChartBar>& bars, int view) {
    if (bars.isEmpty())
        return std::nullopt;
    const QDate last = date_of(bars.last().time);
    return time_of(candle_view(view) ? QDate(last.year(), 1, 1) : last);
}

QDateTime cumulative_start(const QList<Bar>& bars, int view) {
    if (bars.isEmpty())
        return {};
    const QTimeZone kst = QTimeZone::fromSecondsAheadOfUtc(9 * 3600);
    const QDate last = bars.last().start.toTimeZone(kst).date();
    return QDateTime(candle_view(view) ? QDate(last.year(), 1, 1) : last, QTime(0, 0), kst);
}

std::optional<qsizetype> cumulative_anchor(const QList<ChartBar>& bars, int view, bool start_whole) {
    const auto zero = cumulative_zero(bars, view);
    if (!zero)
        return std::nullopt;
    // The bars from the moment on are all held once a bar before it is (or one starting at it),
    // or the engine has none before the first held.
    if (bars.first().time > *zero && !start_whole)
        return std::nullopt;
    for (qsizetype i = 0; i < bars.size(); ++i)
        if (bars[i].time >= *zero)
            return i;
    return std::nullopt; // not reached: the last bar is from the moment on
}

Values accumulation_distribution(const QList<ChartBar>& bars, qsizetype anchor) {
    return anchored_sum(money_flow_volumes(bars), anchor);
}

Values chaikin_oscillator(const QList<ChartBar>& bars, int short_period, int long_period) {
    const Values line = accumulation_distribution(bars, 0);
    return difference(smoothed(line, short_period, false), smoothed(line, long_period, false));
}

SignalLines trix(const QList<double>& values, int period, int signal) {
    const Values triple = smoothed(smoothed(smoothed(to_values(values), period, false), period, false), period, false);
    SignalLines out;
    out.line = Values(values.size());
    for (qsizetype i = 1; i < values.size(); ++i)
        if (triple[i] && triple[i - 1] && *triple[i - 1] != 0)
            out.line[i] = 100 * (*triple[i] - *triple[i - 1]) / *triple[i - 1];
    out.signal = smoothed(out.line, signal, false);
    return out;
}

Values price_oscillator(const QList<double>& values, int short_period, int long_period, bool exponential) {
    const Values fast =
        exponential ? smoothed(to_values(values), short_period, false) : moving_average(values, short_period);
    const Values slow =
        exponential ? smoothed(to_values(values), long_period, false) : moving_average(values, long_period);
    Values out(values.size());
    for (qsizetype i = 0; i < values.size(); ++i)
        if (fast[i] && slow[i] && *slow[i] != 0)
            out[i] = 100 * (*fast[i] - *slow[i]) / *slow[i];
    return out;
}

Directional directional_movement(const QList<ChartBar>& bars, int period) {
    const qsizetype n = bars.size();
    Values plus_moves(n);
    Values minus_moves(n);
    Values ranges(n);
    for (qsizetype i = 1; i < n; ++i) { // each needs the bar before
        const auto& b = bars[i];
        const auto& before = bars[i - 1];
        const double up = b.high - before.high;
        const double down = before.low - b.low;
        plus_moves[i] = up > down && up > 0 ? up : 0.0;
        minus_moves[i] = down > up && down > 0 ? down : 0.0;
        ranges[i] = std::max({b.high - b.low, std::abs(b.high - before.close), std::abs(b.low - before.close)});
    }
    const Values plus = smoothed(plus_moves, period, true);
    const Values minus = smoothed(minus_moves, period, true);
    const Values range = smoothed(ranges, period, true);
    Directional out{Values(n), Values(n), {}};
    Values dx(n);
    for (qsizetype i = 0; i < n; ++i) {
        if (!plus[i] || !minus[i] || !range[i] || *range[i] <= 0)
            continue;
        const double p = 100 * *plus[i] / *range[i];
        const double m = 100 * *minus[i] / *range[i];
        out.plus[i] = p;
        out.minus[i] = m;
        dx[i] = 100 * std::abs(p - m) / (p + m == 0 ? 1 : p + m); // as TradingView's
    }
    out.adx = smoothed(dx, period, true);
    return out;
}

Values commodity_channel(const QList<double>& values, int period) {
    const Values average = moving_average(values, period);
    Values out(values.size());
    for (qsizetype i = 0; i < values.size(); ++i) {
        if (!average[i])
            continue;
        double deviation = 0;
        for (qsizetype j = i - period + 1; j <= i; ++j)
            deviation += std::abs(values[j] - *average[i]);
        deviation /= period;
        if (deviation > 0)
            out[i] = (values[i] - *average[i]) / (0.015 * deviation);
    }
    return out;
}

SignalLines macd(const QList<double>& values, int fast, int slow, int signal) {
    const Values all = to_values(values);
    SignalLines out;
    out.line = difference(smoothed(all, fast, false), smoothed(all, slow, false));
    out.signal = smoothed(out.line, signal, false);
    out.histogram = difference(out.line, out.signal);
    return out;
}

Values money_flow(const QList<ChartBar>& bars, const QList<double>& prices, int period) {
    const qsizetype n = std::min(bars.size(), prices.size());
    Values rising(n);
    Values falling(n);
    for (qsizetype i = 1; i < n; ++i) { // each needs the bar before
        const double flow = prices[i] * bars[i].volume;
        rising[i] = prices[i] > prices[i - 1] ? flow : 0.0;
        falling[i] = prices[i] < prices[i - 1] ? flow : 0.0;
    }
    const Values up = sum_of(rising, period);
    const Values down = sum_of(falling, period);
    Values out(n);
    for (qsizetype i = 0; i < n; ++i)
        if (up[i] && down[i] && *up[i] + *down[i] > 0)
            out[i] = 100 * *up[i] / (*up[i] + *down[i]);
    return out;
}

Values on_balance_volume(const QList<ChartBar>& bars, qsizetype anchor) {
    QList<double> steps;
    steps.reserve(bars.size());
    for (qsizetype i = 0; i < bars.size(); ++i) {
        const double change = i > 0 ? bars[i].close - bars[i - 1].close : 0;
        steps << (change > 0 ? bars[i].volume : change < 0 ? -bars[i].volume : 0.0);
    }
    return anchored_sum(steps, anchor);
}

Values rate_of_change(const QList<double>& values, int period) {
    Values out(values.size());
    for (qsizetype i = std::max(1, period); i < values.size(); ++i)
        if (values[i - period] != 0)
            out[i] = 100 * (values[i] - values[i - period]) / values[i - period];
    return out;
}

Values relative_strength(const QList<double>& values, int period) {
    return relative_momentum(values, period, 1);
}

Values relative_momentum(const QList<double>& values, int period, int momentum) {
    const qsizetype n = values.size();
    Values rises(n);
    Values falls(n);
    for (qsizetype i = std::max(1, momentum); i < n; ++i) {
        const double change = values[i] - values[i - momentum];
        rises[i] = std::max(0.0, change);
        falls[i] = std::max(0.0, -change);
    }
    const Values up = smoothed(rises, period, true);
    const Values down = smoothed(falls, period, true);
    Values out(n);
    for (qsizetype i = 0; i < n; ++i)
        if (up[i] && down[i]) // as TradingView's: no falls 100, no rises 0
            out[i] = *down[i] == 0 ? 100 : *up[i] == 0 ? 0 : 100 - 100 / (1 + *up[i] / *down[i]);
    return out;
}

Values session_vwap(const QList<ChartBar>& bars, Source source, bool first_day_whole) {
    Values out(bars.size());
    double priced = 0;
    double volume = 0;
    qsizetype i = 0;
    if (!first_day_whole) // the first date held may be read from its middle: left empty
        while (i < bars.size() && day_of(bars[i].time) == day_of(bars.first().time))
            ++i;
    for (; i < bars.size(); ++i) {
        if (i > 0 && day_of(bars[i].time) != day_of(bars[i - 1].time)) {
            priced = 0;
            volume = 0;
        }
        priced += price_of(bars[i], source) * bars[i].volume;
        volume += bars[i].volume;
        if (volume > 0)
            out[i] = priced / volume;
    }
    return out;
}

Values anchored_vwap(const QList<ChartBar>& bars, Source source, qsizetype anchor) {
    Values out(bars.size());
    double priced = 0;
    double volume = 0;
    for (qsizetype i = std::max<qsizetype>(0, anchor); i < bars.size(); ++i) {
        priced += price_of(bars[i], source) * bars[i].volume;
        volume += bars[i].volume;
        if (volume > 0)
            out[i] = priced / volume;
    }
    return out;
}

std::optional<qsizetype> anchor_index(const QList<ChartBar>& bars, qint64 anchor, int view) {
    if (bars.isEmpty())
        return std::nullopt;
    qint64 start = 0;
    if (!candle_view(view)) {
        const qint64 slot = std::max(1, view) * 60;
        start = anchor - ((anchor % slot) + slot) % slot;
    } else {
        start = time_of(period_start(date_of(anchor), view));
    }
    if (start < bars.first().time)
        return std::nullopt; // its bar is not read
    for (qsizetype i = 0; i < bars.size(); ++i)
        if (bars[i].time >= start)
            return i;
    return std::nullopt;
}

VolumeProfile volume_profile(const QList<ChartBar>& bars, qint64 from, qint64 to, int rows) {
    VolumeProfile out;
    QList<ChartBar> shown;
    for (const auto& b : bars)
        if (b.time >= from && b.time <= to)
            shown << b;
    if (shown.isEmpty() || rows < 1)
        return out;
    out.low = shown.first().low;
    out.high = shown.first().high;
    for (const auto& b : shown) {
        out.low = std::min(out.low, b.low);
        out.high = std::max(out.high, b.high);
    }
    out.volumes = QList<double>(rows, 0.0);
    const double height = (out.high - out.low) / rows;
    for (const auto& b : shown) {
        if (height <= 0) { // every bar at one price
            out.volumes[0] += b.volume;
            continue;
        }
        if (b.high <= b.low) {
            const int row = std::clamp(static_cast<int>(std::floor((b.close - out.low) / height)), 0, rows - 1);
            out.volumes[row] += b.volume;
            continue;
        }
        const int first = std::clamp(static_cast<int>(std::floor((b.low - out.low) / height)), 0, rows - 1);
        const int last = std::clamp(static_cast<int>(std::floor((b.high - out.low) / height)), 0, rows - 1);
        for (int r = first; r <= last; ++r) {
            const double lo = out.low + r * height;
            const double hi = r == rows - 1 ? out.high : lo + height;
            const double overlap = std::min(b.high, hi) - std::max(b.low, lo);
            if (overlap > 0)
                out.volumes[r] += b.volume * overlap / (b.high - b.low);
        }
    }
    return out;
}

QList<qint64> future_times(const QList<ChartBar>& bars, int view, int count) {
    QList<qint64> out;
    if (bars.isEmpty() || count <= 0)
        return out;
    const qint64 last = bars.last().time;
    if (!candle_view(view)) {
        for (int k = 1; k <= count; ++k)
            out << last + qint64(k) * std::max(1, view) * 60;
        return out;
    }
    QDate date = date_of(last);
    for (int k = 0; k < count; ++k) {
        switch (view) {
            case kWeekView:
                date = date.addDays(7);
                break;
            case kMonthView:
                date = QDate(date.year(), date.month(), 1).addMonths(1);
                break;
            case kYearView:
                date = QDate(date.year() + 1, 1, 1);
                break;
            default: // the next weekday
                do
                    date = date.addDays(1);
                while (date.dayOfWeek() > 5);
        }
        out << time_of(date);
    }
    return out;
}

int warmup(const StudySettings& settings) {
    const QList<double>& in = settings.inputs;
    const auto at = [&in](qsizetype i) { return i < in.size() ? whole(in[i]) : 0; };
    switch (settings.study) {
        case Study::MovingAverage:
        case Study::ExponentialAverage: {
            int longest = 0;
            for (const double p : in)
                longest = std::max(longest, whole(p));
            // An EMA weighs every bar before: 4 periods leave about e^-8 of the weight further back.
            return settings.study == Study::ExponentialAverage ? 4 * longest : std::max(0, longest - 1);
        }
        case Study::Ichimoku:
            return std::max(0, std::max({at(0), at(1), at(2)}) - 1 + at(3));
        case Study::Bollinger:
        case Study::Envelope:
        case Study::PriceChannel:
            return std::max(0, at(0) - 1);
        case Study::Supertrend: // Wilder's ATR and the trend carry every bar before
            return 10 * std::max(1, at(0));
        case Study::Fractal:
            return std::max(0, at(0));
        case Study::ParabolicSar: // its trend and extreme carry every bar before
            return 100;
        case Study::VolumeProfile:
        case Study::Vwap:
        case Study::AnchoredVwap:
        case Study::Volume:
            return 0;
        // The lower ones (krx-quant #223): their steps added up. An EMA leaves about e^-8 of its
        // weight beyond 4 periods, Wilder's average (1 / period) beyond 8.
        case Study::MassIndex: // two EMAs, then a sum
            return 4 * at(0) * 2 + std::max(0, at(1) - 1);
        case Study::Momentum:
        case Study::Roc:
            return at(0);
        case Study::VolumeOscillator:
        case Study::ChaikinOscillator: // the AD line itself needs none (cumulative_anchor)
            return 4 * std::max(at(0), at(1));
        case Study::PercentB:
        case Study::BandWidth:
        case Study::WilliamsR:
        case Study::Disparity:
        case Study::IntradayIntensity:
        case Study::Cci:
            return std::max(0, at(0) - 1);
        case Study::Stochastic:
            return std::max(0, at(0) - 1) + std::max(0, at(1) - 1) + std::max(0, at(2) - 1);
        case Study::StochasticRsi:
            return 8 * at(0) + std::max(0, at(1) - 1) + std::max(0, at(2) - 1) + std::max(0, at(3) - 1);
        case Study::Trix: // three EMAs, a change, then the signal's EMA
            return 4 * (3 * at(0) + at(1));
        case Study::PriceOscillator:
            return at(2) == 1 ? 4 * std::max(at(0), at(1)) : std::max(0, std::max(at(0), at(1)) - 1);
        case Study::AdLine:
        case Study::Obv:
            return 0; // 0 at a bar of their own (cumulative_anchor)
        case Study::Adx:
        case Study::Dmi: // Wilder's +DI and −DI, then Wilder's ADX of them
            return 8 * 2 * at(0);
        case Study::Atr:
        case Study::Rsi:
            return 8 * at(0);
        case Study::Macd:
            return 4 * std::max(at(0), at(1)) + 4 * at(2);
        case Study::Mfi:
            return at(0);
        case Study::Rmi:
            return at(1) + 8 * at(0);
    }
    return 0;
}

int history_bars(const StudySet& set, int view) {
    int longest = 0;
    for (const auto& s : set.studies)
        if (s.on && !(study_spec(s.study).minute_only && candle_view(view)))
            longest = std::max(longest, warmup(s));
    return longest == 0 ? 0 : std::min(kFitBars + longest, kMaxHistoryBars);
}

std::optional<OlderRead> history_read(const QDateTime& first, int have, int wanted, int minutes,
                                      const QDateTime& back_to) {
    if (!first.isValid() || have >= kMaxHistoryBars)
        return std::nullopt;
    if (have >= wanted && !(back_to.isValid() && back_to < first))
        return std::nullopt;
    return older_read(first, 0, minutes); // at the left end: the bars before the first
}

QDateTime first_screen_day(const QList<Bar>& bars, int minutes) {
    const QList<Bar> view = group_bars(bars, minutes);
    if (view.isEmpty())
        return {};
    const QTimeZone kst = QTimeZone::fromSecondsAheadOfUtc(9 * 3600);
    const QDate day = view[std::max<qsizetype>(0, view.size() - kFitBars)].start.toTimeZone(kst).date();
    return QDateTime(day, QTime(0, 0), kst);
}

bool anchor_pickable(const StudySet& set) {
    return set.on(Study::AnchoredVwap) && !set.of(Study::AnchoredVwap).hidden;
}

std::optional<QDateTime> history_candles(const QDateTime& first, int have, int wanted, const QDateTime& back_to) {
    if (!first.isValid() || have >= kMaxHistoryBars)
        return std::nullopt;
    if (have >= wanted && !(back_to.isValid() && back_to < first))
        return std::nullopt;
    return first;
}

QString css_color(const QString& color, double alpha) {
    if (!valid_color(color))
        return QStringLiteral("rgba(128,128,128,%1)").arg(alpha);
    bool ok = false;
    const uint rgb = color.mid(1).toUInt(&ok, 16);
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg((rgb >> 16) & 0xff)
        .arg((rgb >> 8) & 0xff)
        .arg(rgb & 0xff)
        .arg(alpha);
}

namespace {

/// A lower indicator's lines (krx-quant #223) over ``bars``: its values, each with its look's
/// index and its id's end ("" for its only line).
struct LowerLine {
    QString name;
    int look = 0;
    Values values;
    bool histogram = false;
};

/// ``zero`` is the cumulative lines' bar (cumulative_anchor); none: they are empty.
QList<LowerLine> lower_lines(const QList<ChartBar>& bars, const StudySettings& s, std::optional<qsizetype> zero) {
    const auto in = [&s](qsizetype i) { return i < s.inputs.size() ? whole(s.inputs[i]) : 0; };
    const QList<double> values = source_values(bars, s.source.value_or(Source::Close));
    switch (s.study) {
        case Study::MassIndex:
            return {{"", 0, mass_index(bars, in(0), in(1))}};
        case Study::Momentum:
            return {{"", 0, momentum(values, in(0))}};
        case Study::VolumeOscillator:
            return {{"", 0, volume_oscillator(bars, in(0), in(1))}};
        case Study::PercentB:
            return {{"", 0, percent_b(values, in(0), s.inputs.value(1))}};
        case Study::BandWidth:
            return {{"", 0, band_width(values, in(0), s.inputs.value(1))}};
        case Study::Stochastic: {
            const SignalLines k = stochastic(bars, in(0), in(1), in(2));
            return {{"k", 0, k.line}, {"d", 1, k.signal}};
        }
        case Study::StochasticRsi: {
            const SignalLines k = stochastic_rsi(values, in(0), in(1), in(2), in(3));
            return {{"k", 0, k.line}, {"d", 1, k.signal}};
        }
        case Study::WilliamsR:
            return {{"", 0, williams_r(bars, in(0))}};
        case Study::Disparity:
            return {{"", 0, disparity(values, in(0))}};
        case Study::IntradayIntensity:
            return {{"", 0, intraday_intensity(bars, in(0))}};
        case Study::ChaikinOscillator:
            return {{"", 0, chaikin_oscillator(bars, in(0), in(1))}};
        case Study::Trix: {
            const SignalLines t = trix(values, in(0), in(1));
            return {{"trix", 0, t.line}, {"signal", 1, t.signal}};
        }
        case Study::PriceOscillator:
            return {{"", 0, price_oscillator(values, in(0), in(1), in(2) == 1)}};
        case Study::AdLine:
            return {{"", 0, zero ? accumulation_distribution(bars, *zero) : Values(bars.size())}};
        case Study::Adx:
            return {{"", 0, directional_movement(bars, in(0)).adx}};
        case Study::Atr:
            return {{"", 0, average_true_range(bars, in(0))}};
        case Study::Cci:
            return {{"", 0, commodity_channel(values, in(0))}};
        case Study::Dmi: {
            const Directional d = directional_movement(bars, in(0));
            return {{"plus", 0, d.plus}, {"minus", 1, d.minus}, {"adx", 2, d.adx}};
        }
        case Study::Macd: {
            const SignalLines m = macd(values, in(0), in(1), in(2));
            return {{"histogram", 2, m.histogram, true}, {"macd", 0, m.line}, {"signal", 1, m.signal}};
        }
        case Study::Mfi:
            return {{"", 0, money_flow(bars, values, in(0))}};
        case Study::Obv:
            return {{"", 0, zero ? on_balance_volume(bars, *zero) : Values(bars.size())}};
        case Study::Roc:
            return {{"", 0, rate_of_change(values, in(0))}};
        case Study::Rsi:
            return {{"", 0, relative_strength(values, in(0))}};
        case Study::Rmi:
            return {{"", 0, relative_momentum(values, in(0), in(1))}};
        default:
            return {};
    }
}

} // namespace

StudyDrawing draw_studies(const QList<ChartBar>& bars, const StudySet& set, int view, std::optional<qint64> anchor,
                          bool picking, bool first_day_whole, const PaneHeights& heights, bool bricks) {
    StudyDrawing out;
    const qsizetype n = bars.size();
    // The AD line's and the OBV's 0 (krx-quant #223): empty until the bars from its moment on are
    // all held, as the VWAP's day; past the read's cap, saying so. On bricks (krx-quant #225) the
    // first brick: their start is fixed, so the bars read before it change nothing.
    const auto zero = bricks ? (bars.isEmpty() ? std::nullopt : std::optional<qsizetype>(0))
                             : cumulative_anchor(bars, view, first_day_whole);
    if (bricks) { // a slot a minute: what reaches past the last brick takes the next slots
        view = 1;
        anchor.reset();
        picking = false;
    }
    const QString since = candle_view(view) ? QStringLiteral("그해") : QStringLiteral("그날");
    const QString unread = zero || bars.isEmpty() ? QString()
                           : n >= kMaxHistoryBars ? QStringLiteral("%1 첫 봉이 %2봉보다 앞이라 비움")
                                                        .arg(since, QLocale(QLocale::English).toString(kMaxHistoryBars))
                                                  : QStringLiteral("%1 첫 봉을 읽지 않아 비움").arg(since);
    QList<qint64> future; // the times past the last bar, as many as the lines reach
    const auto slot_time = [&](qsizetype slot) {
        if (slot < n)
            return bars[slot].time;
        if (future.size() < slot - n + 1)
            future = future_times(bars, view, static_cast<int>(slot - n + 1));
        return future[slot - n];
    };
    // A line's points up to its last value, from its first (``shift`` slots on: the lagging
    // span's are drawn back, so its first value is at slot 0 of what it holds).
    const auto line = [&](const QString& id, Study study, const StudyLine& look, const Values& values, int live = 0,
                          bool dots = false) {
        DrawnSeries s;
        s.id = id;
        s.study = study;
        s.color = look.color;
        s.width = std::max(1, look.width);
        s.dots = dots;
        s.live = live;
        qsizetype first = 0;
        qsizetype last = values.size() - 1;
        while (first <= last && !values[first])
            ++first;
        while (last >= first && !values[last])
            --last;
        for (qsizetype i = first; i <= last; ++i)
            s.points << std::pair{slot_time(i), values[i]};
        out.series << s;
        return id;
    };
    for (const StudySettings& s : set.studies) {
        if (!s.on)
            continue;
        const StudySpec& spec = study_spec(s.study);
        if (!study_drawn(s.study, view, bricks))
            continue; // the VWAP in a candle view, either VWAP on bricks: kept, not drawn
        LegendRow row;
        row.study = s.study;
        row.title = spec.title;
        row.settings = settings_text(s);
        row.hidden = s.hidden;
        const auto value = [&row](const QString& id, const QString& color) {
            row.values << id;
            row.colors << color;
        };
        if (!spec.upper) {
            // A pane of its own below the prices, in the order turned on (krx-quant #223); hidden,
            // it stays with its legend row and draws nothing.
            out.panes << DrawnPane{s.study, heights.value(s.study, kPaneHeight), s.hidden, spec.guides};
            row.pane = static_cast<int>(out.panes.size());
            if (s.hidden) {
                out.legend << row;
                continue;
            }
            if (s.study == Study::Volume) { // its bars are the page's own, sent with the bars
                value(QStringLiteral("volume"), QString());
            } else {
                // A histogram is sent first, so its lines are drawn over it, but shown last.
                QStringList bars_ids;
                QStringList bars_colors;
                if (s.study == Study::AdLine || s.study == Study::Obv)
                    row.missing = unread;
                for (const LowerLine& l : lower_lines(bars, s, zero)) {
                    const QString id = l.name.isEmpty() ? spec.key : spec.key + '.' + l.name;
                    const StudyLine look = s.lines.value(l.look);
                    line(id, s.study, look, l.values);
                    DrawnSeries& drawn = out.series.last();
                    drawn.pane = row.pane;
                    drawn.decimals = spec.decimals;
                    drawn.volume_scale = spec.volume_scale;
                    if (!l.histogram) {
                        value(id, look.color);
                        continue;
                    }
                    drawn.histogram = true;
                    drawn.down_color = s.lines.value(l.look + 1).color;
                    bars_ids << id;
                    bars_colors << look.color;
                }
                row.values << bars_ids;
                row.colors << bars_colors;
            }
            out.legend << row;
            continue;
        }
        const Source source = s.source.value_or(Source::Close);
        const auto in = [&s](qsizetype i) { return i < s.inputs.size() ? s.inputs[i] : 0.0; };
        const auto look = [&s](qsizetype i) { return s.lines.value(i); };
        if (s.study == Study::AnchoredVwap) {
            if (anchor)
                row.settings = QDateTime::fromSecsSinceEpoch(*anchor, QTimeZone::UTC).toString("MM-dd HH:mm");
            if (picking && !s.hidden) // hidden, nothing of it waits for a click
                row.note = QStringLiteral("차트를 눌러 기준 봉을 고르세요");
        }
        if (s.hidden) {
            out.legend << row;
            continue;
        }
        const QString key = spec.key;
        switch (s.study) {
            case Study::MovingAverage:
            case Study::ExponentialAverage: {
                const QList<double> values = source_values(bars, source);
                for (qsizetype i = 0; i < s.inputs.size(); ++i) {
                    const int p = whole(s.inputs[i]);
                    const Values v =
                        s.study == Study::MovingAverage ? moving_average(values, p) : exponential_average(values, p);
                    value(line(QStringLiteral("%1.%2").arg(key).arg(i), s.study, look(i), v), look(i).color);
                }
                break;
            }
            case Study::Ichimoku: {
                const int shift = whole(in(3));
                const Ichimoku c = ichimoku_cloud(bars, whole(in(0)), whole(in(1)), whole(in(2)), shift);
                value(line(key + ".conversion", s.study, look(0), c.conversion), look(0).color);
                value(line(key + ".base", s.study, look(1), c.base), look(1).color);
                value(line(key + ".lagging", s.study, look(2), c.lagging, -shift), look(2).color);
                value(line(key + ".lead_a", s.study, look(3), c.lead_a, shift), look(3).color);
                value(line(key + ".lead_b", s.study, look(4), c.lead_b, shift), look(4).color);
                out.fills << DrawnFill{key + ".lead_a", key + ".lead_b", css_color(look(3).color, 0.18),
                                       css_color(look(4).color, 0.18)};
                break;
            }
            case Study::Bollinger:
            case Study::Envelope: {
                const QList<double> values = source_values(bars, source);
                const Band band = s.study == Study::Bollinger ? bollinger_band(values, whole(in(0)), in(1))
                                                              : envelope_band(values, whole(in(0)), in(1));
                value(line(key + ".middle", s.study, look(0), band.middle), look(0).color);
                value(line(key + ".upper", s.study, look(1), band.upper), look(1).color);
                value(line(key + ".lower", s.study, look(2), band.lower), look(2).color);
                if (s.study == Study::Bollinger)
                    out.fills << DrawnFill{key + ".upper", key + ".lower", css_color(look(1).color, 0.07),
                                           css_color(look(1).color, 0.07)};
                break;
            }
            case Study::Supertrend: {
                const Supertrend t = supertrend_lines(bars, whole(in(0)), in(1));
                value(line(key + ".up", s.study, look(0), t.up), look(0).color);
                value(line(key + ".down", s.study, look(1), t.down), look(1).color);
                break;
            }
            case Study::VolumeProfile:
                out.profile = true;
                out.profile_rows = whole(in(0));
                out.profile_color = css_color(look(0).color, 0.4);
                break;
            case Study::Fractal:
                for (const Fractal& f : williams_fractals(bars, whole(in(0))))
                    out.marks << DrawnMark{bars[f.index].time, f.up, look(f.up ? 0 : 1).color};
                break;
            case Study::ParabolicSar:
                value(line(key, s.study, look(0), parabolic_sar(bars, in(0), in(1), in(2)), 0, true), look(0).color);
                break;
            case Study::PriceChannel: {
                const Band band = price_channel(bars, whole(in(0)));
                value(line(key + ".upper", s.study, look(0), band.upper), look(0).color);
                value(line(key + ".lower", s.study, look(1), band.lower), look(1).color);
                break;
            }
            case Study::Vwap:
                value(line(key, s.study, look(0), session_vwap(bars, source, first_day_whole)), look(0).color);
                if (!first_day_whole && !bars.isEmpty())
                    row.missing = QStringLiteral("그날 첫 봉을 읽지 않아 비움");
                break;
            case Study::AnchoredVwap: {
                out.picking = picking;
                out.anchor_color = look(0).color;
                const auto index = anchor ? anchor_index(bars, *anchor, view) : std::nullopt;
                if (index) {
                    out.anchor = bars[*index].time;
                    value(line(key, s.study, look(0), anchored_vwap(bars, source, *index)), look(0).color);
                } else if (anchor && !picking) {
                    row.note = QStringLiteral("기준 봉을 읽지 않았다");
                }
                break;
            }
            default: // the lower ones, above
                break;
        }
        out.legend << row;
    }
    return out;
}

} // namespace fincept::krx_quant
