#include "config.hpp"
#include "global_shortcuts.h"
#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QComboBox>
#include <QScrollArea>
#include <QFrame>
#include <QMessageBox>
#include <QFileDialog>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QTimer>
#include <QIcon>
#include <QPixmap>
#include <QStyle>
#include <QProcess>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusInterface>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <functional>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <climits>
#include <cassert>
#include <map>

#define GSR_CURRENT_GLOBAL_HOTKEYS_CODE_VERSION 6

#ifndef GSR_VERSION
#define GSR_VERSION "unknown"
#endif

#define SHORTCUT_ID_START_STOP_RECORDING    "gpu_screen_recorder_start_stop_recording"
#define SHORTCUT_ID_PAUSE_UNPAUSE_RECORDING "gpu_screen_recorder_pause_unpause_recording"
#define SHORTCUT_ID_SAVE_REPLAY             "gpu_screen_recorder_save_replay"

struct AudioInput {
    std::string name;
    std::string description;
};

struct vec2i { int x = 0; int y = 0; };

struct GsrMonitor {
    std::string name;
    vec2i size;
};

struct SupportedVideoCodecs {
    bool h264 = false;
    bool h264_software = false;
    bool hevc = false;
    bool hevc_hdr = false;
    bool hevc_10bit = false;
    bool av1 = false;
    bool av1_hdr = false;
    bool av1_10bit = false;
    bool vp8 = false;
    bool vp9 = false;
};

struct SupportedCaptureOptions {
    bool window = false;
    bool focused = false;
    bool portal = false;
    std::vector<GsrMonitor> monitors;
};

enum class DisplayServer { UNKNOWN, X11, WAYLAND };
enum class GpuVendor { UNKNOWN, AMD, INTEL, NVIDIA, BROADCOM };
enum class WaylandCompositor { UNKNOWN, HYPRLAND, KDE };

struct SystemInfo {
    DisplayServer display_server = DisplayServer::UNKNOWN;
    bool supports_app_audio = false;
    bool is_steam_deck = false;
};

struct GpuInfo { GpuVendor vendor = GpuVendor::UNKNOWN; };

struct GsrInfo {
    SystemInfo system_info;
    GpuInfo gpu_info;
    SupportedVideoCodecs supported_video_codecs;
    SupportedCaptureOptions supported_capture_options;
};

enum class GsrInfoExitStatus {
    OK, FAILED_TO_RUN_COMMAND, OPENGL_FAILED, NO_DRM_CARD
};

struct Container {
    const char *container_name;
    const char *file_extension;
};

static const Container supported_containers[] = {
    { "mp4", "mp4" }, { "flv", "flv" }, { "matroska", "mkv" },
    { "mov", "mov" }, { "mpegts", "ts" }, { "hls", "m3u8" }
};

enum class SystrayPage { FRONT, STREAMING, RECORDING, REPLAY };

static double clock_get_monotonic_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 0.000000001;
}

static bool starts_with(const std::string &str, const char *substr) {
    size_t len = strlen(substr);
    return str.size() >= len && memcmp(str.data(), substr, len) == 0;
}

static std::string get_date_str() {
    char str[128];
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    strftime(str, sizeof(str) - 1, "%Y-%m-%d_%H-%M-%S", t);
    return str;
}

static bool is_directory(const char *filepath) {
    struct stat file_stat;
    memset(&file_stat, 0, sizeof(file_stat));
    return stat(filepath, &file_stat) == 0 && S_ISDIR(file_stat.st_mode);
}

static bool is_program_installed(const StringView program_name) {
    const char *path = getenv("PATH");
    if (!path) return false;
    bool found = false;
    char full_path[PATH_MAX];
    string_split_char(path, ':', [&](StringView line) -> bool {
        snprintf(full_path, sizeof(full_path), "%.*s/%.*s",
                 (int)line.size, line.str, (int)program_name.size, program_name.str);
        if (access(full_path, F_OK) == 0) { found = true; return false; }
        return true;
    });
    return found;
}

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(Config config, GsrInfo gsrInfo, bool flatpak, QWidget *parent = nullptr);
    ~MainWindow();

private:
    void setupSystemTray();
    QWidget* createCommonSettingsPage();
    QWidget* createRecordingPage();
    QWidget* createReplayPage();
    QWidget* createStreamingPage();

    QWidget* createAudioDeviceRow(const std::string &selectedId);
    QWidget* createAppAudioRow(const std::string &selectedId);
    QWidget* createAppAudioCustomRow(const std::string &text);

    void saveConfigs();
    void loadConfig();

    void onStartReplayClick();
    void onStartRecordingClick();
    void onStartStreamingClick();
    void onBackClick();

    void onStartRecordButtonClicked();
    void onStartReplayButtonClicked();
    void onStartStreamButtonClicked();
    void onPauseButtonClicked();
    void onSaveReplayClicked();
    void updateSystrayMenu(SystrayPage page);

    void startGpuScreenRecorder(std::vector<const char*> args);

    void showNotification(const QString &title, const QString &body, bool urgent = false);
    void withdrawNotification();

    void onGlobalShortcutActivated(const QString &id);
    void onGlobalShortcutChanged(const GsrShortcut &shortcut);

    void onTimerTick();
    void onStreamServiceChanged(int index);
    void onQualityChanged(int index);
    void onViewModeChanged(int index);
    void onRecordAreaChanged(int index);

    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QAction *m_showHideAction = nullptr;
    QAction *m_hideWhenRecordingAction = nullptr;
    QAction *m_startStopStreamingAction = nullptr;
    QAction *m_startStopRecordingAction = nullptr;
    QAction *m_pauseRecordingAction = nullptr;
    QAction *m_startStopReplayAction = nullptr;
    QAction *m_saveReplayAction = nullptr;

    QStackedWidget *m_stack = nullptr;

    bool m_windowHidden = false;
    bool m_recording = false;
    bool m_paused = false;
    bool m_replaying = false;
    bool m_streaming = false;
    pid_t m_childPid = -1;
    int m_prevExitStatus = -1;
    double m_recordStartTime = 0.0;
    double m_pauseStartSec = 0.0;
    double m_pausedTimeOffsetSec = 0.0;
    std::string m_recordFileCurrentFilename;
    bool m_showingNotification = false;
    double m_notificationTimeout = 0.0;
    double m_notificationStart = 0.0;

    Config m_config;
    GsrInfo m_gsrInfo;
    bool m_flatpak = false;
    bool m_configEmpty = false;

    GlobalShortcuts *m_globalShortcuts = nullptr;
    bool m_globalShortcutsInitialized = false;
    bool m_globalShortcutsReceived = false;
    bool m_kdeWayland = false;

    QComboBox *m_recordAreaCombo = nullptr;
    QComboBox *m_viewCombo = nullptr;
    QSpinBox *m_areaWidthSpin = nullptr;
    QSpinBox *m_areaHeightSpin = nullptr;
    QSpinBox *m_videoWidthSpin = nullptr;
    QSpinBox *m_videoHeightSpin = nullptr;
    QSpinBox *m_fpsSpin = nullptr;
    QSpinBox *m_videoBitrateSpin = nullptr;
    QCheckBox *m_splitAudioCheck = nullptr;
    QCheckBox *m_invertAppAudioCheck = nullptr;
    QCheckBox *m_changeResolutionCheck = nullptr;
    QCheckBox *m_recordCursorCheck = nullptr;
    QCheckBox *m_restorePortalCheck = nullptr;
    QCheckBox *m_overclockCheck = nullptr;
    QCheckBox *m_showStartedNotifCheck = nullptr;
    QCheckBox *m_showStoppedNotifCheck = nullptr;
    QCheckBox *m_showSavedNotifCheck = nullptr;
    QComboBox *m_qualityCombo = nullptr;
    QComboBox *m_codecCombo = nullptr;
    QComboBox *m_audioCodecCombo = nullptr;
    QComboBox *m_colorRangeCombo = nullptr;
    QComboBox *m_framerateModeCombo = nullptr;
    QWidget *m_audioItemsWidget = nullptr;
    QVBoxLayout *m_audioItemsLayout = nullptr;
    QPushButton *m_addAudioDeviceBtn = nullptr;
    QPushButton *m_addAppAudioBtn = nullptr;
    QPushButton *m_addCustomAppAudioBtn = nullptr;
    QWidget *m_bitrateGrid = nullptr;
    QWidget *m_codecGrid = nullptr;
    QWidget *m_audioCodecGrid = nullptr;
    QWidget *m_colorRangeGrid = nullptr;
    QWidget *m_framerateModeGrid = nullptr;
    QWidget *m_overclockGrid = nullptr;
    QGroupBox *m_notifGroupBox = nullptr;

    QPushButton *m_streamBtn = nullptr;
    QPushButton *m_recordBtn = nullptr;
    QPushButton *m_replayBtn = nullptr;

    QSpinBox *m_replayTimeSpin = nullptr;
    QComboBox *m_recordContainerCombo = nullptr;
    QComboBox *m_replayContainerCombo = nullptr;
    QComboBox *m_customStreamContainerCombo = nullptr;
    QPushButton *m_recordFileBtn = nullptr;
    QPushButton *m_replayFileBtn = nullptr;
    QPushButton *m_recordBackBtn = nullptr;
    QPushButton *m_startRecordBtn = nullptr;
    QPushButton *m_pauseRecordBtn = nullptr;
    QLabel *m_recordTimerLabel = nullptr;
    QWidget *m_recordBottomPanel = nullptr;

    QPushButton *m_replayBackBtn = nullptr;
    QPushButton *m_startReplayBtn = nullptr;
    QPushButton *m_saveReplayBtn = nullptr;
    QLabel *m_replayTimerLabel = nullptr;
    QWidget *m_replayBottomPanel = nullptr;

    QComboBox *m_streamServiceCombo = nullptr;
    QLineEdit *m_youtubeKeyEdit = nullptr;
    QLineEdit *m_twitchKeyEdit = nullptr;
    QLineEdit *m_customUrlEdit = nullptr;
    QLabel *m_streamKeyLabel = nullptr;
    QPushButton *m_streamBackBtn = nullptr;
    QPushButton *m_startStreamBtn = nullptr;
    QLabel *m_streamTimerLabel = nullptr;
    QWidget *m_streamBottomPanel = nullptr;
    QWidget *m_customContainerGrid = nullptr;

    std::vector<AudioInput> m_audioInputs;
    std::vector<std::string> m_applicationAudio;

    QTimer *m_timer = nullptr;

    QString m_notificationId;
};

MainWindow::MainWindow(Config config, GsrInfo gsrInfo, bool flatpak, QWidget *parent)
    : QMainWindow(parent)
    , m_config(std::move(config))
    , m_gsrInfo(gsrInfo)
    , m_flatpak(flatpak)
    , m_configEmpty(false)
{
    setWindowTitle(QString("GPU Screen Recorder | Running on %1")
        .arg([&]() -> QString {
            switch (m_gsrInfo.gpu_info.vendor) {
                case GpuVendor::AMD: return "AMD";
                case GpuVendor::INTEL: return "Intel";
                case GpuVendor::NVIDIA: return "NVIDIA";
                case GpuVendor::BROADCOM: return "Broadcom";
                default: return "Unknown";
            }
        }()));

    setWindowIcon(QIcon::fromTheme("com.dec05eba.gpu_screen_recorder"));

    if (qEnvironmentVariableIsSet("KDE_SESSION_VERSION"))
        m_kdeWayland = true;

    FILE *f = popen("gpu-screen-recorder --list-audio-devices", "r");
    if (f) {
        char buf[16384];
        ssize_t n = fread(buf, 1, sizeof(buf) - 1, f);
        if (n > 0) {
            buf[n] = '\0';
            string_split_char(buf, '\n', [&](StringView line) {
                std::string ls(line.str, line.size);
                size_t sep = ls.find('|');
                if (sep != std::string::npos) {
                    AudioInput ai;
                    ai.name = ls.substr(0, sep);
                    ai.description = ls.substr(sep + 1);
                    m_audioInputs.push_back(std::move(ai));
                }
                return true;
            });
        }
        pclose(f);
    }

    f = popen("gpu-screen-recorder --list-application-audio", "r");
    if (f) {
        char buf[16384];
        ssize_t n = fread(buf, 1, sizeof(buf) - 1, f);
        if (n > 0) {
            buf[n] = '\0';
            string_split_char(buf, '\n', [&](StringView line) {
                m_applicationAudio.emplace_back(line.str, line.size);
                return true;
            });
        }
        pclose(f);
    }

    m_stack = new QStackedWidget(this);
    setCentralWidget(m_stack);

    m_stack->addWidget(createCommonSettingsPage());
    m_stack->addWidget(createReplayPage());
    m_stack->addWidget(createRecordingPage());
    m_stack->addWidget(createStreamingPage());
    m_stack->setCurrentIndex(0);

    setupSystemTray();

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::onTimerTick);
    m_timer->start(500);

    loadConfig();

    if (m_gsrInfo.system_info.display_server == DisplayServer::WAYLAND) {
        m_globalShortcuts = new GlobalShortcuts(this);
        connect(m_globalShortcuts, &GlobalShortcuts::initFinished, this, [this](bool success) {
            m_globalShortcutsInitialized = success;
            if (success) {
                m_globalShortcuts->listShortcuts();
                m_globalShortcuts->subscribeActivatedSignal(
                    [this](const QString &id) { onGlobalShortcutActivated(id); },
                    [this](const GsrShortcut &s) { onGlobalShortcutChanged(s); });
            }
        });

        if (!m_globalShortcuts->init()) {
            qWarning("gsr: failed to init global shortcuts");
        }
    }
}

MainWindow::~MainWindow() {
    if (m_childPid != -1) {
        kill(m_childPid, SIGINT);
        waitpid(m_childPid, nullptr, 0);
    }
    saveConfigs();
}

void MainWindow::setupSystemTray() {
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(QIcon("/usr/share/icons/hicolor/32x32/status/com.dec05eba.gpu_screen_recorder.tray-idle.png"));
    m_trayIcon->setToolTip("GPU Screen Recorder");
    m_trayIcon->show();

    m_trayMenu = new QMenu(this);
    m_showHideAction = m_trayMenu->addAction("Hide window");
    connect(m_showHideAction, &QAction::triggered, this, [this]() {
        if (m_windowHidden) {
            show();
            m_showHideAction->setText("Hide window");
            m_windowHidden = false;
        } else {
            hide();
            m_showHideAction->setText("Show window");
            m_windowHidden = true;
        }
    });

    QMenu *optionsMenu = m_trayMenu->addMenu("Options");
    m_hideWhenRecordingAction = optionsMenu->addAction("Hide window when recording starts");
    m_hideWhenRecordingAction->setCheckable(true);
    m_hideWhenRecordingAction->setChecked(m_config.main_config.hide_window_when_recording);
    connect(m_hideWhenRecordingAction, &QAction::triggered, this, [this]() {
        m_config.main_config.hide_window_when_recording = m_hideWhenRecordingAction->isChecked();
    });

    m_trayMenu->addSeparator();

    m_startStopStreamingAction = m_trayMenu->addAction("Start streaming");
    connect(m_startStopStreamingAction, &QAction::triggered, this, [this]() {
        m_startStreamBtn->click();
    });
    m_startStopStreamingAction->setVisible(false);

    m_startStopRecordingAction = m_trayMenu->addAction("Start recording");
    connect(m_startStopRecordingAction, &QAction::triggered, this, [this]() {
        m_startRecordBtn->click();
    });
    m_startStopRecordingAction->setVisible(false);

    m_pauseRecordingAction = m_trayMenu->addAction("Pause recording");
    connect(m_pauseRecordingAction, &QAction::triggered, this, [this]() {
        m_pauseRecordBtn->click();
    });
    m_pauseRecordingAction->setVisible(false);

    m_startStopReplayAction = m_trayMenu->addAction("Start replay");
    connect(m_startStopReplayAction, &QAction::triggered, this, [this]() {
        m_startReplayBtn->click();
    });
    m_startStopReplayAction->setVisible(false);

    m_saveReplayAction = m_trayMenu->addAction("Save replay");
    connect(m_saveReplayAction, &QAction::triggered, this, [this]() {
        m_saveReplayBtn->click();
    });
    m_saveReplayAction->setVisible(false);

    m_trayMenu->addSeparator();

    QAction *exitAction = m_trayMenu->addAction("Exit");
    connect(exitAction, &QAction::triggered, this, &QMainWindow::close);

    m_trayIcon->setContextMenu(m_trayMenu);
}

void MainWindow::updateSystrayMenu(SystrayPage page) {
    m_startStopStreamingAction->setVisible(false);
    m_startStopRecordingAction->setVisible(false);
    m_pauseRecordingAction->setVisible(false);
    m_startStopReplayAction->setVisible(false);
    m_saveReplayAction->setVisible(false);

    switch (page) {
        case SystrayPage::STREAMING:
            m_startStopStreamingAction->setVisible(true);
            break;
        case SystrayPage::RECORDING:
            m_startStopRecordingAction->setVisible(true);
            m_pauseRecordingAction->setVisible(true);
            m_pauseRecordingAction->setEnabled(false);
            break;
        case SystrayPage::REPLAY:
            m_startStopReplayAction->setVisible(true);
            m_saveReplayAction->setVisible(true);
            m_saveReplayAction->setEnabled(false);
            break;
        default: break;
    }
}

void MainWindow::saveConfigs() {
    m_config.main_config.record_area_option = m_recordAreaCombo->currentData().toString().toStdString();
    if (m_gsrInfo.system_info.display_server != DisplayServer::X11) {
        m_config.main_config.record_area_width = m_areaWidthSpin->value();
        m_config.main_config.record_area_height = m_areaHeightSpin->value();
    }
    m_config.main_config.video_width = m_videoWidthSpin->value();
    m_config.main_config.video_height = m_videoHeightSpin->value();
    m_config.main_config.fps = m_fpsSpin->value();
    m_config.main_config.video_bitrate = m_videoBitrateSpin->value();
    m_config.main_config.merge_audio_tracks = !m_splitAudioCheck->isChecked();
    m_config.main_config.record_app_audio_inverted = m_invertAppAudioCheck->isChecked();
    m_config.main_config.change_video_resolution = m_changeResolutionCheck->isChecked();

    m_config.main_config.audio_input.clear();
    for (int i = 0; i < m_audioItemsLayout->count(); ++i) {
        QWidget *row = m_audioItemsLayout->itemAt(i)->widget();
        if (!row) continue;
        QString type = row->property("audio-track-type").toString();
        QComboBox *combo = row->findChild<QComboBox*>();
        QLineEdit *entry = row->findChild<QLineEdit*>();

        if (type == "device" && combo) {
            m_config.main_config.audio_input.push_back("device:" + combo->currentData().toString().toStdString());
        } else if (type == "app" && combo) {
            m_config.main_config.audio_input.push_back("app:" + combo->currentData().toString().toStdString());
        } else if (type == "app-custom" && entry) {
            m_config.main_config.audio_input.push_back("app:" + entry->text().toStdString());
        }
    }

    m_config.main_config.color_range = m_colorRangeCombo->currentData().toString().toStdString();
    m_config.main_config.quality = m_qualityCombo->currentData().toString().toStdString();
    m_config.main_config.codec = m_codecCombo->currentData().toString().toStdString();
    m_config.main_config.audio_codec = m_audioCodecCombo->currentData().toString().toStdString();
    m_config.main_config.framerate_mode = m_framerateModeCombo->currentData().toString().toStdString();
    m_config.main_config.advanced_view = m_viewCombo->currentData().toString() == "advanced";
    m_config.main_config.overclock = m_overclockCheck->isChecked();
    m_config.main_config.show_recording_started_notifications = m_showStartedNotifCheck->isChecked();
    m_config.main_config.show_recording_stopped_notifications = m_showStoppedNotifCheck->isChecked();
    m_config.main_config.show_recording_saved_notifications = m_showSavedNotifCheck->isChecked();
    m_config.main_config.record_cursor = m_recordCursorCheck->isChecked();
    m_config.main_config.hide_window_when_recording = m_hideWhenRecordingAction->isChecked();
    m_config.main_config.restore_portal_session = m_restorePortalCheck->isChecked();

    m_config.streaming_config.streaming_service = m_streamServiceCombo->currentData().toString().toStdString();
    m_config.streaming_config.youtube.stream_key = m_youtubeKeyEdit->text().toStdString();
    m_config.streaming_config.twitch.stream_key = m_twitchKeyEdit->text().toStdString();
    m_config.streaming_config.custom.url = m_customUrlEdit->text().toStdString();
    m_config.streaming_config.custom.container = m_customStreamContainerCombo->currentData().toString().toStdString();

    m_config.record_config.save_directory = m_recordFileBtn->text().toStdString();
    m_config.record_config.container = m_recordContainerCombo->currentData().toString().toStdString();

    m_config.replay_config.save_directory = m_replayFileBtn->text().toStdString();
    m_config.replay_config.container = m_replayContainerCombo->currentData().toString().toStdString();
    m_config.replay_config.replay_time = m_replayTimeSpin->value();

    save_config(m_config);
}

void MainWindow::loadConfig() {
    std::string area = m_config.main_config.record_area_option;
    if (area.empty()) {
        if (!m_gsrInfo.supported_capture_options.monitors.empty())
            area = m_gsrInfo.supported_capture_options.monitors[0].name;
        else
            area = "portal";
    }

    if (m_config.main_config.record_area_width == 0) m_config.main_config.record_area_width = 1920;
    if (m_config.main_config.record_area_height == 0) m_config.main_config.record_area_height = 1080;
    if (m_config.main_config.video_width == 0) m_config.main_config.video_width = 1920;
    if (m_config.main_config.video_height == 0) m_config.main_config.video_height = 1080;
    if (m_config.main_config.fps <= 0) m_config.main_config.fps = 60;
    if (m_config.main_config.color_range != "limited" && m_config.main_config.color_range != "full")
        m_config.main_config.color_range = "limited";
    if (m_config.main_config.quality != "custom" && m_config.main_config.quality != "medium"
        && m_config.main_config.quality != "high" && m_config.main_config.quality != "very_high"
        && m_config.main_config.quality != "ultra")
        m_config.main_config.quality = "very_high";
    if (m_config.main_config.audio_codec != "opus" && m_config.main_config.audio_codec != "aac")
        m_config.main_config.audio_codec = "opus";
    if (m_config.main_config.framerate_mode != "auto" && m_config.main_config.framerate_mode != "cfr"
        && m_config.main_config.framerate_mode != "vfr")
        m_config.main_config.framerate_mode = "auto";

    int areaIdx = m_recordAreaCombo->findData(QString::fromStdString(area));
    if (areaIdx >= 0) m_recordAreaCombo->setCurrentIndex(areaIdx);

    m_areaWidthSpin->setValue(m_config.main_config.record_area_width);
    m_areaHeightSpin->setValue(m_config.main_config.record_area_height);
    m_videoWidthSpin->setValue(m_config.main_config.video_width);
    m_videoHeightSpin->setValue(m_config.main_config.video_height);
    m_fpsSpin->setValue(m_config.main_config.fps);
    m_videoBitrateSpin->setValue(m_config.main_config.video_bitrate);
    m_splitAudioCheck->setChecked(!m_config.main_config.merge_audio_tracks);
    m_invertAppAudioCheck->setChecked(m_config.main_config.record_app_audio_inverted);
    m_changeResolutionCheck->setChecked(m_config.main_config.change_video_resolution);

    for (const std::string &input : m_config.main_config.audio_input) {
        QWidget *row = nullptr;
        if (starts_with(input, "app:")) {
            std::string name = input.substr(4);
            auto it = std::find_if(m_applicationAudio.begin(), m_applicationAudio.end(),
                [&](const std::string &s) { return strcasecmp(s.c_str(), name.c_str()) == 0; });
            if (it != m_applicationAudio.end())
                row = createAppAudioRow(*it);
            else
                row = createAppAudioCustomRow(name);
        } else if (starts_with(input, "device:")) {
            row = createAudioDeviceRow(input.substr(7));
        } else {
            row = createAudioDeviceRow(input);
        }
        if (row)
            m_audioItemsLayout->addWidget(row);
    }

    if (m_configEmpty && m_config.main_config.audio_input.empty())
        m_audioItemsLayout->addWidget(createAudioDeviceRow("Default output"));

    auto setCombo = [this](QComboBox *combo, const std::string &val) {
        int idx = combo->findData(QString::fromStdString(val));
        if (idx >= 0) combo->setCurrentIndex(idx);
    };

    setCombo(m_colorRangeCombo, m_config.main_config.color_range);
    setCombo(m_qualityCombo, m_config.main_config.quality);
    setCombo(m_audioCodecCombo, m_config.main_config.audio_codec);
    setCombo(m_framerateModeCombo, m_config.main_config.framerate_mode);
    m_overclockCheck->setChecked(m_config.main_config.overclock);
    m_showStartedNotifCheck->setChecked(m_config.main_config.show_recording_started_notifications);
    m_showStoppedNotifCheck->setChecked(m_config.main_config.show_recording_stopped_notifications);
    m_showSavedNotifCheck->setChecked(m_config.main_config.show_recording_saved_notifications);
    m_recordCursorCheck->setChecked(m_config.main_config.record_cursor);
    m_hideWhenRecordingAction->setChecked(m_config.main_config.hide_window_when_recording);
    m_restorePortalCheck->setChecked(m_config.main_config.restore_portal_session);

    setCombo(m_streamServiceCombo, m_config.streaming_config.streaming_service);
    m_youtubeKeyEdit->setText(QString::fromStdString(m_config.streaming_config.youtube.stream_key));
    m_twitchKeyEdit->setText(QString::fromStdString(m_config.streaming_config.twitch.stream_key));
    m_customUrlEdit->setText(QString::fromStdString(m_config.streaming_config.custom.url));
    setCombo(m_customStreamContainerCombo, m_config.streaming_config.custom.container);

    m_recordFileBtn->setText(QString::fromStdString(
        m_config.record_config.save_directory.empty() ? get_videos_dir() : m_config.record_config.save_directory));
    setCombo(m_recordContainerCombo, m_config.record_config.container);

    m_replayFileBtn->setText(QString::fromStdString(
        m_config.replay_config.save_directory.empty() ? get_videos_dir() : m_config.replay_config.save_directory));
    setCombo(m_replayContainerCombo, m_config.replay_config.container);
    m_replayTimeSpin->setValue(m_config.replay_config.replay_time);

    setCombo(m_viewCombo, m_config.main_config.advanced_view ? "advanced" : "simple");
    onViewModeChanged(m_viewCombo->currentIndex());
    onQualityChanged(m_qualityCombo->currentIndex());
    onRecordAreaChanged(m_recordAreaCombo->currentIndex());
    onStreamServiceChanged(m_streamServiceCombo->currentIndex());

    if (!m_gsrInfo.system_info.supports_app_audio) {
        m_addAppAudioBtn->setVisible(false);
        m_addCustomAppAudioBtn->setVisible(false);
        m_invertAppAudioCheck->setVisible(false);
    }

    if (!m_config.main_config.software_encoding_warning_shown
        && !m_gsrInfo.supported_video_codecs.h264
        && !m_gsrInfo.supported_video_codecs.hevc
        && !m_gsrInfo.supported_video_codecs.av1
        && !m_gsrInfo.supported_video_codecs.vp8
        && !m_gsrInfo.supported_video_codecs.vp9) {
        QMessageBox msgBox(this);
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setWindowTitle("GPU Screen Recorder");
        msgBox.setText("Unable to find a hardware video encoder on your system, using software video encoder instead (slow!). ...");
        msgBox.exec();
        m_config.main_config.software_encoding_warning_shown = true;
        m_config.main_config.advanced_view = true;
        int idx = m_codecCombo->findData("h264_software");
        if (idx >= 0) m_codecCombo->setCurrentIndex(idx);
        int vi = m_viewCombo->findData("advanced");
        if (vi >= 0) m_viewCombo->setCurrentIndex(vi);
    }
}

QWidget* MainWindow::createCommonSettingsPage() {
    QWidget *page = new QWidget;
    QVBoxLayout *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(10, 10, 10, 10);

    QHBoxLayout *viewLayout = new QHBoxLayout;
    viewLayout->addWidget(new QLabel("View:"));
    m_viewCombo = new QComboBox;
    m_viewCombo->addItem("Simple", "simple");
    m_viewCombo->addItem("Advanced", "advanced");
    viewLayout->addWidget(m_viewCombo, 1);
    pageLayout->addLayout(viewLayout);

    QScrollArea *scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumSize(400, 300);
    pageLayout->addWidget(scrollArea, 1);

    QWidget *scrollContent = new QWidget;
    QVBoxLayout *contentLayout = new QVBoxLayout(scrollContent);
    scrollArea->setWidget(scrollContent);

    QGroupBox *captureGroup = new QGroupBox("Capture target");
    QVBoxLayout *captureLayout = new QVBoxLayout(captureGroup);

    m_recordAreaCombo = new QComboBox;
    for (const auto &monitor : m_gsrInfo.supported_capture_options.monitors) {
        QString label = QString("Monitor %1 (%2x%3)")
            .arg(QString::fromStdString(monitor.name))
            .arg(monitor.size.x).arg(monitor.size.y);
        m_recordAreaCombo->addItem(label, QString::fromStdString(monitor.name));
    }
    if (m_gsrInfo.supported_capture_options.portal) {
        m_recordAreaCombo->addItem("Desktop portal (HDR not supported)", "portal");
    } else {
        m_recordAreaCombo->addItem("Desktop portal (Not available on your system)", "portal");
    }
    captureLayout->addWidget(m_recordAreaCombo);

    QHBoxLayout *areaSizeLayout = new QHBoxLayout;
    areaSizeLayout->addWidget(new QLabel("Video resolution limit:"));
    m_areaWidthSpin = new QSpinBox;
    m_areaWidthSpin->setRange(5, 10000);
    m_areaWidthSpin->setValue(1920);
    areaSizeLayout->addWidget(m_areaWidthSpin);
    areaSizeLayout->addWidget(new QLabel("x"));
    m_areaHeightSpin = new QSpinBox;
    m_areaHeightSpin->setRange(5, 10000);
    m_areaHeightSpin->setValue(1080);
    areaSizeLayout->addWidget(m_areaHeightSpin);
    captureLayout->addLayout(areaSizeLayout);

    m_changeResolutionCheck = new QCheckBox("Change video resolution");
    captureLayout->addWidget(m_changeResolutionCheck);

    QHBoxLayout *videoResLayout = new QHBoxLayout;
    videoResLayout->addWidget(new QLabel("Video resolution:"));
    m_videoWidthSpin = new QSpinBox;
    m_videoWidthSpin->setRange(5, 10000);
    m_videoWidthSpin->setValue(1920);
    videoResLayout->addWidget(m_videoWidthSpin);
    videoResLayout->addWidget(new QLabel("x"));
    m_videoHeightSpin = new QSpinBox;
    m_videoHeightSpin->setRange(5, 10000);
    m_videoHeightSpin->setValue(1080);
    videoResLayout->addWidget(m_videoHeightSpin);
    QWidget *videoResWidget = new QWidget;
    videoResWidget->setLayout(videoResLayout);
    videoResWidget->setVisible(false);

    captureLayout->addWidget(videoResWidget);

    m_restorePortalCheck = new QCheckBox("Restore portal session");
    m_restorePortalCheck->setChecked(true);
    captureLayout->addWidget(m_restorePortalCheck);

    contentLayout->addWidget(captureGroup);

    QGroupBox *audioGroup = new QGroupBox("Audio");
    QVBoxLayout *audioLayout = new QVBoxLayout(audioGroup);

    m_audioItemsWidget = new QWidget;
    m_audioItemsLayout = new QVBoxLayout(m_audioItemsWidget);
    m_audioItemsLayout->setContentsMargins(0, 0, 0, 0);
    audioLayout->addWidget(m_audioItemsWidget);

    QHBoxLayout *addAudioLayout = new QHBoxLayout;
    m_addAudioDeviceBtn = new QPushButton("Add audio device");
    addAudioLayout->addWidget(m_addAudioDeviceBtn);
    m_addAppAudioBtn = new QPushButton("Add application audio");
    addAudioLayout->addWidget(m_addAppAudioBtn);
    m_addCustomAppAudioBtn = new QPushButton("Add custom application audio");
    addAudioLayout->addWidget(m_addCustomAppAudioBtn);
    audioLayout->addLayout(addAudioLayout);

    m_splitAudioCheck = new QCheckBox("Split each device/app audio into separate audio tracks");
    audioLayout->addWidget(m_splitAudioCheck);

    m_invertAppAudioCheck = new QCheckBox("Record audio from all applications except the selected ones");
    audioLayout->addWidget(m_invertAppAudioCheck);

    QHBoxLayout *audioCodecLayout = new QHBoxLayout;
    audioCodecLayout->addWidget(new QLabel("Audio codec:"));
    m_audioCodecCombo = new QComboBox;
    m_audioCodecCombo->addItem("Opus (Recommended)", "opus");
    m_audioCodecCombo->addItem("AAC", "aac");
    m_audioCodecCombo->setCurrentIndex(0);
    audioCodecLayout->addWidget(m_audioCodecCombo, 1);
    m_audioCodecGrid = new QWidget;
    m_audioCodecGrid->setLayout(audioCodecLayout);
    audioLayout->addWidget(m_audioCodecGrid);

    contentLayout->addWidget(audioGroup);

    QGroupBox *videoGroup = new QGroupBox("Video");
    QVBoxLayout *videoLayout = new QVBoxLayout(videoGroup);

    QHBoxLayout *qualityLayout = new QHBoxLayout;
    qualityLayout->addWidget(new QLabel("Video quality:"));
    m_qualityCombo = new QComboBox;
    m_qualityCombo->addItem("Constant bitrate (Recommended for live streaming and replay)", "custom");
    m_qualityCombo->addItem("Medium", "medium");
    m_qualityCombo->addItem("High", "high");
    m_qualityCombo->addItem("Very High (Recommended for recording)", "very_high");
    m_qualityCombo->addItem("Ultra", "ultra");
    m_qualityCombo->setCurrentIndex(0);
    qualityLayout->addWidget(m_qualityCombo, 1);
    videoLayout->addLayout(qualityLayout);

    QHBoxLayout *bitrateLayout = new QHBoxLayout;
    bitrateLayout->addWidget(new QLabel("Video bitrate (kbps):"));
    m_videoBitrateSpin = new QSpinBox;
    m_videoBitrateSpin->setRange(1, 500000);
    m_videoBitrateSpin->setValue(15000);
    bitrateLayout->addWidget(m_videoBitrateSpin, 1);
    m_bitrateGrid = new QWidget;
    m_bitrateGrid->setLayout(bitrateLayout);
    videoLayout->addWidget(m_bitrateGrid);

    QHBoxLayout *codecLayout = new QHBoxLayout;
    codecLayout->addWidget(new QLabel("Video codec:"));
    m_codecCombo = new QComboBox;
    m_codecCombo->addItem("Auto (Recommended)", "auto");
    auto addCodec = [&](const char *label, const char *id, bool available) {
        m_codecCombo->addItem(available ? label : QString("%1 (Not available on your system)").arg(label), id);
    };
    addCodec("H264 (Largest file size, best software compatibility)", "h264", m_gsrInfo.supported_video_codecs.h264);
    addCodec("HEVC", "hevc", m_gsrInfo.supported_video_codecs.hevc);
    addCodec("HEVC (10 bit, reduces banding)", "hevc_10bit", m_gsrInfo.supported_video_codecs.hevc);
    addCodec("HEVC (HDR)", "hevc_hdr", m_gsrInfo.supported_video_codecs.hevc);
    addCodec("AV1 (Smallest file size, worst software compatibility)", "av1", m_gsrInfo.supported_video_codecs.av1);
    addCodec("AV1 (10 bit, reduces banding)", "av1_10bit", m_gsrInfo.supported_video_codecs.av1);
    addCodec("AV1 (HDR)", "av1_hdr", m_gsrInfo.supported_video_codecs.av1);
    addCodec("VP8", "vp8", m_gsrInfo.supported_video_codecs.vp8);
    addCodec("VP9", "vp9", m_gsrInfo.supported_video_codecs.vp9);
    addCodec("H264 Software Encoder (Not recommended, slow)", "h264_software", m_gsrInfo.supported_video_codecs.h264_software);
    codecLayout->addWidget(m_codecCombo, 1);
    m_codecGrid = new QWidget;
    m_codecGrid->setLayout(codecLayout);
    videoLayout->addWidget(m_codecGrid);

    QHBoxLayout *colorLayout = new QHBoxLayout;
    colorLayout->addWidget(new QLabel("Color range:"));
    m_colorRangeCombo = new QComboBox;
    m_colorRangeCombo->addItem("Limited", "limited");
    m_colorRangeCombo->addItem("Full", "full");
    colorLayout->addWidget(m_colorRangeCombo, 1);
    m_colorRangeGrid = new QWidget;
    m_colorRangeGrid->setLayout(colorLayout);
    videoLayout->addWidget(m_colorRangeGrid);

    QHBoxLayout *fpsLayout = new QHBoxLayout;
    fpsLayout->addWidget(new QLabel("Frame rate:"));
    m_fpsSpin = new QSpinBox;
    m_fpsSpin->setRange(1, 500);
    m_fpsSpin->setValue(60);
    fpsLayout->addWidget(m_fpsSpin, 1);
    videoLayout->addLayout(fpsLayout);

    QHBoxLayout *frmLayout = new QHBoxLayout;
    frmLayout->addWidget(new QLabel("Frame rate mode:"));
    m_framerateModeCombo = new QComboBox;
    m_framerateModeCombo->addItem("Auto (Recommended)", "auto");
    m_framerateModeCombo->addItem("Constant", "cfr");
    m_framerateModeCombo->addItem("Variable", "vfr");
    frmLayout->addWidget(m_framerateModeCombo, 1);
    m_framerateModeGrid = new QWidget;
    m_framerateModeGrid->setLayout(frmLayout);
    videoLayout->addWidget(m_framerateModeGrid);

    m_overclockCheck = new QCheckBox("Overclock memory transfer rate to workaround NVIDIA driver performance bug");
    m_overclockGrid = new QWidget;
    QHBoxLayout *ocLayout = new QHBoxLayout(m_overclockGrid);
    ocLayout->setContentsMargins(0, 0, 0, 0);
    ocLayout->addWidget(m_overclockCheck);
    QPushButton *ocInfoBtn = new QPushButton("?");
    ocLayout->addWidget(ocInfoBtn);
    videoLayout->addWidget(m_overclockGrid);

    connect(ocInfoBtn, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(this, "GPU Screen Recorder",
            "NVIDIA driver has a bug where it will downclock memory transfer rate when a program uses CUDA.\n"
            "To work around this, GPU Screen Recorder can overclock your GPU memory transfer rate to its normal optimal level.\n"
            "You also need to have \"Coolbits\" NVIDIA X setting set to \"12\" to enable overclocking.\n"
            "You can set coolbits by running \"sudo nvidia-xconfig --cool-bits=12\" and then rebooting.");
    });

    m_recordCursorCheck = new QCheckBox("Record cursor");
    m_recordCursorCheck->setChecked(true);
    videoLayout->addWidget(m_recordCursorCheck);

    contentLayout->addWidget(videoGroup);

    m_notifGroupBox = new QGroupBox("Notifications");
    QVBoxLayout *notifLayout = new QVBoxLayout(m_notifGroupBox);
    m_showStartedNotifCheck = new QCheckBox("Show recording/streaming/replay started notification");
    notifLayout->addWidget(m_showStartedNotifCheck);
    m_showStoppedNotifCheck = new QCheckBox("Show streaming/replay stopped notification");
    notifLayout->addWidget(m_showStoppedNotifCheck);
    m_showSavedNotifCheck = new QCheckBox("Show video saved notification");
    m_showSavedNotifCheck->setChecked(true);
    notifLayout->addWidget(m_showSavedNotifCheck);
    contentLayout->addWidget(m_notifGroupBox);

    QHBoxLayout *startBtnLayout = new QHBoxLayout;

    m_streamBtn = new QPushButton("Stream");
    m_streamBtn->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    m_streamBtn->setLayoutDirection(Qt::RightToLeft);
    startBtnLayout->addWidget(m_streamBtn);

    m_recordBtn = new QPushButton("Record");
    m_recordBtn->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    m_recordBtn->setLayoutDirection(Qt::RightToLeft);
    startBtnLayout->addWidget(m_recordBtn);

    m_replayBtn = new QPushButton("Replay");
    m_replayBtn->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    m_replayBtn->setLayoutDirection(Qt::RightToLeft);
    startBtnLayout->addWidget(m_replayBtn);

    pageLayout->addLayout(startBtnLayout);

    connect(m_viewCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &MainWindow::onViewModeChanged);
    connect(m_qualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &MainWindow::onQualityChanged);
    connect(m_recordAreaCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &MainWindow::onRecordAreaChanged);

    connect(m_addAudioDeviceBtn, &QPushButton::clicked, this, [this]() {
        FILE *f = popen("gpu-screen-recorder --list-audio-devices", "r");
        if (f) {
            m_audioInputs.clear();
            char buf[16384];
            ssize_t n = fread(buf, 1, sizeof(buf) - 1, f);
            if (n > 0) {
                buf[n] = '\0';
                string_split_char(buf, '\n', [&](StringView line) {
                    std::string ls(line.str, line.size);
                    size_t sep = ls.find('|');
                    if (sep != std::string::npos) {
                        AudioInput ai;
                        ai.name = ls.substr(0, sep);
                        ai.description = ls.substr(sep + 1);
                        m_audioInputs.push_back(std::move(ai));
                    }
                    return true;
                });
            }
            pclose(f);
        }
        m_audioItemsLayout->addWidget(createAudioDeviceRow(""));
    });

    connect(m_addAppAudioBtn, &QPushButton::clicked, this, [this]() {
        FILE *f = popen("gpu-screen-recorder --list-application-audio", "r");
        if (f) {
            m_applicationAudio.clear();
            char buf[16384];
            ssize_t n = fread(buf, 1, sizeof(buf) - 1, f);
            if (n > 0) {
                buf[n] = '\0';
                string_split_char(buf, '\n', [&](StringView line) {
                    m_applicationAudio.emplace_back(line.str, line.size);
                    return true;
                });
            }
            pclose(f);
        }
        m_audioItemsLayout->addWidget(createAppAudioRow(""));
    });

    connect(m_addCustomAppAudioBtn, &QPushButton::clicked, this, [this]() {
        m_audioItemsLayout->addWidget(createAppAudioCustomRow(""));
    });

    auto updateVideoResVisibility = [this, videoResWidget, scrollContent, captureGroup]() {
        const bool checked = m_changeResolutionCheck->isChecked();
        const bool isFocused = (m_recordAreaCombo->currentData().toString() == "focused");
        videoResWidget->setVisible(checked && !isFocused);

        if (QLayout *layout = captureGroup->layout()) {
            layout->activate();
        }

        qApp->sendPostedEvents(nullptr, QEvent::LayoutRequest);
    };

    connect(m_changeResolutionCheck, &QCheckBox::toggled, this, [updateVideoResVisibility](bool) {
        updateVideoResVisibility();
    });
    connect(m_recordAreaCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
+        this, [updateVideoResVisibility](int) {
            updateVideoResVisibility();
        });

    connect(m_streamBtn, &QPushButton::clicked, this, &MainWindow::onStartStreamingClick);
    connect(m_recordBtn, &QPushButton::clicked, this, &MainWindow::onStartRecordingClick);
    connect(m_replayBtn, &QPushButton::clicked, this, &MainWindow::onStartReplayClick);

    return page;
}

void MainWindow::onViewModeChanged(int) {
    bool advanced = m_viewCombo->currentData().toString() == "advanced";
    m_colorRangeGrid->setVisible(advanced);
    m_codecGrid->setVisible(advanced);
    m_audioCodecGrid->setVisible(advanced);
    m_framerateModeGrid->setVisible(advanced);
    m_overclockGrid->setVisible(advanced && m_gsrInfo.gpu_info.vendor == GpuVendor::NVIDIA
        && m_gsrInfo.system_info.display_server != DisplayServer::WAYLAND);
    m_notifGroupBox->setVisible(advanced);
    m_splitAudioCheck->setVisible(advanced);
}

void MainWindow::onQualityChanged(int) {
    bool custom = m_qualityCombo->currentData().toString() == "custom";
    m_bitrateGrid->setVisible(custom);
}

void MainWindow::onRecordAreaChanged(int) {
    QString area = m_recordAreaCombo->currentData().toString();
    m_changeResolutionCheck->setVisible(area != "focused");
    m_restorePortalCheck->setVisible(area == "portal");
}

void MainWindow::onStreamServiceChanged(int) {
    m_youtubeKeyEdit->setVisible(false);
    m_twitchKeyEdit->setVisible(false);
    m_customUrlEdit->setVisible(false);
    m_customContainerGrid->setVisible(false);

    QString svc = m_streamServiceCombo->currentData().toString();
    if (svc == "youtube") {
        m_streamKeyLabel->setText("Stream key:");
        m_youtubeKeyEdit->setVisible(true);
    } else if (svc == "twitch") {
        m_streamKeyLabel->setText("Stream key:");
        m_twitchKeyEdit->setVisible(true);
    } else if (svc == "custom") {
        m_streamKeyLabel->setText("Url:");
        m_customUrlEdit->setVisible(true);
        m_customContainerGrid->setVisible(true);
    }
}

void MainWindow::showNotification(const QString &title, const QString &body, bool urgent) {
    m_notificationTimeout = urgent ? 10.0 : 3.0;
    m_notificationStart = clock_get_monotonic_seconds();
    m_showingNotification = true;
    m_notificationId = "gpu-screen-recorder";
    m_trayIcon->showMessage(title, body, QSystemTrayIcon::Information, (int)(m_notificationTimeout * 1000));
}

void MainWindow::withdrawNotification() {
    m_showingNotification = false;
}

void MainWindow::onTimerTick() {
    if (m_showingNotification) {
        double now = clock_get_monotonic_seconds();
        if (now - m_notificationStart >= m_notificationTimeout) {
            m_showingNotification = false;
        }
    }

    if (m_childPid != -1) {
        int status = 0;
        if (waitpid(m_childPid, &status, WNOHANG) != 0) {
            m_childPid = -1;
            m_prevExitStatus = -1;
            if (WIFEXITED(status))
                m_prevExitStatus = WEXITSTATUS(status);

            if (m_replaying)
                onStartReplayButtonClicked();
            else if (m_recording)
                onStartRecordButtonClicked();
            else if (m_streaming)
                onStartStreamButtonClicked();
        }
    }

    if (m_streaming) {
        double elapsed = clock_get_monotonic_seconds() - m_recordStartTime;
        int secs = (int)elapsed;
        m_streamTimerLabel->setText(QString("%1:%2:%3")
            .arg(secs / 3600, 2, 10, QChar('0'))
            .arg((secs % 3600) / 60, 2, 10, QChar('0'))
            .arg(secs % 60, 2, 10, QChar('0')));
    }
    if (m_recording && !m_paused) {
        double elapsed = (clock_get_monotonic_seconds() - m_recordStartTime) - m_pausedTimeOffsetSec;
        int secs = (int)elapsed;
        m_recordTimerLabel->setText(QString("%1:%2:%3")
            .arg(secs / 3600, 2, 10, QChar('0'))
            .arg((secs % 3600) / 60, 2, 10, QChar('0'))
            .arg(secs % 60, 2, 10, QChar('0')));
    }
    if (m_replaying) {
        double elapsed = clock_get_monotonic_seconds() - m_recordStartTime;
        int secs = (int)elapsed;
        m_replayTimerLabel->setText(QString("%1:%2:%3")
            .arg(secs / 3600, 2, 10, QChar('0'))
            .arg((secs % 3600) / 60, 2, 10, QChar('0'))
            .arg(secs % 60, 2, 10, QChar('0')));
    }
}

QWidget* MainWindow::createAudioDeviceRow(const std::string &selectedId) {
    QWidget *row = new QWidget;
    row->setProperty("audio-track-type", "device");
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget(new QLabel("Device:"));
    QComboBox *combo = new QComboBox;
    for (const auto &ai : m_audioInputs)
        combo->addItem(QString::fromStdString(ai.description), QString::fromStdString(ai.name));
    if (!m_audioInputs.empty()) {
        int idx = combo->findData(QString::fromStdString(selectedId));
        combo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    layout->addWidget(combo, 1);

    QPushButton *removeBtn = new QPushButton("Remove");
    layout->addWidget(removeBtn);
    connect(removeBtn, &QPushButton::clicked, this, [row]() {
        row->deleteLater();
    });

    return row;
}

QWidget* MainWindow::createAppAudioRow(const std::string &selectedId) {
    QWidget *row = new QWidget;
    row->setProperty("audio-track-type", "app");
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget(new QLabel("Application:"));
    QComboBox *combo = new QComboBox;
    for (const auto &name : m_applicationAudio)
        combo->addItem(QString::fromStdString(name), QString::fromStdString(name));
    if (!selectedId.empty())
        combo->setCurrentIndex(combo->findData(QString::fromStdString(selectedId)));
    else if (!m_applicationAudio.empty())
        combo->setCurrentIndex(0);
    layout->addWidget(combo, 1);

    QPushButton *removeBtn = new QPushButton("Remove");
    layout->addWidget(removeBtn);
    connect(removeBtn, &QPushButton::clicked, this, [row]() {
        row->deleteLater();
    });

    return row;
}

QWidget* MainWindow::createAppAudioCustomRow(const std::string &text) {
    QWidget *row = new QWidget;
    row->setProperty("audio-track-type", "app-custom");
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget(new QLabel("Application:"));
    QLineEdit *edit = new QLineEdit(QString::fromStdString(text));
    layout->addWidget(edit, 1);

    QPushButton *removeBtn = new QPushButton("Remove");
    layout->addWidget(removeBtn);
    connect(removeBtn, &QPushButton::clicked, this, [row]() {
        row->deleteLater();
    });

    return row;
}

void MainWindow::onStartRecordingClick() {
    m_stack->setCurrentIndex(2);
    updateSystrayMenu(SystrayPage::RECORDING);
}

void MainWindow::onStartRecordButtonClicked() {
    const QString dir = m_recordFileBtn->text();

    if (m_recording) {
        bool alreadyDead = false;
        bool success = true;
        if (m_childPid != -1) {
            alreadyDead = false;
            int status;
            int ret = waitpid(m_childPid, &status, WNOHANG);
            if (ret == -1) {
                perror("waitpid");
                success = false;
            } else if (ret == 0) {
                kill(m_childPid, SIGINT);
                if (waitpid(m_childPid, &status, 0) == -1) {
                    perror("waitpid");
                    success = false;
                } else {
                    success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
                }
            } else {
                success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
            }
            m_childPid = -1;
        }

        m_startRecordBtn->setText("Start recording");
        m_recording = false;
        m_recordBackBtn->setEnabled(true);
        m_paused = false;
        m_pauseRecordBtn->setEnabled(false);
        m_pauseRecordBtn->setText("Pause recording");
        m_recordBottomPanel->setEnabled(false);
        m_recordTimerLabel->setText("00:00:00");

        m_startStopRecordingAction->setText("Start recording");
        m_pauseRecordingAction->setEnabled(false);
        m_trayIcon->setIcon(QIcon("/usr/share/icons/hicolor/32x32/status/com.dec05eba.gpu_screen_recorder.tray-idle.png"));

        if (m_prevExitStatus == 10) {
            showNotification("GPU Screen Recorder", "You need to have pkexec installed and have a polkit agent running to record your monitor", true);
        } else if (m_prevExitStatus == 50) {
            showNotification("GPU Screen Recorder", "Desktop portal capture failed.", true);
        } else if (m_prevExitStatus != 60 && !success) {
            showNotification("GPU Screen Recorder", "Failed to save video.", true);
        } else if (success && m_prevExitStatus == 0) {
            if (m_showSavedNotifCheck->isChecked())
                showNotification("GPU Screen Recorder",
                    QString("The recording was saved to %1").arg(QString::fromStdString(m_recordFileCurrentFilename)));
        }
        return;
    }

    saveConfigs();

    int fps = m_fpsSpin->value();
    std::string windowStr = m_recordAreaCombo->currentData().toString().toStdString();
    std::string fpsStr = std::to_string(fps);
    bool changeVideoResolution = m_changeResolutionCheck->isChecked();

    std::string containerStr = m_recordContainerCombo->currentData().toString().toStdString();
    std::string containerName = m_recordContainerCombo->currentText().toStdString();
    std::string colorRangeStr = m_colorRangeCombo->currentData().toString().toStdString();
    std::string qualityStr = m_qualityCombo->currentData().toString().toStdString();
    std::string audioCodecStr = m_audioCodecCombo->currentData().toString().toStdString();
    std::string framerateModeStr = m_framerateModeCombo->currentData().toString().toStdString();
    bool recordCursor = m_recordCursorCheck->isChecked();
    bool restorePortal = m_restorePortalCheck->isChecked();
    std::string videoBitrateStr = std::to_string(m_videoBitrateSpin->value());

    int recordWidth = m_videoWidthSpin->value();
    int recordHeight = m_videoHeightSpin->value();

    const char *encoder = "gpu";
    std::string videoCodecInput = m_codecCombo->currentData().toString().toStdString();
    if (videoCodecInput == "h264_software") {
        videoCodecInput = "h264";
        encoder = "cpu";
    } else if (videoCodecInput == "auto") {
        if (!m_gsrInfo.supported_video_codecs.h264 && !m_gsrInfo.supported_video_codecs.hevc
            && !m_gsrInfo.supported_video_codecs.av1 && !m_gsrInfo.supported_video_codecs.vp8
            && !m_gsrInfo.supported_video_codecs.vp9) {
            videoCodecInput = "h264";
            encoder = "cpu";
        }
    }

    if ((videoCodecInput == "vp8" || videoCodecInput == "vp9") && containerName != "webm" && containerName != "matroska") {
        fprintf(stderr, "Warning: container '%s' not compatible with '%s', using webm\n", containerStr.c_str(), videoCodecInput.c_str());
        containerStr = "webm";
        containerName = "webm";
    }

    char dirTmp[PATH_MAX];
    strcpy(dirTmp, dir.toStdString().c_str());
    if (create_directory_recursive(dirTmp) != 0) {
        showNotification("GPU Screen Recorder", QString("Failed to start recording. Failed to create ") + dirTmp, true);
        return;
    }

    m_recordFileCurrentFilename = std::string(dirTmp) + "/Video_" + get_date_str() + "." + containerName;

    char area[64];
    snprintf(area, sizeof(area), "%dx%d", recordWidth, recordHeight);

    std::vector<const char*> args = {
        "gpu-screen-recorder", "-w", windowStr.c_str(), "-c", containerStr.c_str(), "-k", videoCodecInput.c_str(),
        "-ac", audioCodecStr.c_str(), "-f", fpsStr.c_str(), "-cursor", recordCursor ? "yes" : "no",
        "-restore-portal-session", restorePortal ? "yes" : "no", "-cr", colorRangeStr.c_str(),
        "-encoder", encoder, "-o", m_recordFileCurrentFilename.c_str()
    };

    if (qualityStr == "custom") {
        args.push_back("-bm"); args.push_back("cbr");
        args.push_back("-q"); args.push_back(videoBitrateStr.c_str());
    } else {
        args.push_back("-q"); args.push_back(qualityStr.c_str());
    }

    if (m_overclockCheck->isChecked())
        args.insert(args.end(), { "-oc", "yes" });

    if (framerateModeStr != "auto") {
        args.push_back("-fm"); args.push_back(framerateModeStr.c_str());
    }

    std::string mergeAudioTracks;
    std::vector<std::string> audioTracks;
    bool invertAppAudio = m_invertAppAudioCheck->isChecked();
    int numAppAudio = 0;

    for (int i = 0; i < m_audioItemsLayout->count(); ++i) {
        QWidget *row = m_audioItemsLayout->itemAt(i)->widget();
        if (!row) continue;
        QString type = row->property("audio-track-type").toString();
        QComboBox *combo = row->findChild<QComboBox*>();
        QLineEdit *entry = row->findChild<QLineEdit*>();

        if (type == "device" && combo) {
            audioTracks.push_back("device:" + combo->currentData().toString().toStdString());
        } else if (type == "app" && combo) {
            if (!m_gsrInfo.system_info.supports_app_audio) continue;
            audioTracks.push_back((invertAppAudio ? "app-inverse:" : "app:") + combo->currentData().toString().toStdString());
            ++numAppAudio;
        } else if (type == "app-custom" && entry) {
            if (!m_gsrInfo.system_info.supports_app_audio) continue;
            audioTracks.push_back((invertAppAudio ? "app-inverse:" : "app:") + entry->text().toStdString());
            ++numAppAudio;
        }
    }

    if (numAppAudio == 0 && invertAppAudio)
        audioTracks.push_back("app-inverse:");

    if (m_splitAudioCheck && !m_splitAudioCheck->isChecked()) {
        if (!audioTracks.empty()) {
            std::string merged;
            for (size_t j = 0; j < audioTracks.size(); ++j) {
                if (j > 0) merged += '|';
                merged += audioTracks[j];
            }
            args.push_back("-a"); args.push_back(merged.c_str());
        }
    } else {
        for (const auto &track : audioTracks) {
            args.push_back("-a"); args.push_back(track.c_str());
        }
    }

    if (changeVideoResolution)
        args.insert(args.end(), { "-s", area });

    args.push_back(nullptr);

    fprintf(stderr, "info: running command:");
    for (const char *a : args) { if (a) fprintf(stderr, " %s", a); }
    fprintf(stderr, "\n");

    if (m_hideWhenRecordingAction->isChecked()) {
        hide();
        m_windowHidden = true;
        m_showHideAction->setText("Show window");
    }

    startGpuScreenRecorder(args);

    m_recording = true;
    m_startRecordBtn->setText("Stop recording");
    m_recordBackBtn->setEnabled(false);
    m_pauseRecordBtn->setEnabled(true);
    m_recordBottomPanel->setEnabled(true);

    m_startStopRecordingAction->setText("Stop recording");
    m_pauseRecordingAction->setEnabled(true);
    m_trayIcon->setIcon(QIcon("/usr/share/icons/hicolor/32x32/status/com.dec05eba.gpu_screen_recorder.tray-recording.png"));

    if (m_showStartedNotifCheck->isChecked())
        showNotification("GPU Screen Recorder", "Started recording");

    m_recordStartTime = clock_get_monotonic_seconds();
    m_pausedTimeOffsetSec = 0.0;
}

void MainWindow::onPauseButtonClicked() {
    if (!m_recording || m_childPid == -1) return;

    kill(m_childPid, SIGUSR2);
    m_paused = !m_paused;
    if (m_paused) {
        m_pauseRecordBtn->setText("Unpause recording");
        m_pauseRecordingAction->setText("Unpause recording");
        m_trayIcon->setIcon(QIcon("/usr/share/icons/hicolor/32x32/status/com.dec05eba.gpu_screen_recorder.tray-paused.png"));
        m_pauseStartSec = clock_get_monotonic_seconds();
    } else {
        m_pauseRecordBtn->setText("Pause recording");
        m_pauseRecordingAction->setText("Pause recording");
        m_trayIcon->setIcon(QIcon("/usr/share/icons/hicolor/32x32/status/com.dec05eba.gpu_screen_recorder.tray-recording.png"));
        m_pausedTimeOffsetSec += (clock_get_monotonic_seconds() - m_pauseStartSec);
    }
}

QWidget* MainWindow::createRecordingPage() {
    QWidget *page = new QWidget;
    QGridLayout *grid = new QGridLayout(page);
    grid->setContentsMargins(10, 10, 10, 10);

    int row = 0;

    m_recordBackBtn = new QPushButton("Back");
    m_recordBackBtn->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    grid->addWidget(m_recordBackBtn, row++, 0, 1, 5);

    if (m_gsrInfo.system_info.display_server == DisplayServer::WAYLAND) {
        QLabel *waylandLabel = new QLabel("Your Wayland compositor doesn't support global hotkeys. Use KDE Plasma on Wayland if you want to use hotkeys.");
        waylandLabel->setWordWrap(true);
        grid->addWidget(waylandLabel, row++, 0, 1, 5);
    }

    QFrame *sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    grid->addWidget(sep, row++, 0, 1, 5);

    QHBoxLayout *fileLayout = new QHBoxLayout;
    fileLayout->addWidget(new QLabel("Where do you want to save the video?"));
    m_recordFileBtn = new QPushButton(QString::fromStdString(get_videos_dir()));
    m_recordFileBtn->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    fileLayout->addWidget(m_recordFileBtn, 1);
    QWidget *fileWidget = new QWidget;
    fileWidget->setLayout(fileLayout);
    grid->addWidget(fileWidget, row++, 0, 1, 5);

    connect(m_recordFileBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Where do you want to save the video?");
        if (!dir.isEmpty()) {
            m_recordFileBtn->setText(dir);
            m_config.record_config.save_directory = dir.toStdString();
        }
    });

    QHBoxLayout *containerLayout = new QHBoxLayout;
    containerLayout->addWidget(new QLabel("Container:"));
    m_recordContainerCombo = new QComboBox;
    for (auto &c : supported_containers)
        m_recordContainerCombo->addItem(c.file_extension, c.container_name);
    if (m_gsrInfo.supported_video_codecs.vp8 || m_gsrInfo.supported_video_codecs.vp9)
        m_recordContainerCombo->addItem("webm", "webm");
    containerLayout->addWidget(m_recordContainerCombo, 1);
    QWidget *containerWidget = new QWidget;
    containerWidget->setLayout(containerLayout);
    grid->addWidget(containerWidget, row++, 0, 1, 5);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    m_startRecordBtn = new QPushButton("Start recording");
    btnLayout->addWidget(m_startRecordBtn);
    m_pauseRecordBtn = new QPushButton("Pause recording");
    m_pauseRecordBtn->setEnabled(false);
    btnLayout->addWidget(m_pauseRecordBtn);
    QWidget *btnWidget = new QWidget;
    btnWidget->setLayout(btnLayout);
    grid->addWidget(btnWidget, row++, 0, 1, 5);

    QFrame *sep2 = new QFrame;
    sep2->setFrameShape(QFrame::HLine);
    grid->addWidget(sep2, row++, 0, 1, 5);

    m_recordBottomPanel = new QWidget;
    QHBoxLayout *panelLayout = new QHBoxLayout(m_recordBottomPanel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    QLabel *recordIcon = new QLabel;
    recordIcon->setPixmap(style()->standardPixmap(QStyle::SP_MediaPlay));
    panelLayout->addWidget(recordIcon);
    m_recordTimerLabel = new QLabel("00:00:00");
    panelLayout->addWidget(m_recordTimerLabel);
    panelLayout->addStretch();
    m_recordBottomPanel->setEnabled(false);
    grid->addWidget(m_recordBottomPanel, row++, 0, 1, 5);

    connect(m_recordBackBtn, &QPushButton::clicked, this, &MainWindow::onBackClick);
    connect(m_startRecordBtn, &QPushButton::clicked, this, &MainWindow::onStartRecordButtonClicked);
    connect(m_pauseRecordBtn, &QPushButton::clicked, this, &MainWindow::onPauseButtonClicked);

    return page;
}

void MainWindow::onStartReplayClick() {
    m_stack->setCurrentIndex(1);
    updateSystrayMenu(SystrayPage::REPLAY);
}

void MainWindow::onStartReplayButtonClicked() {
    const QString dir = m_replayFileBtn->text();

    if (m_replaying) {
        bool alreadyDead = false;
        bool success = true;
        if (m_childPid != -1) {
            alreadyDead = false;
            int status;
            int ret = waitpid(m_childPid, &status, WNOHANG);
            if (ret == -1) {
                success = false;
            } else if (ret == 0) {
                kill(m_childPid, SIGINT);
                if (waitpid(m_childPid, &status, 0) == -1) {
                    success = false;
                } else {
                    success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
                }
            } else {
                success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
            }
            m_childPid = -1;
        }

        m_startReplayBtn->setText("Start replay");
        m_replaying = false;
        m_replayBackBtn->setEnabled(true);
        m_saveReplayBtn->setEnabled(false);
        m_replayBottomPanel->setEnabled(false);
        m_replayTimerLabel->setText("00:00:00");

        m_startStopReplayAction->setText("Start replay");
        m_saveReplayAction->setEnabled(false);
        m_trayIcon->setIcon(QIcon("/usr/share/icons/hicolor/32x32/status/com.dec05eba.gpu_screen_recorder.tray-idle.png"));

        if (m_prevExitStatus == 10)
            showNotification("GPU Screen Recorder", "You need to have pkexec installed and have a polkit agent running to record your monitor", true);
        else if (m_prevExitStatus == 50)
            showNotification("GPU Screen Recorder", "Desktop portal capture failed.", true);
        else if (m_prevExitStatus != 60 && !success)
            showNotification("GPU Screen Recorder", "Failed to start replay.", true);
        else if (success && m_prevExitStatus == 0) {
            if (m_showStoppedNotifCheck->isChecked())
                showNotification("GPU Screen Recorder", "Stopped replay");
        }
        return;
    }

    saveConfigs();

    int fps = m_fpsSpin->value();
    int replayTime = m_replayTimeSpin->value();
    std::string windowStr = m_recordAreaCombo->currentData().toString().toStdString();
    std::string fpsStr = std::to_string(fps);
    std::string replayTimeStr = std::to_string(replayTime);
    bool changeVideoResolution = m_changeResolutionCheck->isChecked();

    char dirTmp[PATH_MAX];
    strcpy(dirTmp, dir.toStdString().c_str());
    if (create_directory_recursive(dirTmp) != 0) {
        showNotification("GPU Screen Recorder", QString("Failed to start replay. Failed to create ") + dirTmp, true);
        return;
    }

    std::string containerStr = m_replayContainerCombo->currentData().toString().toStdString();
    std::string colorRangeStr = m_colorRangeCombo->currentData().toString().toStdString();
    std::string qualityStr = m_qualityCombo->currentData().toString().toStdString();
    std::string audioCodecStr = m_audioCodecCombo->currentData().toString().toStdString();
    std::string framerateModeStr = m_framerateModeCombo->currentData().toString().toStdString();
    bool recordCursor = m_recordCursorCheck->isChecked();
    bool restorePortal = m_restorePortalCheck->isChecked();
    std::string videoBitrateStr = std::to_string(m_videoBitrateSpin->value());

    int recordWidth = m_videoWidthSpin->value();
    int recordHeight = m_videoHeightSpin->value();

    const char *encoder = "gpu";
    std::string videoCodecInput = m_codecCombo->currentData().toString().toStdString();
    if (videoCodecInput == "h264_software") {
        videoCodecInput = "h264";
        encoder = "cpu";
    } else if (videoCodecInput == "auto") {
        if (!m_gsrInfo.supported_video_codecs.h264 && !m_gsrInfo.supported_video_codecs.hevc
            && !m_gsrInfo.supported_video_codecs.av1 && !m_gsrInfo.supported_video_codecs.vp8
            && !m_gsrInfo.supported_video_codecs.vp9) {
            videoCodecInput = "h264";
            encoder = "cpu";
        }
    }

    if ((videoCodecInput == "vp8" || videoCodecInput == "vp9") && containerStr != "webm" && containerStr != "matroska") {
        containerStr = "webm";
    }

    char area[64];
    snprintf(area, sizeof(area), "%dx%d", recordWidth, recordHeight);

    std::vector<const char*> args = {
        "gpu-screen-recorder", "-w", windowStr.c_str(), "-c", containerStr.c_str(), "-k", videoCodecInput.c_str(),
        "-ac", audioCodecStr.c_str(), "-f", fpsStr.c_str(), "-cursor", recordCursor ? "yes" : "no",
        "-restore-portal-session", restorePortal ? "yes" : "no", "-cr", colorRangeStr.c_str(),
        "-r", replayTimeStr.c_str(), "-encoder", encoder, "-o", dir.toStdString().c_str()
    };

    if (qualityStr == "custom") {
        args.push_back("-bm"); args.push_back("cbr");
        args.push_back("-q"); args.push_back(videoBitrateStr.c_str());
    } else {
        args.push_back("-q"); args.push_back(qualityStr.c_str());
    }

    if (m_overclockCheck->isChecked())
        args.insert(args.end(), { "-oc", "yes" });

    if (framerateModeStr != "auto") {
        args.push_back("-fm"); args.push_back(framerateModeStr.c_str());
    }

    {
        std::string mergeAudioTracks;
        std::vector<std::string> audioTracks;
        bool invertAppAudio = m_invertAppAudioCheck->isChecked();
        int numAppAudio = 0;

        for (int i = 0; i < m_audioItemsLayout->count(); ++i) {
            QWidget *row = m_audioItemsLayout->itemAt(i)->widget();
            if (!row) continue;
            QString type = row->property("audio-track-type").toString();
            QComboBox *combo = row->findChild<QComboBox*>();
            QLineEdit *entry = row->findChild<QLineEdit*>();

            if (type == "device" && combo) {
                audioTracks.push_back("device:" + combo->currentData().toString().toStdString());
            } else if (type == "app" && combo) {
                if (!m_gsrInfo.system_info.supports_app_audio) continue;
                audioTracks.push_back((invertAppAudio ? "app-inverse:" : "app:") + combo->currentData().toString().toStdString());
                ++numAppAudio;
            } else if (type == "app-custom" && entry) {
                if (!m_gsrInfo.system_info.supports_app_audio) continue;
                audioTracks.push_back((invertAppAudio ? "app-inverse:" : "app:") + entry->text().toStdString());
                ++numAppAudio;
            }
        }

        if (numAppAudio == 0 && invertAppAudio)
            audioTracks.push_back("app-inverse:");

        if (m_splitAudioCheck && !m_splitAudioCheck->isChecked()) {
            if (!audioTracks.empty()) {
                std::string merged;
                for (size_t j = 0; j < audioTracks.size(); ++j) {
                    if (j > 0) merged += '|';
                    merged += audioTracks[j];
                }
                args.push_back("-a"); args.push_back(merged.c_str());
            }
        } else {
            for (const auto &track : audioTracks) {
                args.push_back("-a"); args.push_back(track.c_str());
            }
        }
    }

    if (changeVideoResolution)
        args.insert(args.end(), { "-s", area });

    args.push_back(nullptr);

    fprintf(stderr, "info: running command:");
    for (const char *a : args) { if (a) fprintf(stderr, " %s", a); }
    fprintf(stderr, "\n");

    if (m_hideWhenRecordingAction->isChecked()) {
        hide();
        m_windowHidden = true;
        m_showHideAction->setText("Show window");
    }

    startGpuScreenRecorder(args);

    m_replaying = true;
    m_startReplayBtn->setText("Stop replay");
    m_replayBackBtn->setEnabled(false);
    m_saveReplayBtn->setEnabled(true);
    m_replayBottomPanel->setEnabled(true);

    m_startStopReplayAction->setText("Stop replay");
    m_saveReplayAction->setEnabled(true);
    m_trayIcon->setIcon(QIcon("/usr/share/icons/hicolor/32x32/status/com.dec05eba.gpu_screen_recorder.tray-recording.png"));

    if (m_showStartedNotifCheck->isChecked())
        showNotification("GPU Screen Recorder", "Started replay");

    m_recordStartTime = clock_get_monotonic_seconds();
}

void MainWindow::onSaveReplayClicked() {
    if (m_childPid == -1) return;
    kill(m_childPid, SIGUSR1);
    if (m_showSavedNotifCheck->isChecked())
        showNotification("GPU Screen Recorder", "Saved replay");
}

QWidget* MainWindow::createReplayPage() {
    QWidget *page = new QWidget;
    QGridLayout *grid = new QGridLayout(page);
    grid->setContentsMargins(10, 10, 10, 10);

    int row = 0;
    std::string videoDir = get_videos_dir();

    m_replayBackBtn = new QPushButton("Back");
    m_replayBackBtn->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    grid->addWidget(m_replayBackBtn, row++, 0, 1, 5);

    if (m_gsrInfo.system_info.display_server == DisplayServer::WAYLAND) {
        QLabel *wlLabel = new QLabel("Your Wayland compositor doesn't support global hotkeys. Use KDE Plasma on Wayland if you want to use hotkeys.");
        wlLabel->setWordWrap(true);
        grid->addWidget(wlLabel, row++, 0, 1, 5);
    }

    QFrame *sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    grid->addWidget(sep, row++, 0, 1, 5);

    QHBoxLayout *fileLayout = new QHBoxLayout;
    fileLayout->addWidget(new QLabel("Where do you want to save the replays?"));
    m_replayFileBtn = new QPushButton(QString::fromStdString(videoDir));
    m_replayFileBtn->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    fileLayout->addWidget(m_replayFileBtn, 1);
    QWidget *fileWidget = new QWidget;
    fileWidget->setLayout(fileLayout);
    grid->addWidget(fileWidget, row++, 0, 1, 5);

    connect(m_replayFileBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Where do you want to save the replays?");
        if (!dir.isEmpty()) {
            m_replayFileBtn->setText(dir);
            m_config.replay_config.save_directory = dir.toStdString();
        }
    });

    QHBoxLayout *containerLayout = new QHBoxLayout;
    containerLayout->addWidget(new QLabel("Container:"));
    m_replayContainerCombo = new QComboBox;
    for (auto &c : supported_containers)
        m_replayContainerCombo->addItem(c.file_extension, c.container_name);
    if (m_gsrInfo.supported_video_codecs.vp8 || m_gsrInfo.supported_video_codecs.vp9)
        m_replayContainerCombo->addItem("webm", "webm");
    containerLayout->addWidget(m_replayContainerCombo, 1);
    QWidget *containerWidget = new QWidget;
    containerWidget->setLayout(containerLayout);
    grid->addWidget(containerWidget, row++, 0, 1, 5);

    QHBoxLayout *timeLayout = new QHBoxLayout;
    timeLayout->addWidget(new QLabel("Replay time in seconds:"));
    m_replayTimeSpin = new QSpinBox;
    m_replayTimeSpin->setRange(5, 1200);
    m_replayTimeSpin->setValue(30);
    timeLayout->addWidget(m_replayTimeSpin, 1);
    QWidget *timeWidget = new QWidget;
    timeWidget->setLayout(timeLayout);
    grid->addWidget(timeWidget, row++, 0, 1, 5);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    m_startReplayBtn = new QPushButton("Start replay");
    btnLayout->addWidget(m_startReplayBtn);
    m_saveReplayBtn = new QPushButton("Save replay");
    m_saveReplayBtn->setEnabled(false);
    btnLayout->addWidget(m_saveReplayBtn);
    QWidget *btnWidget = new QWidget;
    btnWidget->setLayout(btnLayout);
    grid->addWidget(btnWidget, row++, 0, 1, 5);

    QFrame *sep2 = new QFrame;
    sep2->setFrameShape(QFrame::HLine);
    grid->addWidget(sep2, row++, 0, 1, 5);

    m_replayBottomPanel = new QWidget;
    QHBoxLayout *panelLayout = new QHBoxLayout(m_replayBottomPanel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    QLabel *icon = new QLabel;
    icon->setPixmap(style()->standardPixmap(QStyle::SP_MediaPlay));
    panelLayout->addWidget(icon);
    m_replayTimerLabel = new QLabel("00:00:00");
    panelLayout->addWidget(m_replayTimerLabel);
    panelLayout->addStretch();
    m_replayBottomPanel->setEnabled(false);
    grid->addWidget(m_replayBottomPanel, row++, 0, 1, 5);

    connect(m_replayBackBtn, &QPushButton::clicked, this, &MainWindow::onBackClick);
    connect(m_startReplayBtn, &QPushButton::clicked, this, &MainWindow::onStartReplayButtonClicked);
    connect(m_saveReplayBtn, &QPushButton::clicked, this, &MainWindow::onSaveReplayClicked);

    return page;
}

void MainWindow::onStartStreamingClick() {
    int numAudioTracks = 0;
    for (int i = 0; i < m_audioItemsLayout->count(); ++i) {
        QWidget *row = m_audioItemsLayout->itemAt(i)->widget();
        if (!row) continue;
        if (row->property("audio-track-type").toString() == "device")
            ++numAudioTracks;
    }

    if (numAudioTracks > 1 && m_splitAudioCheck->isChecked()) {
        QMessageBox::critical(this, "GPU Screen Recorder",
            "Streaming doesn't work with more than 1 audio track. Please remove all audio tracks or only use 1 audio track or select to merge audio tracks.");
        return;
    }

    m_stack->setCurrentIndex(3);
    updateSystrayMenu(SystrayPage::STREAMING);
}

void MainWindow::onStartStreamButtonClicked() {
    if (m_streaming) {
        bool alreadyDead = false;
        bool success = true;
        if (m_childPid != -1) {
            alreadyDead = false;
            int status;
            int ret = waitpid(m_childPid, &status, WNOHANG);
            if (ret == -1) {
                success = false;
            } else if (ret == 0) {
                kill(m_childPid, SIGINT);
                if (waitpid(m_childPid, &status, 0) == -1) {
                    success = false;
                } else {
                    success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
                }
            } else {
                success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
            }
            m_childPid = -1;
        }

        m_startStreamBtn->setText("Start streaming");
        m_streaming = false;
        m_streamBackBtn->setEnabled(true);
        m_streamBottomPanel->setEnabled(false);
        m_streamTimerLabel->setText("00:00:00");

        m_startStopStreamingAction->setText("Start streaming");
        m_trayIcon->setIcon(QIcon("/usr/share/icons/hicolor/32x32/status/com.dec05eba.gpu_screen_recorder.tray-idle.png"));

        if (m_prevExitStatus == 10)
            showNotification("GPU Screen Recorder", "You need to have pkexec installed and have a polkit agent running to record your monitor", true);
        else if (m_prevExitStatus == 50)
            showNotification("GPU Screen Recorder", "Desktop portal capture failed.", true);
        else if (m_prevExitStatus != 60 && !success)
            showNotification("GPU Screen Recorder", "Failed to stream video.", true);
        else if (success && m_prevExitStatus == 0) {
            if (m_showStoppedNotifCheck->isChecked())
                showNotification("GPU Screen Recorder", "Stopped streaming");
        }
        return;
    }

    saveConfigs();

    int fps = m_fpsSpin->value();
    std::string windowStr = m_recordAreaCombo->currentData().toString().toStdString();
    std::string fpsStr = std::to_string(fps);
    bool changeVideoResolution = m_changeResolutionCheck->isChecked();

    std::string streamUrl;
    std::string containerStr = "flv";
    QString svc = m_streamServiceCombo->currentData().toString();
    if (svc == "twitch") {
        streamUrl = "rtmp://live.twitch.tv/app/" + m_twitchKeyEdit->text().toStdString();
    } else if (svc == "youtube") {
        streamUrl = "rtmp://a.rtmp.youtube.com/live2/" + m_youtubeKeyEdit->text().toStdString();
    } else if (svc == "custom") {
        streamUrl = m_customUrlEdit->text().toStdString();
        containerStr = m_customStreamContainerCombo->currentData().toString().toStdString();
        if (!(streamUrl.size() >= 7 && memcmp(streamUrl.c_str(), "rtmp://", 7) == 0) &&
            !(streamUrl.size() >= 8 && memcmp(streamUrl.c_str(), "rtmps://", 8) == 0) &&
            !(streamUrl.size() >= 7 && memcmp(streamUrl.c_str(), "rtsp://", 7) == 0) &&
            !(streamUrl.size() >= 6 && memcmp(streamUrl.c_str(), "srt://", 6) == 0) &&
            !(streamUrl.size() >= 7 && memcmp(streamUrl.c_str(), "http://", 7) == 0) &&
            !(streamUrl.size() >= 8 && memcmp(streamUrl.c_str(), "https://", 8) == 0) &&
            !(streamUrl.size() >= 6 && memcmp(streamUrl.c_str(), "tcp://", 6) == 0) &&
            !(streamUrl.size() >= 6 && memcmp(streamUrl.c_str(), "udp://", 6) == 0))
            streamUrl = "rtmp://" + streamUrl;
    }

    std::string colorRangeStr = m_colorRangeCombo->currentData().toString().toStdString();
    std::string qualityStr = m_qualityCombo->currentData().toString().toStdString();
    std::string audioCodecStr = m_audioCodecCombo->currentData().toString().toStdString();
    std::string framerateModeStr = m_framerateModeCombo->currentData().toString().toStdString();
    bool recordCursor = m_recordCursorCheck->isChecked();
    bool restorePortal = m_restorePortalCheck->isChecked();
    std::string videoBitrateStr = std::to_string(m_videoBitrateSpin->value());

    int recordWidth = m_videoWidthSpin->value();
    int recordHeight = m_videoHeightSpin->value();

    const char *encoder = "gpu";
    std::string videoCodecInput = m_codecCombo->currentData().toString().toStdString();
    if (videoCodecInput == "h264_software") {
        videoCodecInput = "h264";
        encoder = "cpu";
    } else if (videoCodecInput == "auto") {
        if (!m_gsrInfo.supported_video_codecs.h264 && !m_gsrInfo.supported_video_codecs.hevc
            && !m_gsrInfo.supported_video_codecs.av1 && !m_gsrInfo.supported_video_codecs.vp8
            && !m_gsrInfo.supported_video_codecs.vp9) {
            videoCodecInput = "h264";
            encoder = "cpu";
        }
    }

    if ((videoCodecInput == "vp8" || videoCodecInput == "vp9") && containerStr != "webm" && containerStr != "matroska") {
        containerStr = "webm";
    }

    char area[64];
    snprintf(area, sizeof(area), "%dx%d", recordWidth, recordHeight);

    std::vector<const char*> args = {
        "gpu-screen-recorder", "-w", windowStr.c_str(), "-c", containerStr.c_str(), "-k", videoCodecInput.c_str(),
        "-ac", audioCodecStr.c_str(), "-f", fpsStr.c_str(), "-cursor", recordCursor ? "yes" : "no",
        "-restore-portal-session", restorePortal ? "yes" : "no", "-cr", colorRangeStr.c_str(),
        "-encoder", encoder, "-o", streamUrl.c_str()
    };

    if (qualityStr == "custom") {
        args.push_back("-bm"); args.push_back("cbr");
        args.push_back("-q"); args.push_back(videoBitrateStr.c_str());
    } else {
        args.push_back("-q"); args.push_back(qualityStr.c_str());
    }

    if (m_overclockCheck->isChecked())
        args.insert(args.end(), { "-oc", "yes" });

    if (framerateModeStr != "auto") {
        args.push_back("-fm"); args.push_back(framerateModeStr.c_str());
    }

    {
        std::string mergeAudioTracks;
        std::vector<std::string> audioTracks;
        bool invertAppAudio = m_invertAppAudioCheck->isChecked();
        int numAppAudio = 0;

        for (int i = 0; i < m_audioItemsLayout->count(); ++i) {
            QWidget *row = m_audioItemsLayout->itemAt(i)->widget();
            if (!row) continue;
            QString type = row->property("audio-track-type").toString();
            QComboBox *combo = row->findChild<QComboBox*>();
            QLineEdit *entry = row->findChild<QLineEdit*>();

            if (type == "device" && combo) {
                audioTracks.push_back("device:" + combo->currentData().toString().toStdString());
            } else if (type == "app" && combo) {
                if (!m_gsrInfo.system_info.supports_app_audio) continue;
                audioTracks.push_back((invertAppAudio ? "app-inverse:" : "app:") + combo->currentData().toString().toStdString());
                ++numAppAudio;
            } else if (type == "app-custom" && entry) {
                if (!m_gsrInfo.system_info.supports_app_audio) continue;
                audioTracks.push_back((invertAppAudio ? "app-inverse:" : "app:") + entry->text().toStdString());
                ++numAppAudio;
            }
        }

        if (numAppAudio == 0 && invertAppAudio)
            audioTracks.push_back("app-inverse:");

        if (m_splitAudioCheck && !m_splitAudioCheck->isChecked()) {
            if (!audioTracks.empty()) {
                std::string merged;
                for (size_t j = 0; j < audioTracks.size(); ++j) {
                    if (j > 0) merged += '|';
                    merged += audioTracks[j];
                }
                args.push_back("-a"); args.push_back(merged.c_str());
            }
        } else {
            for (const auto &track : audioTracks) {
                args.push_back("-a"); args.push_back(track.c_str());
            }
        }
    }

    if (changeVideoResolution)
        args.insert(args.end(), { "-s", area });

    args.push_back(nullptr);

    fprintf(stderr, "info: running command:");
    for (const char *a : args) { if (a) fprintf(stderr, " %s", a); }
    fprintf(stderr, "\n");

    if (m_hideWhenRecordingAction->isChecked()) {
        hide();
        m_windowHidden = true;
        m_showHideAction->setText("Show window");
    }

    startGpuScreenRecorder(args);

    m_streaming = true;
    m_startStreamBtn->setText("Stop streaming");
    m_streamBackBtn->setEnabled(false);
    m_streamBottomPanel->setEnabled(true);

    m_startStopStreamingAction->setText("Stop streaming");
    m_trayIcon->setIcon(QIcon("/usr/share/icons/hicolor/32x32/status/com.dec05eba.gpu_screen_recorder.tray-recording.png"));

    if (m_showStartedNotifCheck->isChecked())
        showNotification("GPU Screen Recorder", "Started streaming");

    m_recordStartTime = clock_get_monotonic_seconds();
}

QWidget* MainWindow::createStreamingPage() {
    QWidget *page = new QWidget;
    QGridLayout *grid = new QGridLayout(page);
    grid->setContentsMargins(10, 10, 10, 10);

    int row = 0;

    m_streamBackBtn = new QPushButton("Back");
    m_streamBackBtn->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    grid->addWidget(m_streamBackBtn, row++, 0, 1, 3);

    QHBoxLayout *svcLayout = new QHBoxLayout;
    svcLayout->addWidget(new QLabel("Stream service:"));
    m_streamServiceCombo = new QComboBox;
    m_streamServiceCombo->addItem("Twitch", "twitch");
    m_streamServiceCombo->addItem("Youtube", "youtube");
    m_streamServiceCombo->addItem("Custom", "custom");
    svcLayout->addWidget(m_streamServiceCombo, 1);
    QWidget *svcWidget = new QWidget;
    svcWidget->setLayout(svcLayout);
    grid->addWidget(svcWidget, row++, 0, 1, 3);

    QHBoxLayout *keyLayout = new QHBoxLayout;
    m_streamKeyLabel = new QLabel("Stream key:");
    keyLayout->addWidget(m_streamKeyLabel);

    m_youtubeKeyEdit = new QLineEdit;
    m_youtubeKeyEdit->setEchoMode(QLineEdit::Password);
    m_youtubeKeyEdit->setVisible(false);
    keyLayout->addWidget(m_youtubeKeyEdit, 1);

    m_twitchKeyEdit = new QLineEdit;
    m_twitchKeyEdit->setEchoMode(QLineEdit::Password);
    m_twitchKeyEdit->setVisible(false);
    keyLayout->addWidget(m_twitchKeyEdit, 1);

    m_customUrlEdit = new QLineEdit;
    m_customUrlEdit->setVisible(false);
    keyLayout->addWidget(m_customUrlEdit, 1);

    QWidget *keyWidget = new QWidget;
    keyWidget->setLayout(keyLayout);
    grid->addWidget(keyWidget, row++, 0, 1, 3);

    QHBoxLayout *ccLayout = new QHBoxLayout;
    ccLayout->addWidget(new QLabel("Container:"));
    m_customStreamContainerCombo = new QComboBox;
    for (auto &c : supported_containers)
        m_customStreamContainerCombo->addItem(c.file_extension, c.container_name);
    if (m_gsrInfo.supported_video_codecs.vp8 || m_gsrInfo.supported_video_codecs.vp9)
        m_customStreamContainerCombo->addItem("webm", "webm");
    ccLayout->addWidget(m_customStreamContainerCombo, 1);
    m_customContainerGrid = new QWidget;
    m_customContainerGrid->setLayout(ccLayout);
    m_customContainerGrid->setVisible(false);
    grid->addWidget(m_customContainerGrid, row++, 0, 1, 3);

    QFrame *sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    grid->addWidget(sep, row++, 0, 1, 3);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    m_startStreamBtn = new QPushButton("Start streaming");
    btnLayout->addWidget(m_startStreamBtn);
    QWidget *btnWidget = new QWidget;
    btnWidget->setLayout(btnLayout);
    grid->addWidget(btnWidget, row++, 0, 1, 3);

    QFrame *sep2 = new QFrame;
    sep2->setFrameShape(QFrame::HLine);
    grid->addWidget(sep2, row++, 0, 1, 3);

    m_streamBottomPanel = new QWidget;
    QHBoxLayout *panelLayout = new QHBoxLayout(m_streamBottomPanel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    QLabel *icon = new QLabel;
    icon->setPixmap(style()->standardPixmap(QStyle::SP_MediaPlay));
    panelLayout->addWidget(icon);
    m_streamTimerLabel = new QLabel("00:00:00");
    panelLayout->addWidget(m_streamTimerLabel);
    panelLayout->addStretch();
    m_streamBottomPanel->setEnabled(false);
    grid->addWidget(m_streamBottomPanel, row++, 0, 1, 3);

    for (auto *edit : { m_youtubeKeyEdit, m_twitchKeyEdit }) {
        QAction *toggleAction = edit->addAction(style()->standardIcon(QStyle::SP_FileDialogContentsView), QLineEdit::TrailingPosition);
        connect(toggleAction, &QAction::triggered, this, [edit]() {
            edit->setEchoMode(edit->echoMode() == QLineEdit::Password ? QLineEdit::Normal : QLineEdit::Password);
        });
    }

    connect(m_streamServiceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &MainWindow::onStreamServiceChanged);
    connect(m_streamBackBtn, &QPushButton::clicked, this, &MainWindow::onBackClick);
    connect(m_startStreamBtn, &QPushButton::clicked, this, &MainWindow::onStartStreamButtonClicked);

    return page;
}

void MainWindow::onBackClick() {
    m_stack->setCurrentIndex(0);
    updateSystrayMenu(SystrayPage::FRONT);
}

void MainWindow::startGpuScreenRecorder(std::vector<const char*> args) {
    pid_t parentPid = getpid();
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        showNotification("GPU Screen Recorder", "Failed to fork process", true);
        return;
    } else if (pid == 0) {
        if (prctl(PR_SET_PDEATHSIG, SIGTERM) == -1) {
            perror("prctl");
            _exit(3);
        }
        if (getppid() != parentPid)
            _exit(3);
        execvp(args[0], (char* const*)args.data());
        perror("execvp");
        _exit(127);
    }
    m_childPid = pid;
}

void MainWindow::onGlobalShortcutActivated(const QString &shortcutId) {
    int pageIdx = m_stack->currentIndex();
    if (shortcutId == SHORTCUT_ID_START_STOP_RECORDING) {
        if (pageIdx == 2) m_startRecordBtn->click();
        else if (pageIdx == 1) m_startReplayBtn->click();
        else if (pageIdx == 3) m_startStreamBtn->click();
    } else if (shortcutId == SHORTCUT_ID_PAUSE_UNPAUSE_RECORDING) {
        if (pageIdx == 2) m_pauseRecordBtn->click();
    } else if (shortcutId == SHORTCUT_ID_SAVE_REPLAY) {
        if (pageIdx == 1) m_saveReplayBtn->click();
    }
}

void MainWindow::onGlobalShortcutChanged(const GsrShortcut &shortcut) {
    Q_UNUSED(shortcut);
}

static bool is_nv_fbc_installed() {
    void *lib = dlopen("libnvidia-fbc.so.1", RTLD_LAZY);
    if (lib) dlclose(lib);
    return lib != nullptr;
}

static bool is_cuda_installed() {
    void *lib = dlopen("libcuda.so.1", RTLD_LAZY);
    if (!lib) lib = dlopen("libcuda.so", RTLD_LAZY);
    if (lib) dlclose(lib);
    return lib != nullptr;
}

static bool is_nvenc_installed() {
    void *lib = dlopen("libnvidia-encode.so.1", RTLD_LAZY);
    if (lib) dlclose(lib);
    return lib != nullptr;
}

static bool is_inside_flatpak() {
    return getenv("FLATPAK_ID") != nullptr;
}

static bool is_pkexec_installed() {
    return is_program_installed({"pkexec", 6});
}

static GsrInfoExitStatus get_gpu_screen_recorder_info(GsrInfo *gsr_info) {
    *gsr_info = GsrInfo{};
    FILE *f = popen("gpu-screen-recorder --info", "r");
    if (!f) return GsrInfoExitStatus::FAILED_TO_RUN_COMMAND;

    char output[8192];
    ssize_t n = fread(output, 1, sizeof(output) - 1, f);
    if (n < 0) { pclose(f); return GsrInfoExitStatus::FAILED_TO_RUN_COMMAND; }
    output[n] = '\0';

    enum Section { UNKNOWN, SYSTEM_INFO, GPU_INFO, VIDEO_CODECS, CAPTURE_OPTIONS };
    Section section = UNKNOWN;

    string_split_char(output, '\n', [&](StringView line) {
        std::string ls(line.str, line.size);
        if (starts_with(ls, "section=")) {
            const char *sn = ls.c_str() + 8;
            if (strcmp(sn, "system_info") == 0) section = SYSTEM_INFO;
            else if (strcmp(sn, "gpu_info") == 0) section = GPU_INFO;
            else if (strcmp(sn, "video_codecs") == 0) section = VIDEO_CODECS;
            else if (strcmp(sn, "capture_options") == 0) section = CAPTURE_OPTIONS;
            else section = UNKNOWN;
            return true;
        }

        auto setField = [&](const std::string &name) -> StringView {
            size_t sep = ls.find('|');
            if (sep == std::string::npos) return {};
            return {ls.c_str() + sep + 1, ls.size() - (sep + 1)};
        };

        switch (section) {
            case SYSTEM_INFO: {
                auto val = setField(ls);
                auto attr = StringView{ls.c_str(), ls.find('|')};
                if (attr == "display_server") {
                    if (val == "x11") gsr_info->system_info.display_server = DisplayServer::X11;
                    else if (val == "wayland") gsr_info->system_info.display_server = DisplayServer::WAYLAND;
                } else if (attr == "is_steam_deck") {
                    gsr_info->system_info.is_steam_deck = val == "yes";
                } else if (attr == "supports_app_audio") {
                    gsr_info->system_info.supports_app_audio = val == "yes";
                }
                break;
            }
            case GPU_INFO: {
                auto val = setField(ls);
                auto attr = StringView{ls.c_str(), ls.find('|')};
                if (attr == "vendor") {
                    if (val == "amd") gsr_info->gpu_info.vendor = GpuVendor::AMD;
                    else if (val == "intel") gsr_info->gpu_info.vendor = GpuVendor::INTEL;
                    else if (val == "nvidia") gsr_info->gpu_info.vendor = GpuVendor::NVIDIA;
                    else if (val == "broadcom") gsr_info->gpu_info.vendor = GpuVendor::BROADCOM;
                }
                break;
            }
            case VIDEO_CODECS: {
                auto parseCodec = [&](const char *name, bool &flag) {
                    if (ls == name) flag = true;
                };
                parseCodec("h264", gsr_info->supported_video_codecs.h264);
                parseCodec("h264_software", gsr_info->supported_video_codecs.h264_software);
                parseCodec("hevc", gsr_info->supported_video_codecs.hevc);
                parseCodec("hevc_hdr", gsr_info->supported_video_codecs.hevc_hdr);
                parseCodec("hevc_10bit", gsr_info->supported_video_codecs.hevc_10bit);
                parseCodec("av1", gsr_info->supported_video_codecs.av1);
                parseCodec("av1_hdr", gsr_info->supported_video_codecs.av1_hdr);
                parseCodec("av1_10bit", gsr_info->supported_video_codecs.av1_10bit);
                parseCodec("vp8", gsr_info->supported_video_codecs.vp8);
                parseCodec("vp9", gsr_info->supported_video_codecs.vp9);
                break;
            }
            case CAPTURE_OPTIONS: {
                if (ls == "window") gsr_info->supported_capture_options.window = true;
                else if (ls == "focused") gsr_info->supported_capture_options.focused = true;
                else if (ls == "portal") gsr_info->supported_capture_options.portal = true;
                else if (ls != "region" && !ls.empty() && ls[0] != '/') {
                    size_t sep = ls.find('|');
                    GsrMonitor mon;
                    if (sep != std::string::npos) {
                        mon.name = ls.substr(0, sep);
                        sscanf(ls.c_str() + sep + 1, "%dx%d", &mon.size.x, &mon.size.y);
                    } else {
                        mon.name = ls;
                    }
                    gsr_info->supported_capture_options.monitors.push_back(std::move(mon));
                }
                break;
            }
            default: break;
        }
        return true;
    });

    int status = pclose(f);
    if (WIFEXITED(status)) {
        switch (WEXITSTATUS(status)) {
            case 0: return GsrInfoExitStatus::OK;
            case 22: return GsrInfoExitStatus::OPENGL_FAILED;
            case 23: return GsrInfoExitStatus::NO_DRM_CARD;
            default: return GsrInfoExitStatus::FAILED_TO_RUN_COMMAND;
        }
    }
    return GsrInfoExitStatus::FAILED_TO_RUN_COMMAND;
}

static bool gsr_startup_validation(const GsrInfo &gsrInfo, GsrInfoExitStatus exitStatus, bool flatpak) {
    if (exitStatus == GsrInfoExitStatus::FAILED_TO_RUN_COMMAND) {
        const char *cmd = flatpak
            ? "flatpak run --command=gpu-screen-recorder com.dec05eba.gpu_screen_recorder -w screen -f 60 -o video.mp4"
            : "gpu-screen-recorder -w screen -f 60 -o video.mp4";
        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setWindowTitle("GPU Screen Recorder");
        msgBox.setText(QString("Failed to run 'gpu-screen-recorder'. Make sure it's installed.\nRun:\n%1\nin a terminal.").arg(cmd));
        msgBox.exec();
        return false;
    }

    if (exitStatus == GsrInfoExitStatus::OPENGL_FAILED) {
        QMessageBox::critical(nullptr, "GPU Screen Recorder",
            "Failed to get OpenGL info. Make sure your GPU drivers are installed.");
        return false;
    }

    if (exitStatus == GsrInfoExitStatus::NO_DRM_CARD) {
        QMessageBox::critical(nullptr, "GPU Screen Recorder",
            "Failed to find a valid DRM card.");
        return false;
    }

    if (gsrInfo.system_info.display_server == DisplayServer::UNKNOWN) {
        QMessageBox::critical(nullptr, "GPU Screen Recorder",
            "Neither X11 nor Wayland is running.");
        return false;
    }

    if (gsrInfo.gpu_info.vendor == GpuVendor::NVIDIA) {
        if (!is_cuda_installed()) {
            QMessageBox::critical(nullptr, "GPU Screen Recorder",
                "CUDA is not installed. GPU Screen Recorder requires CUDA for NVIDIA GPUs.");
            return false;
        }
        if (!is_nvenc_installed()) {
            QMessageBox::critical(nullptr, "GPU Screen Recorder",
                "NVENC is not installed. GPU Screen Recorder requires NVENC for NVIDIA GPUs.");
            return false;
        }
    }

    return true;
}

int main(int argc, char **argv) {
    setlocale(LC_ALL, "C");

    bool useOldUi = argc >= 2 && strcmp(argv[1], "use-old-ui") == 0;
    bool launchedByDaemon = argc >= 2 && strcmp(argv[1], "gsr-ui") == 0;

    if (geteuid() == 0) {
        fprintf(stderr, "Error: don't run as root\n");
        return 1;
    }

    bool configEmpty = false;
    Config config = read_config(configEmpty);

    GsrInfo gsrInfo;
    GsrInfoExitStatus gsrExitStatus = get_gpu_screen_recorder_info(&gsrInfo);

    if (gsrExitStatus == GsrInfoExitStatus::OK) {
        if (gsrInfo.system_info.display_server == DisplayServer::WAYLAND)
            setenv("QT_QPA_PLATFORM", "wayland", false);
    }

    bool flatpak = is_inside_flatpak();

    if (useOldUi) {
        config.main_config.use_new_ui = false;
        save_config(config);
    }

    if (config.main_config.use_new_ui) {
        const char *args[] = { "gsr-ui", launchedByDaemon ? "launch-daemon" : "launch-show", nullptr };
        execvp(args[0], (char* const*)args);
    }

    QApplication app(argc, argv);
    app.setApplicationName("GPU Screen Recorder");
    app.setApplicationDisplayName("GPU Screen Recorder");
    app.setOrganizationName("dec05eba");
    app.setApplicationVersion(GSR_VERSION);
    app.setQuitOnLastWindowClosed(false);

    app.setDesktopFileName("com.dec05eba.gpu_screen_recorder");

    if (!gsr_startup_validation(gsrInfo, gsrExitStatus, flatpak))
        return 1;

    MainWindow window(config, gsrInfo, flatpak);
    window.show();

    return app.exec();
}

#include "main.moc"
