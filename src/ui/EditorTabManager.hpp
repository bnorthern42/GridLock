#pragma once
#include <QTabWidget>
#include <QString>
#include <QMap>
#include <QPoint>

namespace gridlock::ui {

class SourceCodeView;

class EditorTabManager : public QTabWidget {
    Q_OBJECT
public:
    explicit EditorTabManager(QWidget* parent = nullptr);
    ~EditorTabManager() override = default;

    SourceCodeView* openFile(const QString& filePath);
    SourceCodeView* currentSourceCodeView() const;
    SourceCodeView* getSourceCodeViewForFile(const QString& filePath) const;
    void clearAllTabs();

signals:
    void toggleBreakpointRequested(const QString& location);
    void breakpointToggled(const QString& file, int line, bool isSet, const QString& condition);
    void hoverVariableRequested(const QString& varName, const QPoint& globalPos);
    void semanticHoverRequested(const QString& file, int line, int character, const QPoint& globalPos);
    void pinVariableRequested(const QString& varName);

private slots:
    void onTabCloseRequested(int index);

private:
    QMap<QString, SourceCodeView*> m_openFiles;
};

} // namespace gridlock::ui
