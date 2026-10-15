#include "screens/krx_quant/KrxStudyPanel.h"

#include "ui/theme/Theme.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>
#include <functional>

namespace fincept::screens {

using krx_quant::Study;

namespace {

/// The colours a line can take: the defaults' and a few more.
const QStringList& swatches() {
    static const QStringList colors{"#F472B6", "#F59E0B", "#22C55E", "#38BDF8", "#C084FC", "#FB923C",
                                    "#2DD4BF", "#60A5FA", "#EF4444", "#FACC15", "#A3E635", "#E879F9",
                                    "#3B82F6", "#A78BFA", "#86EFAC", "#FCA5A5", "#94A3B8", "#F5F5F5"};
    return colors;
}

/// A 16×16 icon drawn by ``draw``, sharp on a high-density screen.
QIcon paint(const std::function<void(QPainter&)>& draw) {
    QPixmap pixmap(32, 32);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    draw(p);
    return QIcon(pixmap);
}

/// The check circle: filled with a tick when on, an empty ring when off; faint when disabled.
QIcon check_icon(bool on, bool enabled) {
    return paint([on, enabled](QPainter& p) {
        QColor ring(on ? ui::colors::AMBER() : ui::colors::TEXT_DIM());
        if (!enabled)
            ring.setAlphaF(0.4);
        if (on) {
            p.setPen(Qt::NoPen);
            p.setBrush(ring);
            p.drawEllipse(QPointF(8, 8), 6.5, 6.5);
            p.setPen(QPen(QColor(ui::colors::BG_BASE()), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPolyline(QPolygonF{{5, 8.2}, {7.2, 10.4}, {11, 5.8}});
        } else {
            p.setPen(QPen(ring, 1.3));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(8, 8), 6, 6);
        }
    });
}

QIcon swatch_icon(const QString& color) {
    return paint([color](QPainter& p) {
        p.setPen(QPen(QColor(ui::colors::BORDER_MED()), 1));
        p.setBrush(QColor(color));
        p.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 2.5, 2.5);
    });
}

QLabel* label(const QString& text) {
    auto* l = new QLabel(text);
    l->setObjectName("krxStudyLabel");
    return l;
}

/// A box for ``in`` showing ``value``; ``set`` takes each value entered (not each key pressed).
QWidget* number_box(const krx_quant::StudyInput& in, double value, const std::function<void(double)>& set) {
    if (in.decimals == 0) {
        auto* box = new QSpinBox;
        box->setRange(static_cast<int>(in.min), static_cast<int>(in.max));
        box->setValue(static_cast<int>(value));
        box->setKeyboardTracking(false);
        QObject::connect(box, qOverload<int>(&QSpinBox::valueChanged), box, [set](int v) { set(v); });
        return box;
    }
    auto* box = new QDoubleSpinBox;
    box->setLocale(QLocale::c());
    box->setDecimals(in.decimals);
    box->setRange(in.min, in.max);
    box->setSingleStep(in.step);
    box->setValue(value);
    box->setKeyboardTracking(false);
    QObject::connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), box, [set](double v) { set(v); });
    return box;
}

} // namespace

KrxStudyPanel::KrxStudyPanel(QWidget* parent) : QFrame(parent, Qt::Popup) {
    setObjectName("krxStudyPanel");
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(1, 1, 1, 1);
    outer->setSpacing(0);

    auto* left = new QWidget;
    left->setObjectName("krxStudyLists");
    left->setFixedWidth(190);
    auto* list = new QVBoxLayout(left);
    list->setContentsMargins(8, 10, 8, 10);
    list->setSpacing(1);
    const auto section = [list](const QString& text) {
        auto* head = new QLabel(text);
        head->setObjectName("krxStudySection");
        list->addWidget(head);
    };
    const auto add_row = [this, list](Study study) {
        auto* row = new QWidget;
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(2);
        auto* check = new QToolButton;
        check->setObjectName("krxStudyCheck");
        check->setAutoRaise(true);
        check->setIconSize(QSize(16, 16));
        check->setCursor(Qt::PointingHandCursor);
        check->setFocusPolicy(Qt::NoFocus);
        auto* name = new QPushButton(krx_quant::study_title(study));
        name->setObjectName("krxStudyName");
        name->setCheckable(true);
        name->setFlat(true);
        name->setFocusPolicy(Qt::NoFocus);
        name->setCursor(Qt::PointingHandCursor);
        line->addWidget(check);
        line->addWidget(name, 1);
        connect(check, &QToolButton::clicked, this, [this, study]() {
            set_.turn(study, !set_.on(study));
            selected_ = study;
            changed(true);
        });
        connect(name, &QPushButton::clicked, this, [this, study]() { select(study); });
        checks_.insert(static_cast<int>(study), check);
        names_.insert(static_cast<int>(study), name);
        list->addWidget(row);
    };
    section(tr("상단 지표"));
    for (const Study study : krx_quant::kUpperStudies)
        add_row(study);
    list->addSpacing(10);
    section(tr("하단 지표"));
    for (const Study study : krx_quant::kLowerStudies)
        add_row(study);
    list->addStretch(1);
    outer->addWidget(left);

    auto* right = new QWidget;
    right->setObjectName("krxStudyRight");
    right->setMinimumWidth(330);
    right_ = new QVBoxLayout(right);
    right_->setContentsMargins(16, 12, 16, 14);
    right_->setSpacing(8);
    auto* head = new QHBoxLayout;
    head->setSpacing(6);
    title_ = new QLabel;
    title_->setObjectName("krxStudyTitle");
    help_ = new QToolButton;
    help_->setObjectName("krxStudyHelp");
    help_->setText(QStringLiteral("?"));
    help_->setFocusPolicy(Qt::NoFocus);
    help_->setCursor(Qt::WhatsThisCursor);
    connect(help_, &QToolButton::clicked, this,
            [this]() { QToolTip::showText(help_->mapToGlobal(QPoint(0, help_->height())), help_->toolTip(), help_); });
    head->addWidget(title_);
    head->addWidget(help_);
    head->addStretch(1);
    right_->addLayout(head);
    about_ = new QLabel;
    about_->setObjectName("krxStudyAbout");
    about_->setWordWrap(true);
    right_->addWidget(about_);
    form_ = new QWidget;
    right_->addWidget(form_);
    right_->addStretch(1);
    outer->addWidget(right, 1);

    set_ = krx_quant::default_studies();
    restyle();
    fill_lists();
    fill_settings();
}

void KrxStudyPanel::show_studies(const krx_quant::StudySet& set, int view) {
    const bool differs = !(set == set_) || view != view_;
    set_ = set;
    view_ = view;
    fill_lists();
    if (differs)
        fill_settings();
}

void KrxStudyPanel::select(Study study) {
    selected_ = study;
    fill_lists();
    fill_settings();
}

void KrxStudyPanel::open_at(const QPoint& at) {
    adjustSize();
    QPoint pos = at;
    if (const QScreen* screen = QGuiApplication::screenAt(at)) {
        const QRect room = screen->availableGeometry();
        pos.setX(std::clamp(pos.x(), room.left(), std::max(room.left(), room.right() - width())));
        pos.setY(std::clamp(pos.y(), room.top(), std::max(room.top(), room.bottom() - height())));
    }
    move(pos);
    show();
    raise();
}

void KrxStudyPanel::changed(bool rebuild) {
    emit studies_changed(set_);
    fill_lists();
    if (rebuild)
        fill_settings();
}

void KrxStudyPanel::fill_lists() {
    for (const Study study : krx_quant::kStudies) {
        auto* check = checks_.value(static_cast<int>(study));
        auto* name = names_.value(static_cast<int>(study));
        if (!check || !name)
            continue;
        // The VWAP is the minute views' (krx-quant #220): greyed in the others, kept as it is.
        const bool usable = !(krx_quant::study_spec(study).minute_only && krx_quant::candle_view(view_));
        const QString tip = usable ? krx_quant::study_spec(study).about : tr("분봉에서만");
        check->setIcon(check_icon(set_.on(study), usable));
        check->setEnabled(usable);
        check->setToolTip(usable ? (set_.on(study) ? tr("끄기") : tr("켜기")) : tip);
        name->setEnabled(usable);
        name->setChecked(study == selected_);
        name->setToolTip(tip);
    }
}

QToolButton* KrxStudyPanel::color_button(Study study, int line) {
    auto* button = new QToolButton;
    button->setObjectName("krxStudyColor");
    button->setIconSize(QSize(16, 16));
    button->setIcon(swatch_icon(set_.of(study).lines.value(line).color));
    button->setPopupMode(QToolButton::InstantPopup);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(tr("색"));
    auto* menu = new QMenu(button);
    auto* grid_holder = new QWidget(menu);
    auto* grid = new QGridLayout(grid_holder);
    grid->setContentsMargins(6, 6, 6, 6);
    grid->setSpacing(3);
    const QStringList& colors = swatches();
    for (qsizetype i = 0; i < colors.size(); ++i) {
        auto* swatch = new QToolButton;
        swatch->setAutoRaise(true);
        swatch->setIconSize(QSize(16, 16));
        swatch->setIcon(swatch_icon(colors[i]));
        swatch->setToolTip(colors[i]);
        connect(swatch, &QToolButton::clicked, this, [this, study, line, button, menu, color = colors[i]]() {
            auto& lines = set_.of(study).lines;
            if (line < lines.size())
                lines[line].color = color;
            button->setIcon(swatch_icon(color));
            menu->close();
            changed();
        });
        grid->addWidget(swatch, static_cast<int>(i / 6), static_cast<int>(i % 6));
    }
    auto* action = new QWidgetAction(menu);
    action->setDefaultWidget(grid_holder);
    menu->addAction(action);
    button->setMenu(menu);
    return button;
}

void KrxStudyPanel::fill_settings() {
    const krx_quant::StudySpec& spec = krx_quant::study_spec(selected_);
    const krx_quant::StudySettings& settings = set_.of(selected_);
    title_->setText(spec.title);
    help_->setToolTip(spec.help);
    about_->setText(spec.about);

    auto* old = form_;
    form_ = new QWidget;
    right_->replaceWidget(old, form_);
    old->deleteLater();
    auto* grid = new QGridLayout(form_);
    grid->setContentsMargins(0, 4, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    int row = 0;
    const Study study = selected_;

    if (spec.minute_only && krx_quant::candle_view(view_)) {
        auto* note = label(tr("분봉에서만 그린다. 켠 것과 설정은 남는다."));
        note->setObjectName("krxStudyNote");
        grid->addWidget(note, row++, 0, 1, 4);
    }
    if (spec.source) {
        grid->addWidget(label(tr("기준값")), row, 0);
        auto* source = new QComboBox;
        source->setFocusPolicy(Qt::NoFocus);
        for (const krx_quant::Source s : krx_quant::kSources)
            source->addItem(krx_quant::source_title(s), krx_quant::source_key(s));
        source->setCurrentIndex(source->findData(krx_quant::source_key(settings.source.value_or(*spec.source))));
        connect(source, qOverload<int>(&QComboBox::activated), this, [this, study, source](int) {
            set_.of(study).source = krx_quant::parse_source(source->currentData().toString());
            changed();
        });
        grid->addWidget(source, row++, 1, 1, 3);
    }
    const auto width_box = [this, study](int line) {
        auto* box = new QSpinBox;
        box->setRange(1, 4);
        box->setSuffix(QStringLiteral("px"));
        box->setValue(std::max(1, set_.of(study).lines.value(line).width));
        box->setToolTip(tr("굵기"));
        connect(box, qOverload<int>(&QSpinBox::valueChanged), this, [this, study, line](int v) {
            auto& lines = set_.of(study).lines;
            if (line < lines.size())
                lines[line].width = v;
            changed();
        });
        return box;
    };
    if (spec.periods) {
        // A line per period: its colour, period and width; X drops it (the first stays).
        grid->addWidget(label(tr("색")), row, 0);
        grid->addWidget(label(tr("기간")), row, 1);
        grid->addWidget(label(tr("굵기")), row++, 2);
        for (int i = 0; i < settings.inputs.size(); ++i) {
            grid->addWidget(color_button(study, i), row, 0);
            grid->addWidget(number_box(spec.inputs.first(), settings.inputs[i],
                                       [this, study, i](double v) {
                                           auto& inputs = set_.of(study).inputs;
                                           if (i < inputs.size())
                                               inputs[i] = v;
                                           changed();
                                       }),
                            row, 1);
            grid->addWidget(width_box(i), row, 2);
            if (i > 0) {
                auto* drop = new QToolButton;
                drop->setObjectName("krxStudyDrop");
                drop->setText(QStringLiteral("✕"));
                drop->setToolTip(tr("이 기간 빼기"));
                drop->setFocusPolicy(Qt::NoFocus);
                connect(drop, &QToolButton::clicked, this, [this, study, i]() {
                    if (krx_quant::remove_period(set_.of(study), i))
                        changed(true);
                });
                grid->addWidget(drop, row, 3);
            }
            ++row;
        }
        auto* add = new QPushButton(tr("+ 기간 추가"));
        add->setObjectName("krxStudyAdd");
        add->setFocusPolicy(Qt::NoFocus);
        add->setEnabled(settings.inputs.size() < krx_quant::kMaxPeriods);
        add->setToolTip(tr("선을 하나 더한다(최대 %1개).").arg(krx_quant::kMaxPeriods));
        connect(add, &QPushButton::clicked, this, [this, study]() {
            if (krx_quant::add_period(set_.of(study)))
                changed(true);
        });
        grid->addWidget(add, row++, 0, 1, 3, Qt::AlignLeft);
    } else {
        for (int i = 0; i < spec.inputs.size(); ++i) {
            grid->addWidget(label(spec.inputs[i].title), row, 0);
            grid->addWidget(number_box(spec.inputs[i], settings.inputs.value(i, spec.inputs[i].value),
                                       [this, study, i](double v) {
                                           auto& inputs = set_.of(study).inputs;
                                           if (i < inputs.size())
                                               inputs[i] = v;
                                           changed();
                                       }),
                            row++, 1, 1, 2);
        }
        for (int i = 0; i < spec.lines.size(); ++i) {
            grid->addWidget(label(spec.lines[i].title), row, 0);
            grid->addWidget(color_button(study, i), row, 1, Qt::AlignLeft);
            if (spec.lines[i].width > 0)
                grid->addWidget(width_box(i), row, 2);
            ++row;
        }
    }
    if (study == Study::AnchoredVwap) {
        auto* again = new QPushButton(tr("기준 다시 고르기"));
        again->setObjectName("krxStudyAdd");
        again->setFocusPolicy(Qt::NoFocus);
        again->setEnabled(settings.on);
        again->setToolTip(tr("다음에 차트를 누른 봉이 기준이 된다. 기준 표시를 드래그해도 옮긴다."));
        connect(again, &QPushButton::clicked, this, &KrxStudyPanel::anchor_requested);
        grid->addWidget(again, row++, 0, 1, 3, Qt::AlignLeft);
    }
    if (!spec.source && spec.inputs.isEmpty() && spec.lines.isEmpty())
        grid->addWidget(label(tr("바꿀 설정이 없다.")), row++, 0, 1, 4);
    grid->setColumnStretch(3, 1);
}

void KrxStudyPanel::restyle() {
    setStyleSheet(
        QString("#krxStudyPanel{background:%1;border:1px solid %2;}"
                "#krxStudyLists{background:%3;border-right:1px solid %2;}"
                "#krxStudyRight{background:%1;}"
                "#krxStudySection{color:%4;font-weight:700;padding:6px 4px 4px 4px;}"
                "#krxStudyName{text-align:left;color:%5;background:transparent;border:none;padding:3px 4px;}"
                "#krxStudyName:hover{background:%6;}"
                "#krxStudyName:checked{background:%6;color:%7;font-weight:700;}"
                "#krxStudyName:disabled{color:%8;}"
                "#krxStudyCheck{background:transparent;border:none;padding:1px;}"
                "#krxStudyTitle{color:%5;font-weight:700;}"
                "#krxStudyHelp{color:%4;border:1px solid %2;border-radius:8px;min-width:14px;max-width:14px;"
                "min-height:14px;max-height:14px;padding:0;font-weight:700;}"
                "#krxStudyAbout{color:%4;}"
                "#krxStudyLabel{color:%4;}"
                "#krxStudyNote{color:%7;}"
                "#krxStudyColor{background:transparent;border:1px solid transparent;padding:1px;}"
                "#krxStudyColor:hover{border-color:%2;}"
                "#krxStudyColor::menu-indicator{image:none;width:0;}"
                "#krxStudyDrop{color:%4;background:transparent;border:none;padding:0 4px;}"
                "#krxStudyDrop:hover{color:%5;}"
                "#krxStudyAdd{color:%7;background:transparent;border:1px solid %2;padding:3px 10px;}"
                "#krxStudyAdd:hover{border-color:%7;}"
                "#krxStudyAdd:disabled{color:%8;border-color:%9;}"
                "QSpinBox,QDoubleSpinBox,QComboBox{background:%3;color:%5;border:1px solid %2;padding:2px 4px;"
                "min-width:64px;}")
            .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_MED(), ui::colors::BG_BASE(), ui::colors::TEXT_TERTIARY(),
                 ui::colors::TEXT_PRIMARY(), ui::colors::BG_RAISED(), ui::colors::AMBER(), ui::colors::TEXT_DIM(),
                 ui::colors::BORDER_DIM()));
    fill_lists();
}

} // namespace fincept::screens
