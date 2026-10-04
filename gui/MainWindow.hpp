#pragma once

#include <QMainWindow>

#include <dlm/DownloadManager.hpp>

class QLineEdit;
class QPushButton;
class QTableView;
class DownloadTableModel;

// The whole GUI: a form to add a download, a table of jobs with live
// progress, and pause/resume/cancel buttons for the selected row.
// DownloadManager does the actual work on its own worker threads; this
// window only ever touches it through DownloadManager's public API
// (enqueue/pause/resume/cancel/allJobs), which is safe from any thread,
// and re-reads it on a timer rather than being pushed updates — see
// DownloadTableModel's comment for why.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(dlm::DownloadManager& manager, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onAddClicked();
    void onBrowseClicked();
    void onPauseClicked();
    void onResumeClicked();
    void onCancelClicked();
    void onRefreshTimer();

private:
    std::uint64_t selectedJobId() const;

    dlm::DownloadManager& manager_;
    DownloadTableModel* model_ = nullptr;

    QLineEdit* urlEdit_ = nullptr;
    QLineEdit* outputEdit_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* browseButton_ = nullptr;
    QTableView* tableView_ = nullptr;
    QPushButton* pauseButton_ = nullptr;
    QPushButton* resumeButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
};
