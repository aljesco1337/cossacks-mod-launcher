#include "ModManifestParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cctype>

namespace mods {

namespace {

// The manifest may publish the digest in the OCI style ("sha256:<hex>"), which is
// what "sha256sum" output looks like once a prefix is prepended. Only the hex part
// is ever compared, so the prefix is dropped here, once.
std::string NormalizeDigest(const std::string& value)
{
    std::string digest = value;

    const std::size_t colon = digest.find(':');

    if (colon != std::string::npos && colon + 1 < digest.size())
    {
        digest = digest.substr(colon + 1);
    }

    const std::size_t first = digest.find_first_not_of(" \t\r\n");

    if (first == std::string::npos)
    {
        return {};
    }

    const std::size_t last = digest.find_last_not_of(" \t\r\n");
    digest = digest.substr(first, last - first + 1);

    std::transform(
        digest.begin(),
        digest.end(),
        digest.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

    return digest;
}

// Manifest text members are UTF-8. Numbers are accepted as well, so a manifest
// that publishes an id as a number does not break the launcher.
std::string TextMember(const QJsonObject& object, const char* key)
{
    const QJsonValue value = object.value(QLatin1String(key));

    if (value.isString())
    {
        return value.toString().toUtf8().toStdString();
    }

    if (value.isDouble())
    {
        return QString::number(value.toDouble(), 'f', 0).toUtf8().toStdString();
    }

    return {};
}

} // namespace

std::optional<core::ModManifest> ParseModManifest(const QByteArray& json, QString& error)
{
    error.clear();

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);

    if (parseError.error != QJsonParseError::NoError)
    {
        error = QStringLiteral("the manifest is not valid JSON (offset %1: %2)")
                    .arg(parseError.offset)
                    .arg(parseError.errorString());
        return std::nullopt;
    }

    if (!document.isObject())
    {
        error = QStringLiteral("the manifest is not a JSON object");
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    const QJsonValue modsValue = root.value(QStringLiteral("mods"));

    if (!modsValue.isObject())
    {
        error = QStringLiteral("the manifest has no \"mods\" object");
        return std::nullopt;
    }

    core::ModManifest manifest;
    manifest.schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt();

    // Mods known to work together with this one. Optional, so a manifest without
    // the member still parses; non-string entries are skipped.
    const QJsonArray compatible = root.value(QStringLiteral("compatibleMods")).toArray();

    for (const QJsonValue& value : compatible)
    {
        if (!value.isString())
        {
            continue;
        }

        const QString entry = value.toString().trimmed();

        if (!entry.isEmpty())
        {
            manifest.compatibleMods.push_back(entry.toStdString());
        }
    }

    const QJsonObject mods = modsValue.toObject();

    for (auto it = mods.constBegin(); it != mods.constEnd(); ++it)
    {
        if (!it.value().isObject())
        {
            continue;
        }

        const QJsonObject entry = it.value().toObject();

        core::ModRelease release;
        release.id = it.key().toUtf8().toStdString();
        release.workshopId = TextMember(entry, "workshopId");
        release.name = TextMember(entry, "name");
        release.versionLabel = TextMember(entry, "versionLabel");
        release.versionNumber = entry.value(QStringLiteral("versionNumber")).toInt();
        release.updatedAt = TextMember(entry, "updatedAt");
        release.downloadUrl = TextMember(entry, "downloadUrl");
        release.sha256 = NormalizeDigest(TextMember(entry, "sha256"));
        release.size = static_cast<long long>(entry.value(QStringLiteral("size")).toDouble());
        release.installDir = TextMember(entry, "installDir");
        release.archiveRoot = TextMember(entry, "archiveRoot");

        if (release.name.empty())
        {
            release.name = release.id;
        }

        manifest.mods.push_back(std::move(release));
    }

    // The launcher's own release. Optional, and only kept when it carries a
    // readable version and somewhere to send the user: a manifest that announces
    // a version without a page to open must not look like an update.
    //
    // The number is derived from the label rather than read from the JSON, so a
    // hand-edited manifest has one thing to get right instead of two that can
    // disagree.
    const QJsonObject app = root.value(QStringLiteral("app")).toObject();

    if (!app.isEmpty())
    {
        core::AppRelease release;
        release.versionLabel = TextMember(app, "versionLabel");
        release.releasePageUrl = TextMember(app, "releasePageUrl");
        release.versionNumber = core::VersionNumberFromLabel(release.versionLabel);

        if (release.versionNumber > 0 && !release.releasePageUrl.empty())
        {
            manifest.app = std::move(release);
        }
    }

    if (manifest.mods.empty())
    {
        error = QStringLiteral("the manifest does not list any mod");
        return std::nullopt;
    }

    std::sort(
        manifest.mods.begin(),
        manifest.mods.end(),
        [](const core::ModRelease& left, const core::ModRelease& right)
        {
            return left.id < right.id;
        });

    return manifest;
}

} // namespace mods
