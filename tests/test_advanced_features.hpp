#pragma once
#include <QObject>

class TestAdvancedFeatures : public QObject {
    Q_OBJECT
private slots:
    void testDeadlockAnalyzer();
    void testFpeTrapper();
    void testConditionalBreakpoints();
    void testValueChangeHighlighting();
};
