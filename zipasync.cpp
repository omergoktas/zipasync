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

#include "zipasync.h"
#include "miniz.h"
#include "report.h"
#include <async.h>
#include <vector>

#include <QFileInfo>
#include <QTemporaryFile>

namespace ZipAsync {

namespace Internal {

enum { INITIAL_NUMBER_OF_ENTRIES = 40960 };

bool touch(const QString& filePath)
{
    QFile file(filePath);
    return file.open(QIODevice::WriteOnly | QIODevice::Append);
}

QFuture<size_t> invalidFuture()
{
    static QFutureInterface<size_t> future(QFutureInterfaceBase::Canceled);
    return future.future();
}

void copyResourceFile(QString& resourcePath, QTemporaryFile& tmpFile)
{
    if (resourcePath.isEmpty() || resourcePath[0] != QLatin1Char(':'))
        return;

    QFile resourceFile(resourcePath);
    if (!resourceFile.open(QFile::ReadOnly)) {
        qWarning("WARNING: Cannot open file %s", resourcePath.toUtf8().constData());
        return;
    }
    if (!tmpFile.open()) {
        qWarning("WARNING: Cannot open a temporary file");
        return;
    }
    tmpFile.write(resourceFile.readAll());
    tmpFile.close();
    resourcePath = tmpFile.fileName();
}

QByteArray cleanArchivePath(const QString& rootDirectory, const QString& relativePath)
{
    Q_ASSERT(!relativePath.isEmpty());
    QString archivePath(relativePath);
    if (!rootDirectory.isEmpty()) {
        QString root(rootDirectory);
        if (root[0] == '/')
            root.remove(0, 1);
        if (!root.isEmpty())
            archivePath.prepend(root + '/');
    }
    return archivePath.toUtf8();
}

QByteArray cleanArchivePath(const QString& rootDirectory, const QString& relativePath, bool isDir)
{
    Q_ASSERT(!relativePath.isEmpty());
    QString archivePath(relativePath);
    if (rootDirectory.isEmpty()) {
        if (archivePath[0] == '/')
            archivePath.remove(0, 1);
    } else {
        QString root(rootDirectory);
        if (root[0] == '/')
            root.remove(0, 1);
        archivePath.prepend(root);
        if (root.isEmpty()) {
            if (archivePath[0] == '/')
                archivePath.remove(0, 1);
        }
    }
    if (isDir)
        archivePath += '/';
    return archivePath.toUtf8();
}

size_t zipSync(const QString& sourcePath, const QString& destinationZipPath,
               const QString& rootDirectory, const QStringList& nameFilters,
               QDir::Filters filters, CompressionLevel compressionLevel, bool append)
{
    const bool sourceIsAFile = QFileInfo(sourcePath).isFile();
    QScopedPointer<std::vector<QString>> vector(new std::vector<QString>({""}));
    vector->reserve(INITIAL_NUMBER_OF_ENTRIES);

    // Recursive entry resolution
    if (sourceIsAFile) {
        vector->push_back(QString());
    } else {
        for (size_t i = 0; i < vector->size(); ++i) {
            const QString& path = sourcePath + vector->at(i);
            if (QFileInfo(path).isDir()) {
                for (const QString& entryName : QDir(path).entryList({}, filters)) {
                    if (!QDir::match(nameFilters, entryName) || !QFileInfo(path + '/' + entryName).isFile())
                        vector->push_back(vector->at(i) + '/' + entryName);
                }
            }
        }
    }
    vector->shrink_to_fit();

    if (vector->size() <= 1)
        WARNING("Nothing to compress, the source directory is empty.")

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    // Archive initialization
    if (append && QFileInfo::exists(destinationZipPath)) {
        if (!mz_zip_reader_init_file_v2(
                    &zip,
                    destinationZipPath.toUtf8().constData(),
                    MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY, 0, 0)) {
            WARNING("Couldn't initialize a zip reader.")
        }
        if (!mz_zip_writer_init_from_reader_v2(&zip, destinationZipPath.toUtf8().constData(), 0)) {
            mz_zip_reader_end(&zip);
            WARNING("Couldn't initialize a zip writer.")
        }
    } else {
        if (!mz_zip_writer_init_file_v2(&zip, destinationZipPath.toUtf8().constData(), 0, 0))
            WARNING("Couldn't initialize a zip writer.")
    }

    // Compressing and adding entries
    for (size_t i = 1; i < vector->size(); ++i) {
        const QString& path = sourceIsAFile ? sourcePath : (sourcePath + vector->at(i));
        const bool isDir = QFileInfo(path).isDir();
        const QByteArray& archivePath = sourceIsAFile
                ? cleanArchivePath(rootDirectory, QFileInfo(sourcePath).fileName())
                : cleanArchivePath(rootDirectory, vector->at(i), isDir);

        if (isDir) {
            if (!mz_zip_writer_add_mem(&zip, archivePath.constData(), nullptr, 0, 0)) {
                mz_zip_writer_finalize_archive(&zip);
                mz_zip_writer_end(&zip);
                WARNING("Couldn't add a directory entry for: %s.", path.toUtf8().constData())
            }
        } else {
            if (!mz_zip_writer_add_file(&zip, archivePath, path.toUtf8().constData(),
                                        nullptr, 0, compressionLevel)) {
                mz_zip_writer_finalize_archive(&zip);
                mz_zip_writer_end(&zip);
                WARNING("Couldn't compress the file: %s.", path.toUtf8().constData())
            }
        }
    }

    // Archive finalization
    if (!mz_zip_writer_finalize_archive(&zip)) {
        mz_zip_writer_end(&zip);
        WARNING("Couldn't finalize the zip writer.")
    }
    if (!mz_zip_writer_end(&zip))
        WARNING("Couldn't clean the zip writer cache.")

    return vector->size() - 1;
}

size_t unzipSync(const QString& sourceZipPath, const QString& destinationPath, bool overwrite)
{
    QTemporaryFile tempFile;
    QString sourceZipFinalPath(sourceZipPath);
    copyResourceFile(sourceZipFinalPath, tempFile);

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file_v2(&zip, sourceZipFinalPath.toUtf8().constData(), 0, 0, 0))
        WARNING("Couldn't initialize a zip reader.")

    size_t numberOfEntries = mz_zip_reader_get_num_files(&zip);

    if (numberOfEntries == 0) {
        mz_zip_reader_end(&zip);
        WARNING("The archive is either invalid or empty.")
    }

    // Iterate for dirs
    for (size_t i = 0; i < numberOfEntries; ++i) {
        mz_zip_archive_file_stat fileStat;
        if (!mz_zip_reader_file_stat(&zip, i, &fileStat)) {
            mz_zip_reader_end(&zip);
            WARNING("Archive is broken.")
        }
        if (!fileStat.m_is_supported) {
            mz_zip_reader_end(&zip);
            WARNING("Archive isn't supported.")
        }
        if (fileStat.m_is_directory) {
            if (!overwrite) {
                const bool isBase = QString(fileStat.m_filename).count('/') <= 1;
                if (isBase && QFileInfo::exists(destinationPath + '/' + fileStat.m_filename)) {
                    mz_zip_reader_end(&zip);
                    WARNING("Extraction canceled, dir already exists: %s.",
                            (destinationPath + '/' + fileStat.m_filename).toUtf8().constData())
                }
            }
            if (!QDir(destinationPath).mkpath(fileStat.m_filename)) {
                mz_zip_reader_end(&zip);
                WARNING("Directory creation on disk is failed for: %s.",
                        (destinationPath + '/' + fileStat.m_filename).toUtf8().constData())
            }
        }
    }

    // Iterate for files
    for (size_t i = 0; i < numberOfEntries; ++i) {
        mz_zip_archive_file_stat fileStat;
        if (!mz_zip_reader_file_stat(&zip, i, &fileStat)) {
            mz_zip_reader_end(&zip);
            WARNING("Archive is broken.")
        }
        if (!fileStat.m_is_supported) {
            mz_zip_reader_end(&zip);
            WARNING("Archive isn't supported.")
        }
        if (!fileStat.m_is_directory) {
            if (!overwrite) {
                const bool isBase = QString(fileStat.m_filename).count('/') < 1;
                if (isBase && QFileInfo::exists(destinationPath + '/' + fileStat.m_filename)) {
                    mz_zip_reader_end(&zip);
                    WARNING("Extraction canceled, file already exists: %s.",
                            (destinationPath + '/' + fileStat.m_filename).toUtf8().constData())
                }
            }
            if (!mz_zip_reader_extract_to_file(
                        &zip, i, (destinationPath + '/' + fileStat.m_filename).toUtf8().constData(),
                        0)) {
                mz_zip_reader_end(&zip);
                WARNING("Extraction failed, file: %s.",
                        (destinationPath + '/' + fileStat.m_filename).toUtf8().constData())
            }
        }
    }

    if (!mz_zip_reader_end(&zip))
        WARNING("Couldn't clean the zip reader cache.")

    return numberOfEntries;
}

size_t zip(QFutureInterfaceBase* futureInterface, const QString& sourcePath,
           const QString& destinationZipPath, const QString& rootDirectory, const QStringList& nameFilters,
           QDir::Filters filters, CompressionLevel compressionLevel, bool append)
{
    INITIALIZE(size_t, futureInterface)

    const bool sourceIsAFile = QFileInfo(sourcePath).isFile();
    QScopedPointer<std::vector<QString>> vector(new std::vector<QString>({""}));
    vector->reserve(INITIAL_NUMBER_OF_ENTRIES);

    // Recursive entry resolution
    if (sourceIsAFile) {
        vector->push_back(QString());
    } else {
        for (size_t i = 0; i < vector->size(); ++i) {
            const QString& path = sourcePath + vector->at(i);
            if (QFileInfo(path).isDir()) {
                for (const QString& entryName : QDir(path).entryList({}, filters)) {
                    if (!QDir::match(nameFilters, entryName) || !QFileInfo(path + '/' + entryName).isFile())
                        vector->push_back(vector->at(i) + '/' + entryName);
                }
            }
            if (vector->size() > 1)
                REPORT_RESULT_UNSAFE(vector->size() - 1)
        }
    }
    vector->shrink_to_fit();

    if (vector->size() <= 1)
        CRASH("ZipAsync", "Nothing to compress, the source directory is empty.")

    REPORT(1, vector->size() - 1)

    mz_zip_archive zip;
    qreal progress = 1;
    qreal step = (99. - progress) / (vector->size() - 1);
    memset(&zip, 0, sizeof(zip));

    // Archive initialization
    if (append && QFileInfo::exists(destinationZipPath)) {
        if (!mz_zip_reader_init_file_v2(
                    &zip,
                    destinationZipPath.toUtf8().constData(),
                    MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY, 0, 0)) {
            CRASH("ZipAsync", "Couldn't initialize a zip reader.")
        }
        if (!mz_zip_writer_init_from_reader_v2(&zip, destinationZipPath.toUtf8().constData(), 0)) {
            mz_zip_reader_end(&zip);
            CRASH("ZipAsync", "Couldn't initialize a zip writer.")
        }
    } else {
        if (!mz_zip_writer_init_file_v2(&zip, destinationZipPath.toUtf8().constData(), 0, 0))
            CRASH("ZipAsync", "Couldn't initialize a zip writer.")
    }

    // Compressing and adding entries
    for (size_t i = 1; i < vector->size(); ++i) {
        const QString& path = sourceIsAFile ? sourcePath : (sourcePath + vector->at(i));
        const bool isDir = QFileInfo(path).isDir();
        const QByteArray& archivePath = sourceIsAFile
                ? cleanArchivePath(rootDirectory, QFileInfo(sourcePath).fileName())
                : cleanArchivePath(rootDirectory, vector->at(i), isDir);

        if (isDir) {
            if (!mz_zip_writer_add_mem(&zip, archivePath.constData(), nullptr, 0, 0)) {
                mz_zip_writer_finalize_archive(&zip);
                mz_zip_writer_end(&zip);
                CRASH("ZipAsync", "Couldn't add a directory entry for: %1.", path)
            }
        } else {
            if (!mz_zip_writer_add_file(&zip, archivePath, path.toUtf8().constData(),
                                        nullptr, 0, compressionLevel)) {
                mz_zip_writer_finalize_archive(&zip);
                mz_zip_writer_end(&zip);
                CRASH("ZipAsync", "Couldn't compress the file: %1.", path)
            }
        }

        progress += step;
        REPORT_PROGRESS_SAFE(progress, zip)
    }

    // Archive finalization
    if (!mz_zip_writer_finalize_archive(&zip)) {
        mz_zip_writer_end(&zip);
        CRASH("ZipAsync", "Couldn't finalize the zip writer.")
    }
    if (!mz_zip_writer_end(&zip))
        CRASH("ZipAsync", "Couldn't clean the zip writer cache.")

    FINALIZE(vector->size() - 1)
}

size_t unzip(QFutureInterfaceBase* futureInterface, const QString& sourceZipPath,
             const QString& destinationPath, bool overwrite)
{
    INITIALIZE(size_t, futureInterface)

    QTemporaryFile tempFile;
    QString sourceZipFinalPath(sourceZipPath);
    copyResourceFile(sourceZipFinalPath, tempFile);

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file_v2(&zip, sourceZipFinalPath.toUtf8().constData(), 0, 0, 0))
        CRASH("ZipAsync", "Couldn't initialize a zip reader.")

    size_t processedEntryCount = 0;
    size_t numberOfEntries = mz_zip_reader_get_num_files(&zip);

    if (numberOfEntries == 0) {
        mz_zip_reader_end(&zip);
        CRASH("ZipAsync", "The archive is either invalid or empty.")
    }

    // Iterate for dirs
    for (size_t i = 0; i < numberOfEntries; ++i) {
        mz_zip_archive_file_stat fileStat;
        if (!mz_zip_reader_file_stat(&zip, i, &fileStat)) {
            mz_zip_reader_end(&zip);
            CRASH("ZipAsync", "Archive is broken.")
        }
        if (!fileStat.m_is_supported) {
            mz_zip_reader_end(&zip);
            CRASH("ZipAsync", "Archive isn't supported.")
        }
        if (fileStat.m_is_directory) {
            if (!overwrite) {
                const bool isBase = QString(fileStat.m_filename).count('/') <= 1;
                if (isBase && QFileInfo::exists(destinationPath + '/' + fileStat.m_filename)) {
                    mz_zip_reader_end(&zip);
                    CRASH("ZipAsync", "Extraction canceled, dir already exists: %1.",
                                     destinationPath + '/' + fileStat.m_filename)
                }
            }
            if (!QDir(destinationPath).mkpath(fileStat.m_filename)) {
                mz_zip_reader_end(&zip);
                CRASH("ZipAsync", "Directory creation on disk is failed for: %1.",
                                 destinationPath + '/' + fileStat.m_filename)
            }
            processedEntryCount++;
            REPORT_PROGRESS_SAFE(100 * processedEntryCount / numberOfEntries, zip)
        }
    }

    // Iterate for files
    for (size_t i = 0; i < numberOfEntries; ++i) {
        mz_zip_archive_file_stat fileStat;
        if (!mz_zip_reader_file_stat(&zip, i, &fileStat)) {
            mz_zip_reader_end(&zip);
            CRASH("ZipAsync", "Archive is broken.")
        }
        if (!fileStat.m_is_supported) {
            mz_zip_reader_end(&zip);
            CRASH("ZipAsync", "Archive isn't supported.")
        }
        if (!fileStat.m_is_directory) {
            if (!overwrite) {
                const bool isBase = QString(fileStat.m_filename).count('/') < 1;
                if (isBase && QFileInfo::exists(destinationPath + '/' + fileStat.m_filename)) {
                    mz_zip_reader_end(&zip);
                    CRASH("ZipAsync", "Extraction canceled, file already exists: %1.",
                                     destinationPath + '/' + fileStat.m_filename)
                }
            }
            if (!mz_zip_reader_extract_to_file(
                        &zip, i, (destinationPath + '/' + fileStat.m_filename).toUtf8().constData(),
                        0)) {
                mz_zip_reader_end(&zip);
                CRASH("ZipAsync", "Extraction failed, file: %1.",
                      destinationPath + '/' + fileStat.m_filename)
            }
            processedEntryCount++;
            REPORT_PROGRESS_SAFE(100 * processedEntryCount / numberOfEntries, zip)
        }
    }

    if (!mz_zip_reader_end(&zip))
        CRASH("ZipAsync", "Couldn't clean the zip reader cache.")

    FINALIZE(processedEntryCount)
}

} // Internal

size_t zipSync(const QString& sourcePath, const QString& destinationZipPath,
               const QString& rootDirectory, CompressionLevel compressionLevel,
               QDir::Filters filters, const QStringList& nameFilters, bool append)
{
    if (!QFileInfo::exists(sourcePath)) {
        qWarning("WARNING: The source path doesn't exist");
        return 0;
    }

    if (!QFileInfo(sourcePath).isReadable()) {
        qWarning("WARNING: The source path isn't readable");
        return 0;
    }

    if (QFileInfo::exists(destinationZipPath) && QFileInfo(destinationZipPath).isDir()) {
        qWarning("WARNING: The destination zip path cannot be a directory");
        return 0;
    }

    const bool destinationZipPathExists = QFileInfo::exists(destinationZipPath);

    if (!Internal::touch(destinationZipPath)) {
        qWarning("WARNING: The destination zip path isn't writable");
        return 0;
    }

    if (!destinationZipPathExists)
        QFile::remove(destinationZipPath);

    if (filters == QDir::NoFilter)
        filters = QDir::AllEntries | QDir::Hidden;

    filters |= QDir::NoDotAndDotDot;

    return Internal::zipSync(sourcePath, destinationZipPath, rootDirectory,
                             nameFilters, filters, compressionLevel, append);
}

size_t unzipSync(const QString& sourceZipPath, const QString& destinationPath, bool overwrite)
{
    if (!QFileInfo::exists(sourceZipPath)) {
        qWarning("WARNING: The source zip path doesn't exist");
        return 0;
    }

    if (QFileInfo(sourceZipPath).isDir()) {
        qWarning("WARNING: The source zip path cannot be a directory");
        return 0;
    }

    if (!QFileInfo(sourceZipPath).isReadable()) {
        qWarning("WARNING: The source zip path isn't readable");
        return 0;
    }

    if (!QFileInfo::exists(destinationPath)) {
        qWarning("WARNING: The destination path doesn't exist");
        return 0;
    }

    if (!QFileInfo(destinationPath).isDir()) {
        qWarning("WARNING: The destination path cannot be a file");
        return 0;
    }

    if (!QFileInfo(destinationPath).isWritable()) {
        qWarning("WARNING: The destination path isn't writable");
        return 0;
    }

    return Internal::unzipSync(sourceZipPath, destinationPath, overwrite);
}

/*!
    Summary:
        This function compresses, depending on the sourcePath, the file or recursive content of the
        directory given by the sourcePath into the zip archive file given by the destinationZipPath.
        You can use the append parameter if you want, to extend (or overwrite) the content of an
        existing zip file at the destination where the sourcePath points out, or, otherwise, a new
        zip archive file will be created from scratch on the disk where the destinationZipPath
        points out to. You can use the rootDirectory parameter if you want, to put all the source
        resources (files and folders) into a relative root directory under the central directory of
        a zip archive. Also you can use the nameFilters and filters parameters when the sourcePath
        points out to a directory, in order to specify what kind of files and folders recursively
        will be scanned and which one of them will be added into the final zip archive. And finally,
        the compressionLevel parameter can be used to specify compression hardness for the zip archive.

        If the function fails for some reason, before spawning a separate worker thread, then it
        returns an invalid future (in "canceled" state) in the first place. If the worker thread is
        spawned and the zip operation has failed for some reason, then the future returned by this
        function will emit resultReadyAt signal (via QFutureWatcher) with 0 as the result. Also the
        progress value will be set to 100 and the progress text will be set to the appropriate error
        string. So you can catch those error states via QFutureWatcher's progressTextChanged and
        progressValueChanged signals. You can also translate the error string by passing it to a
        QObject::tr() function (which means you can use Qt Linguist Tools (lupdate etc) on this cpp
        file in order to extract out the original English written error strings to translate).

        The zip operation occurs in 2 phases. In the first phase, the files and folders are resolved
        recursively within the sourcePath. And while the first phase is still in progress, the
        resultReadyAt signal is emitted almost 25 times per second with the total number of entries
        resolved so far. After the resolution is done and all the files and folders are resolved,
        the total number of all the resolved files and folders will be reported (resultReadyAt)
        alongside the progress value will be set to %1 (progressValueChanged will be emitted). After
        this point, the second phase starts. In the second phase, resolved files and folders are
        started to be compressed (on the heap if overwrite isn't going to happen, otherwise every
        compression cycle will be saved immediately into the zip archive on the disk that is being
        overwritten). At this point, for each cycle of the compression, the progressValueChanged
        signal is emitted almost 25 times per second with the appropriate progress values of the
        ongoing compression operation and the progress range for the operation is between 0 and 100.
        (Progress reporting may freezes for some time until, for instance, a big chunk of file is
        being completely compressed) When the operation is finished, the finished signal is emitted
        alongside with the progressValueChanged signal that the progress value is set to 100. As we
        mentioned above, if any error occurs at any point in the operation's life time, a
        resultReadyAt signal will be emitted with 0 as the result and the progress value will be
        set to 100 (which means the progressValueChanged signal will also be emitted) and finally
        the operation will be finished.

        Other facilities, like pause/resume and cancel these are provided by the QFuture mechanism
        may also be used at any arbitrary point in the operation's life time in order to pause/resume
        or cancel the operation. Appropriate signals will also be emitted.

        There will be no additional limitations arising from the use of this library on compressed
        or extracted archive files. If there are any limitations that you encounter, this will be
        due to the "miniz" library, which we use as the base implementation. (Limitations such as
        maximum number of files to be compressed or maximum size for an archive file)

    sourcePath:
        This could be either a file or a directory, but it must be exists and readable in any case.
        If it is a directory, then all the files and folders in it will be compressed into the zip
        file. If you want all the files and folders within the directory to be placed under a root
        directory with the same name of the source directory you can use the rootDirectory parameter.
        You can also use the rootDirectory parameter to specify another root directory name (base
        path or however you name it) other than the source directory name. Compression occurs
        recursively. We don't support Qt Resource files and folders yet, on this parameter.

    destinationZipPath:
        This points out to a zip file path. If the zip file is already exists, regardless of whether
        it is a valid or invalid zip file, if append isn't enabled, it will be cleansed and a valid
        zip file will be created on the disk from scratch. On the other hand if append is enabled,
        and the zip file is valid, the files and folders the source path points out will be appended
        into that zip file. If either the zip file is invalid or the operation fails for some reason,
        the existing zip file may also be corrupted. If the file destinationZipPath parameter points
        out doesn't exist, then, regardless of the state of the append parameter, a new valid zip
        file will be created on the disk from scratch and files and folders will be compressed into it.

    rootDirectory:
        It is used to place files and folders under a root directory, relative to the central
        directory of a zip archive. If it is empty, then the central directory of the zip archive is
        chosen. This parameter may be like "/my/relative/path" or "/my/relative/path/" or
        "my/relative/path", or "my/relative/path/". Hence, all the files and folders that sourcePath
        parameter points out will be put under the rootDirectory. The rootDirectory may also points
        out to an existing directory in an existing zip archive when it is used with the append mode
        enabled. Hence, you could replace existing files in a zip file with using rootDirectory and
        append parameters together.

    nameFilters:
        This could be used to filter out some files based on their full file name (filename.ext)
        hence those files won't be included in the zip file. If it is empty, then there will
        no such filtering occur on the zip file. Directory names could not be filtered, only file
        names could be. Each name filter is a wildcard (globbing) filter that understands * and ?
        wildcards. See QRegularExpression Wildcard Matching. For example, the following snippets set
        three name filters on the zip funtion to ensure that files with extensions typically used
        for C++ source files aren't going to be included in the final zip archive: "*.cpp", "*.cxx",
        "*.cc". This parameter is only valid and works when sourcePath points out to a directory.

    filters:
        This flag is used to specify the kind of files that should be zipped. This filter flag only
        valid when sourcePath is a directory. This flag is used to resolve dirs and files recursively
        under the sourcePath (when the sourcePath points out to a directory). With this flag, for
        instance, you can filter out hidden files and don't include them in a zip file. If you pass
        QDir::NoFilter flag, then (QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot) is chosen
        for resolving files and folders withing the sourcePath.

    compressionLevel:
        This parameter is used to specify compression hardness for the zip archive. How hard the
        level you choose, the compression will take longer for it to finish, but final zip file will
        be less in size.

    append:
        This parameter is used to specify if files and folders are going to be appended into the zip
        file that is already exists on the disk pointed out by the destinationZipPath parameter or
        not. This option is similar to the affect of QIODevice::Append on the QFile::open function.
        If destinationZipPath parameter points out to a nonexistent file, then append option doesn't
        have any effect.
*/
QFuture<size_t> zip(const QString& sourcePath, const QString& destinationZipPath,
                    const QString& rootDirectory, CompressionLevel compressionLevel,
                    QDir::Filters filters, const QStringList& nameFilters, bool append)
{
    if (!QFileInfo::exists(sourcePath)) {
        qWarning("WARNING: The source path doesn't exist");
        return Internal::invalidFuture();
    }

    if (!QFileInfo(sourcePath).isReadable()) {
        qWarning("WARNING: The source path isn't readable");
        return Internal::invalidFuture();
    }

    if (QFileInfo::exists(destinationZipPath) && QFileInfo(destinationZipPath).isDir()) {
        qWarning("WARNING: The destination zip path cannot be a directory");
        return Internal::invalidFuture();
    }

    const bool destinationZipPathExists = QFileInfo::exists(destinationZipPath);

    if (!Internal::touch(destinationZipPath)) {
        qWarning("WARNING: The destination zip path isn't writable");
        return Internal::invalidFuture();
    }

    if (!destinationZipPathExists)
        QFile::remove(destinationZipPath);

    if (filters == QDir::NoFilter)
        filters = QDir::AllEntries | QDir::Hidden;

    filters |= QDir::NoDotAndDotDot;

    return Async::run(QThreadPool::globalInstance(), Internal::zip, sourcePath, destinationZipPath,
                      rootDirectory, nameFilters, filters, compressionLevel, append);
}

/*!
    Summary:
        This function extracts the content of the zip archive given by sourceZipPath into the
        directory given by destinationPath. You can use overwrite parameter in order to enable
        overwriting of any existing file or folders at the destination directory (within the
        destinationPath), otherwise (when the overwrite parameter is disabled) the unzip operation
        may fail at any point because if any file or folder in the zip archive is already exists on
        the disk in the destination directory (within the destinationPath).

        If the function fails for some reason, before spawning a separate worker thread, then it
        returns an invalid future (in "canceled" state) in the first place. If the worker thread is
        spawned and the unzip operation has failed for some reason, then the future returned by this
        function will emit resultReadyAt signal (via QFutureWatcher) with 0 as the result. Also the
        progress value will be set to 100 and the progress text will be set to the appropriate error
        string. So you can catch those error states via QFutureWatcher's progressTextChanged and
        progressValueChanged signals. You can also translate the error string by passing it to a
        QObject::tr() function (which means you can use Qt Linguist Tools (lupdate etc) on this cpp
        file in order to extract out the original English written error strings to translate).

        While the operation is still in progress, the progressValueChanged signal is emitted almost
        25 times per second with the appropriate progress values of the ongoing operation and the
        progress range for the operation is between 0 and 100. When the operation is finished, the
        resultReadyAt signal is emitted with a single result that is the total number of entries
        extracted from the zip archive. Overall, this function only returns a single result, either
        it is 0 for errors, or the total number of entries extracted from the zip archive if it is
        successful. Also the finished signal is emitted at the end.

        Other facilities, like pause/resume and cancel these are provided by the QFuture mechanism
        may also be used at any arbitrary point in the operation's life time in order to pause/resume
        or cancel the operation. Appropriate signals will also be emitted.

        There will be no additional limitations arising from the use of this library on compressed
        or extracted archive files. If there are any limitations that you encounter, this will be
        due to the "miniz" library, which we use as the base implementation. (Limitations such as
        maximum number of files to be compressed or maximum size for an archive file)

    sourceZipPath:
        This points out to a zip archive file path where all the content of this zip archive is
        going to be extracted into the destination path. And the zip archive must be exists and
        valid. Otherwise extraction fails. This path could be a Qt Resource path, e.g. ":/file.zip"

    destinationPath:
        This must be a directory. It is the folder where all the content of the root directory of
        the source zip archive is going to be poured into.

    overwrite:
        If this parameter is enabled, then all the files and folders are going to be overwritten
        even if they exists. Otherwise (when it is disabled), the extraction operation is canceled
        at any point if any file in the source zip archive is already exists on the disk at the
        destination folder (within the destinationPath).
*/
QFuture<size_t> unzip(const QString& sourceZipPath, const QString& destinationPath, bool overwrite)
{
    if (!QFileInfo::exists(sourceZipPath)) {
        qWarning("WARNING: The source zip path doesn't exist");
        return Internal::invalidFuture();
    }

    if (QFileInfo(sourceZipPath).isDir()) {
        qWarning("WARNING: The source zip path cannot be a directory");
        return Internal::invalidFuture();
    }

    if (!QFileInfo(sourceZipPath).isReadable()) {
        qWarning("WARNING: The source zip path isn't readable");
        return Internal::invalidFuture();
    }

    if (!QFileInfo::exists(destinationPath)) {
        qWarning("WARNING: The destination path doesn't exist");
        return Internal::invalidFuture();
    }

    if (!QFileInfo(destinationPath).isDir()) {
        qWarning("WARNING: The destination path cannot be a file");
        return Internal::invalidFuture();
    }

    if (!QFileInfo(destinationPath).isWritable()) {
        qWarning("WARNING: The destination path isn't writable");
        return Internal::invalidFuture();
    }

    return Async::run(QThreadPool::globalInstance(), Internal::unzip,
                      sourceZipPath, destinationPath, overwrite);
}

} // ZipAsync
