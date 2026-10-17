#include "imaging/GpuDevelopPreviewRenderer.h"

#include <QColorSpace>
#include <QOffscreenSurface>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QSize>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace imaging {
namespace {

constexpr char kVertexShader[] = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
out vec2 vTexCoord;
void main()
{
    vTexCoord = aTexCoord;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)GLSL";

constexpr char kFragmentShader[] = R"GLSL(
#version 330 core
in vec2 vTexCoord;
out vec4 fragColor;

uniform sampler2D uSource;
uniform float uBaselineGain;
uniform float uExposureGain;
uniform float uHighlights;
uniform float uShadows;
uniform float uWhites;
uniform float uBlacks;
uniform float uContrast;
uniform float uSaturation;

const float kMiddleGray = 0.18;
const vec3 kLuma = vec3(0.2126, 0.7152, 0.0722);

float luminance(vec3 rgb)
{
    return dot(rgb, kLuma);
}

float integratedHighlightWeight(float sceneEv)
{
    const float startEv = 1.0;
    const float fullEv = 3.0;
    if (sceneEv <= startEv) return 0.0;
    float width = fullEv - startEv;
    if (sceneEv >= fullEv) return 0.5 * width + (sceneEv - fullEv);
    float x = (sceneEv - startEv) / width;
    return width * (x * x * x - 0.5 * x * x * x * x);
}

float integratedShadowWeight(float sceneEv)
{
    const float startEv = -1.0;
    const float fullEv = -3.0;
    if (sceneEv >= startEv) return 0.0;
    float width = startEv - fullEv;
    if (sceneEv <= fullEv) return 0.5 * width + (fullEv - sceneEv);
    float x = (startEv - sceneEv) / width;
    return width * (x * x * x - 0.5 * x * x * x * x);
}

float integratedWhitesWeight(float sceneEv)
{
    const float startEv = 1.75;
    const float fullEv = 2.75;
    if (sceneEv <= startEv) return 0.0;
    float width = fullEv - startEv;
    if (sceneEv >= fullEv) return 0.5 * width + (sceneEv - fullEv);
    float x = (sceneEv - startEv) / width;
    return width * (x * x * x - 0.5 * x * x * x * x);
}

float integratedBlacksWeight(float sceneEv)
{
    const float startEv = -2.0;
    const float fullEv = -4.0;
    if (sceneEv >= startEv) return 0.0;
    float width = startEv - fullEv;
    if (sceneEv <= fullEv) return 0.5 * width + (fullEv - sceneEv);
    float x = (startEv - sceneEv) / width;
    return width * (x * x * x - 0.5 * x * x * x * x);
}

vec3 applyHighlights(vec3 rgb)
{
    float y = luminance(rgb);
    if (uHighlights == 0.0 || y <= 0.0) return rgb;
    float w = integratedHighlightWeight(log2(y / kMiddleGray));
    float amount = abs(uHighlights);
    float slope = uHighlights < 0.0 ? -0.9 * amount : 0.5 * amount;
    return rgb * exp2(slope * w);
}

vec3 applyShadows(vec3 rgb)
{
    float y = luminance(rgb);
    if (uShadows == 0.0 || y <= 0.0) return rgb;
    float w = integratedShadowWeight(log2(y / kMiddleGray));
    float amount = abs(uShadows);
    float slope = uShadows > 0.0 ? 0.8 * amount : -0.5 * amount;
    return rgb * exp2(slope * w);
}

vec3 applyWhites(vec3 rgb)
{
    float y = luminance(rgb);
    if (uWhites == 0.0 || y <= 0.0) return rgb;
    float w = integratedWhitesWeight(log2(y / kMiddleGray));
    float amount = abs(uWhites);
    float slope = uWhites < 0.0 ? -0.9 * amount : 0.65 * amount;
    return rgb * exp2(slope * w);
}

vec3 applyBlacks(vec3 rgb)
{
    float y = luminance(rgb);
    if (uBlacks == 0.0 || y <= 0.0) return rgb;
    float w = integratedBlacksWeight(log2(y / kMiddleGray));
    float amount = abs(uBlacks);
    float slope = uBlacks > 0.0 ? 0.9 * amount : -0.65 * amount;
    return rgb * exp2(slope * w);
}

float contrastTone(float y)
{
    if (uContrast == 0.0 || y <= 0.0 || y >= 1.0) return y;
    const float pivot = 0.18;
    float exponentValue = exp2(uContrast);
    if (y < pivot) {
        return pivot * pow(y / pivot, exponentValue);
    }
    return 1.0 - (1.0 - pivot) * pow((1.0 - y) / (1.0 - pivot), exponentValue);
}

vec3 applyContrast(vec3 rgb)
{
    float y = luminance(rgb);
    if (uContrast == 0.0 || y <= 0.0) return rgb;
    float mapped = contrastTone(y);
    return rgb * (mapped / y);
}

vec3 applySaturation(vec3 rgb)
{
    if (uSaturation == 0.0) return rgb;
    float y = luminance(rgb);
    float scale = 1.0 + uSaturation;
    return vec3(y) + (rgb - vec3(y)) * scale;
}

vec3 linearToSrgb(vec3 value)
{
    value = clamp(value, vec3(0.0), vec3(1.0));
    vec3 low = 12.92 * value;
    vec3 high = 1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, lessThanEqual(value, vec3(0.0031308)));
}

void main()
{
    vec3 rgb = texture(uSource, vTexCoord).rgb;
    rgb *= uBaselineGain * uExposureGain;
    rgb = applyHighlights(rgb);
    rgb = applyShadows(rgb);
    rgb = applyWhites(rgb);
    rgb = applyBlacks(rgb);
    rgb = applyContrast(rgb);
    rgb = applySaturation(rgb);
    fragColor = vec4(linearToSrgb(rgb), 1.0);
}
)GLSL";

void setError(QString *error, QString message)
{
    if (error) {
        *error = std::move(message);
    }
}

} // namespace

struct GpuDevelopPreviewRenderer::Impl final : protected QOpenGLFunctions_3_3_Core
{
    explicit Impl(QOffscreenSurface *offscreenSurface)
        : surface(offscreenSurface)
    {
        if (!surface) {
            return;
        }

        context.setFormat(surface->format());
        context.setScreen(surface->screen());
        if (!context.create() || !context.makeCurrent(surface)) {
            return;
        }

        if (!initializeOpenGLFunctions()) {
            context.doneCurrent();
            return;
        }

        if (!program.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)
            || !program.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader)
            || !program.link()) {
            context.doneCurrent();
            return;
        }

        const std::array<float, 16> vertices{
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
            -1.0f,  1.0f, 0.0f, 1.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
        };

        if (!vao.create()) {
            context.doneCurrent();
            return;
        }

        bool bufferReady = false;
        {
            // Binder::~Binder() calls VAO release(), so it must run while this
            // context is still current. Never let this scope cross doneCurrent().
            QOpenGLVertexArrayObject::Binder vaoBinder(&vao);
            bufferReady = vertexBuffer.create();
            if (bufferReady) {
                vertexBuffer.bind();
                vertexBuffer.allocate(vertices.data(),
                                      static_cast<int>(vertices.size() * sizeof(float)));

                program.bind();
                program.enableAttributeArray(0);
                program.enableAttributeArray(1);
                program.setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
                program.setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
                program.release();
                vertexBuffer.release();
            }
        }

        if (!bufferReady) {
            vao.destroy();
            context.doneCurrent();
            return;
        }

        glGenTextures(1, &sourceTexture);
        available = sourceTexture != 0;
        context.doneCurrent();
    }

    ~Impl()
    {
        if (surface && context.makeCurrent(surface)) {
            if (sourceTexture != 0) {
                glDeleteTextures(1, &sourceTexture);
                sourceTexture = 0;
            }
            vao.destroy();
            vertexBuffer.destroy();
            context.doneCurrent();
        }
    }

    bool uploadSource(const std::uint16_t *samples,
                      int width,
                      int height,
                      std::uint64_t revision,
                      QString *error)
    {
        if (revision == uploadedRevision
            && width == uploadedWidth
            && height == uploadedHeight) {
            return true;
        }

        if (!samples || width <= 0 || height <= 0) {
            setError(error, QStringLiteral("GPU preview source is invalid."));
            return false;
        }

        glBindTexture(GL_TEXTURE_2D, sourceTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGB16,
                     width,
                     height,
                     0,
                     GL_RGB,
                     GL_UNSIGNED_SHORT,
                     samples);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_2D, 0);

        if (glGetError() != GL_NO_ERROR) {
            setError(error, QStringLiteral("Uploading the RAW preview to the GPU failed."));
            return false;
        }

        uploadedRevision = revision;
        uploadedWidth = width;
        uploadedHeight = height;
        return true;
    }

    QImage render(const std::uint16_t *samples,
                  int width,
                  int height,
                  int whitePoint,
                  std::uint64_t sourceRevision,
                  int exposureHundredths,
                  int highlights,
                  int shadows,
                  int whites,
                  int blacks,
                  int contrast,
                  int saturation,
                  QString *error)
    {
        if (!available || !surface || !context.makeCurrent(surface)) {
            setError(error, QStringLiteral("OpenGL GPU preview rendering is unavailable."));
            return {};
        }

        if (!uploadSource(samples, width, height, sourceRevision, error)) {
            context.doneCurrent();
            return {};
        }

        QImage image;
        bool framebufferReady = false;
        {
            // QOpenGLFramebufferObject owns GL resources. Destroy it before
            // doneCurrent() so its destructor always has a valid current context.
            QOpenGLFramebufferObjectFormat fboFormat;
            fboFormat.setAttachment(QOpenGLFramebufferObject::NoAttachment);
            fboFormat.setInternalTextureFormat(GL_RGBA8);
            QOpenGLFramebufferObject fbo(QSize(width, height), fboFormat);

            framebufferReady = fbo.isValid() && fbo.bind();
            if (framebufferReady) {
                glViewport(0, 0, width, height);
                glDisable(GL_BLEND);
                glDisable(GL_DEPTH_TEST);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);

                program.bind();
                program.setUniformValue("uSource", 0);
                program.setUniformValue("uBaselineGain",
                                        static_cast<float>(65535.0 / std::max(1, whitePoint)));
                program.setUniformValue("uExposureGain",
                                        static_cast<float>(std::exp2(
                                            static_cast<double>(std::clamp(exposureHundredths, -500, 500))
                                            / 100.0)));
                program.setUniformValue("uHighlights",
                                        static_cast<float>(std::clamp(highlights, -100, 100)) / 100.0f);
                program.setUniformValue("uShadows",
                                        static_cast<float>(std::clamp(shadows, -100, 100)) / 100.0f);
                program.setUniformValue("uWhites",
                                        static_cast<float>(std::clamp(whites, -100, 100)) / 100.0f);
                program.setUniformValue("uBlacks",
                                        static_cast<float>(std::clamp(blacks, -100, 100)) / 100.0f);
                program.setUniformValue("uContrast",
                                        static_cast<float>(std::clamp(contrast, -100, 100)) / 100.0f);
                program.setUniformValue("uSaturation",
                                        static_cast<float>(std::clamp(saturation, -100, 100)) / 100.0f);

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, sourceTexture);
                {
                    QOpenGLVertexArrayObject::Binder vaoBinder(&vao);
                    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                }
                glBindTexture(GL_TEXTURE_2D, 0);
                program.release();

                image = fbo.toImage(true);
                fbo.release();
            }
        }

        context.doneCurrent();

        if (!framebufferReady) {
            setError(error, QStringLiteral("Creating the GPU preview framebuffer failed."));
            return {};
        }

        if (image.isNull()) {
            setError(error, QStringLiteral("Reading the GPU preview framebuffer failed."));
            return {};
        }

        image.setDevicePixelRatio(1.0);
        image.setColorSpace(QColorSpace(QColorSpace::SRgb));
        return image;
    }

    QOffscreenSurface *surface = nullptr;
    QOpenGLContext context;
    QOpenGLShaderProgram program;
    QOpenGLBuffer vertexBuffer{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject vao;
    GLuint sourceTexture = 0;
    std::uint64_t uploadedRevision = 0;
    int uploadedWidth = 0;
    int uploadedHeight = 0;
    bool available = false;
};

GpuDevelopPreviewRenderer::GpuDevelopPreviewRenderer(QOffscreenSurface *surface)
    : m_impl(std::make_unique<Impl>(surface))
{
}

GpuDevelopPreviewRenderer::~GpuDevelopPreviewRenderer() = default;

bool GpuDevelopPreviewRenderer::isAvailable() const noexcept
{
    return m_impl && m_impl->available;
}

QImage GpuDevelopPreviewRenderer::render(const std::uint16_t *samples,
                                         int width,
                                         int height,
                                         int whitePoint,
                                         std::uint64_t sourceRevision,
                                         int exposureHundredths,
                                         int highlights,
                                         int shadows,
                                         int whites,
                                         int blacks,
                                         int contrast,
                                         int saturation,
                                         QString *error)
{
    if (error) {
        error->clear();
    }

    if (!m_impl) {
        setError(error, QStringLiteral("GPU preview renderer is unavailable."));
        return {};
    }

    return m_impl->render(samples,
                          width,
                          height,
                          whitePoint,
                          sourceRevision,
                          exposureHundredths,
                          highlights,
                          shadows,
                          whites,
                          blacks,
                          contrast,
                          saturation,
                          error);
}

} // namespace imaging
