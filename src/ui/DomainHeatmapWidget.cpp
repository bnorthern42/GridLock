#include "DomainHeatmapWidget.hpp"
#include <QMouseEvent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <QDebug>

namespace gridlock::ui {

static const char* vertexShaderSource = R"(
#version 330 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;
out vec2 vTexCoord;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    vTexCoord = texCoord;
}
)";

static const char* fragmentShaderSource = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 fragColor;
uniform sampler2D dataTexture;
uniform float dataMin;
uniform float dataMax;

// Viridis color map approximation
vec3 viridis(float t) {
    const vec3 c0 = vec3(0.2777273272234177, 0.005407344544966578, 0.3340998053353061);
    const vec3 c1 = vec3(0.1050930431085774, 1.404613529898575, 1.384590162594685);
    const vec3 c2 = vec3(-0.3308618287255563, 0.214847559468213, 0.09509516302823659);
    const vec3 c3 = vec3(-4.634230498983486, -5.799100973351585, -19.33244095627987);
    const vec3 c4 = vec3(6.228269936347081, 14.17993336680509, 56.69055240028143);
    const vec3 c5 = vec3(4.776384997670288, -13.74514537750436, -65.35303263337234);
    const vec3 c6 = vec3(-5.435455855934631, 4.645852612178535, 26.3124352495832);
    return c0+t*(c1+t*(c2+t*(c3+t*(c4+t*(c5+t*c6)))));
}

void main() {
    float val = texture(dataTexture, vTexCoord).r;
    float normalized = 0.0;
    if (dataMax > dataMin) {
        normalized = (val - dataMin) / (dataMax - dataMin);
    }
    normalized = clamp(normalized, 0.0, 1.0);
    fragColor = vec4(viridis(normalized), 1.0);
}
)";

DomainHeatmapWidget::DomainHeatmapWidget(QWidget *parent)
    : QOpenGLWidget(parent), m_vbo(QOpenGLBuffer::VertexBuffer) {
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setUpdateBehavior(QOpenGLWidget::PartialUpdate);
}

DomainHeatmapWidget::~DomainHeatmapWidget() {
    makeCurrent();
    if (m_textureId) {
        glDeleteTextures(1, &m_textureId);
    }
    m_vao.destroy();
    m_vbo.destroy();
    doneCurrent();
}

void DomainHeatmapWidget::initializeGL() {
    initializeOpenGLFunctions();

    if (!m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource)) {
        qWarning() << "Vertex shader error:" << m_program.log();
    }
    if (!m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource)) {
        qWarning() << "Fragment shader error:" << m_program.log();
    }
    if (!m_program.link()) {
        qWarning() << "Shader program linking error:" << m_program.log();
    }

    m_vao.create();
    m_vao.bind();

    m_vbo.create();
    m_vbo.bind();

    // Quad vertices: position (x, y), texCoord (u, v)
    float vertices[] = {
        // First triangle
        -1.0f,  1.0f,  0.0f, 0.0f, // Top-left
        -1.0f, -1.0f,  0.0f, 1.0f, // Bottom-left
         1.0f, -1.0f,  1.0f, 1.0f, // Bottom-right
        // Second triangle
        -1.0f,  1.0f,  0.0f, 0.0f, // Top-left
         1.0f, -1.0f,  1.0f, 1.0f, // Bottom-right
         1.0f,  1.0f,  1.0f, 0.0f  // Top-right
    };

    m_vbo.allocate(vertices, sizeof(vertices));

    m_program.enableAttributeArray(0);
    m_program.setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));

    m_program.enableAttributeArray(1);
    m_program.setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));

    m_vao.release();

    glGenTextures(1, &m_textureId);
    glBindTexture(GL_TEXTURE_2D, m_textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void DomainHeatmapWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void DomainHeatmapWidget::paintGL() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (m_dataWidth == 0 || m_dataHeight == 0) return;

    m_program.bind();
    m_vao.bind();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_textureId);
    m_program.setUniformValue("dataTexture", 0);
    m_program.setUniformValue("dataMin", m_dataMin);
    m_program.setUniformValue("dataMax", m_dataMax);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    m_vao.release();
    m_program.release();
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

    makeCurrent();
    glBindTexture(GL_TEXTURE_2D, m_textureId);
    // Use GL_R32F for raw float data
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, width, height, 0, GL_RED, GL_FLOAT, floatData.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    doneCurrent();

    update();
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

bool DomainHeatmapWidget::event(QEvent *e) {
    if (e->type() == QEvent::Paint || e->type() == QEvent::UpdateRequest) {
        qDebug() << "Paint event triggered";
    }
    return QOpenGLWidget::event(e);
}

} // namespace gridlock::ui
