// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <functional>
#include <QMap>
#include <QString>
#include <QStringList>

enum class SFOValueType { String, Integer, Binary, Unknown };

struct SFOKeyInfo {
    QString displayName;                               // Friendly name for UI
    SFOValueType type;                                 // Type
    std::function<QString(const QString&)> decodeFunc; // Optional decoder
};

inline QString identityDecoder(const QString& v) {
    return v;
}

namespace SFOKeyDecoders {

inline QString typeToString(SFOValueType t) {
    switch (t) {
    case SFOValueType::String:
        return "string";
    case SFOValueType::Integer:
        return "int";
    case SFOValueType::Binary:
        return "binary";
    default:
        return "unknown";
    }
}

inline QString decodeAppType(const QString& v) {
    bool ok;
    int val = v.toInt(&ok);
    if (!ok)
        return v;
    switch (val) {
    case 0:
        return "Not Specified";
    case 1:
        return "Paid Standalone Full App";
    case 2:
        return "Upgradable App";
    case 3:
        return "Demo App";
    case 4:
        return "Freemium App";
    default:
        return QString("Unknown (%1)").arg(val);
    }
}

inline QString decodeCategory(const QString& v) {
    // Trim and convert to lowercase for comparison
    QString category = v.trimmed().toLower();

    // Map category codes to their descriptions
    QMap<QString, QString> categoryMap = {
        {"ac", "Additional Content"},
        {"bd", "Blu-ray Disc?"},
        {"gc", "Game Content(?)"},
        {"gd", "Game Digital Application"},
        {"gda", "System Application"},
        {"gdb", "Unknown"},
        {"gdc", "Non-Game Big Application"},
        {"gdd", "BG Application"},
        {"gde", "Non-Game Mini App / Video Service Native App"},
        {"gdk", "Video Service Web App"},
        {"gdl", "PS Cloud Beta App"},
        {"gdO", "PS2 Classic"},
        {"gp", "Game Application Patch"},
        {"gpc", "Non-Game Big App Patch"},
        {"gpd", "BG Application patch"},
        {"gpe", "Non-Game Mini App Patch / Video Service Native App Patch"},
        {"gpk", "Video Service Web App Patch"},
        {"gpl", "PS Cloud Beta App Patch"},
        {"sd", "Save Data"},
        {"la", "License Area (Vita)?"},
        {"wda", "Unknown"}};

    if (categoryMap.contains(category)) {
        return QString("%1 (%2)").arg(category.toUpper()).arg(categoryMap[category]);
    }
    return v;
}

inline QString decodeAttribute(const QString& v) {
    bool ok;
    uint32_t val = v.toUInt(&ok, 16);
    if (!ok) {
        val = v.toUInt(&ok, 10);
        if (!ok)
            return v;
    }

    QStringList flags;

    if (val & (1 << 0))
        flags << "User Logout Supported";
    if (val & (1 << 1))
        flags << "Enter Button Assignment for the common dialog: Cross button";
    if (val & (1 << 2))
        flags << "Menu for Warning Dialog for PS Move is displayed in the option menu";
    if (val & (1 << 3))
        flags << "Stereoscopic 3D";
    if (val & (1 << 4))
        flags
            << "The application is suspended when PS button is pressed (e.g. Amazon Instant Video)";
    if (val & (1 << 5))
        flags << "Enter Button Assignment for the common dialog: Assigned by the System Software";
    if (val & (1 << 6))
        flags << "The application overwrites the default behavior of the Share Menu";
    if (val & (1 << 7))
        flags << "Auto-scaling(?)";
    if (val & (1 << 8))
        flags << "The application is suspended when the special output resolution is set and PS "
                 "button is pressed";
    if (val & (1 << 9))
        flags << "HDCP";
    if (val & (1 << 10))
        flags << "HDCP is disabled for non games app";
    if (val & (1 << 11))
        flags << "USB dir no limit";
    if (val & (1 << 12))
        flags << "Check sign up";
    if (val & (1 << 13))
        flags << "Over 25GB patch";
    if (val & (1 << 14))
        flags << "Supports PVR";
    if (val & (1 << 15))
        flags << "CPU mode (6 CPU)";
    if (val & (1 << 16))
        flags << "CPU mode (7 CPU)";
    if (val & (1 << 17))
        flags << "Unknown(18)";
    if (val & (1 << 18))
        flags << "Use extra USB audio";
    if (val & (1 << 19))
        flags << "Over 1GB savedata";
    if (val & (1 << 20))
        flags << "Use HEVC decoder";
    if (val & (1 << 21))
        flags << "Disable BGDL best-effort";
    if (val & (1 << 22))
        flags << "Improve NP signaling receive message";
    if (val & (1 << 23))
        flags << "Supports PS4 pro";
    if (val & (1 << 24))
        flags << "Support VR Big app";
    if (val & (1 << 25))
        flags << "Enable TTS";
    if (val & (1 << 26))
        flags << "Requires PVR";
    if (val & (1 << 27))
        flags << "Shrink download data";
    if (val & (1 << 28))
        flags << "Not suspend on HDCP version down";
    if (val & (1 << 29))
        flags << "Supports HDR";
    if (val & (1 << 30))
        flags << "Expect HDCP 2.2 on startup";
    if (val & (1 << 31))
        flags << "Check RIF on disc";

    return flags.isEmpty() ? "None" : flags.join(", ");
}

inline QString decodeAttribute2(const QString& v) {
    bool ok;
    uint32_t val = v.toUInt(&ok, 16);
    if (!ok) {
        val = v.toUInt(&ok, 10);
        if (!ok)
            return v;
    }

    QStringList flags;

    if (val & (1 << 0))
        flags << "Initial payload for disc";
    if (val & (1 << 1))
        flags << "Supports Video Recording Feature";
    if (val & (1 << 2))
        flags << "Supports Content Search Feature";
    if (val & (1 << 3))
        flags << "Content format compatible";
    if (val & (1 << 4))
        flags << "PSVR Personal Eye-to-Eye distance setting disabled";
    if (val & (1 << 5))
        flags << "PSVR Personal Eye-to-Eye distance dynamically changeable";
    if (val & (1 << 6))
        flags << "Use resize download data API";
    if (val & (1 << 7))
        flags << "Exclude TTS bitstream by sys";
    if (val & (1 << 8))
        flags << "Supports broadcast separate mode";
    if (val & (1 << 9))
        flags << "The library does not apply dummy load for tracking Playstation Move to CPU";
    if (val & (1 << 10))
        flags << "Download data version 2";
    if (val & (1 << 11))
        flags << "Supports One on One match event with an old SDK";
    if (val & (1 << 12))
        flags << "Supports Team on team tournament with an old SDK";
    if (val & (1 << 13))
        flags << "Use resize download data 1 API";
    if (val & (1 << 14))
        flags << "Enlarge FMEM 256MB";
    if (val & (1 << 15))
        flags << "SELF 2MiB page mode - unknown(16)";
    if (val & (1 << 16))
        flags << "SELF 2MiB page mode - unknown(17)";
    if (val & (1 << 17))
        flags << "Savedata backup force app IO budget";
    if (val & (1 << 18))
        flags << "Support free-for-all tournament";
    if (val & (1 << 19))
        flags << "Unknown(20)";
    if (val & (1 << 20))
        flags << "Enable 0650 scheduler";
    if (val & (1 << 21))
        flags << "Enable hub app util";
    if (val & (1 << 22))
        flags << "Improve savedata performance";
    if (val & (1 << 23))
        flags << "Unknown(24)";
    if (val & (1 << 24))
        flags << "Unknown(25)";
    if (val & (1 << 25))
        flags << "Unknown(26)";
    if (val & (1 << 26))
        flags << "Unknown(27)";
    if (val & (1 << 27))
        flags << "Unknown(28)";
    if (val & (1 << 28))
        flags << "Unknown(29)";
    if (val & (1 << 29))
        flags << "Unknown(30)";
    if (val & (1 << 30))
        flags << "Unknown(31)";
    if (val & (1 << 31))
        flags << "Unknown(32)";

    return flags.isEmpty() ? "None" : flags.join(", ");
}

inline QString decodeSystemVer(const QString& v) {
    bool ok;
    uint32_t fw_int = v.toUInt(&ok);
    if (!ok)
        return v;

    if (fw_int == 0)
        return "0.00";

    // Extract BCD bytes
    u8 major_bcd = (fw_int >> 24) & 0xFF;
    u8 minor_bcd = (fw_int >> 16) & 0xFF;

    int major = ((major_bcd >> 4) * 10) + (major_bcd & 0xF);
    int minor = ((minor_bcd >> 4) * 10) + (minor_bcd & 0xF);

    return QString("%1.%2").arg(major).arg(minor, 2, 10, QChar('0'));
}

inline QString decodeDiscNumber(const QString& v) {
    bool ok;
    int val = v.toInt(&ok);
    return ok ? QString::number(val) : v;
}

inline QString decodeDiscTotal(const QString& v) {
    bool ok;
    int val = v.toInt(&ok);
    return ok ? QString::number(val) : v;
}

inline QString decodePubtoolInfo(
    const QString& v) { // Not used since it needs multilines and i haven't add them yet
    if (v.trimmed().isEmpty())
        return v;

    QStringList parts = v.split(',', Qt::SkipEmptyParts);
    QStringList output;

    for (const QString& p : parts) {
        auto kv = p.split('=', Qt::KeepEmptyParts);
        if (kv.size() != 2)
            continue;

        QString key = kv[0].trimmed();
        QString value = kv[1].trimmed();

        if (key == "c_date") {
            // YYYYMMDD → YYYY-MM-DD
            if (value.length() == 8) {
                QString formatted = value.mid(0, 4) + "-" + value.mid(4, 2) + "-" + value.mid(6, 2);
                output << QString("Creation Date: %1").arg(formatted);
            } else {
                output << "Creation Date: " + value;
            }
        } else if (key == "sdk_ver") {
            // BCD decode
            QString decoded = decodeSystemVer(QString::number(value.toUInt(nullptr, 16)));
            output << QString("SDK Version: %1 (%2)").arg(decoded, value);
        } else if (key == "st_type") {
            // Example: digital25 -> "Digital (25)"
            if (value.startsWith("digital")) {
                QString num = value.mid(QString("digital").size());
                output << QString("Store Type: Digital (%1)").arg(num);
            } else {
                output << "Store Type: " + value;
            }
        } else if (key.startsWith("img0_")) {
            // Generic image sizes → convert to "xxx KB"
            bool ok;
            int size = value.toInt(&ok);
            if (ok && size > 0)
                output << QString("%1: %2 KB").arg(key, QString::number(size));
            else
                output << QString("%1: %2").arg(key, value);
        } else {
            // Unknown → pass through
            output << QString("%1: %2").arg(key, value);
        }
    }

    return output.join("\n");
}

} // namespace SFOKeyDecoders

inline const QMap<QString, SFOKeyInfo>& sfoKeyMap() {
    static QMap<QString, SFOKeyInfo> map = {
        {"TITLE", {"Title", SFOValueType::String, identityDecoder}},
        {"TITLE_ID", {"Title ID", SFOValueType::String, identityDecoder}},
        {"APP_VER", {"App Version", SFOValueType::String, identityDecoder}},
        {"APP_TYPE", {"App Type", SFOValueType::Integer, SFOKeyDecoders::decodeAppType}},
        {"ATTRIBUTE", {"Attribute", SFOValueType::Integer, SFOKeyDecoders::decodeAttribute}},
        {"ATTRIBUTE2", {"Attribute 2", SFOValueType::Integer, SFOKeyDecoders::decodeAttribute2}},
        {"SYSTEM_VER",
         {"Minimum System Firmware", SFOValueType::Integer, SFOKeyDecoders::decodeSystemVer}},
        {"SYSTEM_ROOT_VER",
         {"System Root Version", SFOValueType::Integer, SFOKeyDecoders::decodeSystemVer}},
        {"CONTENT_ID", {"Content ID", SFOValueType::String, identityDecoder}},
        {"CONTENT_VER", {"Content Version", SFOValueType::String, identityDecoder}},
        {"CATEGORY", {"Category", SFOValueType::String, SFOKeyDecoders::decodeCategory}},
        {"PARENTAL_LEVEL", {"Parental Level", SFOValueType::Integer, identityDecoder}},
        {"FORMAT", {"Format", SFOValueType::String, identityDecoder}},
        {"DEV_FLAG", {"Dev Flag", SFOValueType::Integer, identityDecoder}},
        {"DISC_NUMBER", {"Disc Number", SFOValueType::Integer, SFOKeyDecoders::decodeDiscNumber}},
        {"DISC_TOTAL", {"Total Discs", SFOValueType::Integer, SFOKeyDecoders::decodeDiscTotal}},
        {"DISP_LOCATION_1", {"Display Location 1", SFOValueType::Integer, identityDecoder}},
        {"DISP_LOCATION_2", {"Display Location 2", SFOValueType::Integer, identityDecoder}},
        {"DOWNLOAD_DATA_SIZE",
         {"Download Data Size (bytes)", SFOValueType::Integer, identityDecoder}},
        {"EMU_VERSION", {"Emu Version", SFOValueType::Integer, identityDecoder}},
        {"INSTALL_DIR_SAVEDATA",
         {"Install Directory Save Data", SFOValueType::String, identityDecoder}},
        {"IRO_TAG", {"IRO Tag", SFOValueType::Integer, identityDecoder}},
        {"PARAMS", {"Params (Save Data)", SFOValueType::Binary, identityDecoder}},
        {"PROVIDER", {"Provider", SFOValueType::String, identityDecoder}},
        {"PS3_TITLE_ID_LIST_FOR_BOOT",
         {"PS3 Title IDs For Boot", SFOValueType::String, identityDecoder}},
        {"PUBTOOLINFO", {"Pubtool Info", SFOValueType::String, identityDecoder}},
        {"PUBTOOL_VERSION", {"Pubtool Version", SFOValueType::Integer, identityDecoder}},
        {"REMOTE_PLAY_KEY_ASSIGN",
         {"Remote Play Key Assignment", SFOValueType::Integer, identityDecoder}},
        {"SAVEDATA_BLOCKS", {"Save‑Data Blocks", SFOValueType::Unknown, identityDecoder}},
        {"SAVE_DATA_TRANSFER_TITLE_ID_LIST_1",
         {"Transfer Title ID List 1", SFOValueType::String, identityDecoder}},
        {"USER_DEFINED_PARAM_1", {"User Defined Param 1", SFOValueType::Integer, identityDecoder}},
        {"USER_DEFINED_PARAM_2", {"User Defined Param 2", SFOValueType::Integer, identityDecoder}},
        {"USER_DEFINED_PARAM_3", {"User Defined Param 3", SFOValueType::Integer, identityDecoder}},
        {"USER_DEFINED_PARAM_4", {"User Defined Param 4", SFOValueType::Integer, identityDecoder}}};
    return map;
}
