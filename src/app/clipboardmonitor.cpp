/*
    Copyright (c) 2018, Lukas Holecek <hluk@email.cz>

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

#include "common/action.h"
#include "common/appconfig.h"
#include "common/common.h"
#include "common/log.h"
#include "common/mimetypes.h"
#include "common/textdata.h"
#include "item/serialize.h"
#include "platform/platformclipboard.h"
#include "platform/platformwindow.h"

#include <QApplication>

namespace {

bool hasSameData(const QVariantMap &data, const QVariantMap &lastData)
{
    for (auto it = lastData.constBegin(); it != lastData.constEnd(); ++it) {
        const auto &format = it.key();
        if ( !format.startsWith(COPYQ_MIME_PREFIX)
             && !data.contains(format) )
        {
            return false;
        }
    }

    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        const auto &format = it.key();
        if ( !format.startsWith(COPYQ_MIME_PREFIX)
             && !data[format].toByteArray().isEmpty()
             && data[format] != lastData.value(format) )
        {
            return false;
        }
    }

    return true;
}

bool isClipboardDataHidden(const QVariantMap &data)
{
    return data.value(mimeHidden).toByteArray() == "1";
}

} // namespace

ClipboardMonitor::ClipboardMonitor(const QStringList &formats)
    : m_clipboard(createPlatformNativeInterface()->clipboard())
    , m_formats(formats)
{
    const AppConfig config;
    m_storeClipboard = config.option<Config::check_clipboard>();
    m_clipboardTab = config.option<Config::clipboard_tab>();

    m_clipboard->setFormats(formats);
    connect( m_clipboard.get(), &PlatformClipboard::changed,
             this, &ClipboardMonitor::onClipboardChanged );

#ifdef HAS_MOUSE_SELECTIONS
    m_storeSelection = config.option<Config::check_selection>();

    m_clipboardToSelection = config.option<Config::copy_clipboard>();
    m_selectionToClipboard = config.option<Config::copy_selection>();

    onClipboardChanged(ClipboardMode::Selection);
#endif
    onClipboardChanged(ClipboardMode::Clipboard);
}

void ClipboardMonitor::onClipboardChanged(ClipboardMode mode)
{
    QVariantMap data = m_clipboard->data(mode, m_formats);
    auto clipboardData = mode == ClipboardMode::Clipboard
            ? &m_clipboardData : &m_selectionData;

    if ( hasSameData(data, *clipboardData) ) {
        COPYQ_LOG( QString("Ignoring unchanged %1")
                   .arg(mode == ClipboardMode::Clipboard ? "clipboard" : "selection") );
        return;
    }

    *clipboardData = data;

    COPYQ_LOG( QString("%1 changed, owner is \"%2\"")
               .arg(mode == ClipboardMode::Clipboard ? "Clipboard" : "Selection",
                    getTextData(data, mimeOwner)) );

    if (mode != ClipboardMode::Clipboard) {
        const QString modeName = mode == ClipboardMode::Selection
                ? "selection"
                : "find buffer";
        data.insert(mimeClipboardMode, modeName);
    }

    // add window title of clipboard owner
    if ( !data.contains(mimeOwner) && !data.contains(mimeWindowTitle) ) {
        PlatformPtr platform = createPlatformNativeInterface();
        PlatformWindowPtr currentWindow = platform->getCurrentWindow();
        if (currentWindow)
            data.insert( mimeWindowTitle, currentWindow->getTitle().toUtf8() );
    }

#ifdef HAS_MOUSE_SELECTIONS
    if ( (mode == ClipboardMode::Clipboard ? m_clipboardToSelection : m_selectionToClipboard)
        && !data.contains(mimeOwner) )
    {
        const auto text = getTextData(data);
        if ( !text.isEmpty() ) {
            const auto targetData = mode == ClipboardMode::Clipboard
                    ? &m_selectionData : &m_clipboardData;
            const auto targetText = getTextData(*targetData);
            emit synchronizeSelection(mode, text, qHash(targetText));
        }
    }
#endif

    // run automatic commands
    if ( anySessionOwnsClipboardData(data) ) {
        emit clipboardChanged(data, ClipboardOwnership::Own);
    } else if ( isClipboardDataHidden(data) ) {
        emit clipboardChanged(data, ClipboardOwnership::Hidden);
    } else {
        const auto defaultTab = m_clipboardTab.isEmpty() ? defaultClipboardTabName() : m_clipboardTab;
        setTextData(&data, defaultTab, mimeCurrentTab);


#ifdef HAS_MOUSE_SELECTIONS
        if (mode == ClipboardMode::Clipboard ? m_storeClipboard : m_storeSelection) {
#else
        if (m_storeClipboard) {
#endif
            setTextData(&data, m_clipboardTab, mimeOutputTab);
        }

        emit clipboardChanged(data, ClipboardOwnership::Foreign);
    }
}
