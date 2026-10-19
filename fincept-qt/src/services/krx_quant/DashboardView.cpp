#include "services/krx_quant/DashboardView.h"

#include "services/krx_quant/Board.h"
#include "services/krx_quant/KindView.h"

#include <algorithm>
#include <tuple>
#include <utility>

namespace fincept::krx_quant::dashboard {

namespace {

const QString kDash = QStringLiteral("—");

QString per_trade_text(const std::optional<Interval>& per_trade, int decimals) {
    return per_trade ? signed_text(per_trade->value, decimals) : kDash;
}

/// The entry's P&L a trade and with the down calls through the inverse product, in the kind's unit.
std::pair<QString, QString> pnl_texts(const Entry& e, const QString& kind) {
    if (kind == QLatin1String("direction"))
        return {per_trade_text(e.per_trade, 2), per_trade_text(e.inverse ? e.inverse->per_trade : std::nullopt, 2)};
    if (is_barrier_kind(kind) && e.barrier)
        return {per_trade_text(e.barrier->pnl.per_trade, 2),
                per_trade_text(e.barrier->pnl_inverse ? e.barrier->pnl_inverse->per_trade : std::nullopt, 2)};
    if (kind == QLatin1String("open_to_close") && e.open_to_close)
        return {per_trade_text(e.open_to_close->pnl.per_trade, 1),
                per_trade_text(e.open_to_close->pnl_inverse ? e.open_to_close->pnl_inverse->per_trade : std::nullopt,
                               1)};
    return {kDash, kDash};
}

LeaderRow row_of(const Entry& e, const QString& kind, bool baseline) {
    LeaderRow row;
    row.algorithm = e.algorithm;
    row.rank = e.rank ? QString::number(*e.rank) : kDash;
    row.name = predictor_label(e.algorithm);
    row.status = e.status.isEmpty() ? QString() : standing_label(e.status);
    row.accuracy = rate_shown(e.accuracy);
    if (e.accuracy && e.accuracy->low && e.accuracy->high)
        row.accuracy.text += QStringLiteral(" (%1–%2)").arg(number_text(*e.accuracy->low * 100, 1),
                                                            number_text(*e.accuracy->high * 100, 1));
    row.day_low = e.day_low ? signed_text(*e.day_low, 3) : kDash;
    row.n = number_text(e.n, 0);
    std::tie(row.pnl, row.pnl_inverse) = pnl_texts(e, kind);
    row.baseline = baseline;
    return row;
}

QString percent_text(const std::optional<double>& p) {
    return p ? number_text(*p * 100, 0) + QLatin1Char('%') : kDash;
}

std::optional<Prediction> final_consensus_of(const QList<Prediction>& predictions) {
    for (const auto& p : predictions)
        if (p.algorithm == QLatin1String("consensus-final"))
            return p;
    return std::nullopt;
}

/// The latest scored final consensus among ``predictions`` (any order).
std::optional<Prediction> last_scored(const QList<Prediction>& predictions) {
    std::optional<Prediction> last;
    for (const auto& p : predictions)
        if (p.algorithm == QLatin1String("consensus-final") && p.status == QLatin1String("ok") && p.outcome &&
            (!last || p.bar_close > last->bar_close))
            last = p;
    return last;
}

Direction direction_of(const QString& klass) {
    if (klass == QLatin1String("up"))
        return Direction::Up;
    if (klass == QLatin1String("flat"))
        return Direction::Flat;
    if (klass == QLatin1String("down"))
        return Direction::Down;
    return Direction::None;
}

/// The day's answer, the same for every predictor of it: whichever has it.
std::optional<KindAnswer> answer_of(const QList<KindPrediction>& at_day) {
    for (const auto& p : at_day)
        if (p.answer)
            return p.answer;
    return std::nullopt;
}

} // namespace

QString blocked_label(const QString& reason) {
    static const QMap<QString, QString> labels = {
        {"special", QStringLiteral("특수 상황")},
        {"no_reference", QStringLiteral("기준가 없음")},
        {"expired", QStringLiteral("시간 지남")},
    };
    return labels.value(reason, reason);
}

QList<LeaderRow> leaderboard_rows(const Leaderboard& board, int top) {
    QList<LeaderRow> rows;
    for (const auto* list : {&board.ranked, &board.unranked})
        for (const auto& e : *list)
            if (rows.size() < top)
                rows << row_of(e, board.kind, false);
    const auto flat = std::find_if(board.baselines.begin(), board.baselines.end(),
                                   [](const Entry& e) { return e.algorithm == QLatin1String("baseline-flat"); });
    if (flat != board.baselines.end()) {
        rows << row_of(*flat, board.kind, true);
    } else {
        for (const auto& e : board.baselines)
            rows << row_of(e, board.kind, true);
    }
    return rows;
}

QList<SignalRow> signal_rows(const QList<SymbolQuote>& symbols, const QMap<QString, Latest>& latest,
                             const QMap<QString, QList<Prediction>>& recent, const SignalRule& rule,
                             const QDateTime& now) {
    QList<SignalRow> rows;
    for (const auto& quote : symbols) {
        SignalRow row;
        row.symbol = quote.symbol;
        row.name = quote.name;
        row.logo_url = quote.logo_url;
        row.price = quote.mid ? price_text(*quote.mid) : kDash;
        row.bar_close = row.until = row.window = row.up = row.flat = row.down = row.members = row.last = kDash;

        if (const auto final_consensus = final_consensus_of(latest.value(quote.symbol).predictions)) {
            const Prediction& c = *final_consensus;
            row.bar_close = row.window = kst_text(c.bar_close, "HH:mm");
            if (c.status != QLatin1String("ok")) {
                row.signal = status_label(c.status);
            } else {
                row.until = kst_text(c.target_at, "HH:mm");
                row.window = QStringLiteral("%1 → %2").arg(row.bar_close, row.until);
                row.up = percent_text(c.p_up);
                row.flat = percent_text(c.p_flat);
                row.down = percent_text(c.p_down);
                row.call = most_likely(c);
                if (c.votes)
                    row.members = QStringLiteral("정식 %1개 중 %2개 %3")
                                      .arg(c.votes->total())
                                      .arg(c.votes->of(row.call))
                                      .arg(direction_label(row.call));
                if (!c.blocked.isEmpty()) {
                    row.signal = QStringLiteral("막힘: %1").arg(blocked_label(c.blocked));
                } else if (c.votes) {
                    if (const auto signal = judge(c, *c.votes, rule)) {
                        row.signal_direction = signal->direction;
                        row.signal = QStringLiteral("%1 신호").arg(direction_label(signal->direction));
                        row.live = !now.isValid() || now < c.target_at;
                    }
                }
            }
        }
        if (const auto last = last_scored(recent.value(quote.symbol))) {
            const Truth truth = truth_of({*last}, last->bar_close);
            const auto result = verdict(most_likely(*last), truth);
            const QString head =
                QStringLiteral("%1 %2").arg(kst_text(last->bar_close, "HH:mm"), direction_label(most_likely(*last)));
            if (result == Mark::Result::Hit || result == Mark::Result::Miss) {
                row.last_hit = result == Mark::Result::Hit;
                row.last = head + (*row.last_hit ? QStringLiteral(" ✓") : QStringLiteral(" ✗"));
            } else {
                row.last = head + QStringLiteral(" 채점 불가");
            }
        }
        rows << row;
    }
    return rows;
}

StatusView status_view(const EngineStatus& status) {
    StatusView view;
    QStringList troubles;
    if (status.mode == QLatin1String("demo"))
        view.lines << StatusLine{QStringLiteral("모드"), QStringLiteral("데모 엔진 (가상 값)")};
    view.lines << StatusLine{QStringLiteral("장 단계"), phase_label(status.phase)};
    view.lines << StatusLine{QStringLiteral("마지막 봉 마감"), kst_text(status.last_bar_close, "HH:mm")};
    view.lines << StatusLine{QStringLiteral("채점 대기"), QStringLiteral("%1건").arg(status.pending_outcomes)};

    if (status.kis_connected) {
        QStringList gaps;
        for (const auto& feed : status.feeds)
            if (!feed.gap.isEmpty())
                gaps << QStringLiteral("%1 %2").arg(feed.feed, feed.key);
        QString value = *status.kis_connected ? QStringLiteral("연결됨") : QStringLiteral("끊김");
        if (!gaps.isEmpty())
            value += QStringLiteral(" · 결측 %1개 (%2)").arg(gaps.size()).arg(gaps.join(QStringLiteral(", ")));
        if (!*status.kis_connected)
            troubles << QStringLiteral("KIS 연결 끊김");
        else if (!gaps.isEmpty())
            troubles << QStringLiteral("KIS 피드 결측 %1개").arg(gaps.size());
        view.lines << StatusLine{QStringLiteral("KIS"), value, !*status.kis_connected || !gaps.isEmpty()};
    }

    QStringList special;
    for (const auto& w : status.special) {
        QString text = special_label(w.kind);
        if (!w.symbol.isEmpty())
            text += QLatin1Char(' ') + w.symbol;
        special << QStringLiteral("%1 %2–%3").arg(text, kst_text(w.start, "HH:mm"), kst_text(w.end, "HH:mm"));
    }
    view.lines << StatusLine{QStringLiteral("오늘 특수 상황"),
                             special.isEmpty() ? QStringLiteral("없음") : special.join(QStringLiteral(" · "))};

    if (status.training) {
        const Training& t = *status.training;
        // Before 08:00 the night's models wait for the morning swap: not a trouble.
        const bool trouble = !t.applied && t.reason != QLatin1String("waiting for 08:00");
        const QString day = QDate::fromString(t.day, Qt::ISODate).toString("MM-dd");
        view.lines << StatusLine{QStringLiteral("지난밤 학습"),
                                 QStringLiteral("%1 · %2 · 승격 %3개")
                                     .arg(day.isEmpty() ? t.day : day, training_reason_label(t.reason))
                                     .arg(t.promoted.size()),
                                 trouble};
        if (trouble)
            troubles << QStringLiteral("지난밤 학습 미적용");
    } else {
        view.lines << StatusLine{QStringLiteral("지난밤 학습"), kDash};
    }

    if (status.cpu_usage) {
        const bool over = *status.cpu_usage > 1.0;
        QString value = percent_text(status.cpu_usage);
        if (status.cpu_skipped_today > 0)
            value += QStringLiteral(" · 오늘 건너뜀 %1회").arg(status.cpu_skipped_today);
        view.lines << StatusLine{QStringLiteral("CPU 예산"), value, over};
        if (over)
            troubles << QStringLiteral("CPU 예산 초과 (%1)").arg(percent_text(status.cpu_usage));
    }

    if (const QStringList build = engine_build_parts(status); !build.isEmpty())
        view.lines << StatusLine{QStringLiteral("엔진"), build.join(QStringLiteral(" · "))};

    if (!troubles.isEmpty())
        view.headline = troubles.first();
    return view;
}

QList<DailyRow> open_to_close_rows(const QList<SymbolQuote>& symbols, const QMap<QString, KindLatest>& latest,
                                   const KindInfo& kind, const QDate& today) {
    QList<DailyRow> rows;
    for (const auto& quote : symbols) {
        DailyRow row;
        row.symbol = quote.symbol;
        row.name = quote.name;
        row.logo_url = quote.logo_url;
        row.predicted_at = QStringLiteral("예측 없음");
        row.up = row.flat = row.down = row.basis = row.band = row.answer = row.result = kDash;

        const QList<KindPrediction> at_day = latest.value(quote.symbol).predictions;
        if (const auto call = daily_call(at_day, kind)) {
            const QDateTime predicted_at = at_day.first().bar_close;
            row.predicted_at = kst_text(predicted_at, "MM-dd HH:mm");
            if (daily_day(predicted_at) != today)
                row.predicted_at += QStringLiteral(" (지난 거래일)");
            const auto probability = [&](const char* klass) {
                const qsizetype i = kind.classes.indexOf(QLatin1String(klass));
                return i >= 0 && i < call->probabilities.size() ? std::optional<double>(call->probabilities[i])
                                                                : std::nullopt;
            };
            row.up = percent_text(probability("up"));
            row.flat = percent_text(probability("flat"));
            row.down = percent_text(probability("down"));
            const QString called = kind.classes.value(call->klass);
            row.call = direction_of(called);
            row.basis = QStringLiteral("알고리즘 %1개 평균").arg(call->algorithms.size());
            if (const auto band = daily_band(at_day))
                row.band = band_text(*band);
            const std::optional<KindAnswer> answer = answer_of(at_day);
            row.answer = row.result = daily_answer_text(answer, kind);
            if (answer && !answer->klass.isEmpty()) {
                row.hit = answer->klass == called;
                row.result += *row.hit ? QStringLiteral(" ✓") : QStringLiteral(" ✗");
            }
        }
        rows << row;
    }
    return rows;
}

QString pnl_unit(const QString& kind) {
    if (kind == QLatin1String("direction") || is_barrier_kind(kind))
        return QStringLiteral("틱");
    if (kind == QLatin1String("open_to_close"))
        return QStringLiteral("bp");
    return {};
}

} // namespace fincept::krx_quant::dashboard
