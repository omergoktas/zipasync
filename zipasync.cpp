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

#define REPORT_NOW(progress, result) \
{ \
    future->setProgressValue(progress); \
    future->reportResult(result); \
}

#define REPORT_PROGRESS(progress) \
{ \
    if (future->isProgressUpdateNeeded()) { \
        if (future->isPaused()) \
            future->waitForResume(); \
        if (future->isCanceled()) \
            return -1; \
        future->setProgressValue(progress); \
    } \
}

#define REPORT_PROGRESS_CLEAN(progress, reader) \
{ \
    if (future->isProgressUpdateNeeded()) { \
        if (future->isPaused()) \
            future->waitForResume(); \
        if (future->isCanceled()) { \
            if (reader) \
                mz_zip_reader_end(&zip); \
            else \
                mz_zip_writer_end(&zip); \
            return -1; \
        } \
        future->setProgressValue(progress); \
    } \
}

#define REPORT_RESULT(result) \
{ \
    if (future->isProgressUpdateNeeded()) { \
        if (future->isPaused()) \
            future->waitForResume(); \
        if (future->isCanceled()) \
            return -1; \
        future->reportResult(result); \
    } \
}

#define RETURN(result) \
{ \
    future->setProgressValue(100); \
    future->reportResult(result); \
    return result; \
}

#define RETURN_ERROR(msg) \
{ \
    future->setProgressValueAndText(100, QT_TRANSLATE_NOOP("ZipAsync", msg)); \
    future->reportResult(-1); \
    return -1; \
}

#define RETURN_ERROR_ARG(msg, ...) \
{ \
    future->setProgressValueAndText(100, \
        Internal::combineStringArguments(QT_TRANSLATE_NOOP("ZipAsync", msg), __VA_ARGS__)); \
    future->reportResult(-1); \
    return -1; \
}

namespace ZipAsync {

namespace Internal {

bool touch(const QString& filePath)
{
    QFile file(filePath);
    return file.open(QIODevice::WriteOnly | QIODevice::Append);
}

QFuture<int> invalidFuture()
{
    static QFutureInterface<int> future(QFutureInterfaceBase::Canceled);
    return future.future();
}

void combineStringArguments(QString&) {}

template <typename First, typename... Rest>
void combineStringArguments(QString& msg, First&& first, Rest&&... rest)
{
    msg = msg.arg(first);
    combineStringArguments(msg, rest...);
}

template <typename... Args>
QString combineStringArguments(const char* msg, Args&&... args)
{
    QString errorString(msg);
    combineStringArguments(errorString, args...);
    return errorString;
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

int zip(QFutureInterfaceBase* futureInterface, const QString& inputPath,
        const QString& outputFilePath, const QString& rootDirectory, const QStringList& nameFilters,
        QDir::Filters filters, CompressionLevel compressionLevel, bool append)
{
    auto future = static_cast<QFutureInterface<int>*>(futureInterface);
    future->setProgressRange(0, 100);
    future->setProgressValue(0);

    //! inputPath is a file
    if (QFileInfo(inputPath).isFile()) {
        REPORT_NOW(1, 1)

        QFile file(inputPath);
        if (!file.open(QIODevice::ReadOnly))
            RETURN_ERROR("Couldn't read the input path.")
        const QByteArray& data = file.readAll();
        file.close();

        REPORT_PROGRESS(30)

        if (QFileInfo::exists(outputFilePath) && !append)
            QFile::remove(outputFilePath);

        if (!mz_zip_add_mem_to_archive_file_in_place_v2(
                    outputFilePath.toUtf8().constData(),
                    cleanArchivePath(rootDirectory, QFileInfo(inputPath).fileName()).constData(),
                    data.constData(), data.size(), nullptr,
                    0, compressionLevel, nullptr))
            RETURN_ERROR("Archive creation failed.")
        RETURN(1)
    }

    //! inputPath is a directory
    QVector<QString> vector({""}); // Resolve files and folders recursively
    for (int i = 0; i < vector.size(); ++i) {
        const QString basePath = vector[i];
        const QString& fullPath = inputPath + basePath;
        if (QFileInfo(fullPath).isDir()) {
            for (const QString& entryName : QDir(fullPath).entryList(
                     nameFilters, filters, QDir::DirsLast | QDir::IgnoreCase)) {
                vector.append(basePath + '/' + entryName);
            }
        }
        REPORT_RESULT(vector.size() - 1)
    }

    REPORT_NOW(1, vector.size() - 1)

    qreal progress = 1;
    qreal step = (99. - progress) / (vector.size() - 1);
    if (append && QFileInfo::exists(outputFilePath)) { // Modify an existing zip file
        for (int i = 1; i < vector.size(); ++i) {
            const QString& fullPath = inputPath + vector[i];
            const bool isDir = QFileInfo(fullPath).isDir();
            const QByteArray& archivePath = cleanArchivePath(rootDirectory, vector[i], isDir);
            if (isDir) {
                if (!mz_zip_add_mem_to_archive_file_in_place_v2(
                            outputFilePath.toUtf8().constData(),
                            archivePath.constData(),
                            nullptr, 0, nullptr, 0, 0, nullptr))
                    RETURN_ERROR("Archive modification failed, couldn't add a directory.")
            } else {
                QFile file(fullPath);
                if (!file.open(QIODevice::ReadOnly))
                    RETURN_ERROR("Archive modification failed, couldn't read the input path.")
                const QByteArray& data = file.readAll();
                file.close();
                if (!mz_zip_add_mem_to_archive_file_in_place_v2(
                            outputFilePath.toUtf8().constData(),
                            archivePath.constData(),
                            data.constData(), data.size(), nullptr,
                            0, compressionLevel, nullptr))
                    RETURN_ERROR("Archive modification failed, couldn't add a file.")
            }
            progress += step;
            REPORT_PROGRESS(progress)
        }
    } else { // Create a new zip file
        mz_zip_archive zip; // Initialize the zip archive on heap
        memset(&zip, 0, sizeof(zip));
        if (!mz_zip_writer_init_heap(&zip, 0, 0))
            RETURN_ERROR("Couldn't initialize the zip writer on heap.")
        for (int i = 1; i < vector.size(); ++i) {
            const QString& fullPath = inputPath + vector[i];
            const bool isDir = QFileInfo(fullPath).isDir();
            const QString& archivePath = cleanArchivePath(rootDirectory, vector[i], isDir);
            if (isDir) {
                if (!mz_zip_writer_add_mem(&zip, archivePath.toUtf8().constData(), nullptr, 0, 0)) {
                    mz_zip_writer_end(&zip);
                    RETURN_ERROR_ARG("Couldn't create a directory, path: %1.", fullPath.toUtf8().constData())
                }
            } else {
                if (!mz_zip_writer_add_file(&zip, archivePath.toUtf8().constData(),
                                            fullPath.toUtf8().constData(),
                                            nullptr, 0, compressionLevel)) {
                    mz_zip_writer_end(&zip);
                    RETURN_ERROR_ARG("Couldn't compress a file, path: %1.", fullPath.toUtf8().constData())
                }
            }
            progress += step;
            REPORT_PROGRESS_CLEAN(progress, false)
        }
        void* data; // Finalize the zip archive on heap
        size_t size;
        mz_zip_writer_finalize_heap_archive(&zip, &data, &size);
        mz_zip_writer_end(&zip);
        if (data && size > 0 && vector.size() > 1) { // Flush the zip archive from memory to file on disk
            QFile file(outputFilePath);
            if (!file.open(QIODevice::WriteOnly)) {
                mz_free(data);
                RETURN_ERROR("Couldn't write archive from memory to disk.")
            }
            file.write((char*)data, size);
        }
        if (data)
            mz_free(data);
    }

    RETURN(vector.size() - 1)
}

int unzip(QFutureInterfaceBase* futureInterface, const QString& inputFilePath,
          const QString& outputPath, bool overwrite)
{
    auto future = static_cast<QFutureInterface<int>*>(futureInterface);
    future->setProgressRange(0, 100);
    future->setProgressValue(0);

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file_v2(&zip, inputFilePath.toUtf8().constData(), 0, 0, 0))
        RETURN_ERROR("Couldn't initialize the zip reader.")

    mz_uint processedEntryCount = 0;
    mz_uint numberOfFiles = mz_zip_reader_get_num_files(&zip);

    if (numberOfFiles <= 0) {
        mz_zip_reader_end(&zip);
        RETURN_ERROR("Archive is either invalid or empty.")
    }

    // Iterate for dirs
    for (mz_uint i = 0; i < numberOfFiles; ++i) {
        mz_zip_archive_file_stat fileStat;
        if (!mz_zip_reader_file_stat(&zip, i, &fileStat)) {
            mz_zip_reader_end(&zip);
            RETURN_ERROR("Archive is broken.")
        }
        if (!fileStat.m_is_supported) {
            mz_zip_reader_end(&zip);
            RETURN_ERROR("Archive isn't supported.")
        }
        if (fileStat.m_is_directory) {
            if (!overwrite) {
                QDir target(outputPath + '/' + fileStat.m_filename); target.cdUp();
                const bool isBase = target == QDir(outputPath);
                if (isBase && QFileInfo::exists(outputPath + '/' + fileStat.m_filename)) {
                    mz_zip_reader_end(&zip);
                    RETURN_ERROR("Operation canceled, dir already exists.")
                }
            }
            QDir(outputPath).mkpath(fileStat.m_filename);
            processedEntryCount++;
            REPORT_PROGRESS_CLEAN(100 * processedEntryCount / numberOfFiles, true)
        }
    }

    // Iterate for files
    for (mz_uint i = 0; i < numberOfFiles; ++i) {
        mz_zip_archive_file_stat fileStat;
        if (!mz_zip_reader_file_stat(&zip, i, &fileStat)) {
            mz_zip_reader_end(&zip);
            RETURN_ERROR("Archive is broken.")
        }
        if (!fileStat.m_is_supported) {
            mz_zip_reader_end(&zip);
            RETURN_ERROR("Archive isn't supported.")
        }
        if (!fileStat.m_is_directory) {
            if (!overwrite && QFileInfo::exists(outputPath + '/' + fileStat.m_filename)) {
                mz_zip_reader_end(&zip);
                RETURN_ERROR("Operation canceled, file already exists.")
            }
            if (!mz_zip_reader_extract_to_file(
                        &zip, i, (outputPath + '/' + fileStat.m_filename).toUtf8().constData(),
                        0)) {
                mz_zip_reader_end(&zip);
                RETURN_ERROR_ARG("Extraction failed, file: %1.", fileStat.m_filename)
            }
            processedEntryCount++;
            REPORT_PROGRESS_CLEAN(100 * processedEntryCount / numberOfFiles, true)
        }
    }
    mz_zip_reader_end(&zip);
    RETURN(processedEntryCount)
}
} // Internal

/*!
    Summary:
        This function compresses, depending on the inputPath, the file or recursive content of the
        directory given by inputPath into the zip archive file given by outputFilePath. You can use
        append parameter if you want, to extend (or overwrite) the content of an existing zip file
        at the destination where inputPath points out. You can use the rootDirectory parameter if
        you want, to put all the input resources (files and folders) into a relative root directory
        under the central directory of a zip archive. Also you can use nameFilters and filters
        parameters when the inputPath points out to a directory, in order to specify what kind of
        files and folders recursively will be scanned and which one of them will be added into the
        final zip archive. On the other hand, compressionLevel parameter could be used to specify
        compression hardness for the zip archive.

        If the function fails for some reason, before spawning a separate worker thread, then it
        returns an invalid future (in "canceled" state) in the first place. If the worker thread is
        spawned and the zip operation has failed for some reason, then the future returned by this
        function will emit resultReadyAt signal (via QFutureWatcher) with -1 as the result. Also
        progress will be set to 100 and progress text will be set to the appropriate error string.
        So you can catch those error states via QFutureWatcher's progressTextChanged and
        progressValueChanged signals. You can also translate the error string by passing it to a
        QObject::tr() function.

        While the operation is still in progress, the progressValueChanged signal is emit almost 25
        times per second with the appropriate progress values of the ongoing operation and the
        progress range for the operation is between 0 and 100. When the operation is finished, the
        resultReadyAt signal is emitted with a single result that is the total number of entries
        extracted from the zip archive. Overall, this function only returns a single result, either
        it is -1 for errors, or the total number of entries extracted from the zip archive if it is
        successful. Also the finished signal is emit at the end.

        Other facilities, like pause/resume and cancel these are provided by the QFuture mechanism
        may also be used at any arbitrary point in operation's life time in order to pause/resume or
        cancel the operation. Appropriate signals will also be emit.

    inputPath:
        This could be either a file or directory, but it must be exists and readable in any case. If
        it is a directory, then all the files and folders in it will be compressed into the output zip
        file. If you want all the files and folders within the directory to be placed under a root
        directory with the same name of the input directory you can use rootDirectory parameter. You
        can also use rootDirectory parameter to specify another root directory name (base path or
        however you name it) other than the input directory name. Compression occurs recursively.

    outputFilePath:
        This points out to a zip file path. If the zip file is already exists, regardless of whether
        it is a valid or invalid zip file, if append isn't enabled, it will be cleansed and a valid
        zip file will be created on the disk from scratch. On the other hand if append is enabled,
        and the zip file is valid, the files and folders the input path points out will be appended
        into that zip file. If either the zip file is invalid or operation fails for some reason,
        the existing zip file may also be corrupted. If the file outputFilePath parameter points out
        doesn't exist, then, regardless of the state of the append parameter, a new valid zip file
        will be created on the disk from scratch and files and folders will be compressed into it.

    rootDirectory:
        It is used to place files and folders under a root directory, relative to the central
        directory of a zip archive. If it is empty, then the central directory of the zip archive is
        chosen. This parameter may be like "/my/relative/path" or "/my/relative/path/" or
        "my/relative/path", or "my/relative/path/". Hence, all the files and folders that inputPath
        parameter points out will be put under the rootDirectory. The rootDirectory may also point
        out to an existing directory in an existing zip archive when it is used with the append mode
        enabled. Hence, you could replace existing files in a zip file with using rootDirectory and
        append parameters together.

    nameFilters:
        This could be used to filter out some files based on their full file name (filename.ext)
        hence those files won't be included into the output zip file. If it is empty, then there will
        no such filtering occur on the zip file. Directory names could also be filtered in addition
        to file names. Each name filter is a wildcard (globbing) filter that understands * and ?
        wildcards. See QRegularExpression Wildcard Matching. For example, the following code sets
        three name filters on a QDir to ensure that only files with extensions typically used for
        C++ source files are listed: "*.cpp", "*.cxx", "*.cc". This parameter is only valid and works
        when inputPath points out to a directory.

    filters:
        The filter is used to specify the kind of files that should be zipped. This filter flags
        only valid when inputPath is a dir and it is used to resolve dirs and files recursively under
        a directory (when the inputPath points out a directory). With this flag, for instance, you
        can filter out hidden files and don't include them in a zip file.

    compressionLevel:
        This parameter is used to specify compression hardness for the zip archive. How hard the
        level you choose, the compression will take longer for to finish, but final zip file will be
        less in size.

    append:
        This parameter is used to specify if file and folders are going to appended into the zip file
        that is already exists on the disk pointed out by the outputFilePath parameter. This option
        is similar to the affect of QIODevice::Append on the QFile::open function. If outputFilePath
        parameter points out to a nonexistent file, then append option doesn't have any effect.
*/

QFuture<int> zip(const QString& inputPath, const QString& outputFilePath,
                 const QString& rootDirectory, const QStringList& nameFilters,
                 QDir::Filters filters, CompressionLevel compressionLevel, bool append)
{
    if (!QFileInfo::exists(inputPath)) {
        qWarning("WARNING: The input path doesn't exist");
        return Internal::invalidFuture();
    }

    if (!QFileInfo(inputPath).isReadable()) {
        qWarning("WARNING: The input path isn't readable");
        return Internal::invalidFuture();
    }

    if (QFileInfo::exists(outputFilePath) && QFileInfo(outputFilePath).isDir()) {
        qWarning("WARNING: The output file path cannot be a directory");
        return Internal::invalidFuture();
    }

    const bool outputFilePathExists = QFileInfo::exists(outputFilePath);

    if (!Internal::touch(outputFilePath)) {
        qWarning("WARNING: The output file path isn't writable");
        return Internal::invalidFuture();
    }

    if (!outputFilePathExists)
        QFile::remove(outputFilePath);

    if (filters == QDir::NoFilter)
        filters = QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot;

    return Async::run(Internal::zip, inputPath, outputFilePath,
                      rootDirectory, nameFilters, filters, compressionLevel, append);
}

/*!
    Summary:
        This function extracts the content of the zip archive given by inputFilePath into the
        directory given by outputPath. You can use overwrite parameter in order to enable overwriting
        of any existing file or folders at the destination directory (within the outputPath),
        otherwise (when the overwrite parameter is disabled) the unzip operation may fail at any
        point because if any file or folder in the zip archive is already exists on the disk in the
        destination directory (within the outputPath).

        If the function fails for some reason, before spawning a separate worker thread, then it
        returns an invalid future (in "canceled" state) in the first place. If the worker thread is
        spawned and the unzip operation has failed for some reason, then the future returned by this
        function will emit resultReadyAt signal (via QFutureWatcher) with -1 as the result. Also
        progress will be set to 100 and progress text will be set to the appropriate error string.
        So you can catch those error states via QFutureWatcher's progressTextChanged and
        progressValueChanged signals. You can also translate the error string by passing it to a
        QObject::tr() function.

        While the operation is still in progress, the progressValueChanged signal is emit almost 25
        times per second with the appropriate progress values of the ongoing operation and the
        progress range for the operation is between 0 and 100. When the operation is finished, the
        resultReadyAt signal is emitted with a single result that is the total number of entries
        extracted from the zip archive. Overall, this function only returns a single result, either
        it is -1 for errors, or the total number of entries extracted from the zip archive if it is
        successful. Also the finished signal is emit at the end.

        Other facilities, like pause/resume and cancel these are provided by the QFuture mechanism
        may also be used at any arbitrary point in operation's life time in order to pause/resume or
        cancel the operation. Appropriate signals will also be emit.

    inputFilePath:
        This points out to a zip archive file path where all the content of this zip archive is going
        to be extracted into the output path. And the zip archive must be exists and valid. Otherwise
        extraction fails.

    outputPath:
        Output path must be a directory. It is the folder where all the content of the root directory
        of the input zip archive is going to be poured into directly.

    overwrite:
        If this parameter is enabled, then all the files and folders are going to be overwritten even
        if they exists. Otherwise (when it is disabled), extraction operation is canceled at any
        point if any file in the input zip archive is already exists on the disk at the destination
        folder (within the outputPath).
*/
QFuture<int> unzip(const QString& inputFilePath, const QString& outputPath, bool overwrite)
{
    if (!QFileInfo::exists(inputFilePath)) {
        qWarning("WARNING: The input file path doesn't exist");
        return Internal::invalidFuture();
    }

    if (QFileInfo(inputFilePath).isDir()) {
        qWarning("WARNING: The input file path cannot be a directory");
        return Internal::invalidFuture();
    }

    if (!QFileInfo(inputFilePath).isReadable()) {
        qWarning("WARNING: The input file path isn't readable");
        return Internal::invalidFuture();
    }

    if (!QFileInfo::exists(outputPath)) {
        qWarning("WARNING: The output path doesn't exist");
        return Internal::invalidFuture();
    }

    if (!QFileInfo(outputPath).isDir()) {
        qWarning("WARNING: The output path cannot be a file");
        return Internal::invalidFuture();
    }

    if (!QFileInfo(outputPath).isWritable()) {
        qWarning("WARNING: The output path isn't writable");
        return Internal::invalidFuture();
    }

    return Async::run(Internal::unzip, inputFilePath, outputPath, overwrite);
}
} // ZipAsync
