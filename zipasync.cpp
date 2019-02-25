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
        const QString& outputFilePath, const QString& rootDirectory, const QStringList& nameFilters,
        QDir::Filters filters, CompressionLevel compressionLevel, bool append)
{
    auto future = static_cast<QFutureInterface<int>*>(futureInterface);
    future->setProgressRange(0, 100);
    future->setProgressValue(0);
    QString root(rootDirectory);

    if (filters == QDir::NoFilter)
        filters = QDir::AllEntries | QDir::System | QDir::Hidden | QDir::NoDotAndDotDot;

    if (QFileInfo(inputPath).isFile()) {
        future->setProgressValue(1);

        QFile file(inputPath);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning("ERROR: Could not read the input path");
            return -1;
        }
        const QByteArray& data = file.readAll();
        file.close();

        if (future->isPaused())
            future->waitForResume();
        if (future->isCanceled())
            return -1;
        future->setProgressValue(50);

        QString archivePath = QFileInfo(inputPath).fileName();
        if (!root.isEmpty()) {
            if (root[0] == '/')
                root.remove(0, 1);
            if (!root.isEmpty())
                archivePath.prepend(root + '/');
        }

        if (QFileInfo::exists(outputFilePath) && !append)
            QFile::remove(outputFilePath);

        if (!mz_zip_add_mem_to_archive_file_in_place_v2(
                    outputFilePath.toUtf8().constData(),
                    archivePath.toUtf8().constData(),
                    data.constData(), data.size(), nullptr,
                    0, compressionLevel, nullptr)) {
            qWarning("ERROR: Archive file creation failed");
            return -1;
        }

        future->setProgressValue(100);
        future->reportResult(1); // Only one file processed
    } else {
        QDeadlineTimer deadline; // Resolve files and folders recursively
        QVector<QString> vector({""});
        for (int i = 0; i < vector.size(); ++i) {
            const QString basePath = vector[i];
            const QString& fullPath = inputPath + basePath;
            if (QFileInfo(fullPath).isDir()) {
                for (const QString& entryName : QDir(fullPath).entryList(
                         nameFilters, filters, QDir::DirsLast | QDir::IgnoreCase)) {
                        vector.append(basePath + '/' + entryName);
                }
            }
            if (deadline.hasExpired()) { // Report entry count in every 200ms
                if (future->isPaused())
                    future->waitForResume();
                if (future->isCanceled())
                    return -1;
                future->reportResult(vector.size() - 1);
                deadline = QDeadlineTimer(200);
            }
        }

        future->setProgressValue(1);
        future->reportResult(vector.size() - 1);

        qreal progress = 1;
        qreal step = (99. - progress) / (vector.size() - 1);
        if (append && QFileInfo::exists(outputFilePath)) {
            for (int i = 1; i < vector.size(); ++i) {
                QString archivePath(vector[i]);
                const QString& fullPath = inputPath + archivePath;
                if (!root.isEmpty()) {
                    if (root[0] == '/')
                        root.remove(0, 1);
                    archivePath.prepend(root);
                    if (root.isEmpty()) {
                        if (archivePath[0] == '/')
                            archivePath.remove(0, 1);
                    }
                } else {
                    if (archivePath[0] == '/')
                        archivePath.remove(0, 1);
                }

                if (QFileInfo(fullPath).isDir()) {
                    archivePath += '/';
                    if (!mz_zip_add_mem_to_archive_file_in_place_v2(
                                outputFilePath.toUtf8().constData(),
                                archivePath.toUtf8().constData(),
                                nullptr, 0, nullptr, 0, 0, nullptr)) {
                        qWarning("ERROR: Archive file modification failed, couldn't create a directory");
                        return -1;
                    }
                } else {
                    QFile file(fullPath);
                    if (!file.open(QIODevice::ReadOnly)) {
                        qWarning("ERROR: Archive file modification failed, couldn't read the input path");
                        return -1;
                    }
                    const QByteArray& data = file.readAll();
                    file.close();
                    if (!mz_zip_add_mem_to_archive_file_in_place_v2(
                                outputFilePath.toUtf8().constData(),
                                archivePath.toUtf8().constData(),
                                data.constData(), data.size(), nullptr,
                                0, compressionLevel, nullptr)) {
                        qWarning("ERROR: Archive file modification failed, couldn't add a file");
                        return -1;
                    }
                }

                progress += step;
                if (future->isProgressUpdateNeeded()) {
                    if (future->isPaused())
                        future->waitForResume();
                    if (future->isCanceled())
                        return -1;
                    future->setProgressValue(progress);
                }
            }
        } else {
            mz_zip_archive zip; // Initialize the zip archive on heap
            memset(&zip, 0, sizeof(zip));
            if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
                qWarning("ERROR: Could not initialize memory");
                return -1;
            }

            for (int i = 1; i < vector.size(); ++i) {
                QString archivePath(vector[i]);
                const QString& fullPath = inputPath + archivePath;
                if (!root.isEmpty()) {
                    if (root[0] == '/')
                        root.remove(0, 1);
                    archivePath.prepend(root);
                    if (root.isEmpty()) {
                        if (archivePath[0] == '/')
                            archivePath.remove(0, 1);
                    }
                } else {
                    if (archivePath[0] == '/')
                        archivePath.remove(0, 1);
                }

                if (QFileInfo(fullPath).isDir()) {
                    archivePath += '/';
                    if (!mz_zip_writer_add_mem(&zip, archivePath.toUtf8().constData(), nullptr, 0, 0)) {
                        mz_zip_writer_end(&zip);
                        qWarning("ERROR: Could not create a directory entry for some reason, dir: %s",
                                 fullPath.toUtf8().constData());
                        return -1;
                    }
                } else {
                    if (!mz_zip_writer_add_file(&zip, archivePath.toUtf8().constData(),
                                                fullPath.toUtf8().constData(),
                                                nullptr, 0, compressionLevel)) {
                        mz_zip_writer_end(&zip);
                        qWarning("ERROR: Could not compress a file for some reason, file: %s",
                                 fullPath.toUtf8().constData());
                        return -1;
                    }
                }
                progress += step;
                if (future->isProgressUpdateNeeded()) {
                    if (future->isPaused())
                        future->waitForResume();
                    if (future->isCanceled()) {
                        mz_zip_writer_end(&zip);
                        return -1;
                    }
                    future->setProgressValue(progress);
                }
            }
            future->setProgressValue(99);

            void* data; // Finalize the zip archive on heap
            size_t size;
            mz_zip_writer_finalize_heap_archive(&zip, &data, &size);
            mz_zip_writer_end(&zip);

            if (vector.size() > 1) { // Flush the zip archive from memory to file on disk
                QFile file(outputFilePath);
                if (!file.open(QIODevice::WriteOnly)) {
                    mz_free(data);
                    qWarning("ERROR: Could not write archive file from memory to disk");
                    return -1;
                }
                file.write((char*)data, size);
            }
            mz_free(data);
        }
        future->setProgressValue(100);
    }
    return 0; // Ignored
}
} // Internal

/*!
    inputPath:
        This could be either a file or directory, but it must be exists and readable in any case. If
        it is a directory, then all the files and folders in it will be compressed into the output zip
        file. If you want all the files and folders within the directory to be placed under a root
        directory with the same name of the input directory you can use rootDirectory parameter. You
        can also use rootDirectory parameter to specify another root directory name (base path or
        however you name it) other than the input directory name. Compression occurs recursively.

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
        no such filtering occur on the zip file. Directory names could also be filtered in addition
        to file names. Each name filter is a wildcard (globbing) filter that understands * and ?
        wildcards. See QRegularExpression Wildcard Matching. For example, the following code sets
        three name filters on a QDir to ensure that only files with extensions typically used for
        C++ source files are listed: "*.cpp", "*.cxx", "*.cc". Only works when inputPath is a dir.

    filters:
        The filter is used to specify the kind of files that should be zipped. This filter flags
        only works when inputPath is a dir and it is used to resolve dirs and files recursively under
        a directory (when the inputPath points out a directory). With this flag, for instance, you
        can filter out hidden files and not include them in a zip file.

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
                 QDir::Filters filters, CompressionLevel compressionLevel, bool append)
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
                      rootDirectory, nameFilters, filters, compressionLevel, append);
}

QFuture<int> unzip(const QString& inputPath, const QString& outputPath)
{

}
} // ZipAsync
