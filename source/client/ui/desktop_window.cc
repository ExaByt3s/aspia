//
// Aspia Project
// Copyright (C) 2018 Dmitry Chapyshev <dmitry@aspia.ru>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//

#include "client/ui/desktop_window.h"

#include <QBrush>
#include <QDesktopWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QPalette>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>

#include "base/logging.h"
#include "build/version.h"
#include "client/ui/desktop_config_dialog.h"
#include "client/ui/desktop_panel.h"
#include "client/ui/system_info_window.h"
#include "common/clipboard.h"
#include "desktop/desktop_frame_qimage.h"

namespace client {

DesktopWindow::DesktopWindow(const ConnectData& connect_data, QWidget* parent)
    : ClientWindow(parent)
{
    createClient<ClientDesktop>(connect_data, this);

    setWindowTitle(createWindowTitle(connect_data));
    setMinimumSize(400, 300);

    desktop_ = new DesktopWidget(this, this);

    scroll_area_ = new QScrollArea(this);
    scroll_area_->setAlignment(Qt::AlignCenter);
    scroll_area_->setFrameShape(QFrame::NoFrame);
    scroll_area_->setAutoFillBackground(true);
    scroll_area_->setWidget(desktop_);

    QPalette palette(scroll_area_->palette());
    palette.setBrush(QPalette::Background, QBrush(QColor(25, 25, 25)));
    scroll_area_->setPalette(palette);

    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->addWidget(scroll_area_);

    panel_ = new DesktopPanel(connect_data.session_type, this);

    desktop_->enableKeyCombinations(panel_->sendKeyCombinations());

    connect(panel_, &DesktopPanel::keyCombination, desktop_, &DesktopWidget::executeKeyCombination);
    connect(panel_, &DesktopPanel::settingsButton, this, &DesktopWindow::changeSettings);
    connect(panel_, &DesktopPanel::switchToAutosize, this, &DesktopWindow::autosizeWindow);
    connect(panel_, &DesktopPanel::takeScreenshot, this, &DesktopWindow::takeScreenshot);
    connect(panel_, &DesktopPanel::scalingChanged, this, &DesktopWindow::onScalingChanged);
    connect(panel_, &DesktopPanel::screenSelected, desktopClient(), &ClientDesktop::sendScreen);
    connect(panel_, &DesktopPanel::powerControl, desktopClient(), &ClientDesktop::sendPowerControl);
    connect(panel_, &DesktopPanel::startRemoteUpdate, desktopClient(), &ClientDesktop::sendRemoteUpdate);
    connect(panel_, &DesktopPanel::startSystemInfo, desktopClient(), &ClientDesktop::sendSysInfoRequest);

    connect(panel_, &DesktopPanel::switchToFullscreen, [this](bool fullscreen)
    {
        if (fullscreen)
        {
            is_maximized_ = isMaximized();
            showFullScreen();
        }
        else
        {
            if (is_maximized_)
                showMaximized();
            else
                showNormal();
        }
    });

    connect(panel_, &DesktopPanel::keyCombinationsChanged,
            desktop_, &DesktopWidget::enableKeyCombinations);

    desktop_->installEventFilter(this);
    scroll_area_->viewport()->installEventFilter(this);

    clipboard_ = new common::Clipboard(this);
    connect(clipboard_, &common::Clipboard::clipboardEvent,
            desktopClient(), &ClientDesktop::sendClipboardEvent);

    connect(panel_, &DesktopPanel::startSession, [this](proto::SessionType session_type)
    {
        ConnectData connect_data = currentClient()->connectData();
        connect_data.session_type = session_type;
        ClientWindow::connectToHost(&connect_data);
    });
}

void DesktopWindow::resizeDesktopFrame(const QRect& screen_rect)
{
    QSize prev_size;

    desktop::Frame* frame = desktop_->desktopFrame();
    if (frame)
        prev_size = desktop_->desktopFrame()->size();

    desktop_->resizeDesktopFrame(screen_rect.size());

    if (!panel_->scaling())
    {
        desktop_->resize(screen_rect.width(), screen_rect.height());

        if (screen_rect.size() != prev_size && !isMaximized() && !isFullScreen())
            autosizeWindow();
    }
    else
    {
        onScalingChanged();
    }

    screen_top_left_ = screen_rect.topLeft();
}

void DesktopWindow::drawDesktopFrame()
{
    desktop_->update();
    panel_->update();
}

desktop::Frame* DesktopWindow::desktopFrame()
{
    return desktop_->desktopFrame();
}

void DesktopWindow::injectCursor(const QCursor& cursor)
{
    desktop_->setCursor(cursor);
}

void DesktopWindow::injectClipboard(const proto::desktop::ClipboardEvent& event)
{
    clipboard_->injectClipboardEvent(event);
}

void DesktopWindow::setScreenList(const proto::desktop::ScreenList& screen_list)
{
    panel_->setScreenList(screen_list);
}

void DesktopWindow::setSystemInfo(const proto::system_info::SystemInfo& system_info)
{
    SystemInfoWindow* window = new SystemInfoWindow(this);

    window->setSystemInfo(system_info);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
    window->activateWindow();
}

void DesktopWindow::sendPointerEvent(const QPoint& pos, uint32_t mask)
{
    if (panel_->autoScrolling() && !panel_->scaling())
    {
        QPoint cursor = desktop_->mapTo(scroll_area_, pos);
        QRect client_area = scroll_area_->rect();

        QScrollBar* hscrollbar = scroll_area_->horizontalScrollBar();
        QScrollBar* vscrollbar = scroll_area_->verticalScrollBar();

        if (!hscrollbar->isHidden())
            client_area.setHeight(client_area.height() - hscrollbar->height());

        if (!vscrollbar->isHidden())
            client_area.setWidth(client_area.width() - vscrollbar->width());

        scroll_delta_.setX(0);
        scroll_delta_.setY(0);

        if (client_area.width() < desktop_->width())
        {
            if (cursor.x() > client_area.width() - 150)
                scroll_delta_.setX(10);
            else if (cursor.x() < 150)
                scroll_delta_.setX(-10);
        }

        if (client_area.height() < desktop_->height())
        {
            if (cursor.y() > client_area.height() - 150)
                scroll_delta_.setY(10);
            else if (cursor.y() < 150)
                scroll_delta_.setY(-10);
        }

        if (!scroll_delta_.isNull())
        {
            if (scroll_timer_id_ == 0)
                scroll_timer_id_ = startTimer(15);
        }
        else if (scroll_timer_id_ != 0)
        {
            killTimer(scroll_timer_id_);
            scroll_timer_id_ = 0;
        }
    }
    else if (scroll_timer_id_ != 0)
    {
        killTimer(scroll_timer_id_);
        scroll_timer_id_ = 0;
    }

    ClientDesktop* client = desktopClient();

    int remote_scale_factor = client->connectData().desktop_config.scale_factor();
    if (remote_scale_factor)
    {
        const QSize& source_size = desktopFrame()->size();
        QSize scaled_size = desktop_->size();

        double scale_x = (scaled_size.width() * 100) / double(source_size.width());
        double scale_y = (scaled_size.height() * 100) / double(source_size.height());
        double scale = std::min(scale_x, scale_y);

        double x = (double(pos.x() * 10000) / (remote_scale_factor * scale))
            + screen_top_left_.x();
        double y = (double(pos.y() * 10000) / (remote_scale_factor * scale))
            + screen_top_left_.y();

        client->sendPointerEvent(QPoint(x, y), mask);
    }
}

void DesktopWindow::sendKeyEvent(uint32_t usb_keycode, uint32_t flags)
{
    desktopClient()->sendKeyEvent(usb_keycode, flags);
}

void DesktopWindow::changeSettings()
{
    const ConnectData& connect_data = currentClient()->connectData();

    QScopedPointer<DesktopConfigDialog> dialog(
        new DesktopConfigDialog(connect_data.session_type,
                                connect_data.desktop_config,
                                this));

    connect(dialog.get(), &DesktopConfigDialog::configChanged,
            this, &DesktopWindow::onConfigChanged);

    dialog->exec();
}

void DesktopWindow::onConfigChanged(const proto::desktop::Config& config)
{
    ClientDesktop* session = static_cast<ClientDesktop*>(currentClient());

    ConnectData& connect_data = session->connectData();
    connect_data.desktop_config = config;
    session->sendConfig(config);
}

void DesktopWindow::autosizeWindow()
{
    QRect screen_rect = QApplication::desktop()->availableGeometry(this);
    QSize window_size = desktop_->size() + frameSize() - size();

    if (window_size.width() < screen_rect.width() && window_size.height() < screen_rect.height())
    {
        showNormal();

        resize(desktop_->size());
        move(screen_rect.x() + (screen_rect.width() / 2 - window_size.width() / 2),
             screen_rect.y() + (screen_rect.height() / 2 - window_size.height() / 2));
    }
    else
    {
        showMaximized();
    }
}

void DesktopWindow::takeScreenshot()
{
    QString selected_filter;
    QString file_path = QFileDialog::getSaveFileName(this,
                                                     tr("Save File"),
                                                     QString(),
                                                     tr("PNG Image (*.png);;BMP Image (*.bmp)"),
                                                     &selected_filter);
    if (file_path.isEmpty() || selected_filter.isEmpty())
        return;

    desktop::FrameQImage* frame = dynamic_cast<desktop::FrameQImage*>(desktop_->desktopFrame());
    if (!frame)
        return;

    const char* format = nullptr;

    if (selected_filter.contains(QLatin1String("*.png")))
        format = "PNG";
    else if (selected_filter.contains(QLatin1String("*.bmp")))
        format = "BMP";

    if (!format)
        return;

    if (!frame->constImage().save(file_path, format))
        QMessageBox::warning(this, tr("Warning"), tr("Could not save image"), QMessageBox::Ok);
}

void DesktopWindow::onScalingChanged(bool enabled)
{
    desktop::Frame* frame = desktopFrame();
    if (!frame)
        return;

    QSize scaled_size = frame->size();

    if (enabled)
        scaled_size.scale(size(), Qt::KeepAspectRatio);

    desktop_->resize(scaled_size);
}

void DesktopWindow::timerEvent(QTimerEvent* event)
{
    if (event->timerId() == scroll_timer_id_)
    {
        if (scroll_delta_.x() != 0)
        {
            QScrollBar* scrollbar = scroll_area_->horizontalScrollBar();

            int pos = scrollbar->sliderPosition() + scroll_delta_.x();

            pos = std::max(pos, scrollbar->minimum());
            pos = std::min(pos, scrollbar->maximum());

            scrollbar->setSliderPosition(pos);
        }

        if (scroll_delta_.y() != 0)
        {
            QScrollBar* scrollbar = scroll_area_->verticalScrollBar();

            int pos = scrollbar->sliderPosition() + scroll_delta_.y();

            pos = std::max(pos, scrollbar->minimum());
            pos = std::min(pos, scrollbar->maximum());

            scrollbar->setSliderPosition(pos);
        }
    }

    QWidget::timerEvent(event);
}

void DesktopWindow::resizeEvent(QResizeEvent* event)
{
    panel_->move(QPoint(width() / 2 - panel_->width() / 2, 0));

    if (panel_->scaling())
        onScalingChanged();

    QWidget::resizeEvent(event);
}

void DesktopWindow::closeEvent(QCloseEvent* event)
{
    for (const auto& object : children())
    {
        QWidget* widget = dynamic_cast<QWidget*>(object);
        if (widget)
            widget->close();
    }

    QWidget::closeEvent(event);
}

void DesktopWindow::leaveEvent(QEvent* event)
{
    if (scroll_timer_id_)
    {
        killTimer(scroll_timer_id_);
        scroll_timer_id_ = 0;
    }

    QWidget::leaveEvent(event);
}

bool DesktopWindow::eventFilter(QObject* object, QEvent* event)
{
    if (object == desktop_)
    {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
        {
            QKeyEvent* key_event = dynamic_cast<QKeyEvent*>(event);
            if (key_event && key_event->key() == Qt::Key_Tab)
            {
                desktop_->doKeyEvent(key_event);
                return true;
            }
        }

        return false;
    }
    else if (object == scroll_area_->viewport())
    {
        if (event->type() == QEvent::Wheel)
        {
            QWheelEvent* wheel_event = dynamic_cast<QWheelEvent*>(event);
            if (wheel_event)
            {
                QPoint pos = desktop_->mapFromGlobal(wheel_event->globalPos());

                desktop_->doMouseEvent(wheel_event->type(),
                                       wheel_event->buttons(),
                                       pos,
                                       wheel_event->angleDelta());
                return true;
            }
        }
    }

    return QWidget::eventFilter(object, event);
}

void DesktopWindow::sessionStarted()
{
    if (currentClient()->connectData().session_type != proto::SESSION_TYPE_DESKTOP_MANAGE)
        return;

    QVersionNumber client_version(
        ASPIA_VERSION_MAJOR, ASPIA_VERSION_MINOR, ASPIA_VERSION_PATCH);

    if (client_version > desktopClient()->hostVersion())
        panel_->setUpdateAvaliable(true);
}

ClientDesktop* DesktopWindow::desktopClient()
{
    return static_cast<ClientDesktop*>(currentClient());
}

// static
QString DesktopWindow::createWindowTitle(const ConnectData& connect_data)
{
    QString session_name;
    if (connect_data.session_type == proto::SESSION_TYPE_DESKTOP_MANAGE)
    {
        session_name = tr("Aspia Desktop Manage");
    }
    else
    {
        DCHECK(connect_data.session_type == proto::SESSION_TYPE_DESKTOP_VIEW);
        session_name = tr("Aspia Desktop View");
    }

    QString computer_name;
    if (!connect_data.computer_name.isEmpty())
        computer_name = connect_data.computer_name;
    else
        computer_name = connect_data.address;

    return QString("%1 - %2").arg(computer_name).arg(session_name);
}

} // namespace client
