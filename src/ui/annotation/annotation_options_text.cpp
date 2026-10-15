#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_option_widgets.h"
#include "ui/annotation/annotation_options.h"
#include "ui/annotation/annotation_toolbar_icons.h"
#include "ui/snip/capture_icons.h"
#include <QButtonGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QToolButton>
namespace snipaste::ui
{
using namespace annotation;
QWidget *AnnotationOptions::createTextPage()
{
    auto *pageText = new QWidget(this);
    auto *layoutText = new QHBoxLayout(pageText);
    layoutText->setSizeConstraint(QLayout::SetFixedSize);
    layoutText->setContentsMargins(0, 0, 0, 0);
    layoutText->setSpacing(2);

    auto *btnBold = new QToolButton(pageText);
    btnBold->setIcon(captureIcon(CaptureIcon::TextBold));
    btnBold->setFixedSize(24, 24);
    btnBold->setCheckable(true);
    btnBold->setToolTip(QStringLiteral("加粗"));
    btnBold->setFocusPolicy(Qt::NoFocus);
    layoutText->addWidget(btnBold);
    connect(btnBold, &QToolButton::toggled, this, [this](bool b) {
        if (m_canvas)
            m_canvas->setTextBold(b);
    });

    auto *btnItalic = new QToolButton(pageText);
    btnItalic->setIcon(captureIcon(CaptureIcon::TextItalic));
    btnItalic->setFixedSize(24, 24);
    btnItalic->setCheckable(true);
    btnItalic->setToolTip(QStringLiteral("斜体"));
    btnItalic->setFocusPolicy(Qt::NoFocus);
    layoutText->addWidget(btnItalic);
    connect(btnItalic, &QToolButton::toggled, this, [this](bool i) {
        if (m_canvas)
            m_canvas->setTextItalic(i);
    });

    auto *btnOutline = new QToolButton(pageText);
    btnOutline->setIcon(captureIcon(CaptureIcon::TextOutline));
    btnOutline->setFixedSize(24, 24);
    btnOutline->setCheckable(true);
    btnOutline->setToolTip(QStringLiteral("描边"));
    btnOutline->setFocusPolicy(Qt::NoFocus);
    layoutText->addWidget(btnOutline);
    connect(btnOutline, &QToolButton::toggled, this, [this](bool o) {
        if (m_canvas)
            m_canvas->setTextOutlineWidth(o ? 1.5 : 0.0);
    });

    auto *btnTextBg = new QToolButton(pageText);
    btnTextBg->setIcon(captureIcon(CaptureIcon::TextBackground));
    btnTextBg->setFixedSize(24, 24);
    btnTextBg->setCheckable(true);
    btnTextBg->setToolTip(QStringLiteral("背景填充"));
    btnTextBg->setFocusPolicy(Qt::NoFocus);
    layoutText->addWidget(btnTextBg);
    connect(btnTextBg, &QToolButton::toggled, this, [this](bool bg) {
        if (m_canvas)
            m_canvas->setTextBackground(bg);
    });

    layoutText->addWidget(makeSeparator(pageText));

    auto *comboFontFamily = new QComboBox(pageText);
    comboFontFamily->setFixedSize(110, 24);
    comboFontFamily->addItems({QStringLiteral("Microsoft YaHei UI"), QStringLiteral("SimSun"),
                               QStringLiteral("SimHei"), QStringLiteral("Segoe UI"),
                               QStringLiteral("Arial")});
    comboFontFamily->setFocusPolicy(Qt::NoFocus);
    layoutText->addWidget(comboFontFamily);
    connect(comboFontFamily, &QComboBox::currentTextChanged, this, [this](const QString &f) {
        if (m_canvas)
            m_canvas->setTextFontFamily(f);
    });

    auto *comboFontSize = new QComboBox(pageText);
    comboFontSize->setFixedSize(45, 24);
    for (int i = 1; i <= 10; ++i)
        comboFontSize->addItem(QString::number(i));
    comboFontSize->setCurrentText(QStringLiteral("4"));
    comboFontSize->setFocusPolicy(Qt::NoFocus);
    layoutText->addWidget(comboFontSize);
    connect(comboFontSize, &QComboBox::currentTextChanged, this, [this](const QString &s) {
        if (m_canvas)
            m_canvas->setTextFontSize(s.toInt() * 4.0 + 4.0);
    });

    layoutText->addWidget(makeSeparator(pageText));
    layoutText->addWidget(makeColorModule(pageText));
    return pageText;
}

QWidget *AnnotationOptions::createNumberPage()
{
    auto *pageNumber = new QWidget(this);
    auto *layoutNumber = new QHBoxLayout(pageNumber);
    layoutNumber->setSizeConstraint(QLayout::SetFixedSize);
    layoutNumber->setContentsMargins(0, 0, 0, 0);
    layoutNumber->setSpacing(2);

    auto *numStyleGroup = new QButtonGroup(pageNumber);
    numStyleGroup->setExclusive(true);
    const QList<QPair<CaptureIcon, int>> numStyles{
        {CaptureIcon::NumberFilled, 0}, {CaptureIcon::NumberDoubleRing, 1}, {CaptureIcon::NumberOutline, 2}};
    for (const auto &ns : numStyles)
    {
        auto *b = new QToolButton(pageNumber);
        b->setIcon(captureIcon(ns.first));
        b->setFixedSize(24, 24);
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        numStyleGroup->addButton(b);
        layoutNumber->addWidget(b);
        if (ns.second == 0)
            b->setChecked(true);
        connect(b, &QToolButton::clicked, this, [this, style = ns.second] {
            if (m_canvas)
                m_canvas->setNumberStyle(style);
        });
    }

    layoutNumber->addWidget(makeSeparator(pageNumber));

    auto *comboNumScale = new QComboBox(pageNumber);
    comboNumScale->setFixedSize(45, 24);
    for (int i = 1; i <= 10; ++i)
        comboNumScale->addItem(QString::number(i));
    comboNumScale->setCurrentText(QStringLiteral("5"));
    comboNumScale->setToolTip(QStringLiteral("字体缩放"));
    comboNumScale->setFocusPolicy(Qt::NoFocus);
    layoutNumber->addWidget(comboNumScale);
    connect(comboNumScale, &QComboBox::currentTextChanged, this, [this](const QString &s) {
        if (m_canvas)
            m_canvas->setNumberSize(s.toInt() * 4.0 + 4.0);
    });

    layoutNumber->addWidget(makeSeparator(pageNumber));
    layoutNumber->addWidget(makeColorModule(pageNumber));
    return pageNumber;
}
} // namespace snipaste::ui
