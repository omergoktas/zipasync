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

#include <zipasync.h>
#include <async.h>
#include <miniz.h>
#include <QFileInfo>

namespace ZipAsync {

namespace Internal {

QFuture<qint64> invalidFuture()
{
    static QFutureInterface<qint64> future(QFutureInterfaceBase::Throttled);
    future.reportResult(-1);
    return future.future();
}

bool touch(const QString& filePath)
{
    QFile file(filePath);
    return file.open(QIODevice::WriteOnly);
}

qint64 zip(QFutureInterfaceBase* futureInterface, const QString& inputPath,
           const QString& outputFilePath, const QString& rootDirectory,
           const QStringList& nameFilters, CompressionLevel compressionLevel)
{
    auto future = static_cast<QFutureInterface<int>*>(futureInterface);
    future->setProgressRange(0, 100);
    future->setProgressValue(0);

//    int value = QRandomGenerator::global()->bounded(rangeMin, rangeMax);
//    for (int i = 1; i <= 100; ++i) {
//        if (future->isPaused())
//            future->waitForResume();
//        if (future->isCanceled())
//            return value;
//        value = QRandomGenerator::global()->bounded(rangeMin, rangeMax);
//        future->setProgressValueAndText(i, QString("Random number: %1").arg(value));
//        QThread::msleep(50);
//    }


//    // Spin for data
//    for (auto file : lsfile(dir)) {
//        auto data = rdfile(dir + separator() + file);
//        if (!mz_zip_add_mem_to_archive_file_in_place(outFilename.toStdString().c_str(), base.isEmpty() ?
//                                                         file.toStdString().c_str() : (base + separator() + file).toStdString().c_str(),
//                                                         data.constData(), data.size(), NULL, 0, MZ_BEST_COMPRESSION)) {
//            qWarning("Zipper : Error occurred 0x01");
//            return false;
//        }
//    }

//    // Spin for dirs
//    for (auto dr : lsdir(dir)) {
//        #if defined(Q_OS_DARWIN)
//        if (dr == "__MACOSX")
//            continue;
//        #endif
//        if (!mz_zip_add_mem_to_archive_file_in_place(outFilename.toStdString().c_str(), base.isEmpty() ?
//                                                         (dr + separator()).toStdString().c_str() :
//                                                         (base + separator() + dr + separator()).toStdString().c_str(),
//                                                         NULL, 0, NULL, 0, MZ_BEST_COMPRESSION)) {
//            qWarning("Zipper : Error occurred 0x02");
//            return false;
//        }
//        if (!compressDir(dir + separator() + dr, outFilename, base.isEmpty() ? dr : base + separator() + dr)) {
//            return false;
//        }
//    }
//    return true;


//    future->reportResult(value);
//    return value;
}
}

/*
   inputPath: It could be a file or folder, but it must be exists.
   outputFilePath: It must be a file path containing file name which is going to be
   the zip file output. And it must not be exists.
*/
QFuture<qint64> zip(const QString& inputPath, const QString& outputFilePath,
                    const QString& rootDirectory, const QStringList& nameFilters,
                    CompressionLevel compressionLevel)
{
    if (!QFileInfo::exists(inputPath)) {
        qWarning("WARNING: Input path doesn't exist");
        return Internal::invalidFuture();
    }

    if (QFileInfo::exists(outputFilePath)) {
        qWarning("WARNING: Output file path already exists");
        return Internal::invalidFuture();
    }

    if (!Internal::touch(outputFilePath)) {
        qWarning("WARNING: Cannot access to output file path");
        return Internal::invalidFuture();
    }

    return Async::run(Internal::zip, inputPath, outputFilePath,
                      rootDirectory, nameFilters, compressionLevel);
}

QFuture<qint64> unzip(const QString& inputPath, const QString& outputPath)
{

}
} // ZipAsync
