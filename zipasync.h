/****************************************************************************
**
** Copyright (C) 2019 Ömer Göktaş
** Contact: omergoktas.com
**
** This file is part of the ZipAsync library.
**
** The ZipAsync is free software: you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public License
** version 3 as published by the Free Software Foundation.
**
** The ZipAsync is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU Lesser General Public License for more details.
**
** You should have received a copy of the GNU Lesser General Public
** License along with the ZipAsync. If not, see
** <https://www.gnu.org/licenses/>.
**
****************************************************************************/

#ifndef ZIPASYNC_H
#define ZIPASYNC_H

#include "zipasync_global.h"
#include <QFuture>
#include <QDir>

namespace ZipAsync {

enum CompressionLevel {
    NoCompression = 0,
    VeryLow       = 1,
    Low           = 3,
    Medium        = 5,
    High          = 7,
    VeryHigh      = 9,
    Ultra         = 10
};

size_t ZIPASYNC_EXPORT zipSync(const QString& sourcePath, const QString& destinationZipPath,
                               const QString& rootDirectory = QString(), CompressionLevel compressionLevel = Medium,
                               QDir::Filters filters = QDir::NoFilter, const QStringList& nameFilters = {},
                               bool append = true);

size_t ZIPASYNC_EXPORT unzipSync(const QString& sourceZipPath, const QString& destinationPath, bool overwrite = false);

QFuture<size_t> ZIPASYNC_EXPORT zip(const QString& sourcePath, const QString& destinationZipPath,
                                    const QString& rootDirectory = QString(), CompressionLevel compressionLevel = Medium,
                                    QDir::Filters filters = QDir::NoFilter, const QStringList& nameFilters = {},
                                    bool append = true);

QFuture<size_t> ZIPASYNC_EXPORT unzip(const QString& sourceZipPath, const QString& destinationPath, bool overwrite = false);

} // ZipAsync

#endif // ZIPASYNC_H
