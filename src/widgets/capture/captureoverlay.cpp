// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 OpenAI & Contributors

#include "captureoverlay.h"
#include "capturewidget.h"
#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QRegion>
#include <QScreen>
#include <QWindow>
#include <QWheelEvent>

CaptureOverlay::CaptureOverlay(CaptureWidget* controller,
                               QScreen* screen,
                               const QRect& viewportRect)
  : QWidget(nullptr,
            Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint | Qt::Tool)
  , m_controller(controller)
  , m_screen(screen)
  , m_viewportRect(viewportRect)
  , m_closing(false)
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    syncGeometry();
}

void CaptureOverlay::syncGeometry()
{
    if (!m_screen) {
        return;
    }

    setGeometry(m_screen->geometry());
    if (windowHandle()) {
        windowHandle()->setScreen(m_screen);
    }
}

void CaptureOverlay::prepareToClose()
{
    m_closing = true;
}

void CaptureOverlay::refreshCache(const QRegion& dirtyRegion)
{
    if (!m_controller) {
        return;
    }

    if (m_cachedFrame.size() != size()) {
        m_cachedFrame = QPixmap(size());
        m_cachedFrame.fill(Qt::transparent);
    }

    QRegion localDirty = dirtyRegion;
    if (localDirty.isEmpty()) {
        localDirty = rect();
    }

    QPainter cachePainter(&m_cachedFrame);
    for (const QRect& dirtyRect : localDirty) {
        cachePainter.save();
        cachePainter.setClipRect(dirtyRect);
        cachePainter.fillRect(dirtyRect, Qt::transparent);
        cachePainter.translate(-m_viewportRect.topLeft());
        m_controller->render(&cachePainter,
                             QPoint(),
                             QRegion(dirtyRect.translated(
                               m_viewportRect.topLeft())),
                             QWidget::DrawWindowBackground |
                               QWidget::DrawChildren);
        cachePainter.restore();
    }
}

void CaptureOverlay::paintEvent(QPaintEvent* event)
{
    if (!m_controller) {
        return;
    }

    if (m_cachedFrame.size() != size()) {
        refreshCache(rect());
    }

    QPainter painter(this);
    for (const QRect& dirtyRect : event->region()) {
        painter.drawPixmap(dirtyRect, m_cachedFrame, dirtyRect);
    }
}

void CaptureOverlay::mousePressEvent(QMouseEvent* event)
{
    if (m_controller) {
        m_controller->dispatchOverlayMouseEvent(event, mapToController(event->position()));
    }
}

void CaptureOverlay::mouseMoveEvent(QMouseEvent* event)
{
    if (m_controller) {
        m_controller->dispatchOverlayMouseEvent(event, mapToController(event->position()));
    }
}

void CaptureOverlay::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_controller) {
        m_controller->dispatchOverlayMouseEvent(event, mapToController(event->position()));
    }
}

void CaptureOverlay::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_controller) {
        m_controller->dispatchOverlayMouseEvent(event, mapToController(event->position()));
    }
}

void CaptureOverlay::wheelEvent(QWheelEvent* event)
{
    if (m_controller) {
        m_controller->dispatchOverlayWheelEvent(event, mapToController(event->position()));
    }
}

void CaptureOverlay::keyPressEvent(QKeyEvent* event)
{
    if (m_controller) {
        m_controller->dispatchOverlayKeyEvent(event);
    }
}

void CaptureOverlay::keyReleaseEvent(QKeyEvent* event)
{
    if (m_controller) {
        m_controller->dispatchOverlayKeyEvent(event);
    }
}

void CaptureOverlay::closeEvent(QCloseEvent* event)
{
    if (!m_closing && m_controller) {
        event->ignore();
        m_controller->close();
        return;
    }

    QWidget::closeEvent(event);
}

QPoint CaptureOverlay::mapToController(const QPointF& localPos) const
{
    return localPos.toPoint() + m_viewportRect.topLeft();
}
