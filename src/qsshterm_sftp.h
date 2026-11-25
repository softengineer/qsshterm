#ifndef SSHTERM_SFTP_H
#define SSHTERM_SFTP_H

#include <libssh/libssh.h>
#include <libssh/sftp.h>
#include <QtWidgets>
#include <atomic>

#include "qsshterm_common.h"

// One entry of a remote directory listing
struct SftpEntry {
    QString name;
    qint64 size = 0;
    quint32 permissions = 0;
    qint64 mtime = 0;          // seconds since epoch
    bool isDir = false;
    bool isLink = false;
};
Q_DECLARE_METATYPE(SftpEntry)

class QSSHTerm_Sftp_Session;

/**
 * Two pane (local | remote) file manager for one site. Non-modal, deletes
 * itself on close. All SFTP I/O runs in a QSSHTerm_Sftp_Session living in its
 * own thread; the dialog only talks to it through queued signals.
 */
class QSSHTerm_Sftp_Dialog : public QDialog
{
    Q_OBJECT

public:
    explicit QSSHTerm_Sftp_Dialog(const SiteInfo &info, QWidget *parent = nullptr);
    ~QSSHTerm_Sftp_Dialog() override;

signals:
    // requests to the worker thread
    void req_connect();
    void req_list(QString dir);
    void req_mkdir(QString path);
    void req_remove(QString path, bool isDir, int gen);
    void req_rename(QString from, QString to);
    void req_download(QString remotePath, QString localDir, int gen);
    void req_upload(QString localPath, QString remoteDir, int gen);

private slots:
    void onConnected(QString home);
    void onListing(QString dir, QList<SftpEntry> entries);
    void onProgress(QString name, qint64 done, qint64 total);
    void onJobFinished(bool ok, QString message, bool remoteChanged, bool localChanged);
    void log(QString msg);

private:
    enum Column { ColName, ColSize, ColModified, ColPerms };

    SiteInfo siteInfo;
    QSSHTerm_Sftp_Session *session = nullptr;
    std::atomic<int> *cancelGen = nullptr;   // shared with the worker, owned by it

    QLineEdit *localPath = nullptr;
    QLineEdit *remotePath = nullptr;
    QTreeWidget *localList = nullptr;
    QTreeWidget *remoteList = nullptr;
    QPlainTextEdit *console = nullptr;
    QProgressBar *progress = nullptr;
    QLabel *status = nullptr;
    QPushButton *cancelButton = nullptr;
    QWidget *remotePane = nullptr;

    QString localDir;
    QString remoteDir;
    QString remoteHome;
    int pendingJobs = 0;

    void initGui();
    QTreeWidget *makeList();
    QWidget *makePane(const QString &title, QLineEdit **pathEdit, QTreeWidget **list,
                      bool remote);

    void listLocal(const QString &dir);
    void listRemote(const QString &dir);
    void fillItem(QTreeWidgetItem *item, const QString &name, bool isDir, bool isLink,
                  qint64 size, const QDateTime &mtime, const QString &perms);

    QList<QTreeWidgetItem *> selectedEntries(QTreeWidget *list) const;
    bool nameExists(QTreeWidget *list, const QString &name) const;
    QString remoteJoin(const QString &dir, const QString &name) const;
    void startJob();

    void localActivated(QTreeWidgetItem *item);
    void remoteActivated(QTreeWidgetItem *item);
    void uploadSelected();
    void downloadSelected();
    void deleteLocal();
    void deleteRemote();
    void renameLocal();
    void renameRemote();
    void mkdirLocal();
    void mkdirRemote();
    void cancelTransfers();
    void localContextMenu(const QPoint &pos);
    void remoteContextMenu(const QPoint &pos);
};

/**
 * Blocking SFTP worker. Lives in its own thread; every public slot is invoked
 * through a queued connection, so jobs run one at a time in request order.
 */
class QSSHTerm_Sftp_Session : public QObject
{
    Q_OBJECT
public:
    QSSHTerm_Sftp_Session(const SiteInfo &info, std::atomic<int> *cancelGen)
        : siteInfo(info), cancelGen(cancelGen) {}
    ~QSSHTerm_Sftp_Session() override;

signals:
    void connected(QString home);
    void listing(QString dir, QList<SftpEntry> entries);
    void progress(QString name, qint64 done, qint64 total);
    void jobFinished(bool ok, QString message, bool remoteChanged, bool localChanged);
    void message(QString msg);
    void finished();

public slots:
    void connect_to();
    void list(QString dir);
    void mkdir(QString path);
    void remove(QString path, bool isDir, int gen);
    void rename(QString from, QString to);
    void download(QString remotePath, QString localDir, int gen);
    void upload(QString localPath, QString remoteDir, int gen);
    void close();

private:
    ssh_session session = nullptr;
    sftp_session sftp = nullptr;
    SiteInfo siteInfo;
    std::atomic<int> *cancelGen;
    int jobGen = 0;
    QByteArray buffer;
    size_t readChunk = 32768;    // capped by the server's sftp limits
    size_t writeChunk = 32768;

    bool ensureConnected();
    void disconnectSession();
    QString lastError() const;
    bool cancelled() const { return cancelGen->load() != jobGen; }

    bool removeRecursive(const QString &path, bool isDir, QString *error);
    bool downloadPath(const QString &remotePath, const QString &localDir, bool followLink,
                      QString *error);
    bool downloadFile(const QString &remotePath, const QString &localFile, qint64 size,
                      quint32 perms, QString *error);
    bool uploadPath(const QString &localPath, const QString &remoteDir, QString *error);
    bool uploadFile(const QString &localFile, const QString &remoteFile, QString *error);
};

#endif
