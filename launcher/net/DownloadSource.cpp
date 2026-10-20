// SPDX-License-Identifier: GPL-3.0-only
#include "DownloadSource.h"

#include <QRegularExpression>

namespace Net::DownloadSource {
namespace {
bool safePath(const QString& path)
{
    if (!path.startsWith('/') || path.contains("//") || path.contains('\\') || path.contains('%')) {
        return false;
    }
    for (const auto& segment : path.split('/')) {
        if (segment == "." || segment == "..") {
            return false;
        }
    }
    return true;
}

bool matches(const QString& value, const QString& pattern)
{
    return QRegularExpression(QRegularExpression::anchoredPattern(pattern)).match(value).hasMatch();
}
}  // namespace

bool validCustomBase(const QString& value)
{
    const auto url = QUrl(value, QUrl::StrictMode);
    const auto path = url.path(QUrl::FullyEncoded);
    return !value.isEmpty() && !value.contains("/../") && !value.contains("/./") && !value.contains('\\') && url.isValid() &&
           url.scheme() == "https" && !url.host().isEmpty() && url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment() &&
           (path.isEmpty() || safePath(path)) && (path.isEmpty() || matches(path, "/[A-Za-z0-9_./-]*"));
}

QUrl baseUrl(Type type, const QString& customBase)
{
    if (type == Type::BMCLAPI) {
        return QUrl("https://bmclapi2.bangbang93.com");
    }
    if (type == Type::Custom && validCustomBase(customBase)) {
        return QUrl(customBase);
    }
    return {};
}

QUrl mirrorUrl(const QUrl& official, const QUrl& base, bool resourceOverride)
{
    if (!base.isValid() || base.isEmpty() || official.scheme() != "https" || official.port() != -1 || !official.userInfo().isEmpty() ||
        official.hasQuery() || official.hasFragment()) {
        return {};
    }
    const auto host = official.host().toLower();
    const auto path = official.path(QUrl::FullyEncoded);
    if (!safePath(path)) {
        return {};
    }

    QString targetPath;
    if (host == "resources.download.minecraft.net" && !resourceOverride && matches(path, "/[0-9a-fA-F]{2}/[0-9a-fA-F]{40}")) {
        targetPath = "/assets" + path;
    } else if (host == "libraries.minecraft.net" && matches(path, "/(?:[A-Za-z0-9_.-]+/)+[A-Za-z0-9_.-]+\\.(?:jar|zip|pom)")) {
        targetPath = "/maven" + path;
    } else if ((host == "piston-data.mojang.com" || host == "launcher.mojang.com") &&
               matches(path, "/v1/objects/[0-9a-fA-F]{40}/[A-Za-z0-9_.-]+")) {
        targetPath = path;
    } else if ((host == "piston-meta.mojang.com" || host == "launchermeta.mojang.com") &&
               (matches(path, "/v1/packages/[0-9a-fA-F]{40}/[A-Za-z0-9_.-]+\\.json") ||
                matches(path, "/mc/assets/legacy/[0-9a-fA-F]{40}/legacy\\.json"))) {
        targetPath = path;
    } else {
        return {};
    }

    auto mirror = base;
    auto prefix = base.path(QUrl::FullyEncoded);
    if (prefix.endsWith('/')) {
        prefix.chop(1);
    }
    mirror.setPath(prefix + targetPath, QUrl::StrictMode);
    return mirror;
}
}  // namespace Net::DownloadSource
