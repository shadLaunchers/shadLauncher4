// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListView>
#include <QMap>
#include <QNetworkAccessManager>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>
#include "core/ipc/ipc_client.h"
#include "downloader.h"
#include "gui_settings.h"

namespace Ui {
class CheatsPatchesDialog;
}

class CheatsPatches : public QWidget {
    Q_OBJECT

public:
    CheatsPatches(std::shared_ptr<GUISettings> gui_settings, std::shared_ptr<IpcClient> ipc_client,
                  const QString& gameName, const QString& gameSerial, const QString& gameVersion,
                  const QString& gameSize, const QPixmap& gameImage, QWidget* parent = nullptr);
    ~CheatsPatches();

    void downloadCheats(const QString& source, const QString& m_gameSerial,
                        const QString& m_gameVersion, bool showMessageBox);
    void downloadPatches(const QString repository, const bool showMessageBox);
    void createFilesJson(const QString& repository);
    void clearListCheats();
    void compatibleVersionNotice(const QString repository);

signals:
    void downloadFinished();

private:
    // UI Setup and Event Handlers
    void setupUI();
    void onSaveButtonClicked();
    QCheckBox* findCheckBoxByName(const QString& name);
    bool eventFilter(QObject* obj, QEvent* event);
    void onPatchCheckBoxHovered(QCheckBox* checkBox, bool hovered);

    // Cheat and Patch Management
    void populateFileListCheats();
    void populateFileListPatches();

    void addCheatsToLayout(const QJsonArray& modsArray, const QJsonArray& creditsArray);
    void addPatchesToLayout(const QString& serial);

    void applyCheat(const QString& modName, bool enabled);
    void applyPatch(const QString& patchName, bool enabled);

    void uncheckAllCheatCheckBoxes();
    void updateNoteTextEdit(const QString& patchName);

    // Network Manager
    QNetworkAccessManager* manager;

    // Patch Info Structures
    struct MemoryMod {
        QString offset;
        QString on;
        QString off;
    };

    struct Cheat {
        QString name;
        QString type;
        bool hasHint;
        QVector<MemoryMod> memoryMods;
    };

    struct PatchInfo {
        QString name;
        QString author;
        QString note;
        QJsonArray linesArray;
        QString serial;
    };

    // Members
    QString m_gameName;
    QString m_gameSerial;
    QString m_gameVersion;
    QString m_gameSize;
    QPixmap m_gameImage;
    QString m_cheatFilePath;
    QMap<QString, Cheat> m_cheats;
    QMap<QString, PatchInfo> m_patchInfos;
    QVector<QCheckBox*> m_cheatCheckBoxes;
    std::shared_ptr<GUISettings> m_gui_settings;
    std::shared_ptr<IpcClient> m_ipc_client;
    Ui::CheatsPatchesDialog* ui;
    Downloader* m_downloader = nullptr;

    // Strings
    QString defaultTextEdit_MSG;
    QString CheatsNotFound_MSG;
    QString CheatsDownloadedSuccessfully_MSG;
    QString DownloadComplete_MSG;
};
