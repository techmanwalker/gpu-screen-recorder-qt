#include "global_shortcuts.h"
#include <QDBusPendingReply>
#include <QDBusObjectPath>
#include <QRandomGenerator>
#include <QDebug>

#define DBUS_RANDOM_STR_SIZE 16

static QString generateRandomString(int length) {
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    QString result;
    result.resize(length);
    for (int i = 0; i < length; ++i)
        result[i] = alphabet[QRandomGenerator::global()->bounded(62)];
    return result;
}

static QVector<GsrShortcut> parseShortcuts(const QVariant &shortcutsVar) {
    QVector<GsrShortcut> result;
    const QVariantList shortcutList = shortcutsVar.toList();
    for (const QVariant &item : shortcutList) {
        QVariantList tuple = item.toList();
        if (tuple.size() < 2)
            continue;
        GsrShortcut s;
        s.id = tuple[0].toString();
        const QVariantMap props = tuple[1].toMap();
        s.triggerDescription = props.value("trigger_description").toString();
        result.append(s);
    }
    return result;
}

GlobalShortcuts::GlobalShortcuts(QObject *parent)
    : QObject(parent)
    , m_connection(QDBusConnection::sessionBus())
{
    m_randomStr = generateRandomString(DBUS_RANDOM_STR_SIZE);
}

GlobalShortcuts::~GlobalShortcuts() {
    deinit();
}

QString GlobalShortcuts::generateHandleToken() {
    return QStringLiteral("gpu_screen_recorder_gtk_handle_%1_%2")
        .arg(m_randomStr).arg(m_handleCounter++);
}

bool GlobalShortcuts::init() {
    m_dbusPortal = new QDBusInterface(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.GlobalShortcuts",
        m_connection, this);

    if (!m_dbusPortal->isValid()) {
        qWarning("gsr: Failed to create D-Bus portal interface");
        delete m_dbusPortal;
        m_dbusPortal = nullptr;
        return false;
    }

    QString handleToken = generateHandleToken();
    QString sessionHandleToken = "gpu_screen_recorder_gtk";

    QVariantMap options;
    options["handle_token"] = handleToken;
    options["session_handle_token"] = sessionHandleToken;

    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.GlobalShortcuts",
        "CreateSession");
    msg << QVariant::fromValue(options);

    QDBusMessage reply = m_connection.call(msg, QDBus::BlockWithGui, 1000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        qWarning("gsr: CreateSession call failed");
        return false;
    }

    QString requestPath = reply.arguments().first().value<QDBusObjectPath>().path();
    m_pendingCreateSessionPath = requestPath;

    bool ok = m_connection.connect("org.freedesktop.portal.Desktop",
        requestPath, "org.freedesktop.portal.Request", "Response",
        this, SLOT(onCreateSessionResponse(uint, QVariantMap)));

    if (!ok) {
        qWarning("gsr: failed to connect CreateSession response");
        return false;
    }

    return true;
}

void GlobalShortcuts::onCreateSessionResponse(uint response, const QVariantMap &results) {
    if (!m_pendingCreateSessionPath.isEmpty()) {
        m_connection.disconnect("org.freedesktop.portal.Desktop",
            m_pendingCreateSessionPath, "org.freedesktop.portal.Request", "Response",
            this, SLOT(onCreateSessionResponse(uint, QVariantMap)));
        m_pendingCreateSessionPath.clear();
    }

    if (response != 0) {
        qWarning("gsr: CreateSession response error: %u", response);
        emit initFinished(false);
        return;
    }

    QString sessionHandle = results.value("session_handle").toString();
    if (!sessionHandle.isEmpty()) {
        m_sessionHandle = sessionHandle;
        m_sessionCreated = true;
        emit initFinished(true);
    } else {
        emit initFinished(false);
    }
}

void GlobalShortcuts::deinit() {
    if (m_sessionCreated && !m_sessionHandle.isEmpty()) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            "org.freedesktop.portal.Desktop",
            m_sessionHandle,
            "org.freedesktop.portal.Session",
            "Close");
        m_connection.send(msg);
    }

    if (!m_pendingCreateSessionPath.isEmpty()) {
        m_connection.disconnect("org.freedesktop.portal.Desktop",
            m_pendingCreateSessionPath, "org.freedesktop.portal.Request", "Response",
            this, SLOT(onCreateSessionResponse(uint, QVariantMap)));
        m_pendingCreateSessionPath.clear();
    }
    if (!m_pendingListShortcutsPath.isEmpty()) {
        m_connection.disconnect("org.freedesktop.portal.Desktop",
            m_pendingListShortcutsPath, "org.freedesktop.portal.Request", "Response",
            this, SLOT(onListShortcutsResponse(uint, QVariantMap)));
        m_pendingListShortcutsPath.clear();
    }
    if (!m_pendingBindShortcutsPath.isEmpty()) {
        m_connection.disconnect("org.freedesktop.portal.Desktop",
            m_pendingBindShortcutsPath, "org.freedesktop.portal.Request", "Response",
            this, SLOT(onBindShortcutsResponse(uint, QVariantMap)));
        m_pendingBindShortcutsPath.clear();
    }

    delete m_dbusPortal;
    m_dbusPortal = nullptr;
    m_sessionCreated = false;
    m_sessionHandle.clear();
}

bool GlobalShortcuts::listShortcuts() {
    if (!m_sessionCreated)
        return false;

    QString handleToken = generateHandleToken();
    QVariantMap options;
    options["handle_token"] = handleToken;

    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.GlobalShortcuts",
        "ListShortcuts");
    msg << QVariant::fromValue(QDBusObjectPath(m_sessionHandle))
        << QVariant::fromValue(options);

    QDBusMessage reply = m_connection.call(msg, QDBus::BlockWithGui, 1000);
    if (reply.type() != QDBusMessage::ReplyMessage)
        return false;

    QString requestPath = reply.arguments().first().value<QDBusObjectPath>().path();
    m_pendingListShortcutsPath = requestPath;

    bool ok = m_connection.connect("org.freedesktop.portal.Desktop",
        requestPath, "org.freedesktop.portal.Request", "Response",
        this, SLOT(onListShortcutsResponse(uint, QVariantMap)));

    return ok;
}

void GlobalShortcuts::onListShortcutsResponse(uint response, const QVariantMap &results) {
    if (!m_pendingListShortcutsPath.isEmpty()) {
        m_connection.disconnect("org.freedesktop.portal.Desktop",
            m_pendingListShortcutsPath, "org.freedesktop.portal.Request", "Response",
            this, SLOT(onListShortcutsResponse(uint, QVariantMap)));
        m_pendingListShortcutsPath.clear();
    }

    if (response != 0)
        return;

    QVariant shortcutsVar = results.value("shortcuts");
    QVector<GsrShortcut> shortcuts = parseShortcuts(shortcutsVar);
    emit shortcutsListed(shortcuts);
}

bool GlobalShortcuts::bindShortcuts(const QVector<GsrBindShortcut> &shortcuts) {
    if (!m_sessionCreated)
        return false;

    QString handleToken = generateHandleToken();
    QVariantMap options;
    options["handle_token"] = handleToken;

    QVariantList shortcutsVarList;
    for (const GsrBindShortcut &bs : shortcuts) {
        QVariantMap props;
        props["description"] = bs.description;
        props["preferred_trigger"] = bs.shortcut.triggerDescription;
        QVariantList entry;
        entry << bs.shortcut.id << QVariant(props);
        shortcutsVarList << QVariant(entry);
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.GlobalShortcuts",
        "BindShortcuts");
    msg << QVariant::fromValue(QDBusObjectPath(m_sessionHandle))
        << QVariant(shortcutsVarList)
        << QString("")
        << QVariant::fromValue(options);

    QDBusMessage reply = m_connection.call(msg, QDBus::BlockWithGui, 10000);
    if (reply.type() != QDBusMessage::ReplyMessage)
        return false;

    QString requestPath = reply.arguments().first().value<QDBusObjectPath>().path();
    m_pendingBindShortcutsPath = requestPath;

    bool ok = m_connection.connect("org.freedesktop.portal.Desktop",
        requestPath, "org.freedesktop.portal.Request", "Response",
        this, SLOT(onBindShortcutsResponse(uint, QVariantMap)));

    return ok;
}

void GlobalShortcuts::onBindShortcutsResponse(uint response, const QVariantMap &results) {
    if (!m_pendingBindShortcutsPath.isEmpty()) {
        m_connection.disconnect("org.freedesktop.portal.Desktop",
            m_pendingBindShortcutsPath, "org.freedesktop.portal.Request", "Response",
            this, SLOT(onBindShortcutsResponse(uint, QVariantMap)));
        m_pendingBindShortcutsPath.clear();
    }

    if (response != 0)
        return;

    QVariant shortcutsVar = results.value("shortcuts");
    QVector<GsrShortcut> parsed = parseShortcuts(shortcutsVar);
    for (const GsrShortcut &s : parsed)
        emit shortcutChanged(s);
}

bool GlobalShortcuts::subscribeActivatedSignal(
    GsrDeactivatedCallback deactivatedCallback,
    GsrShortcutCallback shortcutChangedCallback)
{
    if (!m_sessionCreated)
        return false;

    m_deactivatedCallback = deactivatedCallback;
    m_shortcutChangedCallback = shortcutChangedCallback;

    bool ok = m_connection.connect(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.GlobalShortcuts",
        "Deactivated",
        this,
        SLOT(onActivated(QDBusObjectPath, QString, QString, QVariantMap)));

    if (!ok)
        return false;

    ok = m_connection.connect(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.GlobalShortcuts",
        "ShortcutsChanged",
        this,
        SLOT(onShortcutsChanged(QDBusObjectPath, QVariant)));

    return ok;
}

void GlobalShortcuts::onActivated(const QDBusObjectPath &sessionHandle,
                                   const QString &shortcutId,
                                   const QString &,
                                   const QVariantMap &)
{
    if (sessionHandle.path() == m_sessionHandle && m_deactivatedCallback)
        m_deactivatedCallback(shortcutId);
}

void GlobalShortcuts::onShortcutsChanged(const QDBusObjectPath &sessionHandle,
                                          const QVariant &shortcuts)
{
    if (sessionHandle.path() == m_sessionHandle && m_shortcutChangedCallback) {
        QVector<GsrShortcut> parsed = parseShortcuts(shortcuts);
        for (const GsrShortcut &s : parsed)
            m_shortcutChangedCallback(s);
    }
}
