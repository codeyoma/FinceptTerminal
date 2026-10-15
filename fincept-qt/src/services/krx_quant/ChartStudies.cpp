#include "services/krx_quant/ChartStudies.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
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
    return StudyInput{title, value, min, max, step, decimals};
}

StudyInput period(double value, const QString& title = QStringLiteral("기간")) {
    return input(title, value, 1, kMaxHistoryBars);
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

QString settings_text(const StudySettings& settings) {
    const StudySpec& spec = study_spec(settings.study);
    QStringList parts;
    for (qsizetype i = 0; i < settings.inputs.size(); ++i) {
        const StudyInput& in = spec.periods ? spec.inputs.first() : spec.inputs.value(i);
        QString text = number_text(settings.inputs[i], in.decimals);
        if (in.title.contains('%'))
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

Values session_vwap(const QList<ChartBar>& bars, Source source) {
    Values out(bars.size());
    double priced = 0;
    double volume = 0;
    for (qsizetype i = 0; i < bars.size(); ++i) {
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
            return std::max(0, longest - 1);
        }
        case Study::Ichimoku:
            return std::max(0, std::max({at(0), at(1), at(2)}) - 1 + at(3));
        case Study::Bollinger:
        case Study::Envelope:
        case Study::PriceChannel:
        case Study::Supertrend:
            return std::max(0, at(0) - 1);
        case Study::Fractal:
            return std::max(0, at(0));
        case Study::ParabolicSar:
            return 1;
        case Study::VolumeProfile:
        case Study::Vwap:
        case Study::AnchoredVwap:
        case Study::Volume:
            return 0;
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

StudyDrawing draw_studies(const QList<ChartBar>& bars, const StudySet& set, int view, std::optional<qint64> anchor,
                          bool picking) {
    StudyDrawing out;
    const qsizetype n = bars.size();
    out.volume = set.on(Study::Volume);
    out.volume_hidden = set.of(Study::Volume).hidden;
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
        if (!s.on || s.study == Study::Volume)
            continue;
        const StudySpec& spec = study_spec(s.study);
        if (spec.minute_only && candle_view(view))
            continue; // the VWAP: kept, not drawn
        LegendRow row;
        row.study = s.study;
        row.title = spec.title;
        row.settings = settings_text(s);
        row.hidden = s.hidden;
        const auto value = [&row](const QString& id, const QString& color) {
            row.values << id;
            row.colors << color;
        };
        const Source source = s.source.value_or(Source::Close);
        const auto in = [&s](qsizetype i) { return i < s.inputs.size() ? s.inputs[i] : 0.0; };
        const auto look = [&s](qsizetype i) { return s.lines.value(i); };
        if (s.study == Study::AnchoredVwap) {
            if (anchor)
                row.settings = QDateTime::fromSecsSinceEpoch(*anchor, QTimeZone::UTC).toString("MM-dd HH:mm");
            if (picking)
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
                value(line(key, s.study, look(0), session_vwap(bars, source)), look(0).color);
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
            case Study::Volume:
                break;
        }
        out.legend << row;
    }
    if (out.volume) {
        LegendRow row;
        row.study = Study::Volume;
        row.pane = 1;
        row.title = study_title(Study::Volume);
        row.hidden = out.volume_hidden;
        if (!row.hidden) {
            row.values << QStringLiteral("volume");
            row.colors << QString();
        }
        out.legend << row;
    }
    return out;
}

} // namespace fincept::krx_quant
