#include "services/krx_quant/ActionView.h"

#include <QLocale>
#include <QTimeZone>

#include <cmath>

namespace fincept::krx_quant::action {

namespace {

const QTimeZone& kst() {
    static const QTimeZone zone = QTimeZone::fromSecondsAheadOfUtc(9 * 3600);
    return zone;
}

QString won(double value) {
    return price_text(value) + QStringLiteral("원");
}

/// "+31원", "−19원", "0원".
QString signed_won(double value) {
    const double rounded = std::round(value);
    if (rounded == 0)
        return QStringLiteral("0원");
    return (rounded > 0 ? QStringLiteral("+") : QStringLiteral("−")) + won(std::abs(rounded));
}

QString clock(const QDateTime& at) {
    return kst_text(at, QStringLiteral("HH:mm"));
}

/// "3분 남음", "40초 남음"; empty once passed.
QString left(const QDateTime& until, const QDateTime& now) {
    if (!until.isValid() || !now.isValid())
        return {};
    const qint64 seconds = now.secsTo(until);
    if (seconds <= 0)
        return {};
    if (seconds < 60)
        return QStringLiteral("%1초 남음").arg(seconds);
    return QStringLiteral("%1분 남음").arg((seconds + 59) / 60);
}

bool busy(const ActionCard& card) {
    return card.action == QLatin1String("buy") || card.action == QLatin1String("sell") || card.plan.has_value();
}

} // namespace

int poll_ms(const std::optional<ActionBoard>& last, const QDateTime& now) {
    if (last)
        for (const auto& card : last->cards)
            if (busy(card))
                return kFastMs;
    const QDateTime local = now.toTimeZone(kst());
    const int weekday = local.date().dayOfWeek();
    const QTime t = local.time();
    if (weekday <= 5 && t >= QTime(8, 55) && t < QTime(15, 35))
        return kSessionMs;
    return kSlowMs;
}

CardText card_text(const ActionCard& card, const QDateTime& now) {
    CardText out;
    out.word = card.action;
    out.tone = card.action == QLatin1String("buy") || card.action == QLatin1String("sell") ? card.action
                                                                                          : QStringLiteral("wait");
    out.title = card.name.isEmpty() ? card.symbol : QStringLiteral("%1 %2").arg(card.name, card.symbol);
    out.line = card.said;
    out.plan = plan_lines(card, now);
    if (card.record) {
        out.record = record_line(*card.record);
        out.record_tip = QStringLiteral(
            "자동 매매가 쓰는 신호를 지난 거래일마다 1주씩 따로 사고팔아 본 결과입니다(밤마다 다시 잽니다). "
            "\"나쁘게 봐도\"는 날마다의 결과로 잡은 95% 하한입니다. 비용(수수료와 세금, 호가 차이)을 뺀 값입니다.");
    }
    return out;
}

QStringList plan_lines(const ActionCard& card, const QDateTime& now) {
    QStringList lines;
    if (!card.plan)
        return lines;
    const ActionPlan& p = *card.plan;
    if (card.exit) {
        const QString sold = card.exit->price ? won(*card.exit->price) : QStringLiteral("—");
        lines << QStringLiteral("판 값 %1 · 산 값 %2").arg(sold, won(p.entry_price));
        if (card.exit->price)
            lines << QStringLiteral("1주당 %1 (비용 전)").arg(signed_won(*card.exit->price - p.entry_price));
        return lines;
    }
    lines << QStringLiteral("이익 실현 %1").arg(won(p.target));
    lines << QStringLiteral("손절 %1").arg(won(p.stop));
    const QString remaining = left(p.sell_by, now);
    lines << (remaining.isEmpty() ? QStringLiteral("늦어도 %1에 매도").arg(clock(p.sell_by))
                                  : QStringLiteral("늦어도 %1에 매도 (%2)").arg(clock(p.sell_by), remaining));
    if (p.bid)
        lines << QStringLiteral("지금 %1 · 산 값 %2보다 1주당 %3 (비용 전)")
                     .arg(won(*p.bid), won(p.entry_price), signed_won(*p.bid - p.entry_price));
    else
        lines << QStringLiteral("산 값 %1").arg(won(p.entry_price));
    return lines;
}

QString record_line(const ActionRecord& record) {
    if (!record.bought || !record.wins)
        return {};
    QString line = record.days ? QStringLiteral("지난 %1일 이 신호대로 %2번 샀다면 %3번 이익")
                                     .arg(*record.days)
                                     .arg(*record.bought)
                                     .arg(*record.wins)
                               : QStringLiteral("이 신호대로 %1번 샀다면 %2번 이익").arg(*record.bought).arg(*record.wins);
    if (record.per_signal_won) {
        line += QStringLiteral(" · 비용 빼고 1주당 평균 %1").arg(signed_won(*record.per_signal_won));
        if (record.low_won)
            line += QStringLiteral(" (나쁘게 봐도 %1)").arg(signed_won(*record.low_won));
    }
    return line;
}

QString mode_line(const ActionBoard& board) {
    if (!board.alive)
        return QStringLiteral("자동 매매가 돌지 않습니다");
    if (board.mode == QLatin1String("live"))
        return QStringLiteral("자동 매매: 실전 계좌 · 진짜 돈입니다");
    if (board.mode == QLatin1String("mock"))
        return QStringLiteral("자동 매매: KIS 모의 계좌 · 진짜 돈이 아닙니다");
    return QStringLiteral("자동 매매: 모의 · 진짜 돈이 아닙니다");
}

QStringList alerts(const ActionBoard& board, QMap<QString, QString>& seen, bool first) {
    QStringList out;
    for (const auto& card : board.cards) {
        const bool acts = card.action == QLatin1String("buy") || card.action == QLatin1String("sell");
        // A trade's buy and its sale once each: keyed by the trade (its entry), not by the moment, so that a
        // sale begun and then done is one sale.
        const QDateTime trade = card.plan ? card.plan->entry_at : card.since;
        const QString key = acts ? card.action + "@" + trade.toString(Qt::ISODateWithMs) : QString();
        if (acts && !first && seen.value(card.symbol) != key) {
            const QString who = card.name.isEmpty() ? card.symbol : card.name;
            out << QStringLiteral("%1 %2 · %3").arg(card.action, who, card.said);
        }
        seen.insert(card.symbol, key);
    }
    return out;
}

} // namespace fincept::krx_quant::action
