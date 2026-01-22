// SPDX-FileCopyrightText: Copyright 2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdexcept>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QString>
#include <common/zip_util.h>
#include <libdeflate.h>

namespace Zip {
void Extract(const QString& zipPath, const QString& outDir) {
    QFile file(zipPath);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Cannot open ZIP file");

    QDir().mkpath(outDir);

    libdeflate_decompressor* d = libdeflate_alloc_decompressor();
    if (!d)
        throw std::runtime_error("libdeflate alloc failed");

    try {
        while (!file.atEnd()) {
            char sig[4];
            if (file.read(sig, 4) != 4)
                break;

            if (!(sig[0] == 0x50 && sig[1] == 0x4B && sig[2] == 0x03 && sig[3] == 0x04))
                break;

            quint16 version, flags, method, modTime, modDate;
            quint32 crc32Header, compSizeHeader, uncompSizeHeader;
            quint16 nameLen, extraLen;

            file.read(reinterpret_cast<char*>(&version), 2);
            file.read(reinterpret_cast<char*>(&flags), 2);
            file.read(reinterpret_cast<char*>(&method), 2);
            file.read(reinterpret_cast<char*>(&modTime), 2);
            file.read(reinterpret_cast<char*>(&modDate), 2);
            file.read(reinterpret_cast<char*>(&crc32Header), 4);
            file.read(reinterpret_cast<char*>(&compSizeHeader), 4);
            file.read(reinterpret_cast<char*>(&uncompSizeHeader), 4);
            file.read(reinterpret_cast<char*>(&nameLen), 2);
            file.read(reinterpret_cast<char*>(&extraLen), 2);

            if (method != 8)
                throw std::runtime_error("Only DEFLATE supported");

            QByteArray filename = file.read(nameLen);
            file.skip(extraLen);

            QByteArray compressed;

            if (flags & 0x08) {
                // Data Descriptor
                constexpr int BUF = 256 * 1024;
                char buffer[BUF];

                qint64 dataStart = file.pos();

                while (!file.atEnd()) {
                    qint64 r = file.read(buffer, sizeof(buffer));
                    if (r <= 0)
                        break;

                    compressed.append(buffer, r);

                    int idx = compressed.indexOf(QByteArray("\x50\x4B\x07\x08", 4));

                    if (idx != -1) {
                        file.seek(dataStart + idx + 4);
                        compressed.truncate(idx);
                        break;
                    }
                }

                quint32 crc32, compSize, uncompSize;
                file.read(reinterpret_cast<char*>(&crc32), 4);
                file.read(reinterpret_cast<char*>(&compSize), 4);
                file.read(reinterpret_cast<char*>(&uncompSize), 4);

                QByteArray decompressed;
                decompressed.resize(uncompSize);

                size_t actualOut = 0;
                libdeflate_result res = libdeflate_deflate_decompress(
                    d, compressed.constData(), compressed.size(), decompressed.data(),
                    decompressed.size(), &actualOut);

                if (res != LIBDEFLATE_SUCCESS)
                    throw std::runtime_error("Decompression failed");

                QString outFilePath = QDir(outDir).filePath(QString::fromUtf8(filename));

                QDir().mkpath(QFileInfo(outFilePath).path());

                QFile outFile(outFilePath);
                outFile.open(QIODevice::WriteOnly);
                outFile.write(decompressed);
            } else {
                // No Data Descriptor
                QByteArray compressed = file.read(compSizeHeader);

                QByteArray decompressed;
                decompressed.resize(uncompSizeHeader);

                size_t actualOut = 0;
                libdeflate_result res = libdeflate_deflate_decompress(
                    d, compressed.constData(), compressed.size(), decompressed.data(),
                    decompressed.size(), &actualOut);

                if (res != LIBDEFLATE_SUCCESS)
                    throw std::runtime_error("Decompression failed");

                QString outFilePath = QDir(outDir).filePath(QString::fromUtf8(filename));

                QDir().mkpath(QFileInfo(outFilePath).path());

                QFile outFile(outFilePath);
                outFile.open(QIODevice::WriteOnly);
                outFile.write(decompressed);
            }
        }
    } catch (...) {
        libdeflate_free_decompressor(d);
        throw;
    }

    libdeflate_free_decompressor(d);
}
} // namespace Zip