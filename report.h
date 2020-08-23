#ifndef REPORT_H
#define REPORT_H

#include <QDebug>

#define INITIALIZE(type, futureInterface)                                    \
    auto future = static_cast<QFutureInterface<type>*>(futureInterface);     \
    future->setProgressRange(0, 100);                                        \
    future->setProgressValue(0);

#define FINALIZE(result)                                                     \
    future->setProgressValue(100);                                           \
    future->reportResult(result);                                            \
    return result;

#define REPORT(progress, result)                                             \
    future->reportResult(result);                                            \
    future->setProgressValue(progress);                                      \

#define REPORT_RESULT_UNSAFE(result)                                         \
    if (future->isProgressUpdateNeeded()) {                                  \
        if (future->isPaused())                                              \
            future->waitForResume();                                         \
        if (future->isCanceled())                                            \
            return 0;                                                        \
        future->reportResult(result);                                        \
    }

#define REPORT_PROGRESS_SAFE(progress, zip)                                  \
    if (future->isProgressUpdateNeeded()) {                                  \
        if (future->isPaused())                                              \
            future->waitForResume();                                         \
        if (future->isCanceled()) {                                          \
            if (zip.m_zip_mode == MZ_ZIP_MODE_READING) {                     \
                mz_zip_reader_end(&zip);                                     \
            } else {                                                         \
                mz_zip_writer_finalize_archive(&zip);                        \
                mz_zip_writer_end(&zip);                                     \
            }                                                                \
            return 0;                                                        \
        }                                                                    \
        future->setProgressValue(progress);                                  \
    }

namespace ZipAsync {
namespace Internal {
inline QString& combineStringArguments(QString& str) { return str; }

template <typename First, typename... Rest>
QString& combineStringArguments(QString& msg, First&& first, Rest&&... rest)
{
    msg = msg.arg(first);
    return combineStringArguments(msg, std::forward<Rest>(rest)...);
}
} // Internal

int WARNING(const char* msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    qWarning(msg, ap);
    va_end(ap);
    return 0;
}

template <typename Future, typename... Args>
int CRASH(Future future, QString msg, Args&&... args)
{
    future->setProgressValueAndText(100, Internal::combineStringArguments(msg, std::forward<Args>(args)...));
    future->reportResult(size_t(0));
    return 0;
}
} // ZipAsync

#endif // REPORT_H
