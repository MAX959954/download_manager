#include "MainWindow.hpp"
#include "DownloadTableModel.hpp"

#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableView>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace {
constexpr int kRefreshIntervalMs = 500;
} // namespace

MainWindow::MainWindow(dlm::DownloadManager& manager, QWidget* parent)
    : QMainWindow(parent), manager_(manager) {
    setWindowTitle(QStringLiteral("Download Manager"));
    resize(820, 480);

    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);

    // --- "Add download" row ---
    auto* formLayout = new QHBoxLayout();
    urlEdit_ = new QLineEdit(central);
    urlEdit_->setPlaceholderText(QStringLiteral("https://example.com/file.zip"));
    outputEdit_ = new QLineEdit(central);
    outputEdit_->setPlaceholderText(QStringLiteral("Output file"));
    browseButton_ = new QPushButton(QStringLiteral("Browse…"), central);
    addButton_ = new QPushButton(QStringLiteral("Add"), central);

    formLayout->addWidget(urlEdit_, /*stretch=*/3);
    formLayout->addWidget(outputEdit_, /*stretch=*/2);
    formLayout->addWidget(browseButton_);
    formLayout->addWidget(addButton_);
    rootLayout->addLayout(formLayout);

    // --- Job table ---
    model_ = new DownloadTableModel(manager_, this);
    tableView_ = new QTableView(central);
    tableView_->setModel(model_);
    tableView_->setItemDelegateForColumn(DownloadTableModel::ColProgress,
                                          new ProgressBarDelegate(tableView_));
    tableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView_->setSelectionMode(QAbstractItemView::SingleSelection);
    tableView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableView_->horizontalHeader()->setStretchLastSection(false);
    tableView_->horizontalHeader()->setSectionResizeMode(DownloadTableModel::ColUrl,
                                                           QHeaderView::Stretch);
    tableView_->horizontalHeader()->setSectionResizeMode(DownloadTableModel::ColOutput,
                                                           QHeaderView::Stretch);
    tableView_->horizontalHeader()->setSectionResizeMode(DownloadTableModel::ColProgress,
                                                           QHeaderView::Fixed);
    tableView_->setColumnWidth(DownloadTableModel::ColProgress, 220);
    tableView_->setColumnWidth(DownloadTableModel::ColId, 40);
    rootLayout->addWidget(tableView_, /*stretch=*/1);

    // --- Pause/resume/cancel row ---
    auto* actionsLayout = new QHBoxLayout();
    pauseButton_ = new QPushButton(QStringLiteral("Pause"), central);
    resumeButton_ = new QPushButton(QStringLiteral("Resume"), central);
    cancelButton_ = new QPushButton(QStringLiteral("Cancel"), central);
    actionsLayout->addWidget(pauseButton_);
    actionsLayout->addWidget(resumeButton_);
    actionsLayout->addWidget(cancelButton_);
    actionsLayout->addStretch(1);
    rootLayout->addLayout(actionsLayout);

    setCentralWidget(central);

    connect(addButton_, &QPushButton::clicked, this, &MainWindow::onAddClicked);
    connect(browseButton_, &QPushButton::clicked, this, &MainWindow::onBrowseClicked);
    connect(pauseButton_, &QPushButton::clicked, this, &MainWindow::onPauseClicked);
    connect(resumeButton_, &QPushButton::clicked, this, &MainWindow::onResumeClicked);
    connect(cancelButton_, &QPushButton::clicked, this, &MainWindow::onCancelClicked);

    // DownloadManager's worker threads can't safely touch Qt widgets, so
    // instead of a callback from the engine, the GUI thread polls
    // allJobs() on a timer and repaints from that snapshot.
    auto* refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &MainWindow::onRefreshTimer);
    refreshTimer->start(kRefreshIntervalMs);
}

void MainWindow::onAddClicked() {
    const QString url = urlEdit_->text().trimmed();
    QString output = outputEdit_->text().trimmed();

    if (url.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Enter a URL first."));
        return;
    }
    if (output.isEmpty()) {
        // Guess a file name from the URL's last path segment so the user
        // doesn't have to type one for the common case.
        const QString guessed = QFileInfo(QUrl(url).path()).fileName();
        output = guessed.isEmpty() ? QStringLiteral("download.bin") : guessed;
        outputEdit_->setText(output);
    }

    manager_.enqueue(url.toStdString(), output.toStdString());
    urlEdit_->clear();
    outputEdit_->clear();
    model_->refresh();
}

void MainWindow::onBrowseClicked() {
    const QString suggested = outputEdit_->text().trimmed().isEmpty()
                                   ? QFileInfo(QUrl(urlEdit_->text().trimmed()).path()).fileName()
                                   : outputEdit_->text().trimmed();
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save as"), suggested);
    if (!path.isEmpty()) {
        outputEdit_->setText(path);
    }
}

std::uint64_t MainWindow::selectedJobId() const {
    const QModelIndex index = tableView_->currentIndex();
    return index.isValid() ? model_->jobIdAt(index.row()) : 0;
}

void MainWindow::onPauseClicked() {
    const std::uint64_t id = selectedJobId();
    if (id && manager_.status(id).state == dlm::JobState::Running) {
        manager_.pause(id);
        model_->refresh();
    }
}

void MainWindow::onResumeClicked() {
    const std::uint64_t id = selectedJobId();
    // DownloadManager::resume() always re-enqueues the job, with no check
    // of its own — calling it on a job that isn't actually paused would
    // start a second worker racing the first, so that check belongs here.
    if (id && manager_.status(id).state == dlm::JobState::Paused) {
        manager_.resume(id);
        model_->refresh();
    }
}

void MainWindow::onCancelClicked() {
    const std::uint64_t id = selectedJobId();
    if (!id) {
        return;
    }
    const dlm::JobState state = manager_.status(id).state;
    if (state == dlm::JobState::Queued || state == dlm::JobState::Running ||
        state == dlm::JobState::Paused) {
        manager_.cancel(id);
        model_->refresh();
    }
}

void MainWindow::onRefreshTimer() {
    model_->refresh();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Cancel every job that's still in flight so DownloadManager's
    // destructor (which blocks on waitAll()) doesn't hang the app on
    // exit waiting for a slow download to finish on its own.
    for (const dlm::JobInfo& job : manager_.allJobs()) {
        if (job.state == dlm::JobState::Queued || job.state == dlm::JobState::Running ||
            job.state == dlm::JobState::Paused) {
            manager_.cancel(job.id);
        }
    }
    event->accept();
}
