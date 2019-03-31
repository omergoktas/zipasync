# zipasync

Basic utility library based on Qt/C++ for asynchronous zipping/unzipping. It uses [miniz](https://github.com/richgel999/miniz) (for zipping/unzipping) and [async](https://github.com/omergoktas/async) (for asynchronous operations) as low level implementations. Calls to asynchronous zip/unzip functions return a QFuture object that will eventually let you handle the results (states) of the zip/unzip operation. It supports pause/resume/cancel functionality for an asynchronous zip/unzip operation. It supports filtering out files based on file names (supports wildcards, see below example). It also supports instant progress reporting via QFutureWatcher class. You can use Qt's signal/slot mechanism to catch those progress changes happens on the worker thread from the main thread.

> C++14 needed


## Function prototypes

```cpp
// Asynchronous versions
QFuture<size_t> zip(const QString& sourcePath, const QString& destinationZipPath,
                    const QString& rootDirectory = QString(), CompressionLevel compressionLevel = Medium,
                    QDir::Filters filters = QDir::NoFilter, const QStringList& nameFilters = {},
                    bool append = true);

QFuture<size_t> unzip(const QString& sourceZipPath, const QString& destinationPath, bool overwrite = false);

// Synchronous versions
size_t zipSync(const QString& sourcePath, const QString& destinationZipPath,
               const QString& rootDirectory = QString(), CompressionLevel compressionLevel = Medium,
               QDir::Filters filters = QDir::NoFilter, const QStringList& nameFilters = {},
               bool append = true);

size_t unzipSync(const QString& sourceZipPath, const QString& destinationPath, bool overwrite = false);
```


## Example code

```cpp
#include <QtWidgets>
#include <zipasync.h>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QFutureWatcher<size_t> watcher;
    QPushButton pauseButton;

    //! A push button to pause/resume the zipping task
    pauseButton.setText("Pause/Resume");
    pauseButton.show();
    QObject::connect(&pauseButton, &QPushButton::clicked, [&] {
        watcher.future().togglePaused();
        qWarning("Operation %s", watcher.isPaused() ? "paused" : "running");
    });

    //! Catch state changes on the task by connecting appropriate signals to the slots
    // Note: See QTBUG-12152 for QFutureWatcherBase::paused signal
    QObject::connect(&watcher, &QFutureWatcherBase::resultReadyAt, [&] (int i) // i: index of result
    { qWarning("%s resolved...", QString::number(watcher.resultAt(i)).toUtf8().data()); });
    QObject::connect(&watcher, &QFutureWatcherBase::progressValueChanged, [] (int progress)
    { qWarning("Progress: %d", progress); });
    QObject::connect(&watcher, &QFutureWatcherBase::canceled, []
    { qWarning("Operation canceled!"); });
    QObject::connect(&watcher, &QFutureWatcherBase::finished, [&] {
        if (watcher.future().resultCount() > 0) {
            int lastIndex = watcher.future().resultCount() - 1;
            auto lastResult = watcher.resultAt(lastIndex);
            if (lastResult == 0) // Error occurred
                qWarning("Error: %s", watcher.progressText().toUtf8().data());
            else if (!watcher.isCanceled()) // Succeed
                qWarning("Done: %s entries compressed!", QString::number(lastResult).toUtf8().data());
            // else -> Do nothing, Operation canceled in the middle
        } // else -> Do nothing, operation canceled even before attempting to do anything
        app.quit();
    });

    //! Zip recursively the content of a folder asynchronously (except C++ source files)
    // To avoid a race condition, it is important to call this function after doing the connections
    watcher.setFuture(ZipAsync::zip("/Users/omergoktas/Desktop/SourceFolder",
                                    "/Users/omergoktas/Desktop/Destination.zip",
                                    "SomeRootDirectory", ZipAsync::Low, QDir::NoFilter,
                                    {"*.cpp", "*.cc", "*.cxx"}));

    // Check if it fails even before attempting to do anything (or spawning the worker thread)
    if (watcher.isCanceled())
        return EXIT_FAILURE;

    qWarning("Compression in progress...");

    //! (Optional) Cancel the operation after 1 second
    // QTimer::singleShot(1000, [&] { watcher.cancel(); watcher.waitForFinished(); });

    return app.exec();
}
```


## Further reading
Read more info from [here](https://github.com/omergoktas/zipasync/blob/bd5385f0d16b064574d7e57066144f2f26e99416/zipasync.cpp#L496) and [here](https://github.com/omergoktas/zipasync/blob/bd5385f0d16b064574d7e57066144f2f26e99416/zipasync.cpp#L644)
