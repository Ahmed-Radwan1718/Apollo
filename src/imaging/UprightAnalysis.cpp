#include "imaging/UprightAnalysis.h"

#include "imaging/GeometryTransform.h"

#include <QPainterPath>
#include <QTransform>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace imaging {
namespace {

constexpr int kAnalysisMaxDimension = 900;
constexpr int kMaximumConstraintsPerAxis = 5000;
constexpr int kMinimumAutomaticConstraintsPerAxis = 45;
constexpr qreal kAxisAcceptance = 0.57;
constexpr qreal kSegmentHalfLength = 4.0;

struct SegmentSample
{
    QPointF start;
    QPointF end;
    qreal weight = 0.0;
};

struct ClassifiedSegments
{
    std::vector<SegmentSample> horizontal;
    std::vector<SegmentSample> vertical;
};

struct Candidate
{
    int vertical = 0;
    int horizontal = 0;
    int rotateHundredths = 0;
    int aspect = 0;
};

DevelopState analysisBaseState(const DevelopState &state)
{
    DevelopState base = state;

    // Upright is solved against the photo's crop/orientation, not against any
    // previous Upright or manual Transform correction.
    base.transformUprightVertical = 0;
    base.transformUprightHorizontal = 0;
    base.transformUprightRotateHundredths = 0;
    base.transformUprightAspect = 0;

    base.transformVertical = 0;
    base.transformHorizontal = 0;
    base.transformRotateHundredths = 0;
    base.transformAspect = 0;
    base.transformScale = 100;
    base.transformXOffset = 0;
    base.transformYOffset = 0;
    base.transformConstrainCrop = false;
    return base;
}

QImage analysisImage(const QImage &source)
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
    return image.convertToFormat(QImage::Format_Grayscale8);
}

void capStrongest(std::vector<SegmentSample> &samples)
{
    if (static_cast<int>(samples.size()) <= kMaximumConstraintsPerAxis) {
        return;
    }

    auto cutoff = samples.begin() + kMaximumConstraintsPerAxis;
    std::nth_element(samples.begin(),
                     cutoff,
                     samples.end(),
                     [](const SegmentSample &left, const SegmentSample &right) {
                         return left.weight > right.weight;
                     });
    samples.resize(kMaximumConstraintsPerAxis);
}

std::vector<SegmentSample> extractSegments(const QImage &source)
{
    std::vector<SegmentSample> result;
    const QImage gray = analysisImage(source);
    if (gray.width() < 8 || gray.height() < 8) {
        return result;
    }

    const int width = gray.width();
    const int height = gray.height();
    const qreal scaleX = static_cast<qreal>(source.width()) / width;
    const qreal scaleY = static_cast<qreal>(source.height()) / height;
    const auto pixel = [&gray](int x, int y) -> int {
        return gray.constScanLine(y)[x];
    };

    std::vector<float> magnitudes;
    magnitudes.reserve(static_cast<std::size_t>((width / 2) * (height / 2)));

    for (int y = 2; y < height - 2; y += 2) {
        for (int x = 2; x < width - 2; x += 2) {
            const int gx = -pixel(x - 1, y - 1) + pixel(x + 1, y - 1)
                - 2 * pixel(x - 1, y) + 2 * pixel(x + 1, y)
                - pixel(x - 1, y + 1) + pixel(x + 1, y + 1);
            const int gy = -pixel(x - 1, y - 1) - 2 * pixel(x, y - 1)
                - pixel(x + 1, y - 1) + pixel(x - 1, y + 1)
                + 2 * pixel(x, y + 1) + pixel(x + 1, y + 1);
            const float magnitude = std::hypot(static_cast<float>(gx),
                                               static_cast<float>(gy));
            if (magnitude >= 12.0F) {
                magnitudes.push_back(magnitude);
            }
        }
    }

    if (magnitudes.empty()) {
        return result;
    }

    const std::size_t percentileIndex = std::min<std::size_t>(
        magnitudes.size() - 1,
        static_cast<std::size_t>(magnitudes.size() * 0.80));
    std::nth_element(magnitudes.begin(),
                     magnitudes.begin() + static_cast<std::ptrdiff_t>(percentileIndex),
                     magnitudes.end());
    const qreal threshold = std::max<qreal>(28.0, magnitudes[percentileIndex]);

    result.reserve(14000);
    for (int y = 2; y < height - 2; y += 2) {
        for (int x = 2; x < width - 2; x += 2) {
            const int gx = -pixel(x - 1, y - 1) + pixel(x + 1, y - 1)
                - 2 * pixel(x - 1, y) + 2 * pixel(x + 1, y)
                - pixel(x - 1, y + 1) + pixel(x + 1, y + 1);
            const int gy = -pixel(x - 1, y - 1) - 2 * pixel(x, y - 1)
                - pixel(x + 1, y - 1) + pixel(x - 1, y + 1)
                + 2 * pixel(x, y + 1) + pixel(x + 1, y + 1);
            const qreal magnitude = std::hypot(static_cast<qreal>(gx),
                                               static_cast<qreal>(gy));
            if (magnitude < threshold) {
                continue;
            }

            const qreal inverseMagnitude = 1.0 / std::max<qreal>(1.0, magnitude);
            const qreal tangentX = -static_cast<qreal>(gy) * inverseMagnitude;
            const qreal tangentY = static_cast<qreal>(gx) * inverseMagnitude;

            SegmentSample sample;
            sample.start = QPointF((x - tangentX * kSegmentHalfLength) * scaleX,
                                   (y - tangentY * kSegmentHalfLength) * scaleY);
            sample.end = QPointF((x + tangentX * kSegmentHalfLength) * scaleX,
                                 (y + tangentY * kSegmentHalfLength) * scaleY);
            sample.weight = std::sqrt(magnitude);
            result.push_back(sample);
        }
    }

    if (result.size() > static_cast<std::size_t>(kMaximumConstraintsPerAxis * 3)) {
        auto cutoff = result.begin() + kMaximumConstraintsPerAxis * 3;
        std::nth_element(result.begin(),
                         cutoff,
                         result.end(),
                         [](const SegmentSample &left, const SegmentSample &right) {
                             return left.weight > right.weight;
                         });
        result.resize(kMaximumConstraintsPerAxis * 3);
    }
    return result;
}

ClassifiedSegments classifySegments(const QImage &source,
                                    const DevelopState &state)
{
    ClassifiedSegments result;
    const std::vector<SegmentSample> extracted = extractSegments(source);
    if (extracted.empty()) {
        return result;
    }

    const DevelopState base = analysisBaseState(state);
    const GeometryMapping mapping = geometryMappingForState(QSizeF(source.size()), base, false);
    if (!mapping.valid) {
        return result;
    }

    result.horizontal.reserve(extracted.size() / 2);
    result.vertical.reserve(extracted.size() / 2);
    for (const SegmentSample &sample : extracted) {
        const QPointF a = mapping.transform.map(sample.start);
        const QPointF b = mapping.transform.map(sample.end);
        const qreal dx = b.x() - a.x();
        const qreal dy = b.y() - a.y();
        const qreal length = std::hypot(dx, dy);
        if (length <= 1.0e-6) {
            continue;
        }

        const qreal horizontalError = std::abs(dy) / length;
        const qreal verticalError = std::abs(dx) / length;
        if (horizontalError <= verticalError && horizontalError <= kAxisAcceptance) {
            result.horizontal.push_back(sample);
        } else if (verticalError < horizontalError && verticalError <= kAxisAcceptance) {
            result.vertical.push_back(sample);
        }
    }

    capStrongest(result.horizontal);
    capStrongest(result.vertical);
    return result;
}

qreal groupAlignmentScore(const std::vector<SegmentSample> &samples,
                          const QTransform &transform,
                          bool targetVertical)
{
    if (samples.empty()) {
        return std::numeric_limits<qreal>::quiet_NaN();
    }

    qreal weightedScore = 0.0;
    qreal totalWeight = 0.0;
    for (const SegmentSample &sample : samples) {
        const QPointF a = transform.map(sample.start);
        const QPointF b = transform.map(sample.end);
        const qreal dx = b.x() - a.x();
        const qreal dy = b.y() - a.y();
        const qreal length = std::hypot(dx, dy);
        if (length <= 1.0e-6) {
            continue;
        }

        const qreal error = targetVertical ? std::abs(dx) / length
                                           : std::abs(dy) / length;
        const qreal quality = std::exp(-18.0 * error * error);
        weightedScore += quality * sample.weight;
        totalWeight += sample.weight;
    }

    return totalWeight > 0.0 ? weightedScore / totalWeight
                             : std::numeric_limits<qreal>::quiet_NaN();
}

qreal polygonArea(const QPolygonF &polygon)
{
    if (polygon.size() < 3) {
        return 0.0;
    }

    qreal twiceArea = 0.0;
    for (qsizetype index = 0; index < polygon.size(); ++index) {
        const QPointF a = polygon.at(index);
        const QPointF b = polygon.at((index + 1) % polygon.size());
        twiceArea += a.x() * b.y() - b.x() * a.y();
    }
    return std::abs(twiceArea) * 0.5;
}

qreal visibleCanvasCoverage(const GeometryMapping &mapping)
{
    const qreal canvasArea = mapping.canvasRect.width() * mapping.canvasRect.height();
    if (!mapping.valid || canvasArea <= 1.0e-9 || mapping.contentPolygon.size() < 3) {
        return 0.0;
    }

    QPainterPath contentPath;
    contentPath.addPolygon(mapping.contentPolygon);
    contentPath.closeSubpath();

    QPainterPath canvasPath;
    canvasPath.addRect(mapping.canvasRect);

    const QPolygonF visible = contentPath.intersected(canvasPath).toFillPolygon();
    return std::clamp(polygonArea(visible) / canvasArea, 0.0, 1.0);
}

qreal candidateScore(const QSizeF &sourceSize,
                     const DevelopState &base,
                     const ClassifiedSegments &segments,
                     const Candidate &candidate,
                     qreal horizontalWeight,
                     qreal verticalWeight,
                     qreal regularization,
                     qreal coverageWeight)
{
    DevelopState state = base;
    state.transformVertical = candidate.vertical;
    state.transformHorizontal = candidate.horizontal;
    state.transformRotateHundredths = candidate.rotateHundredths;
    state.transformAspect = candidate.aspect;

    const GeometryMapping mapping = geometryMappingForState(sourceSize, state, false);
    if (!mapping.valid) {
        return -std::numeric_limits<qreal>::infinity();
    }

    qreal alignmentScore = 0.0;
    qreal totalAxisWeight = 0.0;
    if (horizontalWeight > 0.0 && !segments.horizontal.empty()) {
        const qreal horizontal = groupAlignmentScore(segments.horizontal,
                                                     mapping.transform,
                                                     false);
        if (std::isfinite(horizontal)) {
            alignmentScore += horizontal * horizontalWeight;
            totalAxisWeight += horizontalWeight;
        }
    }
    if (verticalWeight > 0.0 && !segments.vertical.empty()) {
        const qreal vertical = groupAlignmentScore(segments.vertical,
                                                   mapping.transform,
                                                   true);
        if (std::isfinite(vertical)) {
            alignmentScore += vertical * verticalWeight;
            totalAxisWeight += verticalWeight;
        }
    }
    if (totalAxisWeight <= 0.0) {
        return -std::numeric_limits<qreal>::infinity();
    }

    alignmentScore /= totalAxisWeight;
    const qreal correctionMagnitude =
        std::abs(candidate.vertical) / 100.0
        + std::abs(candidate.horizontal) / 100.0
        + std::abs(candidate.rotateHundredths) / 1000.0
        + std::abs(candidate.aspect) / 100.0;

    const qreal coverageBonus = coverageWeight > 0.0
        ? coverageWeight * visibleCanvasCoverage(mapping)
        : 0.0;

    return alignmentScore + coverageBonus
        - regularization * correctionMagnitude;
}

Candidate optimizeCandidate(const QSizeF &sourceSize,
                            const DevelopState &base,
                            const ClassifiedSegments &segments,
                            qreal horizontalWeight,
                            qreal verticalWeight,
                            bool enableHorizontal,
                            bool enableVertical,
                            bool enableRotate,
                            bool enableAspect,
                            qreal regularization,
                            qreal coverageWeight)
{
    Candidate best;
    qreal bestScore = candidateScore(sourceSize,
                                     base,
                                     segments,
                                     best,
                                     horizontalWeight,
                                     verticalWeight,
                                     regularization,
                                     coverageWeight);

    constexpr std::array<int, 7> perspectiveSteps{28, 14, 8, 4, 2, 1, 1};
    constexpr std::array<int, 7> rotateSteps{250, 120, 60, 30, 12, 5, 2};
    constexpr std::array<int, 7> aspectSteps{18, 10, 5, 3, 2, 1, 1};

    for (std::size_t stage = 0; stage < perspectiveSteps.size(); ++stage) {
        const int perspectiveStep = perspectiveSteps[stage];
        const int rotateStep = rotateSteps[stage];
        const int aspectStep = aspectSteps[stage];
        for (int iteration = 0; iteration < 18; ++iteration) {
            Candidate nextBest = best;
            qreal nextScore = bestScore;

            const auto consider = [&](Candidate candidate) {
                candidate.vertical = std::clamp(candidate.vertical, -100, 100);
                candidate.horizontal = std::clamp(candidate.horizontal, -100, 100);
                candidate.rotateHundredths = std::clamp(candidate.rotateHundredths,
                                                        -1000,
                                                        1000);
                candidate.aspect = std::clamp(candidate.aspect, -100, 100);
                const qreal score = candidateScore(sourceSize,
                                                   base,
                                                   segments,
                                                   candidate,
                                                   horizontalWeight,
                                                   verticalWeight,
                                                   regularization,
                                                   coverageWeight);
                if (score > nextScore + 1.0e-8) {
                    nextScore = score;
                    nextBest = candidate;
                }
            };

            if (enableVertical) {
                Candidate positive = best;
                positive.vertical += perspectiveStep;
                consider(positive);
                Candidate negative = best;
                negative.vertical -= perspectiveStep;
                consider(negative);
            }
            if (enableHorizontal) {
                Candidate positive = best;
                positive.horizontal += perspectiveStep;
                consider(positive);
                Candidate negative = best;
                negative.horizontal -= perspectiveStep;
                consider(negative);
            }
            if (enableRotate) {
                Candidate positive = best;
                positive.rotateHundredths += rotateStep;
                consider(positive);
                Candidate negative = best;
                negative.rotateHundredths -= rotateStep;
                consider(negative);
            }
            if (enableAspect) {
                Candidate positive = best;
                positive.aspect += aspectStep;
                consider(positive);
                Candidate negative = best;
                negative.aspect -= aspectStep;
                consider(negative);
            }

            if (nextBest.vertical == best.vertical
                && nextBest.horizontal == best.horizontal
                && nextBest.rotateHundredths == best.rotateHundredths
                && nextBest.aspect == best.aspect) {
                break;
            }
            best = nextBest;
            bestScore = nextScore;
        }
    }

    constexpr std::array<int, 3> finalRotateSteps{5, 2, 1};
    if (enableRotate) {
        for (int step : finalRotateSteps) {
            for (;;) {
                Candidate positive = best;
                Candidate negative = best;
                positive.rotateHundredths = std::clamp(best.rotateHundredths + step,
                                                       -1000,
                                                       1000);
                negative.rotateHundredths = std::clamp(best.rotateHundredths - step,
                                                       -1000,
                                                       1000);
                const qreal positiveScore = candidateScore(sourceSize,
                                                           base,
                                                           segments,
                                                           positive,
                                                           horizontalWeight,
                                                           verticalWeight,
                                                           regularization,
                                                           coverageWeight);
                const qreal negativeScore = candidateScore(sourceSize,
                                                           base,
                                                           segments,
                                                           negative,
                                                           horizontalWeight,
                                                           verticalWeight,
                                                           regularization,
                                                           coverageWeight);
                if (positiveScore > bestScore + 1.0e-8
                    && positiveScore >= negativeScore) {
                    best = positive;
                    bestScore = positiveScore;
                } else if (negativeScore > bestScore + 1.0e-8) {
                    best = negative;
                    bestScore = negativeScore;
                } else {
                    break;
                }
            }
        }
    }
    return best;
}

ClassifiedSegments guidedSegments(const QSizeF &sourceSize,
                                  const std::vector<UprightGuideConstraint> &guides)
{
    ClassifiedSegments result;
    for (const UprightGuideConstraint &guide : guides) {
        SegmentSample sample;
        sample.start = QPointF(guide.startNormalized.x() * sourceSize.width(),
                               guide.startNormalized.y() * sourceSize.height());
        sample.end = QPointF(guide.endNormalized.x() * sourceSize.width(),
                             guide.endNormalized.y() * sourceSize.height());
        if (std::hypot(sample.end.x() - sample.start.x(),
                       sample.end.y() - sample.start.y()) <= 1.0e-6) {
            continue;
        }
        sample.weight = 1.0;
        if (guide.axis == UprightGuideAxis::Vertical) {
            result.vertical.push_back(sample);
        } else {
            result.horizontal.push_back(sample);
        }
    }
    return result;
}

UprightCorrection correctionFromCandidate(const QSizeF &sourceSize,
                                          const DevelopState &base,
                                          const ClassifiedSegments &segments,
                                          const Candidate &candidate,
                                          qreal horizontalWeight,
                                          qreal verticalWeight,
                                          qreal regularization,
                                          qreal coverageWeight)
{
    UprightCorrection result;
    const qreal score = candidateScore(sourceSize,
                                       base,
                                       segments,
                                       candidate,
                                       horizontalWeight,
                                       verticalWeight,
                                       regularization,
                                       coverageWeight);
    if (!std::isfinite(score)) {
        return result;
    }

    result.valid = true;
    result.vertical = candidate.vertical;
    result.horizontal = candidate.horizontal;
    result.rotateHundredths = candidate.rotateHundredths;
    result.aspect = candidate.aspect;
    result.score = score;
    return result;
}

} // namespace

UprightCorrection estimateAutomaticUpright(const QImage &image,
                                            const DevelopState &state,
                                            UprightMode mode)
{
    UprightCorrection result;
    if (image.isNull() || mode == UprightMode::Off || mode == UprightMode::Guided) {
        return result;
    }

    const ClassifiedSegments segments = classifySegments(image, state);
    const bool haveHorizontal =
        static_cast<int>(segments.horizontal.size()) >= kMinimumAutomaticConstraintsPerAxis;
    const bool haveVertical =
        static_cast<int>(segments.vertical.size()) >= kMinimumAutomaticConstraintsPerAxis;

    qreal horizontalWeight = 0.0;
    qreal verticalWeight = 0.0;
    bool enableHorizontal = false;
    bool enableVertical = false;
    bool enableRotate = false;
    bool enableAspect = false;
    qreal regularization = 0.01;
    qreal coverageWeight = 0.0;

    switch (mode) {
    case UprightMode::Level:
        if (!haveHorizontal) return result;
        horizontalWeight = 1.0;
        enableRotate = true;
        regularization = 0.006;
        coverageWeight = 0.02;
        break;

    case UprightMode::Vertical:
        if (!haveVertical) return result;
        verticalWeight = 1.0;
        horizontalWeight = haveHorizontal ? 0.35 : 0.0;
        enableVertical = true;
        enableRotate = true;
        regularization = 0.008;
        coverageWeight = 0.03;
        break;

    case UprightMode::Full:
        if (!haveHorizontal && !haveVertical) return result;
        horizontalWeight = haveHorizontal ? 1.0 : 0.0;
        verticalWeight = haveVertical ? 1.0 : 0.0;
        enableHorizontal = haveHorizontal;
        enableVertical = haveVertical;
        enableRotate = true;
        enableAspect = haveHorizontal && haveVertical;
        regularization = 0.003;
        coverageWeight = 0.0;
        break;

    case UprightMode::Auto:
        if (!haveHorizontal && !haveVertical) return result;
        horizontalWeight = haveHorizontal ? 1.0 : 0.0;
        verticalWeight = haveVertical ? 1.0 : 0.0;
        enableHorizontal = haveHorizontal;
        enableVertical = haveVertical;
        enableRotate = true;
        enableAspect = haveHorizontal && haveVertical;
        regularization = 0.015;
        // Auto deliberately trades a little alignment accuracy for keeping more
        // of the visible frame, which separates it from the aggressive Full mode.
        coverageWeight = 0.14;
        break;

    default:
        return result;
    }

    const DevelopState base = analysisBaseState(state);
    const Candidate best = optimizeCandidate(QSizeF(image.size()),
                                             base,
                                             segments,
                                             horizontalWeight,
                                             verticalWeight,
                                             enableHorizontal,
                                             enableVertical,
                                             enableRotate,
                                             enableAspect,
                                             regularization,
                                             coverageWeight);

    return correctionFromCandidate(QSizeF(image.size()),
                                   base,
                                   segments,
                                   best,
                                   horizontalWeight,
                                   verticalWeight,
                                   regularization,
                                   coverageWeight);
}

UprightCorrection estimateGuidedUpright(
    const QSizeF &sourceSize,
    const DevelopState &state,
    const std::vector<UprightGuideConstraint> &guides)
{
    UprightCorrection result;
    if (!sourceSize.isValid() || sourceSize.isEmpty() || guides.size() < 2) {
        return result;
    }

    const ClassifiedSegments segments = guidedSegments(sourceSize, guides);
    if (segments.horizontal.empty() && segments.vertical.empty()) {
        return result;
    }

    const qreal horizontalWeight = segments.horizontal.empty() ? 0.0 : 1.0;
    const qreal verticalWeight = segments.vertical.empty() ? 0.0 : 1.0;
    const bool enableHorizontal = !segments.horizontal.empty();
    const bool enableVertical = !segments.vertical.empty();
    const bool enableAspect = enableHorizontal && enableVertical;
    const DevelopState base = analysisBaseState(state);
    constexpr qreal regularization = 0.0015;
    constexpr qreal coverageWeight = 0.0;

    const Candidate best = optimizeCandidate(sourceSize,
                                             base,
                                             segments,
                                             horizontalWeight,
                                             verticalWeight,
                                             enableHorizontal,
                                             enableVertical,
                                             true,
                                             enableAspect,
                                             regularization,
                                             coverageWeight);

    return correctionFromCandidate(sourceSize,
                                   base,
                                   segments,
                                   best,
                                   horizontalWeight,
                                   verticalWeight,
                                   regularization,
                                   coverageWeight);
}

} // namespace imaging
