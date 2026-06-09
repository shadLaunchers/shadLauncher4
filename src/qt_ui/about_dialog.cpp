// SPDX-FileCopyrightText: Copyright 2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSysInfo>
#include <QVBoxLayout>

#include <common/scm_rev.h>

#include "about_dialog.h"
#include "version.h"

QString ShortCommit() {
    const QString rev = QString::fromUtf8(Common::g_scm_rev);
    if (rev.isEmpty()) {
        return QObject::tr("unknown");
    }
    return rev.left(7);
}

QString AboutDialog::RepositoryUrl() {
    const QString remote = QString::fromUtf8(Common::g_scm_remote_url);
    if (remote.startsWith("https://")) {
        QString url = remote;
        if (url.endsWith(".git")) {
            url.chop(4);
        }
        return url;
    }
    if (remote.startsWith("git@")) {
        QString url = remote;
        url.remove(0, 4);
        url.replace(':', '/');
        if (url.endsWith(".git")) {
            url.chop(4);
        }
        return "https://" + url;
    }
    return QStringLiteral("https://github.com/shadLaunchers/shadLauncher4");
}

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("About shadLauncher4"));
    setObjectName("AboutDialog");
    setModal(true);

    // --- Header: logo + name/version --------------------------------------
    auto* logo = new QLabel(this);
    logo->setObjectName("AboutDialog_logo");
    QPixmap pix(":/images/shadLauncher4.png");
    if (pix.isNull()) {
        logo->hide();
    } else {
        logo->setPixmap(pix.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        logo->setFixedSize(96, 96);
        logo->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    }

    auto* name = new QLabel(QStringLiteral("shadLauncher4"), this);
    name->setObjectName("AboutDialog_name");
    {
        QFont f = name->font();
        f.setPointSizeF(f.pointSizeF() + 8.0);
        f.setBold(true);
        name->setFont(f);
    }

    const QString channel = Common::g_is_release ? tr("Release") : tr("Development build");
    auto* version = new QLabel(this);
    version->setObjectName("AboutDialog_version");
    version->setTextFormat(Qt::RichText);
    version->setText(tr("<b>Version %1</b> &nbsp;&middot;&nbsp; %2")
                         .arg(QString::fromUtf8(APP_VERSION), channel));

    auto* build = new QLabel(this);
    build->setObjectName("AboutDialog_build");
    build->setTextFormat(Qt::RichText);
    build->setText(tr("Commit %1 &nbsp;&middot;&nbsp; branch %2<br>Built %3")
                       .arg(ShortCommit(), QString::fromUtf8(Common::g_scm_branch),
                            QString::fromUtf8(Common::g_scm_date)));
    {
        QFont f = build->font();
        f.setPointSizeF(f.pointSizeF() - 1.0);
        build->setFont(f);
    }

    auto* header_text = new QVBoxLayout();
    header_text->setSpacing(2);
    header_text->addWidget(name);
    header_text->addWidget(version);
    header_text->addWidget(build);
    header_text->addStretch();

    auto* header = new QHBoxLayout();
    header->setSpacing(16);
    header->addWidget(logo, 0, Qt::AlignTop);
    header->addLayout(header_text, 1);

    // --- Separator --------------------------------------------------------
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);

    // --- Description ------------------------------------------------------
    auto* desc = new QLabel(this);
    desc->setObjectName("AboutDialog_description");
    desc->setWordWrap(true);
    desc->setTextFormat(Qt::RichText);
    desc->setText(tr("A graphical launcher for the shadPS4 PlayStation 4 emulator. "
                     "Browse, organize, and start your game library, manage emulator "
                     "versions, and configure settings from one place."));

    // --- Links ------------------------------------------------------------
    const QString repo = RepositoryUrl();
    auto* links = new QLabel(this);
    links->setObjectName("AboutDialog_links");
    links->setTextFormat(Qt::RichText);
    links->setOpenExternalLinks(true);
    links->setText(tr("<a href=\"%1\">GitHub repository</a> &nbsp;&middot;&nbsp; "
                      "<a href=\"%1/issues\">Report an issue</a>")
                       .arg(repo));

    auto* credits = new QLabel(this);
    credits->setObjectName("AboutDialog_credits");
    credits->setWordWrap(true);
    credits->setTextFormat(Qt::RichText);
    credits->setOpenExternalLinks(true);
    credits->setText(
        tr("Powered by the <a href=\"https://github.com/shadps4-emu/shadPS4\">shadPS4</a> "
           "emulator.<br>"
           "Licensed under the GNU General Public License v2.0 or later."));
    {
        QFont f = credits->font();
        f.setPointSizeF(f.pointSizeF() - 1.0);
        credits->setFont(f);
    }

    auto* buttons = new QDialogButtonBox(this);
    auto* copy_btn = buttons->addButton(tr("Copy Build Info"), QDialogButtonBox::ActionRole);
    auto* close_btn = buttons->addButton(QDialogButtonBox::Close);
    close_btn->setDefault(true);

    connect(copy_btn, &QPushButton::clicked, this, [this, copy_btn]() {
        if (QClipboard* cb = QGuiApplication::clipboard()) {
            cb->setText(BuildInfoText());
            copy_btn->setText(tr("Copied!"));
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);

    // --- Assemble ---------------------------------------------------------
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 16);
    root->setSpacing(12);
    root->addLayout(header);
    root->addWidget(line);
    root->addWidget(desc);
    root->addWidget(links);
    root->addWidget(credits);
    root->addStretch();
    root->addWidget(buttons);

    setLayout(root);
    setMinimumWidth(440);
}

QString AboutDialog::BuildInfoText() const {
    const QString channel =
        Common::g_is_release ? QStringLiteral("release") : QStringLiteral("development");
    return QStringLiteral("shadLauncher4 %1 (%2)\n"
                          "Commit:    %3\n"
                          "Branch:    %4\n"
                          "Built:     %5\n"
                          "Qt:        %6\n"
                          "OS:        %7\n"
                          "Arch:      %8")
        .arg(QString::fromUtf8(APP_VERSION), channel, QString::fromUtf8(Common::g_scm_rev),
             QString::fromUtf8(Common::g_scm_branch), QString::fromUtf8(Common::g_scm_date),
             QString::fromUtf8(qVersion()), QSysInfo::prettyProductName(),
             QSysInfo::currentCpuArchitecture());
}
