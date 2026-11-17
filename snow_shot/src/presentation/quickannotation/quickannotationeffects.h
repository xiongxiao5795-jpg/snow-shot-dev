#pragma once

#include "quickannotationoptions.h"

#include <QImage>
#include <QLinearGradient>
#include <QLineF>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QVector>
#include <QtMath>

namespace snow_shot::presentation {

// `points` and `origin` are logical widget coordinates; the source is a cropped
// physical-pixel snapshot of the current gesture. The hue is chosen once at press.
inline QImage quickAnnotationEffects(const QImage& source, QuickAnnotationColorMode mode, int hue,
                                     int glowWidth, const QVector<QPointF>& points,
                                     const QPointF& origin, qreal dpr, qreal strokeWidth) {
    if (source.isNull())
        return {};
    QImage stroke = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (mode != QuickAnnotationColorMode::Solid) {
        QImage colors(stroke.size(), QImage::Format_ARGB32_Premultiplied);
        colors.fill(Qt::transparent);
        QPainter painter(&colors);
        painter.setRenderHint(QPainter::Antialiasing);
        if (mode == QuickAnnotationColorMode::RandomStroke) {
            painter.fillRect(colors.rect(), QColor::fromHsv(hue, 230, 255));
        } else {
            QLinearGradient gradient(0, 0, qMax(1, colors.width()), 0);
            for (int i = 0; i <= 6; ++i)
                gradient.setColorAt(i / 6.0, QColor::fromHsv((hue + i * 60) % 360, 230, 255));
            painter.fillRect(colors.rect(), gradient);
            // Overlay the hue progression along the sampled stroke. The wide pen
            // fills the mask even when the canvas smooths the input points.
            qreal distance = 0;
            for (qsizetype i = 1; i < points.size(); ++i) {
                const QPointF a = (points[i - 1] - origin) * dpr;
                const QPointF b = (points[i] - origin) * dpr;
                const qreal length = QLineF(a, b).length();
                QLinearGradient segment(a, b);
                segment.setColorAt(
                    0, QColor::fromHsv((hue + qRound(distance * 1.8 / dpr)) % 360, 230, 255));
                distance += length;
                segment.setColorAt(
                    1, QColor::fromHsv((hue + qRound(distance * 1.8 / dpr)) % 360, 230, 255));
                QPen pen(QBrush(segment), (strokeWidth + 12) * dpr, Qt::SolidLine, Qt::RoundCap,
                         Qt::RoundJoin);
                painter.setPen(pen);
                painter.drawLine(a, b);
            }
        }
        painter.end();
        QPainter mask(&stroke);
        mask.setCompositionMode(QPainter::CompositionMode_SourceIn);
        mask.drawImage(QPoint(), colors);
    }
    if (glowWidth <= 0)
        return stroke;
    QImage result(stroke.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    const int radius = qMax(1, qRound(glowWidth * dpr));
    painter.setOpacity(0.1);
    for (int r = radius; r > 0; --r) {
        const int diagonal = qMax(1, qRound(r * 0.707));
        for (const QPoint offset : {QPoint(r, 0), QPoint(-r, 0), QPoint(0, r), QPoint(0, -r),
                                    QPoint(diagonal, diagonal), QPoint(-diagonal, diagonal),
                                    QPoint(diagonal, -diagonal), QPoint(-diagonal, -diagonal)})
            painter.drawImage(offset, stroke);
    }
    painter.setOpacity(1);
    painter.drawImage(QPoint(), stroke);
    return result;
}

} // namespace snow_shot::presentation
