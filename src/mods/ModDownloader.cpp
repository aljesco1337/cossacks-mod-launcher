#include "ModDownloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "PathBridge.h"

namespace mods {

namespace {

// Long enough for a slow connection to deliver the manifest.
constexpr int kManifestTimeoutMs = 20000;

// The archive is 20+ MB; this is an inactivity timeout, not a total limit.
constexpr int kArchiveTimeoutMs = 60000;

const char kUserAgent[] = "CossacksModLauncher";

bool IsSupportedUrl(const QUrl& url)
{
    return url.isValid() &&
        (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http"));
}

} // namespace

ModDownloader::ModDownloader(QObject* parent)
    : QObject(parent)
    , network_(new QNetworkAccessManager(this))
{
}

ModDownloader::~ModDownloader()
{
    AbortRequest();
    DiscardPartialFile();
}

void ModDownloader::SetManifestUrl(const QString& url)
{
    manifestUrl_ = url;
}

QString ModDownloader::ManifestUrl() const
{
    return manifestUrl_;
}

void ModDownloader::SetManifestETag(const QByteArray& etag)
{
    manifestETag_ = etag;
}

bool ModDownloader::IsBusy() const
{
    return reply_ != nullptr;
}

void ModDownloader::FetchManifest()
{
    AbortRequest();
    DiscardPartialFile();

    const QUrl url(manifestUrl_);

    if (!IsSupportedUrl(url))
    {
        emit Failed(Request::Manifest, Tr("no valid manifest address is configured"), false);
        return;
    }

    QNetworkRequest request(url);

    // Qt 5 defaults to not following redirects, while GitHub answers with one
    // when the requested file moves. Setting the policy explicitly keeps both
    // versions behaving the same.
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", kUserAgent);
    request.setRawHeader("Accept", "application/json");
    // Revalidate instead of trusting a possibly stale CDN copy.
    request.setRawHeader("Cache-Control", "no-cache");
    request.setTransferTimeout(kManifestTimeoutMs);

    if (!manifestETag_.isEmpty())
    {
        request.setRawHeader("If-None-Match", manifestETag_);
    }

    Start(request, Request::Manifest);
}

void ModDownloader::DownloadArchive(const QString& url, const QString& targetFilePath)
{
    AbortRequest();
    DiscardPartialFile();

    const QUrl target(url);

    if (!IsSupportedUrl(target))
    {
        emit Failed(Request::Archive, Tr("the manifest points to an unsupported download address"), false);
        return;
    }

    if (!QDir().mkpath(QFileInfo(targetFilePath).absolutePath()))
    {
        emit Failed(Request::Archive, Tr("the temporary folder could not be created"), false);
        return;
    }

    output_ = new QFile(targetFilePath);

    if (!output_->open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        const QString error = output_->errorString();
        delete output_;
        output_ = nullptr;

        emit Failed(Request::Archive, error, false);
        return;
    }

    outputPath_ = targetFilePath;

    QNetworkRequest request(target);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", kUserAgent);
    request.setRawHeader("Accept", "application/octet-stream");
    request.setTransferTimeout(kArchiveTimeoutMs);

    Start(request, Request::Archive);
}

void ModDownloader::Cancel()
{
    if (!IsBusy())
    {
        return;
    }

    const Request request = current_;
    cancelled_ = true;

    AbortRequest();
    DiscardPartialFile();

    emit Failed(request, Tr("the transfer was cancelled"), true);
}

void ModDownloader::Start(const QNetworkRequest& request, Request requestKind)
{
    current_ = requestKind;
    cancelled_ = false;
    reply_ = network_->get(request);

    connect(reply_, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total)
    {
        if (current_ == Request::Archive)
        {
            emit ArchiveProgress(received, total);
        }
    });

    connect(reply_, &QNetworkReply::readyRead, this, [this]()
    {
        if (current_ != Request::Archive || !output_)
        {
            return;
        }

        output_->write(reply_->readAll());
    });

    connect(reply_, &QNetworkReply::finished, this, [this]()
    {
        if (current_ == Request::Archive)
        {
            FinishArchive();
        }
        else
        {
            FinishManifest();
        }
    });
}

void ModDownloader::FinishManifest()
{
    QNetworkReply* reply = reply_;

    if (!reply)
    {
        return;
    }

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorText = reply->errorString();
    const QByteArray body = reply->readAll();
    const QByteArray etag = reply->rawHeader("ETag");
    const bool cancelled = cancelled_;

    reply_ = nullptr;
    reply->deleteLater();

    // A 304 surfaces as a network error, so the status code is examined first:
    // for a revalidated manifest "not modified" is a success.
    if (statusCode == 304)
    {
        emit ManifestNotModified();
        return;
    }

    if (error != QNetworkReply::NoError)
    {
        emit Failed(Request::Manifest, errorText, cancelled);
        return;
    }

    if (statusCode != 0 && (statusCode < 200 || statusCode >= 300))
    {
        emit Failed(Request::Manifest, Tr("the server answered with HTTP %1").arg(statusCode), false);
        return;
    }

    emit ManifestFetched(body, etag);
}

void ModDownloader::FinishArchive()
{
    QNetworkReply* reply = reply_;

    if (!reply)
    {
        return;
    }

    const QNetworkReply::NetworkError error = reply->error();
    const QString errorText = reply->errorString();
    const bool cancelled = cancelled_;
    const QString path = outputPath_;

    reply_ = nullptr;
    reply->deleteLater();

    const bool writeFailed = output_ && output_->error() != QFile::NoError;
    const QString writeError = output_ ? output_->errorString() : QString();

    CloseOutput();

    if (error != QNetworkReply::NoError)
    {
        DiscardPartialFile();
        emit Failed(Request::Archive, errorText, cancelled);
        return;
    }

    if (writeFailed)
    {
        DiscardPartialFile();
        emit Failed(Request::Archive, writeError, false);
        return;
    }

    outputPath_.clear();
    emit ArchiveFinished(path);
}

void ModDownloader::AbortRequest()
{
    QNetworkReply* reply = reply_;
    reply_ = nullptr;

    if (reply)
    {
        // Detached first, so the aborted reply cannot report a failure of its
        // own while we are already unwinding it.
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }

    CloseOutput();
}

void ModDownloader::CloseOutput()
{
    if (!output_)
    {
        return;
    }

    output_->close();
    delete output_;
    output_ = nullptr;
}

void ModDownloader::DiscardPartialFile()
{
    if (outputPath_.isEmpty())
    {
        return;
    }

    QFile::remove(outputPath_);
    outputPath_.clear();
}

} // namespace mods
