#pragma once

#include <QOpenGLWidget>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <vector>
#include <cstdint>

namespace gridlock::ui {

class DomainHeatmapWidget : public QOpenGLWidget, protected QOpenGLExtraFunctions {
    Q_OBJECT

public:
    explicit DomainHeatmapWidget(QWidget *parent = nullptr);
    ~DomainHeatmapWidget() override;

    void loadData(const std::vector<double>& rawData, int width, int height, uintptr_t baseAddress = 0);
    void setDifferentialMode(bool enabled);

signals:
    void cellClicked(int index, double value, uintptr_t absoluteMemoryAddress);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    QOpenGLShaderProgram m_program;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo;
    GLuint m_textureId = 0;

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
