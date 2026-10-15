#include "ui/annotation/annotation_dialog.h"
#include "ui/annotation/annotation_canvas.h"
#include "ui/clipboard_output.h"
#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace snipaste::ui {

AnnotationDialog::AnnotationDialog(const QImage &image, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("编辑截图"));
    resize(1000, 720);
    m_canvas = new AnnotationCanvas(image, this);

    auto *rectangle = new QToolButton(this);
    rectangle->setText(QStringLiteral("矩形"));
    auto *ellipse = new QToolButton(this);
    ellipse->setText(QStringLiteral("椭圆"));
    auto *roundedRectangle = new QToolButton(this);
    roundedRectangle->setText(QStringLiteral("圆角矩形"));
    auto *line = new QToolButton(this);
    line->setText(QStringLiteral("直线"));
    auto *arrow = new QToolButton(this);
    arrow->setText(QStringLiteral("箭头"));
    auto *pen = new QToolButton(this);
    pen->setText(QStringLiteral("画笔"));
    auto *text = new QToolButton(this);
    text->setText(QStringLiteral("文字"));
    auto *number = new QToolButton(this);
    number->setText(QStringLiteral("序号"));
    auto *magnifier = new QToolButton(this);
    magnifier->setText(QStringLiteral("放大镜标注"));
    auto *mosaic = new QToolButton(this);
    mosaic->setText(QStringLiteral("马赛克"));
    auto *blur = new QToolButton(this);
    blur->setText(QStringLiteral("模糊"));
    auto *eraser = new QToolButton(this);
    eraser->setText(QStringLiteral("橡皮擦"));
    auto *color = new QPushButton(QStringLiteral("颜色"), this);
    auto *width = new QSpinBox(this);
    width->setRange(1, 32);
    width->setValue(3);
    width->setSuffix(QStringLiteral(" px"));
    auto *lineStyle = new QComboBox(this);
    lineStyle->setObjectName(QStringLiteral("annotationLineStyle"));
    lineStyle->addItem(QStringLiteral("实线"), int(Qt::SolidLine));
    lineStyle->addItem(QStringLiteral("虚线"), int(Qt::DashLine));
    lineStyle->addItem(QStringLiteral("点线"), int(Qt::DotLine));
    lineStyle->addItem(QStringLiteral("点划线"), int(Qt::DashDotLine));
    lineStyle->addItem(QStringLiteral("双点划线"), int(Qt::DashDotDotLine));
    auto *textBackground = new QCheckBox(QStringLiteral("文字底色"), this);
    auto *undo = new QPushButton(QStringLiteral("撤销"), this);
    auto *redo = new QPushButton(QStringLiteral("重做"), this);
    auto *clear = new QPushButton(QStringLiteral("清空标注"), this);
    auto *copy = new QPushButton(QStringLiteral("复制"), this);
    auto *save = new QPushButton(QStringLiteral("保存…"), this);

    // The professional build keeps the compact toolbar.  Less frequently
    // used arrow and text-outline variants are available from the tool's
    // context menu, matching Snipaste's secondary tool settings without
    // widening the toolbar.
    arrow->setContextMenuPolicy(Qt::CustomContextMenu);
    text->setContextMenuPolicy(Qt::CustomContextMenu);

    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(rectangle);
    toolbar->addWidget(ellipse);
    toolbar->addWidget(roundedRectangle);
    toolbar->addWidget(line);
    toolbar->addWidget(arrow);
    toolbar->addWidget(pen);
    toolbar->addWidget(text);
    toolbar->addWidget(number);
    toolbar->addWidget(magnifier);
    toolbar->addWidget(mosaic);
    toolbar->addWidget(blur);
    toolbar->addWidget(eraser);
    toolbar->addWidget(color);
    toolbar->addWidget(width);
    toolbar->addWidget(lineStyle);
    toolbar->addWidget(textBackground);
    toolbar->addSpacing(12);
    toolbar->addWidget(undo);
    toolbar->addWidget(redo);
    toolbar->addWidget(clear);
    toolbar->addStretch();
    toolbar->addWidget(copy);
    toolbar->addWidget(save);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("完成"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(toolbar);
    layout->addWidget(m_canvas, 1);
    layout->addWidget(buttons);

    connect(rectangle, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Rectangle);
    });
    connect(ellipse, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Ellipse);
    });
    connect(roundedRectangle, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::RoundedRectangle);
    });
    connect(line, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Line);
    });
    connect(arrow, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Arrow);
    });
    connect(arrow, &QToolButton::customContextMenuRequested, this, [this, arrow](const QPoint &position) {
        QMenu menu;
        QActionGroup group(&menu);
        group.setExclusive(true);
        const auto addStyle = [&](const QString &label, AnnotationCanvas::ArrowStyle style) {
            auto *action = menu.addAction(label);
            action->setCheckable(true);
            action->setChecked(m_canvas->arrowStyle() == style);
            action->setData(static_cast<int>(style));
            group.addAction(action);
        };
        addStyle(QStringLiteral("末端箭头"), AnnotationCanvas::ArrowStyle::End);
        addStyle(QStringLiteral("起点箭头"), AnnotationCanvas::ArrowStyle::Start);
        addStyle(QStringLiteral("双向箭头"), AnnotationCanvas::ArrowStyle::Both);
        addStyle(QStringLiteral("空心末端"), AnnotationCanvas::ArrowStyle::HollowEnd);
        addStyle(QStringLiteral("空心双向"), AnnotationCanvas::ArrowStyle::HollowBoth);
        if (QAction *chosen = menu.exec(arrow->mapToGlobal(position)))
            m_canvas->setArrowStyle(static_cast<AnnotationCanvas::ArrowStyle>(chosen->data().toInt()));
    });
    connect(pen, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Pen);
    });
    connect(text, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Text);
    });
    connect(text, &QToolButton::customContextMenuRequested, this, [this, text](const QPoint &position) {
        QMenu menu;
        QAction *disable = menu.addAction(QStringLiteral("无描边"));
        QAction *colorAction = menu.addAction(QStringLiteral("描边颜色…"));
        QAction *widthAction = menu.addAction(QStringLiteral("描边宽度…"));
        QAction *chosen = menu.exec(text->mapToGlobal(position));
        if (chosen == disable) {
            m_canvas->setTextOutlineWidth(0.0);
        } else if (chosen == colorAction) {
            QColor color = QColorDialog::getColor(m_canvas->textOutlineColor(), this,
                                                  QStringLiteral("文字描边颜色"),
                                                  QColorDialog::ShowAlphaChannel);
            if (color.isValid()) {
                m_canvas->setTextOutlineColor(color);
                if (m_canvas->textOutlineWidth() <= 0.0)
                    m_canvas->setTextOutlineWidth(1.0);
            }
        } else if (chosen == widthAction) {
            bool ok = false;
            const double width = QInputDialog::getDouble(this, QStringLiteral("文字描边宽度"),
                                                         QStringLiteral("宽度 (px):"),
                                                         m_canvas->textOutlineWidth(), 0.0, 16.0,
                                                         1, &ok);
            if (ok)
                m_canvas->setTextOutlineWidth(width);
        }
    });
    connect(number, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Number);
    });
    connect(magnifier, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Magnifier);
    });
    connect(mosaic, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Mosaic);
    });
    connect(blur, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Blur);
    });
    connect(eraser, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Eraser);
    });
    connect(color, &QPushButton::clicked, this, [this, color] {
        const QColor selected = QColorDialog::getColor(Qt::red, this, QStringLiteral("选择标注颜色"));
        if (selected.isValid()) {
            m_canvas->setPenColor(selected);
            color->setStyleSheet(QStringLiteral("background-color: %1;").arg(selected.name()));
        }
    });
    connect(width, &QSpinBox::valueChanged, this, [this](int value) {
        m_canvas->setPenWidth(value);
    });
    connect(lineStyle, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, lineStyle](int index) {
        m_canvas->setPenStyle(Qt::PenStyle(lineStyle->itemData(index).toInt()));
    });
    connect(textBackground, &QCheckBox::toggled, m_canvas, &AnnotationCanvas::setTextBackground);
    connect(undo, &QPushButton::clicked, m_canvas, &AnnotationCanvas::undo);
    connect(redo, &QPushButton::clicked, m_canvas, &AnnotationCanvas::redo);
    connect(clear, &QPushButton::clicked, m_canvas, &AnnotationCanvas::clearAnnotations);
    connect(copy, &QPushButton::clicked, this, [this] {
        copyImageToClipboard(resultImage(), false, ClipboardImageSource::Screenshot);
    });
    connect(save, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("保存标注截图"),
            QStringLiteral("snipaste-annotated.png"),
            QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg)"));
        if (!path.isEmpty())
            resultImage().save(path);
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void AnnotationDialog::setAnnotationOptions(bool rememberColors, bool freeSelect, bool disableEraserEditing)
{
    m_canvas->setRememberToolColors(rememberColors);
    m_canvas->setFreeSelect(freeSelect);
    m_canvas->setDisableEraserEditing(disableEraserEditing);
}

QImage AnnotationDialog::resultImage() const
{
    m_canvas->finishTextInput();
    return m_canvas->renderedImage();
}

} // namespace snipaste::ui
