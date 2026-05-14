#ifndef GLOBAL_SHORTCUTS_H
#define GLOBAL_SHORTCUTS_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QVariantMap>
#include <QDBusInterface>
#include <QDBusConnection>
#include <functional>

struct GsrShortcut {
    QString id;
    QString triggerDescription;
};

struct GsrBindShortcut {
    QString description;
    GsrShortcut shortcut;
};

using GsrDeactivatedCallback = std::function<void(const QString&)>;
using GsrShortcutCallback = std::function<void(const GsrShortcut&)>;

class GlobalShortcuts : public QObject {
    Q_OBJECT
public:
    explicit GlobalShortcuts(QObject *parent = nullptr);
    ~GlobalShortcuts();

    bool init();
    void deinit();
    bool listShortcuts();
    bool bindShortcuts(const QVector<GsrBindShortcut> &shortcuts);
    bool subscribeActivatedSignal(
        GsrDeactivatedCallback deactivatedCallback,
        GsrShortcutCallback shortcutChangedCallback);

signals:
    void initFinished(bool success);
    void shortcutsListed(const QVector<GsrShortcut> &shortcuts);
    void shortcutChanged(const GsrShortcut &shortcut);

private slots:
    void onCreateSessionResponse(uint response, const QVariantMap &results);
    void onListShortcutsResponse(uint response, const QVariantMap &results);
    void onBindShortcutsResponse(uint response, const QVariantMap &results);
    void onActivated(const QDBusObjectPath &sessionHandle,
                     const QString &shortcutId,
                     const QString &,
                     const QVariantMap &);
    void onShortcutsChanged(const QDBusObjectPath &sessionHandle,
                            const QVariant &shortcuts);

private:
    QString generateHandleToken();

    QDBusConnection m_connection;
    QDBusInterface *m_dbusPortal = nullptr;
    QString m_randomStr;
    unsigned int m_handleCounter = 0;
    QString m_sessionHandle;
    bool m_sessionCreated = false;
    QString m_pendingCreateSessionPath;
    QString m_pendingListShortcutsPath;
    QString m_pendingBindShortcutsPath;

    GsrDeactivatedCallback m_deactivatedCallback;
    GsrShortcutCallback m_shortcutChangedCallback;
};

#endif
