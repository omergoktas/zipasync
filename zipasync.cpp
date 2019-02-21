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
#include <QDir>
#include <QDeadlineTimer>

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

    //! If inputPath is a file
    if (QFileInfo(inputPath).isFile()) {
        if (nameFilters.contains(QFileInfo(inputPath).fileName(), Qt::CaseInsensitive)) {
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
        if (archivePath.size() > 0 && archivePath[0] == '/')
            archivePath.remove(0, 1);
        if (archivePath.size() > 0 && archivePath[0] == '/')
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
        future->reportResult(1); // Only a file compressed
        return 1;                // Only a file compressed
    }

    //! If inputPath is a directory
    QVector<QString> vector({""});
    QDeadlineTimer deadline(200);
    for (int i = 0; i < vector.size(); ++i) {
        const QString basePath = vector[i];
        const QString& fullPath = inputPath + '/' + basePath;
        if (QFileInfo(fullPath).isDir()) {
            for (const QString& entryName : QDir(fullPath).entryList(
                     QDir::AllEntries | QDir::System |
                     QDir::Hidden | QDir::NoDotAndDotDot)) {
                if (!nameFilters.contains(entryName, Qt::CaseInsensitive))
                    vector.append(basePath + '/' + entryName);
            }
        }
        if (deadline.hasExpired()) {
            if (future->isPaused())
                future->waitForResume();
            if (future->isCanceled())
                return -1;
            future->reportResult(vector.size());
            deadline = QDeadlineTimer(200);
        }
    }

    int progress = 0;
    int step = vector.size() / 100;
    int size;
    char* data;
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
        qWarning("ERROR 1: Something went wrong");
        return -1;
    }
    for (int i = 1; i < vector.size(); ++i) {
        QString relativePath = vector[i];
        const QString& fullPath = inputPath + relativePath;
        relativePath.remove(0, 1);
        if (QFileInfo(fullPath).isDir()) {
            relativePath += '/';
            mz_zip_writer_add_mem(&zip, relativePath.toUtf8().constData(), nullptr, 0, 0);
        } else {
            mz_zip_writer_add_file(&zip, relativePath.toUtf8().constData(),
                                   fullPath.toUtf8().constData(),
                                   nullptr, 0, compressionLevel);
        }
        if (i % step == 0) {
            if (future->isPaused())
                future->waitForResume();
            if (future->isCanceled())
                return -1;
            future->setProgressValue(++progress);
        }
    }
    mz_zip_writer_finalize_heap_archive(&zip, (void**)&data, (size_t*)&size);
    mz_zip_writer_end(&zip);

    QFile file(outputFilePath);
    if (!file.open(QIODevice::WriteOnly))
        return -1;
    file.write(data, size);
    file.close();

    future->setProgressValue(100);
    future->reportResult(vector.size());
    return vector.size();
}
} // Internal

/*!
    inputPath:
        This could be either a file or directory, but it must be exists and readable in any case. If
        it is a directory, then all the files and folders in it will be compressed into the output zip
        file. If you want all the files and folders within the directory to be placed under a root
        directory with the same name of the input directory you can use rootDirectory parameter. You
        can also use rootDirectory parameter to specify another root directory name (base path or
        however you name it) other than the input directory name.

    outputFilePath:
        This points out to a zip file path. If the zip file is already exists, regardless of whether
        it is a valid or invalid zip file, if append is not enabled, it will be cleansed and a valid
        zip file will be created on the disk from scratch. On the other hand if append is enabled,
        and the zip file is valid, the files and folders the input path points out will be appended
        into that zip file. If either the zip file is invalid or operation fails for some reason,
        the existing zip file may also be corrupted. If the file outputFilePath parameter points out
        doesn't exist, then, regardless of the state of the append parameter, a new valid zip file
        will be created on the disk from scratch and files and folders will be compressed into it.

    rootDirectory:
        It is used to place files and folders under a root directory, relative to central directory
        of a zip archive. If it is empty, then central directory is chosen. You could replace
        existing files in a zip file with using rootDirectory and append parameters together.

    nameFilters:
        This could be used to filter out some files based on their full file name (filename.ext)
        hence those files won't be included into the output zip file. If it is empty, then there will
        no such filtering occur on the zip file. Beware, this parameter is case-insensitive.
        Directory names could also be filtered in addition to file names.

    compressionLevel:
        This parameter is used to specify compression hardness for the zip archive file. How hard
        the level you choose, the compression will take longer for to finish, but final zip file
        will be less in size.

    append:
        This parameter is used to specify if file and folders are going to appended into the zip file
        that is already exists on the disk pointed out by the outputFilePath parameter. This option
        is similar to the affect of QIODevice::Append on the QFile::open function. If outputFilePath
        parameter points out to a nonexistent file, then append option does not have any effect.
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
