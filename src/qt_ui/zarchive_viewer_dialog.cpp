// SPDX-FileCopyrightText: Copyright 2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <memory>

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include "common/path_util.h"
#include "core/file_sys/zar_packer.h"
#include "progress_dialog.h"
#include "zarchive_viewer_dialog.h"

constexpr int RelPathRole = Qt::UserRole;
constexpr int IsDirRole = Qt::UserRole + 1;
constexpr int PopulatedRole = Qt::UserRole + 2;
constexpr int IsDummyRole = Qt::UserRole + 3;

ZArchiveViewerDialog::ZArchiveViewerDialog(std::filesystem::path archive_path, QWidget* parent)
    : QDialog(parent), m_archive_path(std::move(archive_path)) {
    m_backend = Core::FileSys::OpenGameBackend(m_archive_path);

    SetupUi();

    if (!m_backend || !m_backend->IsOpen()) {
        m_status_label->setText(tr("Failed to open archive."));
        m_tree->setEnabled(false);
        return;
    }

    PopulateRoot();
    UpdateStatusLabel();
}

ZArchiveViewerDialog::~ZArchiveViewerDialog() = default;

void ZArchiveViewerDialog::SetupUi() {
    QString archive_qpath;
    Common::FS::PathToQString(archive_qpath, m_archive_path);

    setWindowTitle(
        tr("ZArchive Viewer - %1").arg(QString::fromStdString(m_archive_path.filename().string())));
    resize(640, 520);

    auto* layout = new QVBoxLayout(this);

    auto* path_label = new QLabel(archive_qpath, this);
    path_label->setWordWrap(true);
    layout->addWidget(path_label);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Name"), tr("Type")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setUniformRowHeights(true);
    layout->addWidget(m_tree, /*stretch=*/1);

    connect(m_tree, &QTreeWidget::itemExpanded, this, &ZArchiveViewerDialog::OnItemExpanded);
    connect(m_tree, &QTreeWidget::itemChanged, this, &ZArchiveViewerDialog::OnItemChanged);

    m_status_label = new QLabel(this);
    layout->addWidget(m_status_label);

    auto* button_row = new QHBoxLayout();
    auto* select_all = new QPushButton(tr("Select All"), this);
    auto* select_none = new QPushButton(tr("Select None"), this);
    connect(select_all, &QPushButton::clicked, this, &ZArchiveViewerDialog::OnSelectAll);
    connect(select_none, &QPushButton::clicked, this, &ZArchiveViewerDialog::OnSelectNone);
    button_row->addWidget(select_all);
    button_row->addWidget(select_none);
    button_row->addStretch(1);

    auto* extract_selected = new QPushButton(tr("Extract Selected..."), this);
    auto* extract_all = new QPushButton(tr("Extract All..."), this);
    connect(extract_selected, &QPushButton::clicked, this,
            &ZArchiveViewerDialog::OnExtractSelected);
    connect(extract_all, &QPushButton::clicked, this, &ZArchiveViewerDialog::OnExtractAll);
    button_row->addWidget(extract_selected);
    button_row->addWidget(extract_all);
    layout->addLayout(button_row);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(box);
}

void ZArchiveViewerDialog::PopulateRoot() {
    m_updating_checks = true;
    m_tree->clear();
    PopulateChildren(nullptr, "");
    m_updating_checks = false;
}

void ZArchiveViewerDialog::PopulateChildren(QTreeWidgetItem* parent_item,
                                            const std::string& rel_path) {
    auto entries = m_backend->ListDir(rel_path);
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (a.is_directory != b.is_directory) {
            return a.is_directory; // directories first
        }
        return a.name < b.name;
    });

    const QIcon folder_icon = style()->standardIcon(QStyle::SP_DirIcon);
    const QIcon file_icon = style()->standardIcon(QStyle::SP_FileIcon);

    for (const auto& entry : entries) {
        const std::string child_rel = rel_path.empty() ? entry.name : rel_path + "/" + entry.name;

        auto* item = new QTreeWidgetItem();
        item->setText(0, QString::fromStdString(entry.name));
        item->setText(1, entry.is_directory ? tr("Folder") : tr("File"));
        item->setIcon(0, entry.is_directory ? folder_icon : file_icon);
        item->setData(0, RelPathRole, QString::fromStdString(child_rel));
        item->setData(0, IsDirRole, entry.is_directory);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Unchecked);

        if (parent_item) {
            parent_item->addChild(item);
        } else {
            m_tree->addTopLevelItem(item);
        }

        if (entry.is_directory) {
            item->setData(0, PopulatedRole, false);
            auto* dummy = new QTreeWidgetItem();
            dummy->setText(0, tr("Loading..."));
            dummy->setData(0, IsDummyRole, true);
            dummy->setFlags(Qt::NoItemFlags);
            item->addChild(dummy);
        }
    }
}

void ZArchiveViewerDialog::EnsurePopulated(QTreeWidgetItem* item) {
    if (!item || !item->data(0, IsDirRole).toBool() || item->data(0, PopulatedRole).toBool()) {
        return;
    }

    const bool was_updating = m_updating_checks;
    m_updating_checks = true;

    // Drop the placeholder "Loading..." child.
    while (item->childCount() > 0) {
        delete item->takeChild(0);
    }

    PopulateChildren(item, item->data(0, RelPathRole).toString().toStdString());
    item->setData(0, PopulatedRole, true);

    m_updating_checks = was_updating;
}

void ZArchiveViewerDialog::EnsurePopulatedRecursive(QTreeWidgetItem* item) {
    if (!item || !item->data(0, IsDirRole).toBool()) {
        return;
    }
    EnsurePopulated(item);
    for (int i = 0; i < item->childCount(); ++i) {
        EnsurePopulatedRecursive(item->child(i));
    }
}

void ZArchiveViewerDialog::SetChildrenCheckState(QTreeWidgetItem* item, Qt::CheckState state) {
    for (int i = 0; i < item->childCount(); ++i) {
        QTreeWidgetItem* child = item->child(i);
        if (child->data(0, IsDummyRole).toBool()) {
            continue;
        }
        child->setCheckState(0, state);
        if (child->data(0, IsDirRole).toBool()) {
            SetChildrenCheckState(child, state);
        }
    }
}

void ZArchiveViewerDialog::UpdateAncestorCheckState(QTreeWidgetItem* item) {
    while (item) {
        bool any_checked = false;
        bool any_unchecked = false;
        for (int i = 0; i < item->childCount(); ++i) {
            const QTreeWidgetItem* child = item->child(i);
            if (child->data(0, IsDummyRole).toBool()) {
                continue;
            }
            switch (child->checkState(0)) {
            case Qt::Checked:
                any_checked = true;
                break;
            case Qt::Unchecked:
                any_unchecked = true;
                break;
            case Qt::PartiallyChecked:
                any_checked = true;
                any_unchecked = true;
                break;
            }
        }

        Qt::CheckState new_state = Qt::Unchecked;
        if (any_checked && !any_unchecked) {
            new_state = Qt::Checked;
        } else if (any_checked && any_unchecked) {
            new_state = Qt::PartiallyChecked;
        }
        item->setCheckState(0, new_state);

        item = item->parent();
    }
}

void ZArchiveViewerDialog::OnItemExpanded(QTreeWidgetItem* item) {
    EnsurePopulated(item);
}

void ZArchiveViewerDialog::OnItemChanged(QTreeWidgetItem* item, int column) {
    if (column != 0 || m_updating_checks || !item) {
        return;
    }

    m_updating_checks = true;

    if (item->data(0, IsDirRole).toBool()) {
        const Qt::CheckState state = item->checkState(0);
        if (state != Qt::PartiallyChecked) {
            if (state == Qt::Checked) {
                EnsurePopulatedRecursive(item);
            }
            SetChildrenCheckState(item, state);
        }
    }

    UpdateAncestorCheckState(item->parent());

    m_updating_checks = false;
    UpdateStatusLabel();
}

void ZArchiveViewerDialog::CollectCheckedFiles(QTreeWidgetItem* item,
                                               std::vector<std::string>& out) const {
    if (item->data(0, IsDummyRole).toBool()) {
        return;
    }
    if (!item->data(0, IsDirRole).toBool()) {
        if (item->checkState(0) == Qt::Checked) {
            out.push_back(item->data(0, RelPathRole).toString().toStdString());
        }
        return;
    }
    for (int i = 0; i < item->childCount(); ++i) {
        CollectCheckedFiles(item->child(i), out);
    }
}

std::vector<std::string> ZArchiveViewerDialog::GetCheckedFiles() const {
    std::vector<std::string> files;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        CollectCheckedFiles(m_tree->topLevelItem(i), files);
    }
    return files;
}

void ZArchiveViewerDialog::UpdateStatusLabel() {
    const auto checked = GetCheckedFiles();
    if (checked.empty()) {
        m_status_label->setText(tr("No files selected."));
    } else {
        m_status_label->setText(tr("%1 file(s) selected.").arg(checked.size()));
    }
}

void ZArchiveViewerDialog::OnSelectAll() {
    m_updating_checks = true;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_tree->topLevelItem(i);
        if (item->data(0, IsDirRole).toBool()) {
            EnsurePopulatedRecursive(item);
            SetChildrenCheckState(item, Qt::Checked);
        }
        item->setCheckState(0, Qt::Checked);
    }
    m_updating_checks = false;
    UpdateStatusLabel();
}

void ZArchiveViewerDialog::OnSelectNone() {
    m_updating_checks = true;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_tree->topLevelItem(i);
        if (item->data(0, IsDirRole).toBool()) {
            SetChildrenCheckState(item, Qt::Unchecked);
        }
        item->setCheckState(0, Qt::Unchecked);
    }
    m_updating_checks = false;
    UpdateStatusLabel();
}

void ZArchiveViewerDialog::OnExtractSelected() {
    const auto files = GetCheckedFiles();
    if (files.empty()) {
        QMessageBox::information(this, tr("Extract Selected"),
                                 tr("Select one or more files to extract first."));
        return;
    }

    const QString output_path_str = QFileDialog::getExistingDirectory(
        this, tr("Extract Selected Files To"), QString(), QFileDialog::ShowDirsOnly);
    if (output_path_str.isEmpty()) {
        return;
    }

    RunExtraction(files, Common::FS::PathFromQString(output_path_str));
}

void ZArchiveViewerDialog::OnExtractAll() {
    const QString output_path_str = QFileDialog::getExistingDirectory(
        this, tr("Extract All Files To"), QString(), QFileDialog::ShowDirsOnly);
    if (output_path_str.isEmpty()) {
        return;
    }

    RunExtraction({}, Common::FS::PathFromQString(output_path_str));
}

void ZArchiveViewerDialog::RunExtraction(const std::vector<std::string>& rel_paths,
                                         const std::filesystem::path& output_dir) {
    const bool extract_all = rel_paths.empty();

    auto* progress = new ProgressDialog(tr("Extract from ZArchive"),
                                        extract_all ? tr("Extracting all files...")
                                                    : tr("Extracting selected files..."),
                                        tr("Cancel"), 0, 1000, /*delete_on_close=*/true, this);
    progress->SetValue(0);
    progress->show();

    auto cancel_flag = std::make_shared<std::atomic<bool>>(false);
    connect(progress, &QProgressDialog::canceled, this, [cancel_flag] { *cancel_flag = true; });

    struct ExtractResult {
        bool success = false;
        std::string error_message;
    };

    QPointer<ProgressDialog> progress_guard(progress);
    auto* watcher = new QFutureWatcher<ExtractResult>(this);

    connect(
        watcher, &QFutureWatcher<ExtractResult>::finished, this, [this, watcher, progress_guard]() {
            const ExtractResult result = watcher->result();
            watcher->deleteLater();

            if (progress_guard) {
                progress_guard->close();
            }

            if (!result.success) {
                if (result.error_message != "Canceled") {
                    QMessageBox::critical(this, tr("Extract from ZArchive"),
                                          tr("Failed to extract files:\n%1")
                                              .arg(QString::fromStdString(result.error_message)));
                }
                return;
            }

            QMessageBox::information(this, tr("Extract from ZArchive"), tr("Extraction finished."));
        });

    auto archive_path = m_archive_path;
    auto future = QtConcurrent::run([archive_path, output_dir, rel_paths, extract_all, cancel_flag,
                                     progress_guard]() -> ExtractResult {
        std::string error_message;
        const auto progress_cb = [cancel_flag,
                                  progress_guard](const Core::FileSys::UnpackProgress& p) {
            if (progress_guard) {
                const int percent =
                    p.files_total > 0 ? static_cast<int>((p.files_done * 1000) / p.files_total) : 0;
                QMetaObject::invokeMethod(
                    progress_guard.data(),
                    [progress_guard, percent, file = p.current_file]() {
                        if (!progress_guard) {
                            return;
                        }
                        progress_guard->SetValue(percent);
                        progress_guard->setLabelText(
                            QObject::tr("Extracting: %1").arg(QString::fromStdString(file)));
                    },
                    Qt::QueuedConnection);
            }
            return !cancel_flag->load();
        };

        const bool ok =
            extract_all ? Core::FileSys::UnpackZArchiveToDirectory(archive_path, output_dir,
                                                                   progress_cb, &error_message)
                        : Core::FileSys::ExtractZArchiveFiles(archive_path, output_dir, rel_paths,
                                                              progress_cb, &error_message);

        return ExtractResult{ok, ok ? std::string() : error_message};
    });

    watcher->setFuture(future);
}
