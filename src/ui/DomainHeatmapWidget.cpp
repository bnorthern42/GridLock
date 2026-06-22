#include "DomainHeatmapWidget.hpp"
#include <QMouseEvent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <QDebug>

#include "DomainHeatmapRenderer.hpp"
#include <QVBoxLayout>

namespace gridlock::ui {

QVulkanWindowRenderer *DomainHeatmapWindow::createRenderer() {
    m_renderer = new DomainHeatmapRenderer(this);
    return m_renderer;
}

void DomainHeatmapWindow::loadData(const std::vector<double>& rawData, int width, int height, float min_val, float max_val) {
    if (m_renderer) {
        m_renderer->uploadData(rawData, height, width, min_val, max_val);
        requestUpdate();
    }
}

DomainHeatmapWidget::DomainHeatmapWidget(QWidget *parent)
    : QWidget(parent) {
    setMouseTracking(true);
    
    m_vulkanWindow = new DomainHeatmapWindow();
    
    m_container = QWidget::createWindowContainer(m_vulkanWindow, this);
    
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    
    QHBoxLayout *toolbar = new QHBoxLayout();
    m_addressInput = new QLineEdit(this);
    m_addressInput->setPlaceholderText("Variable/Address, e.g., &a");
    
    m_rowsInput = new QSpinBox(this);
    m_rowsInput->setPrefix("Rows: ");
    m_rowsInput->setRange(1, 99999);
    m_rowsInput->setValue(500);
    
    m_colsInput = new QSpinBox(this);
    m_colsInput->setPrefix("Cols: ");
    m_colsInput->setRange(1, 99999);
    m_colsInput->setValue(500);
    
    m_renderButton = new QPushButton("Render Frame", this);
    
    toolbar->addWidget(m_addressInput);
    toolbar->addWidget(m_rowsInput);
    toolbar->addWidget(m_colsInput);
    toolbar->addWidget(m_renderButton);
    
    layout->addLayout(toolbar);
    layout->addWidget(m_container);
    setLayout(layout);
}

DomainHeatmapWidget::~DomainHeatmapWidget() {
}

void DomainHeatmapWidget::setVulkanInstance(QVulkanInstance *inst) {
    if (m_vulkanWindow) {
        m_vulkanWindow->setVulkanInstance(inst);
    }
}



void DomainHeatmapWidget::setDifferentialMode(bool enabled) {
    if (m_differentialMode != enabled) {
        m_differentialMode = enabled;
        // Re-process current data if desired.
        if (!m_currentData.empty() && m_dataWidth > 0 && m_dataHeight > 0) {
            loadData(m_currentData, m_dataWidth, m_dataHeight, m_baseAddress);
        }
    }
}

void DomainHeatmapWidget::loadData(const std::vector<double>& rawData, int width, int height, uintptr_t baseAddress) {
    m_dataWidth = width;
    m_dataHeight = height;
    m_baseAddress = baseAddress;

    if (rawData.size() != static_cast<size_t>(width * height)) {
        return;
    }

    m_currentData = rawData;
    std::vector<float> floatData(rawData.size());

    double dMin = std::numeric_limits<double>::max();
    double dMax = std::numeric_limits<double>::lowest();

    for (size_t i = 0; i < rawData.size(); ++i) {
        double val = rawData[i];
        if (m_differentialMode && m_previousData.size() == rawData.size()) {
            val -= m_previousData[i];
        }
        
        if (val < dMin) dMin = val;
        if (val > dMax) dMax = val;
        
        floatData[i] = static_cast<float>(val);
    }

    if (dMin == std::numeric_limits<double>::max()) {
        dMin = 0.0;
        dMax = 1.0;
    }

    m_dataMin = static_cast<float>(dMin);
    m_dataMax = static_cast<float>(dMax);

    if (!m_differentialMode) {
        m_previousData = rawData;
    }

    m_vulkanWindow->loadData(m_currentData, width, height, m_dataMin, m_dataMax);
}

void DomainHeatmapWidget::mousePressEvent(QMouseEvent *event) {
    mouseMoveEvent(event);
}

void DomainHeatmapWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_dataWidth == 0 || m_dataHeight == 0 || m_currentData.empty()) return;

    int x = event->position().x();
    int y = event->position().y();

    if (x < 0 || x >= width() || y < 0 || y >= height()) return;

    // Map to texture coordinates
    int texX = (x * m_dataWidth) / width();
    int texY = (y * m_dataHeight) / height();

    // Ensure within bounds
    texX = std::clamp(texX, 0, m_dataWidth - 1);
    texY = std::clamp(texY, 0, m_dataHeight - 1);

    int index = texY * m_dataWidth + texX;
    if (index >= 0 && index < static_cast<int>(m_currentData.size())) {
        double val = m_currentData[index];
        if (m_differentialMode && m_previousData.size() == m_currentData.size()) {
            val -= m_previousData[index];
        }
        uintptr_t absoluteAddr = m_baseAddress + index * sizeof(double);
        emit cellClicked(index, val, absoluteAddr);
    }
}

} // namespace gridlock::ui
