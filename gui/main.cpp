#include <QApplication>

#include <dlm/CurlHttpClient.hpp>
#include <dlm/DownloadManager.hpp>

#include "MainWindow.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // Both must outlive MainWindow/DownloadManager usage below — declared
    // here in main() so they're destroyed only after app.exec() returns
    // and the window (and anything it touched through manager) is gone.
    // DownloadManager's destructor blocks on waitAll(); MainWindow's
    // closeEvent cancels every in-flight job first so that doesn't hang
    // on exit.
    dlm::CurlHttpClient httpClient;
    dlm::DownloadManager manager(httpClient, /*maxConcurrentDownloads=*/3,
                                  /*workersPerDownload=*/4);

    MainWindow window(manager);
    window.show();

    return app.exec();
}
