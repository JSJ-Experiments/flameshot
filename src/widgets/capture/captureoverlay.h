// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 OpenAI & Contributors

#pragma once

#include <QPointer>
#include <QPixmap>
#include <QRect>
#include <QRegion>
#include <QWidget>

class CaptureWidget;
class QScreen;

class CaptureOverlay : public QWidget
{
    Q_OBJECT

public:
    CaptureOverlay(CaptureWidget* controller,
                   QScreen* screen,
                   const QRect& viewportRect);

    void syncGeometry();
    QRect viewportRect() const { return m_viewportRect; }
    void prepareToClose();
    void refreshCache(const QRegion& dirtyRegion = QRegion());

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    QPoint mapToController(const QPointF& localPos) const;

    QPointer<CaptureWidget> m_controller;
    QPointer<QScreen> m_screen;
    QRect m_viewportRect;
    bool m_closing;
    QPixmap m_cachedFrame;
};
