#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

namespace mods {

// Thin wrapper around QNetworkAccessManager covering the two requests the
// launcher needs: revalidating the manifest and downloading a release archive.
//
// Only one request is in flight at a time; starting another one aborts the
// first.
class ModDownloader : public QObject
{
    Q_OBJECT

public:
    // Which request a failure belongs to.
    enum class Request
    {
        Manifest,
        Archive,
    };

    explicit ModDownloader(QObject* parent = nullptr);
    ~ModDownloader() override;

    void SetManifestUrl(const QString& url);
    QString ManifestUrl() const;

    // Sent as If-None-Match so a repeated check transfers almost nothing.
    void SetManifestETag(const QByteArray& etag);

    void FetchManifest();
    void DownloadArchive(const QString& url, const QString& targetFilePath);
    void Cancel();

    bool IsBusy() const;

signals:
    void ManifestFetched(QByteArray body, QByteArray etag);
    void ManifestNotModified();
    void ArchiveProgress(qint64 received, qint64 total);
    void ArchiveFinished(QString filePath);

    // "cancelled" is true when the request was stopped by Cancel() rather than
    // by a network or server problem.
    void Failed(Request request, QString message, bool cancelled);

private:
    void Start(const QNetworkRequest& request, Request requestKind);
    void FinishManifest();
    void FinishArchive();
    void AbortRequest();
    void CloseOutput();
    void DiscardPartialFile();

    QNetworkAccessManager* network_ = nullptr;
    QNetworkReply* reply_ = nullptr;
    Request current_ = Request::Manifest;

    QString manifestUrl_;
    QByteArray manifestETag_;

    QFile* output_ = nullptr;
    QString outputPath_;

    bool cancelled_ = false;
};

} // namespace mods
