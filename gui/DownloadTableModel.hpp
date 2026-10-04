#pragma once

#include <cstdint>
#include <vector>

#include <QAbstractTableModel>
#include <QStyledItemDelegate>

#include <dlm/DownloadManager.hpp>

// Thin read-model over DownloadManager: refresh() re-fetches
// DownloadManager::allJobs() and the view re-reads through the usual
// QAbstractItemModel interface. DownloadManager is polled on a timer
// (see MainWindow) rather than pushing updates, because its worker
// threads have no safe way to reach into Qt's GUI thread — polling keeps
// all Qt calls on the GUI thread without touching the download engine.
class DownloadTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { ColId = 0, ColUrl, ColOutput, ColState, ColProgress, ColumnCount };

    explicit DownloadTableModel(dlm::DownloadManager& manager, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;

    // Re-reads DownloadManager::allJobs() and refreshes the whole table.
    // Called periodically from a QTimer in MainWindow.
    void refresh();

    // The job id shown in a given row, or 0 if row is out of range.
    std::uint64_t jobIdAt(int row) const;

private:
    dlm::DownloadManager& manager_;
    std::vector<dlm::JobInfo> jobs_; // sorted by id, oldest first
};

// Paints DownloadTableModel::ColProgress as an actual progress bar instead
// of plain percentage text, using the Qt::UserRole value (0-100, or -1 for
// an indeterminate/unknown-size download) set by DownloadTableModel::data.
class ProgressBarDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
};
