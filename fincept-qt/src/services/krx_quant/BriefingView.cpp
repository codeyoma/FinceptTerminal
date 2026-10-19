#include "services/krx_quant/BriefingView.h"

#include "services/krx_quant/StatsView.h"

#include <QLocale>
#include <QMap>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace fincept::krx_quant::briefing {

namespace {

const QTimeZone& kst() {
    static const QTimeZone zone("Asia/Seoul");
    return zone;
}

QString dash() {
    return QStringLiteral("—");
}

/// "81,500 (+0.37%)"; "—" without a price.
QString price_and_change(double price, const std::optional<double>& pct, int decimals = 0) {
    return QStringLiteral("%1 (%2)").arg(price_text(price, decimals), pct_text(pct));
}

QString weekday(const QDate& day) {
    static const QStringList names = {"월", "화", "수", "목", "금", "토", "일"};
    return day.isValid() ? names[day.dayOfWeek() - 1] : QString();
}

/// "3.7%", "23.75K$": a calendar value in its unit and scale; empty when unknown.
QString event_value(const std::optional<double>& v, const BriefingEvent& e) {
    if (!v)
        return {};
    QString text = number_text(*v, 2); // as the calendar writes it: no trailing zeros
    if (text.contains('.')) {
        while (text.endsWith('0'))
            text.chop(1);
        if (text.endsWith('.'))
            text.chop(1);
    }
    return text + e.scale + e.unit;
}

/// A baseline of the direction or of open_to_close ("otc-flat", "otc-climate", "otc-persist").
bool baseline(const QString& predictor) {
    return is_baseline(predictor) || predictor.startsWith(QLatin1String("otc-"));
}

/// ``value`` to two decimals, a value that rounds to zero as "0.00" (not "-0.00").
QString decimal_text(double value) {
    return number_text(std::round(value * 100) == 0 ? 0.0 : value, 2);
}

} // namespace

int poll_ms(const std::optional<Briefing>& last, const QDateTime& now) {
    if (!last)
        return kFastMs;
    const QDateTime local = now.toTimeZone(kst());
    if (!last->trading_day || local.date() != last->day)
        return kSlowMs;
    const QTime t = local.time();
    return t >= QTime(7, 0) && t < QTime(9, 10) ? kFastMs : kSlowMs;
}

QString pct_text(const std::optional<double>& pct) {
    if (!pct)
        return dash();
    const QString text = number_text(std::abs(*pct), 2) + "%";
    if (std::round(*pct * 100) == 0)
        return number_text(0, 2) + "%";
    return (*pct > 0 ? QStringLiteral("+") : QStringLiteral("−")) + text;
}

QString price_text(const std::optional<double>& price, int decimals) {
    return price ? number_text(*price, decimals) : dash();
}

QString shares_text(const std::optional<double>& shares) {
    if (!shares)
        return dash();
    return signed_text(*shares, 0).replace('-', QStringLiteral("−")) + QStringLiteral("주");
}

QString headline(const Briefing& b) {
    QString line = QStringLiteral("%1(%2) 장전").arg(b.day.toString(Qt::ISODate), weekday(b.day));
    if (!b.trading_day)
        line += QStringLiteral(" (휴장)");
    else if (b.special == "exam_day")
        line += QStringLiteral(" · 수능일 %1 개장").arg(kst_text(b.open, "HH:mm"));
    else if (b.special == "first_day")
        line += QStringLiteral(" · 연초 첫 거래일 %1 개장").arg(kst_text(b.open, "HH:mm"));
    if (!b.macro.regime.isEmpty()) {
        line += QStringLiteral(" · 국면 %1").arg(regime_label(b.macro.regime));
        if (b.macro.day != b.day.toString(Qt::ISODate))
            line += QStringLiteral("(%1)").arg(b.macro.day);
    }
    return line;
}

QList<UsRow> us_rows(const Briefing& b) {
    static const QMap<QString, QString> names = {
        {"sox", "SOX"}, {"nvda", QStringLiteral("엔비디아")}, {"mu", QStringLiteral("마이크론")}, {"vix", "VIX"}};
    QList<UsRow> rows;
    for (const auto& us : b.us) {
        UsRow row{names.value(us.indicator, us.indicator), dash(), dash(), {}, {}, std::nullopt};
        QStringList days, tip;
        for (const auto& c : us.closes) {
            days << (c.change_pct ? pct_text(c.change_pct).remove('%') : dash());
            tip << QStringLiteral("%1 %2 (%3)")
                       .arg(c.day.toString(Qt::ISODate), number_text(c.close, 2), pct_text(c.change_pct));
        }
        if (!us.closes.isEmpty()) {
            const auto& last = us.closes.last();
            row.last = number_text(last.close, 2);
            row.change = pct_text(last.change_pct);
            row.pct = last.change_pct;
        }
        row.days = days.join(QStringLiteral(" · "));
        row.tip = tip.join('\n');
        rows.push_back(row);
    }
    return rows;
}

NightLine night_line(const NightSession& night) {
    NightLine line;
    const QString window =
        QStringLiteral("%1 %2 – %3")
            .arg(night.evening.toString(Qt::ISODate), kst_text(night.from, "HH:mm"), kst_text(night.to, "MM-dd HH:mm"));
    if (!night.recorded) {
        line.text = QStringLiteral("녹화 없음");
        line.tip = QStringLiteral("%1 밤의 KOSPI200 야간선물 녹화가 없습니다").arg(window);
        return line;
    }
    line.pct = night.change_pct;
    line.text =
        QStringLiteral("%1 %2 (%3) · 고 %4 저 %5 · %6계약 · 마지막 %7")
            .arg(night.contract, price_text(night.last, 2), pct_text(night.change_pct), price_text(night.high, 2),
                 price_text(night.low, 2), price_text(night.volume), kst_text(night.last_trade_at, "HH:mm"));
    line.tip =
        QStringLiteral("KOSPI200 야간선물 %1 (%2). 등락은 KIS 체결의 전일 대비입니다.").arg(window, night.contract);
    return line;
}

QList<SymbolRow> symbol_rows(const Briefing& b) {
    QList<SymbolRow> rows;
    for (const auto& s : b.symbols) {
        SymbolRow row;
        row.symbol = s.symbol;
        row.previous_close = price_text(s.previous_close);
        row.after_hours = row.premarket = row.expected = dash();
        if (s.after_hours) {
            row.after_hours = price_and_change(s.after_hours->price, s.after_hours->change_pct);
            row.after_hours_pct = s.after_hours->change_pct;
            row.after_hours_tip = QStringLiteral("%1 통합 1분봉(장 마감 뒤 20:00 전 마지막)")
                                      .arg(kst_text(s.after_hours->at, "MM-dd HH:mm"));
        }
        if (s.premarket) {
            row.premarket = price_and_change(s.premarket->price, s.premarket->change_pct);
            row.premarket_pct = s.premarket->change_pct;
            row.premarket_tip =
                QStringLiteral("NXT 프리마켓 %1 구간의 마지막 체결 %2. cox의 premarket_gap과 같은 가격입니다.")
                    .arg(s.premarket->window, kst_text(s.premarket->at, "HH:mm:ss"));
        }
        if (s.expected_open) {
            const auto& e = *s.expected_open;
            row.expected = price_and_change(e.price, e.change_pct);
            row.expected_pct = e.change_pct;
            row.expected_tip =
                QStringLiteral("%1 %2 · 예상 체결량 %3 · 거래소 구분 %4")
                    .arg(e.final ? QStringLiteral("시가 단일가의 마지막 예상가") : QStringLiteral("지금 예상가"),
                         kst_text(e.at, "HH:mm:ss"), price_text(e.qty), e.venue_code.isEmpty() ? dash() : e.venue_code);
        }
        row.foreign = shares_text(s.supply.foreign_net);
        row.institution = shares_text(s.supply.institution_net);
        row.foreign_sum = shares_text(s.supply.foreign_net_sum);
        row.institution_sum = shares_text(s.supply.institution_net_sum);
        row.supply_tip =
            QStringLiteral("%1 KIS 일별 수급(주). 합은 그날까지 저장된 마지막 %2거래일입니다. 값이 없는 날은 빠집니다.")
                .arg(s.supply.day.toString(Qt::ISODate))
                .arg(s.supply.days);
        rows.push_back(row);
    }
    return rows;
}

QList<ForecastRow> forecast_rows(const Briefing& b) {
    QList<ForecastRow> rows;
    if (!b.open_to_close)
        return rows;
    for (const auto& day : b.open_to_close->symbols) {
        for (const auto& p : day.predictions) {
            if (baseline(p.algorithm))
                continue;
            ForecastRow row{day.symbol,     predictor_label(p.algorithm), dash(), dash(), dash(), dash(), {}, {},
                            Direction::None};
            if (p.band)
                row.band = QStringLiteral("±%1%").arg(number_text(*p.band * 100, 2));
            if (p.status != "ok") {
                row.status = status_label(p.status);
            } else {
                const double up = p.output.value("p_up"), flat = p.output.value("p_flat"),
                             down = p.output.value("p_down");
                row.up = number_text(up * 100, 1) + "%";
                row.flat = number_text(flat * 100, 1) + "%";
                row.down = number_text(down * 100, 1) + "%";
                // The engine's rule (most_likely, likeliest_class): a tie goes to flat, then up.
                const double best = std::max({up, flat, down});
                row.lean = flat == best ? Direction::Flat : up == best ? Direction::Up : Direction::Down;
            }
            if (p.answer) {
                if (p.answer->klass.isEmpty())
                    row.result = QStringLiteral("채점 불가 (%1)").arg(p.answer->unscorable);
                else
                    row.result = direction_label(p.answer->klass == "up"     ? Direction::Up
                                                 : p.answer->klass == "down" ? Direction::Down
                                                                             : Direction::Flat);
            }
            rows.push_back(row);
        }
    }
    return rows;
}

QString input_label(const QString& name) {
    static const QMap<QString, QString> labels = {
        {"us_sox", QStringLiteral("밤사이 SOX 등락")},
        {"us_nvda", QStringLiteral("밤사이 엔비디아 등락")},
        {"us_mu", QStringLiteral("밤사이 마이크론 등락")},
        {"vix", QStringLiteral("VIX (로그)")},
        {"foreign_net", QStringLiteral("전일 외국인 순매수 (거래량 대비)")},
        {"institution_net", QStringLiteral("전일 기관 순매수 (거래량 대비)")},
        {"oc_1", QStringLiteral("전일 시가→종가 (σ)")},
        {"cc_1", QStringLiteral("전일 종가→종가 (σ)")},
        {"gap_1", QStringLiteral("전일 시가 갭 (σ)")},
        {"range_1", QStringLiteral("전일 고저 범위 (σ)")},
        {"oc_5", QStringLiteral("최근 5일 평균 시가→종가 (σ)")},
        {"vol_ratio", QStringLiteral("변동성 국면 (20일 ÷ 250일, 로그)")},
        {"sigma", QStringLiteral("변동성 σ 수준 (로그)")},
        {"climate_up", QStringLiteral("최근 250일 상승 비율")},
        {"climate_down", QStringLiteral("최근 250일 하락 비율")},
        {"premarket_gap", QStringLiteral("NXT 프리마켓 갭 (σ)")},
        {"weekday", QStringLiteral("요일")},
        {"month_end", QStringLiteral("월말")},
        {"month_start", QStringLiteral("월초")},
        {"night_futures", QStringLiteral("KOSPI200 야간선물 등락")},
    };
    return labels.value(name, name);
}

QList<InputRow> input_rows(const AlgorithmInputs& inputs) {
    QList<InputRow> rows;
    const bool explained = !inputs.bias.isEmpty();
    for (const auto& part : inputs.inputs) {
        InputRow row{input_label(part.name), part.name, {}, dash(), dash(), dash(), dash(), 0};
        row.value = part.value ? decimal_text(*part.value) : QStringLiteral("없음");
        if (part.z)
            row.z = decimal_text(*part.z);
        if (explained && !part.scores.isEmpty()) {
            const double up = part.scores.value("up"), down = part.scores.value("down");
            row.up = signed_text(up, 3).replace('-', QStringLiteral("−"));
            row.down = signed_text(down, 3).replace('-', QStringLiteral("−"));
            row.weight = std::abs(up) + std::abs(down);
            if (row.weight > 0)
                row.lean = up > 0 && up >= down    ? QStringLiteral("상승 쪽")
                           : down > 0 && down > up ? QStringLiteral("하락 쪽")
                                                   : QStringLiteral("보합 쪽");
        }
        rows.push_back(row);
    }
    if (explained)
        std::stable_sort(rows.begin(), rows.end(),
                         [](const InputRow& a, const InputRow& b) { return a.weight > b.weight; });
    return rows;
}

std::optional<AlgorithmInputs> inputs_of(const Briefing& b, const QString& symbol) {
    if (!b.open_to_close)
        return std::nullopt;
    for (const auto& day : b.open_to_close->symbols)
        if (day.symbol == symbol && !day.inputs.isEmpty())
            return day.inputs.first();
    return std::nullopt;
}

QList<EventRow> event_rows(const Briefing& b) {
    static const QMap<QString, QString> importance = {
        {"high", QStringLiteral("높음")}, {"medium", QStringLiteral("보통")}, {"low", QStringLiteral("낮음")}};
    QList<EventRow> rows;
    for (const auto& e : b.events)
        rows.push_back({kst_text(e.at, "HH:mm"), e.country, e.title, importance.value(e.importance, e.importance),
                        event_value(e.forecast, e), event_value(e.previous, e), event_value(e.actual, e),
                        e.importance == "high"});
    return rows;
}

QStringList flags(const Briefing& b) {
    QStringList out;
    if (b.special == "exam_day")
        out << QStringLiteral("수능일 %1 개장").arg(kst_text(b.open, "HH:mm"));
    else if (b.special == "first_day")
        out << QStringLiteral("연초 첫 거래일 %1 개장").arg(kst_text(b.open, "HH:mm"));
    if (b.futures_expiry)
        out << QStringLiteral("선물·옵션 만기일");
    else if (b.option_expiry)
        out << QStringLiteral("옵션 만기일");
    if (b.month_end)
        out << QStringLiteral("월말");
    if (b.month_start)
        out << QStringLiteral("월초");
    return out;
}

} // namespace fincept::krx_quant::briefing
