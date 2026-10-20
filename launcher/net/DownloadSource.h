// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QUrl>

namespace Net::DownloadSource {

enum class Type { Official = 0, BMCLAPI = 1, Custom = 2 };

// A custom server must implement BMCLAPI's /assets, /maven and Mojang paths.
bool validCustomBase(const QString& value);
QUrl mirrorUrl(const QUrl& official, const QUrl& base, bool resourceOverride = false);
QUrl baseUrl(Type type, const QString& customBase);

}  // namespace Net::DownloadSource
