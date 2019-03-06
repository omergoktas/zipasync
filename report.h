#ifndef REPORT_H
#define REPORT_H

#include <QString>

#define INITIALIZE(type, futureInterface) \
    auto future = static_cast<QFutureInterface<type>*>(futureInterface); \
    future->setProgressRange(0, 100); \
    future->setProgressValue(0);

#define FINALIZE(result) \
    future->setProgressValue(100); \
    future->reportResult(result); \
    return result;

#define WARNING(msg, ...) { \
    qWarning(msg, ##__VA_ARGS__); \
    return 0; }

#define CRASH(context, msg, ...) { \
    future->setProgressValueAndText(100, \
    combineStringArguments(QT_TRANSLATE_NOOP(context, msg), ##__VA_ARGS__)); \
    future->reportResult(size_t(0)); \
    return 0; }

#define REPORT(progress, result) \
    future->setProgressValue(progress); \
    future->reportResult(result); \

#define REPORT_RESULT_UNSAFE(result) \
    if (future->isProgressUpdateNeeded()) { \
        if (future->isPaused()) \
            future->waitForResume(); \
        if (future->isCanceled()) \
            return 0; \
        future->reportResult(result); \
    }

#define REPORT_PROGRESS_SAFE(progress, zip) \
    if (future->isProgressUpdateNeeded()) { \
        if (future->isPaused()) \
            future->waitForResume(); \
        if (future->isCanceled()) { \
            if (zip.m_zip_mode == MZ_ZIP_MODE_READING) { \
                mz_zip_reader_end(&zip); \
            } else { \
                mz_zip_writer_finalize_archive(&zip); \
                mz_zip_writer_end(&zip); \
            } \
            return 0; \
        } \
        future->setProgressValue(progress); \
    }

namespace ZipAsync {
namespace Internal {

inline void combineStringArguments(QString&) {}

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

} // Internal
} // ZipAsync

#endif // REPORT_H
