#pragma once

#include <QWidget>
#include <QVulkanWindow>
#include <vector>
#include <cstdint>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>

namespace gridlock::ui {

class DomainHeatmapRenderer;

class DomainHeatmapWindow : public QVulkanWindow {
    Q_OBJECT
public:
    QVulkanWindowRenderer *createRenderer() override;
    void setRenderer(DomainHeatmapRenderer *renderer) { m_renderer = renderer; }
    void loadData(const std::vector<double>& rawData, int width, int height, float min_val, float max_val);

private:
    DomainHeatmapRenderer *m_renderer = nullptr;
};

class DomainHeatmapWidget : public QWidget {
    Q_OBJECT

public:
    explicit DomainHeatmapWidget(QWidget *parent = nullptr);
    ~DomainHeatmapWidget() override;

    void loadData(const std::vector<double>& rawData, int width, int height, uintptr_t baseAddress = 0);
    void setDifferentialMode(bool enabled);
    void setVulkanInstance(QVulkanInstance *inst);

    QLineEdit* addressInput() const { return m_addressInput; }
    QSpinBox* rowsInput() const { return m_rowsInput; }
    QSpinBox* colsInput() const { return m_colsInput; }
    QPushButton* renderButton() const { return m_renderButton; }

signals:
    void cellClicked(int index, double value, uintptr_t absoluteMemoryAddress);
    void requestRender(const QString& expression, int rows, int cols);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    DomainHeatmapWindow *m_vulkanWindow;
    QWidget *m_container;
    
    QLineEdit *m_addressInput = nullptr;
    QSpinBox *m_rowsInput = nullptr;
    QSpinBox *m_colsInput = nullptr;
    QPushButton *m_renderButton = nullptr;

    int m_dataWidth = 0;
    int m_dataHeight = 0;
    uintptr_t m_baseAddress = 0;
    bool m_differentialMode = false;

    std::vector<double> m_previousData;
    std::vector<double> m_currentData;

    float m_dataMin = 0.0f;
    float m_dataMax = 1.0f;
};

} // namespace gridlock::ui
