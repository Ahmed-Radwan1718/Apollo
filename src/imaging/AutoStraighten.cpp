#include "imaging/AutoStraighten.h"

#include <QTransform>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

namespace imaging {
namespace {

constexpr int kAnalysisMaxDimension = 1280;
constexpr int kMagnitudeHistogramBins = 2048;
constexpr int kMaximumSamplesPerAxis = 50000;
constexpr int kMinimumSamplesPerAxis = 80;
constexpr qreal kMaximumAutoAngle = 30.0;
constexpr qreal kCoarseAngleStep = 0.25;
constexpr qreal kFineAngleStep = 0.02;
constexpr qreal kFineSearchRadius = 0.40;
constexpr qreal kSecondBestExclusion = 1.50;
constexpr qreal kSharpnessOffset = 0.50;
constexpr qreal kMinimumSupport = 0.012;
constexpr qreal kMinimumConfidence = 0.03;

struct EdgeSample
{
    float x = 0.0F;
    float y = 0.0F;
    float weight = 0.0F;
};

struct EdgeSets
{
    std::vector<EdgeSample> horizontal;
    std::vector<EdgeSample> vertical;
};

struct AxisEstimate
{
    bool valid = false;
    qreal tilt = 0.0;
    qreal score = 0.0;
    qreal support = 0.0;
    qreal confidence = 0.0;
    qreal quality = 0.0;
};

qreal normalizedLineAngle(qreal degrees)
{
    while (degrees >= 90.0) {
        degrees -= 180.0;
    }
    while (degrees < -90.0) {
        degrees += 180.0;
    }
    return degrees;
}

int normalizedQuarterTurns(int quarterTurns)
{
    int normalized = quarterTurns % 4;
    if (normalized < 0) {
        normalized += 4;
    }
    return normalized;
}

QImage prepareImage(const QImage &source, int quarterTurns)
{
    if (source.isNull()) {
        return {};
    }

    QImage image = source;
    const int maximumDimension = std::max(image.width(), image.height());
    if (maximumDimension > kAnalysisMaxDimension) {
        image = image.scaled(kAnalysisMaxDimension,
                             kAnalysisMaxDimension,
                             Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }

    const int turns = normalizedQuarterTurns(quarterTurns);
    if (turns != 0) {
        QTransform transform;
        transform.rotate(static_cast<qreal>(turns) * 90.0);
        image = image.transformed(transform, Qt::FastTransformation);
    }

    return image.convertToFormat(QImage::Format_Grayscale8);
}

int magnitudeThreshold(const std::vector<float> &magnitudes,
                       int width,
                       int height)
{
    std::array<int, kMagnitudeHistogramBins> histogram{};
    int sampleCount = 0;

    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            const float magnitude = magnitudes[static_cast<std::size_t>(y) * width + x];
            if (magnitude < 8.0F) {
                continue;
            }
            const int bin = std::clamp(static_cast<int>(std::lround(magnitude)),
                                       0,
                                       kMagnitudeHistogramBins - 1);
            ++histogram[bin];
            ++sampleCount;
        }
    }

    if (sampleCount == 0) {
        return kMagnitudeHistogramBins - 1;
    }

    const int percentileTarget = static_cast<int>(std::lround(sampleCount * 0.82));
    int cumulative = 0;
    int threshold = 0;
    for (int bin = 0; bin < kMagnitudeHistogramBins; ++bin) {
        cumulative += histogram[bin];
        if (cumulative >= percentileTarget) {
            threshold = bin;
            break;
        }
    }

    return std::max(24, threshold);
}

void capStrongestSamples(std::vector<EdgeSample> &samples)
{
    if (static_cast<int>(samples.size()) <= kMaximumSamplesPerAxis) {
        return;
    }

    auto cutoff = samples.begin() + kMaximumSamplesPerAxis;
    std::nth_element(samples.begin(),
                     cutoff,
                     samples.end(),
                     [](const EdgeSample &left, const EdgeSample &right) {
                         return left.weight > right.weight;
                     });
    samples.resize(kMaximumSamplesPerAxis);
}

EdgeSets extractEdges(const QImage &gray)
{
    EdgeSets result;
    const int width = gray.width();
    const int height = gray.height();
    if (width < 8 || height < 8) {
        return result;
    }

    std::vector<float> magnitudes(static_cast<std::size_t>(width) * height, 0.0F);

    const auto pixel = [&gray](int x, int y) -> int {
        return gray.constScanLine(y)[x];
    };

    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            const int gx = -pixel(x - 1, y - 1) + pixel(x + 1, y - 1)
                - 2 * pixel(x - 1, y) + 2 * pixel(x + 1, y)
                - pixel(x - 1, y + 1) + pixel(x + 1, y + 1);
            const int gy = -pixel(x - 1, y - 1) - 2 * pixel(x, y - 1)
                - pixel(x + 1, y - 1) + pixel(x - 1, y + 1)
                + 2 * pixel(x, y + 1) + pixel(x + 1, y + 1);
            magnitudes[static_cast<std::size_t>(y) * width + x] =
                std::hypot(static_cast<float>(gx), static_cast<float>(gy));
        }
    }

    const int threshold = magnitudeThreshold(magnitudes, width, height);
    result.horizontal.reserve(12000);
    result.vertical.reserve(12000);

    for (int y = 2; y < height - 2; ++y) {
        for (int x = 2; x < width - 2; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            const float magnitude = magnitudes[index];
            if (magnitude < static_cast<float>(threshold)) {
                continue;
            }

            const int gx = -pixel(x - 1, y - 1) + pixel(x + 1, y - 1)
                - 2 * pixel(x - 1, y) + 2 * pixel(x + 1, y)
                - pixel(x - 1, y + 1) + pixel(x + 1, y + 1);
            const int gy = -pixel(x - 1, y - 1) - 2 * pixel(x, y - 1)
                - pixel(x + 1, y - 1) + pixel(x - 1, y + 1)
                + 2 * pixel(x, y + 1) + pixel(x + 1, y + 1);

            const int absoluteGx = std::abs(gx);
            const int absoluteGy = std::abs(gy);
            float neighborA = 0.0F;
            float neighborB = 0.0F;
            if (absoluteGx >= absoluteGy * 2) {
                neighborA = magnitudes[index - 1];
                neighborB = magnitudes[index + 1];
            } else if (absoluteGy >= absoluteGx * 2) {
                neighborA = magnitudes[index - width];
                neighborB = magnitudes[index + width];
            } else if ((gx >= 0) == (gy >= 0)) {
                neighborA = magnitudes[index - width - 1];
                neighborB = magnitudes[index + width + 1];
            } else {
                neighborA = magnitudes[index - width + 1];
                neighborB = magnitudes[index + width - 1];
            }

            if (magnitude < neighborA || magnitude < neighborB) {
                continue;
            }

            qreal lineAngle = std::atan2(static_cast<qreal>(gy),
                                         static_cast<qreal>(gx))
                * 180.0 / std::numbers::pi_v<qreal>
                + 90.0;
            lineAngle = normalizedLineAngle(lineAngle);

            EdgeSample sample;
            sample.x = static_cast<float>(x);
            sample.y = static_cast<float>(y);
            sample.weight = magnitude;

            if (std::abs(lineAngle) <= kMaximumAutoAngle) {
                result.horizontal.push_back(sample);
            } else if (lineAngle >= 90.0 - kMaximumAutoAngle) {
                result.vertical.push_back(sample);
            } else if (lineAngle <= -90.0 + kMaximumAutoAngle) {
                result.vertical.push_back(sample);
            }
        }
    }

    capStrongestSamples(result.horizontal);
    capStrongestSamples(result.vertical);
    return result;
}

qreal lineScore(const std::vector<EdgeSample> &edges,
                qreal angleDegrees,
                bool horizontal,
                int width,
                int height,
                std::vector<float> &accumulator)
{
    const int radius = static_cast<int>(std::ceil(std::hypot(width, height))) + 4;
    const int requiredSize = radius * 2 + 5;
    if (static_cast<int>(accumulator.size()) != requiredSize) {
        accumulator.assign(requiredSize, 0.0F);
    } else {
        std::fill(accumulator.begin(), accumulator.end(), 0.0F);
    }

    const qreal radians = angleDegrees * std::numbers::pi_v<qreal> / 180.0;
    const qreal sine = std::sin(radians);
    const qreal cosine = std::cos(radians);
    const qreal offset = static_cast<qreal>(radius + 2);

    for (const EdgeSample &edge : edges) {
        const qreal rho = horizontal
            ? -static_cast<qreal>(edge.x) * sine + static_cast<qreal>(edge.y) * cosine
            : static_cast<qreal>(edge.x) * cosine + static_cast<qreal>(edge.y) * sine;
        const qreal shifted = rho + offset;
        const int lower = static_cast<int>(std::floor(shifted));
        if (lower < 0 || lower + 1 >= requiredSize) {
            continue;
        }
        const qreal fraction = shifted - lower;
        accumulator[lower] += static_cast<float>(edge.weight * (1.0 - fraction));
        accumulator[lower + 1] += static_cast<float>(edge.weight * fraction);
    }

    std::vector<std::pair<float, int>> peaks;
    peaks.reserve(accumulator.size() / 8);
    for (int i = 2; i < requiredSize - 2; ++i) {
        const float previous = 0.25F * accumulator[i - 2]
            + 0.50F * accumulator[i - 1]
            + 0.25F * accumulator[i];
        const float current = 0.25F * accumulator[i - 1]
            + 0.50F * accumulator[i]
            + 0.25F * accumulator[i + 1];
        const float next = 0.25F * accumulator[i]
            + 0.50F * accumulator[i + 1]
            + 0.25F * accumulator[i + 2];
        if (current >= previous && current >= next && current > 0.0F) {
            peaks.emplace_back(current, i);
        }
    }

    std::sort(peaks.begin(),
              peaks.end(),
              [](const auto &left, const auto &right) {
                  return left.first > right.first;
              });

    constexpr std::array<qreal, 6> peakWeights{1.0, 0.55, 0.35, 0.25, 0.18, 0.12};
    std::array<int, peakWeights.size()> selected{};
    int selectedCount = 0;
    qreal score = 0.0;

    for (const auto &[value, index] : peaks) {
        bool separated = true;
        for (int selectedIndex = 0; selectedIndex < selectedCount; ++selectedIndex) {
            if (std::abs(index - selected[selectedIndex]) < 4) {
                separated = false;
                break;
            }
        }
        if (!separated) {
            continue;
        }

        selected[selectedCount] = index;
        score += peakWeights[selectedCount] * static_cast<qreal>(value);
        ++selectedCount;
        if (selectedCount == static_cast<int>(peakWeights.size())) {
            break;
        }
    }

    return score;
}

AxisEstimate estimateAxis(const std::vector<EdgeSample> &edges,
                          bool horizontal,
                          int width,
                          int height)
{
    AxisEstimate result;
    if (static_cast<int>(edges.size()) < kMinimumSamplesPerAxis) {
        return result;
    }

    qreal totalWeight = 0.0;
    for (const EdgeSample &edge : edges) {
        totalWeight += edge.weight;
    }
    if (totalWeight <= std::numeric_limits<qreal>::epsilon()) {
        return result;
    }

    std::vector<float> accumulator;
    std::vector<std::pair<qreal, qreal>> coarseScores;
    coarseScores.reserve(static_cast<std::size_t>(
        std::ceil((kMaximumAutoAngle * 2.0) / kCoarseAngleStep)) + 1);

    qreal bestCoarseAngle = 0.0;
    qreal bestCoarseScore = -1.0;
    for (qreal angle = -kMaximumAutoAngle;
         angle <= kMaximumAutoAngle + 1.0e-9;
         angle += kCoarseAngleStep) {
        const qreal score = lineScore(edges,
                                      angle,
                                      horizontal,
                                      width,
                                      height,
                                      accumulator);
        coarseScores.emplace_back(angle, score);
        if (score > bestCoarseScore) {
            bestCoarseScore = score;
            bestCoarseAngle = angle;
        }
    }

    qreal refinedAngle = bestCoarseAngle;
    qreal refinedScore = bestCoarseScore;
    std::vector<std::pair<qreal, qreal>> fineScores;
    const qreal fineStart = std::max(-kMaximumAutoAngle,
                                     bestCoarseAngle - kFineSearchRadius);
    const qreal fineEnd = std::min(kMaximumAutoAngle,
                                   bestCoarseAngle + kFineSearchRadius);
    for (qreal angle = fineStart;
         angle <= fineEnd + 1.0e-9;
         angle += kFineAngleStep) {
        const qreal score = lineScore(edges,
                                      angle,
                                      horizontal,
                                      width,
                                      height,
                                      accumulator);
        fineScores.emplace_back(angle, score);
        if (score > refinedScore) {
            refinedScore = score;
            refinedAngle = angle;
        }
    }

    auto bestFine = std::max_element(
        fineScores.begin(),
        fineScores.end(),
        [](const auto &left, const auto &right) {
            return left.second < right.second;
        });
    if (bestFine != fineScores.end()) {
        refinedAngle = bestFine->first;
        refinedScore = bestFine->second;
        const auto index = std::distance(fineScores.begin(), bestFine);
        if (index > 0 && index + 1 < static_cast<std::ptrdiff_t>(fineScores.size())) {
            const qreal left = fineScores[static_cast<std::size_t>(index - 1)].second;
            const qreal center = bestFine->second;
            const qreal right = fineScores[static_cast<std::size_t>(index + 1)].second;
            const qreal denominator = left - 2.0 * center + right;
            if (std::abs(denominator) > 1.0e-9) {
                const qreal offset = std::clamp(
                    0.5 * (left - right) / denominator * kFineAngleStep,
                    -kFineAngleStep,
                    kFineAngleStep);
                refinedAngle = std::clamp(refinedAngle + offset,
                                          -kMaximumAutoAngle,
                                          kMaximumAutoAngle);
            }
        }
    }

    qreal secondBestScore = 0.0;
    for (const auto &[angle, score] : coarseScores) {
        if (std::abs(angle - refinedAngle) >= kSecondBestExclusion) {
            secondBestScore = std::max(secondBestScore, score);
        }
    }

    const qreal lowerScore = lineScore(edges,
                                       std::max(-kMaximumAutoAngle,
                                                refinedAngle - kSharpnessOffset),
                                       horizontal,
                                       width,
                                       height,
                                       accumulator);
    const qreal upperScore = lineScore(edges,
                                       std::min(kMaximumAutoAngle,
                                                refinedAngle + kSharpnessOffset),
                                       horizontal,
                                       width,
                                       height,
                                       accumulator);

    if (refinedScore <= std::numeric_limits<qreal>::epsilon()) {
        return result;
    }

    const qreal separation = std::clamp(
        (refinedScore - secondBestScore) / refinedScore,
        0.0,
        1.0);
    const qreal sharpness = std::clamp(
        (refinedScore - 0.5 * (lowerScore + upperScore)) / refinedScore,
        0.0,
        1.0);
    const qreal support = refinedScore / totalWeight;
    const qreal confidence = std::clamp(0.55 * separation + 0.45 * sharpness,
                                        0.0,
                                        1.0);
    const qreal supportQuality = std::clamp(support / 0.08, 0.0, 1.0);
    const qreal quality = (0.35 + 0.65 * confidence) * supportQuality;

    result.valid = support >= kMinimumSupport && confidence >= kMinimumConfidence;
    result.tilt = refinedAngle;
    result.score = refinedScore;
    result.support = support;
    result.confidence = confidence;
    result.quality = quality;
    return result;
}

} // namespace

AutoStraightenResult estimateAutoStraighten(const QImage &image, int quarterTurns)
{
    AutoStraightenResult result;
    const QImage prepared = prepareImage(image, quarterTurns);
    if (prepared.isNull()) {
        return result;
    }

    EdgeSets edges = extractEdges(prepared);
    const AxisEstimate horizontal = estimateAxis(edges.horizontal,
                                                 true,
                                                 prepared.width(),
                                                 prepared.height());
    const AxisEstimate vertical = estimateAxis(edges.vertical,
                                               false,
                                               prepared.width(),
                                               prepared.height());

    AxisEstimate chosen;
    if (horizontal.valid && vertical.valid) {
        const qreal difference = std::abs(horizontal.tilt - vertical.tilt);
        if (difference <= 0.75) {
            const qreal horizontalWeight = std::max(0.001, horizontal.quality);
            const qreal verticalWeight = std::max(0.001, vertical.quality);
            chosen.valid = true;
            chosen.tilt = (horizontal.tilt * horizontalWeight
                           + vertical.tilt * verticalWeight)
                / (horizontalWeight + verticalWeight);
            chosen.confidence = (horizontal.confidence * horizontalWeight
                                 + vertical.confidence * verticalWeight)
                / (horizontalWeight + verticalWeight);
            chosen.quality = std::max(horizontal.quality, vertical.quality);
        } else if (horizontal.quality >= vertical.quality * 0.85) {
            chosen = horizontal;
        } else if (vertical.quality >= horizontal.quality * 1.25) {
            chosen = vertical;
        } else {
            return result;
        }
    } else if (horizontal.valid) {
        chosen = horizontal;
    } else if (vertical.valid) {
        chosen = vertical;
    } else {
        return result;
    }

    result.valid = chosen.valid;
    result.degrees = std::clamp(-chosen.tilt,
                                -kMaximumAutoAngle,
                                kMaximumAutoAngle);
    result.confidence = chosen.confidence;
    return result;
}

} // namespace imaging
