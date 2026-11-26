#ifndef QSSH_ICONS_H
#define QSSH_ICONS_H

#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyle>
#include <initializer_list>

namespace qssh {

/** First icon of the desktop theme found in @p names, else a Qt standard icon. */
inline QIcon themeIcon(std::initializer_list<const char *> names, QStyle::StandardPixmap fallback)
{
    for (const char *name : names) {
        QIcon icon = QIcon::fromTheme(QString::fromLatin1(name));
        if (!icon.isNull())
            return icon;
    }
    return QApplication::style()->standardIcon(fallback);
}

enum class StatusIcon { Connected, Activity, Disconnected, Connecting };

/** Small painted tab status badges, independent of the icon theme. */
inline QIcon statusIcon(StatusIcon kind)
{
    QIcon icon;
    for (int size : {16, 32}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal s = size / 16.0;
        QPen pen;
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        switch (kind) {
        case StatusIcon::Connected: {   // green check mark
            pen.setColor(QColor(0x2e, 0xa0, 0x43));
            pen.setWidthF(2.4 * s);
            p.setPen(pen);
            QPainterPath path;
            path.moveTo(3 * s, 8.5 * s);
            path.lineTo(6.5 * s, 12 * s);
            path.lineTo(13 * s, 4.5 * s);
            p.drawPath(path);
            break;
        }
        case StatusIcon::Activity:      // blue dot: new output in a background tab
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x1e, 0x78, 0xd7));
            p.drawEllipse(QRectF(3.5 * s, 3.5 * s, 9 * s, 9 * s));
            break;
        case StatusIcon::Disconnected: { // red circle with a cross
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xd9, 0x30, 0x25));
            p.drawEllipse(QRectF(1.5 * s, 1.5 * s, 13 * s, 13 * s));
            pen.setColor(Qt::white);
            pen.setWidthF(1.8 * s);
            p.setPen(pen);
            p.drawLine(QPointF(5.5 * s, 5.5 * s), QPointF(10.5 * s, 10.5 * s));
            p.drawLine(QPointF(10.5 * s, 5.5 * s), QPointF(5.5 * s, 10.5 * s));
            break;
        }
        case StatusIcon::Connecting:    // amber ring
            pen.setColor(QColor(0xe0, 0x9b, 0x10));
            pen.setWidthF(2.2 * s);
            p.setPen(pen);
            p.drawArc(QRectF(3 * s, 3 * s, 10 * s, 10 * s), 90 * 16, -270 * 16);
            break;
        }
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

/** Lightning bolt, used for Quick Connect like SecureCRT. */
inline QIcon boltIcon()
{
    QIcon icon;
    for (int size : {16, 24, 32}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal s = size / 16.0;
        QPainterPath bolt;
        bolt.moveTo(9.5 * s, 1 * s);
        bolt.lineTo(3.5 * s, 9 * s);
        bolt.lineTo(7.5 * s, 9 * s);
        bolt.lineTo(6 * s, 15 * s);
        bolt.lineTo(12.5 * s, 6.5 * s);
        bolt.lineTo(8.5 * s, 6.5 * s);
        bolt.closeSubpath();
        p.setPen(QPen(QColor(0x8a, 0x5a, 0x00), 0.8 * s));
        p.setBrush(QColor(0xf5, 0xb8, 0x00));
        p.drawPath(bolt);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace qssh

#endif
