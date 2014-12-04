/*
    Copyright (c) 2014, Lukas Holecek <hluk@email.cz>

    This file is part of CopyQ.

    CopyQ is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CopyQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with CopyQ.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "clipboardmonitor.h"

#include "common/arguments.h"
#include "common/log.h"
#include "common/mimetypes.h"
#include "common/monitormessagecode.h"
#include "item/serialize.h"
#include "platform/platformclipboard.h"

#include <QApplication>

ClipboardMonitor::ClipboardMonitor(int &argc, char **argv)
    : Client()
    , App(createPlatformNativeInterface()->createMonitorApplication(argc, argv))
    , m_clipboard(createPlatformNativeInterface()->clipboard())
{
    Q_ASSERT(argc == 3);
    const QString serverName( QString::fromUtf8(argv[2]) );

#ifdef HAS_TESTS
    if ( serverName == QString("copyq_TEST") )
        QCoreApplication::instance()->setProperty("CopyQ_testing", true);
#endif

    Arguments arguments(
                createPlatformNativeInterface()->getCommandLineArguments(argc, argv) );
    if ( !startClientSocket(serverName, arguments) )
        exit(1);
}

void ClipboardMonitor::onClipboardChanged(PlatformClipboard::Mode mode)
{
    QVariantMap data = m_clipboard->data(mode, m_formats);

    if (mode != PlatformClipboard::Clipboard)
        data.insert(mimeClipboardMode, PlatformClipboard::Selection ? "selection" : "find buffer");

    // add window title of clipboard owner
    if ( !data.contains(mimeOwner) && !data.contains(mimeWindowTitle) ) {
        PlatformPtr platform = createPlatformNativeInterface();
        PlatformWindowPtr currentWindow = platform->getCurrentWindow();
        if (currentWindow)
            data.insert( mimeWindowTitle, currentWindow->getTitle().toUtf8() );
    }

    sendMessage( serializeData(data), MonitorClipboardChanged );
}

void ClipboardMonitor::onMessageReceived(const QByteArray &message, int messageCode)
{
    if (messageCode == MonitorPing) {
        sendMessage( QByteArray(), MonitorPong );
    } else if (messageCode == MonitorSettings) {
        QVariantMap settings;
        QDataStream stream(message);
        stream >> settings;

        if ( hasLogLevel(LogDebug) ) {
            COPYQ_LOG("Loading configuration:");
            foreach (const QString &key, settings.keys()) {
                const QVariant val = settings[key];
                const QString str = val.canConvert<QStringList>() ? val.toStringList().join(",")
                                                                  : val.toString();
                COPYQ_LOG( QString(" %1=%2").arg(key).arg(str) );
            }
        }

        if ( settings.contains("formats") )
            m_formats = settings["formats"].toStringList();

        connect( m_clipboard.data(), SIGNAL(changed(PlatformClipboard::Mode)),
                 this, SLOT(onClipboardChanged(PlatformClipboard::Mode)),
                 Qt::UniqueConnection );

        m_clipboard->loadSettings(settings);

        COPYQ_LOG("Configured");
    } else if (messageCode == MonitorChangeClipboard) {
        QVariantMap data;
        deserializeData(&data, message);
        m_clipboard->setData(PlatformClipboard::Clipboard, data);
    } else {
        log( QString("Unknown message code %1!").arg(messageCode), LogError );
    }
}

void ClipboardMonitor::onDisconnected()
{
    exit(0);
}
