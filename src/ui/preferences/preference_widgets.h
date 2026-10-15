#pragma once
#include <QColor>
#include <QString>
class QWidget;
class QLabel;
class QPushButton;
class QToolButton;
class QLineEdit;
class QKeySequenceEdit;
class QKeySequence;
class QLayout;
namespace snipaste::ui::preferences
{
QLabel *makeHint(const QString &text, QWidget *parent);
QPushButton *makeColorButton(const QColor &color, QWidget *parent);
QPushButton *makeInfoButton(QWidget *parent);
QToolButton *makeCommandPreset(QLineEdit *edit, QWidget *parent);
QString defaultOutputFileNamePattern();
QString outputDirectoryFromPath(const QString &path);
QString outputPathForDirectory(const QString &directory, const QString &pattern);
QKeySequenceEdit *makeEditableHotkeyRow(const QString &label, const QKeySequence &shortcut, QWidget *parent);
QWidget *makeHotkeyRow(const QString &label, const QString &shortcut, QWidget *parent);
void compactLayoutTree(QLayout *layout);
} // namespace snipaste::ui::preferences
