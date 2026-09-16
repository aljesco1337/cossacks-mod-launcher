#include "ModStateStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "PathBridge.h"

namespace mods {

namespace {

QString StateFilePath(const QString& modDirectory)
{
    return QDir(modDirectory).filePath(ModStateStore::StateFileName());
}

} // namespace

QString ModStateStore::StateFileName()
{
    return QStringLiteral(".clv-mod-state.json");
}

QString ModStateStore::NowIso8601()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

std::optional<core::InstalledModState> ModStateStore::Read(const QString& modDirectory)
{
    QFile file(StateFilePath(modDirectory));

    if (!file.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::nullopt;
    }

    const QJsonObject object = document.object();

    core::InstalledModState state;
    state.schemaVersion = object.value(QStringLiteral("schemaVersion")).toInt(1);
    state.id = object.value(QStringLiteral("id")).toString().toUtf8().toStdString();
    state.versionNumber = object.value(QStringLiteral("versionNumber")).toInt();
    state.versionLabel = object.value(QStringLiteral("versionLabel")).toString().toUtf8().toStdString();
    state.sha256 = object.value(QStringLiteral("sha256")).toString().toUtf8().toStdString();
    state.installedAt = object.value(QStringLiteral("installedAt")).toString().toUtf8().toStdString();
    state.downloadUrl = object.value(QStringLiteral("downloadUrl")).toString().toUtf8().toStdString();

    return state;
}

bool ModStateStore::Write(const QString& modDirectory, const core::InstalledModState& state, QString& error)
{
    error.clear();

    if (!QDir().mkpath(modDirectory))
    {
        error = QStringLiteral("the folder %1 could not be created").arg(modDirectory);
        return false;
    }

    QJsonObject object;
    object.insert(QStringLiteral("schemaVersion"), state.schemaVersion);
    object.insert(QStringLiteral("id"), QString::fromStdString(state.id));
    object.insert(QStringLiteral("versionNumber"), state.versionNumber);
    object.insert(QStringLiteral("versionLabel"), QString::fromStdString(state.versionLabel));
    object.insert(QStringLiteral("sha256"), QString::fromStdString(state.sha256));
    object.insert(QStringLiteral("installedAt"), QString::fromStdString(state.installedAt));
    object.insert(QStringLiteral("downloadUrl"), QString::fromStdString(state.downloadUrl));

    QSaveFile file(StateFilePath(modDirectory));

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        error = file.errorString();
        return false;
    }

    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));

    if (!file.commit())
    {
        error = file.errorString();
        return false;
    }

    return true;
}

void ModStateStore::Remove(const QString& modDirectory)
{
    QFile::remove(StateFilePath(modDirectory));
}

} // namespace mods
