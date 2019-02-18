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

#define MINIZ_NO_ZLIB_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES

#include <miniz.h>
#include <QFileInfo>

/* Define MINIZ_NO_STDIO to disable all usage and any functions which rely on stdio for file I/O. */
/*#define MINIZ_NO_STDIO */

/* If MINIZ_NO_TIME is specified then the ZIP archive functions will not be able to get the current time, or */
/* get/set file times, and the C run-time funcs that get/set times won't be called. */
/* The current downside is the times written to your archives will be from 1979. */
/*#define MINIZ_NO_TIME */

/* Define MINIZ_NO_ARCHIVE_APIS to disable all ZIP archive API's. */
/*#define MINIZ_NO_ARCHIVE_APIS */

/* Define MINIZ_NO_ARCHIVE_WRITING_APIS to disable all writing related ZIP archive API's. */
/*#define MINIZ_NO_ARCHIVE_WRITING_APIS */

/* Define MINIZ_NO_MALLOC to disable all calls to malloc, free, and realloc.
   Note if MINIZ_NO_MALLOC is defined then the user must always provide custom user alloc/free/realloc
   callbacks to the zlib and archive API's, and a few stand-alone helper API's which don't provide custom user
   functions (such as tdefl_compress_mem_to_heap() and tinfl_decompress_mem_to_heap()) won't work. */
/*#define MINIZ_NO_MALLOC */

namespace ZipAsync {

namespace Internal {

bool touch(const QString& filePath)
{
    QFile file(filePath);
    return file.open(QIODevice::WriteOnly | QIODevice::Append);
}

QFuture<int> invalidFuture()
{
    static const int invalidResult = -1;
    static QFutureInterface<int> future;
    future.reportFinished(&invalidResult);
    return future.future();
}

int zip(QFutureInterfaceBase* futureInterface, const QString& inputPath,
        const QString& outputFilePath, const QString& rootDirectory,
        const QStringList& nameFilters, CompressionLevel compressionLevel, bool append)
{
    auto future = static_cast<QFutureInterface<int>*>(futureInterface);
    future->setProgressRange(0, 100);
    future->setProgressValue(0);

    if (QFileInfo(inputPath).isFile()) {
        if (nameFilters.contains(QFileInfo(inputPath).fileName())) {
            future->setProgressValue(100);
            future->reportResult(0);
            return 0;
        }

        QFile file(inputPath);
        if (!file.open(QIODevice::ReadOnly))
            return -1;
        const QByteArray& data = file.readAll();
        file.close();

        if (future->isPaused())
            future->waitForResume();
        if (future->isCanceled())
            return -1;
        future->setProgressValue(50);

        QString archivePath = QFileInfo(inputPath).fileName();
        if (!rootDirectory.isEmpty())
            archivePath.prepend(rootDirectory + '/');
        if (archivePath.size() > 0 && (archivePath[0] == '/' || archivePath[0] == '\\'))
            archivePath.remove(0, 1);
        if (archivePath.size() > 0 && (archivePath[0] == '/' || archivePath[0] == '\\'))
            archivePath.remove(0, 1);
        Q_ASSERT(!archivePath.isEmpty());

        if (QFileInfo::exists(outputFilePath) && !append)
            QFile::remove(outputFilePath);

        if (!mz_zip_add_mem_to_archive_file_in_place_v2(
                    outputFilePath.toUtf8().constData(),
                    archivePath.toUtf8().constData(),
                    data.constData(), data.size(), nullptr,
                    0, compressionLevel, nullptr)) {
            qWarning("ERROR 0: Something went wrong");
            return -1;
        }

        future->setProgressValue(100);
        future->reportResult(data.size());
        return data.size();
    }

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
} // Internal

/*!
    inputPath:
        This could be either a file or directory, but it must be exists and readable in any case. If
        it is a directory, then all the files and folders in it will be copied into the output zip
        file. If you want all the files and folders within the directory to be placed under a root
        directory with the same name of the input directory you can use rootDirectory parameter. You
        can also use rootDirectory parameter to specify another root directory (base path, however
        you call) name other than the input directory name.

    outputFilePath:
        This points out to a zip file path. If the zip file is already exists, regardless of whether
        it is a valid or invalid zip file, if append is not enabled, it will be cleansed and a valid
        zip file will be created on the disk from scratch. On the other hand if append is enabled,
        and the zip file is valid, the files and folders the input path points out will be appended
        into that zip file. If either the zip file is invalid or operation fails for some reason,
        the existing zip file may also be corrupted. If the file outputFilePath parameter points out
        doesn't exist, then, regardless of the state of the append parameter, a new valid zip file
        will be created on the disk from scratch.

    rootDirectory:
        It is used to place files and folders under a root directory, relative to central directory
        of a zip archive.

    nameFilters:
        This could be used to filter files based on their full file name, hence those files won't be
        included into the output zip file. If it is empty, then there is no filtering occurs on zip.

    compressionLevel:
        This parameter is used to specify compression hardness for the zip archive file. How hard
        the level you choose, the compression will take longer for to finish.

    append:
        This parameter is used to specify if file and folders are going to appended into the zip file
        that is already exists on the disk pointed out by the outputFilePath parameter. This option
        is similar to the affect of QIODevice::Append on the QFile::open function. If outputFilePath
        parameter points out to an unexistent file, then appen option does not have any effect.
*/

QFuture<int> zip(const QString& inputPath, const QString& outputFilePath,
                    const QString& rootDirectory, const QStringList& nameFilters,
                    CompressionLevel compressionLevel, bool append)
{
    if (!QFileInfo::exists(inputPath)) {
        qWarning("WARNING: The input path doesn not exist");
        return Internal::invalidFuture();
    }

    if (!QFileInfo(inputPath).isReadable()) {
        qWarning("WARNING: The input path is not readable");
        return Internal::invalidFuture();
    }

    if (QFileInfo::exists(outputFilePath) && QFileInfo(outputFilePath).isDir()) {
        qWarning("WARNING: The output file path cannot be a directory");
        return Internal::invalidFuture();
    }

    const bool outputFilePathExists = QFileInfo::exists(outputFilePath);

    if (!Internal::touch(outputFilePath)) {
        qWarning("WARNING: The output file path is not writable");
        return Internal::invalidFuture();
    }

    if (!outputFilePathExists)
        QFile::remove(outputFilePath);

    return Async::run(Internal::zip, inputPath, outputFilePath,
                      rootDirectory, nameFilters, compressionLevel, append);
}

QFuture<int> unzip(const QString& inputPath, const QString& outputPath)
{

}
} // ZipAsync
