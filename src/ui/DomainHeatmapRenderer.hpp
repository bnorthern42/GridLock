#pragma once
#include <QVulkanWindow>
#include <vector>

namespace gridlock::ui {

class DomainHeatmapRenderer : public QVulkanWindowRenderer {
public:
    DomainHeatmapRenderer(QVulkanWindow *w);

    void initResources() override;
    void initSwapChainResources() override;
    void releaseSwapChainResources() override;
    void releaseResources() override;

    void startNextFrame() override;

    void updateData(const std::vector<double>& rawData, int width, int height);

private:
    QVulkanWindow *m_window;
    std::vector<double> m_data;
    int m_width = 0;
    int m_height = 0;
};

} // namespace gridlock::ui
