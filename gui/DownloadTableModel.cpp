#include "DownloadTableModel.hpp"

#include <algorithm>

#include <QApplication>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionProgressBar>

namespace {

QString stateText(dlm::JobState state) {
    switch (state) {
        case dlm::JobState::Queued: return QStringLiteral("Queued");
        case dlm::JobState::Running: return QStringLiteral("Running");
        case dlm::JobState::Paused: return QStringLiteral("Paused");
        case dlm::JobState::Completed: return QStringLiteral("Completed");
        case dlm::JobState::Failed: return QStringLiteral("Failed");
        case dlm::JobState::Cancelled: return QStringLiteral("Cancelled");
    }
    return QStringLiteral("Unknown");
}

// -1 means "unknown size, can't show a percentage" (e.g. before the probe
// request comes back) — ProgressBarDelegate draws that as indeterminate.
int progressPercent(const dlm::JobInfo& job) {
    if (job.state == dlm::JobState::Completed) {
        return 100;
    }
    if (job.totalBytes <= 0) {
        return -1;
    }
    const double fraction = static_cast<double>(job.bytesDone) / static_cast<double>(job.totalBytes);
    return std::clamp(static_cast<int>(fraction * 100.0), 0, 100);
}

QString humanBytes(std::int64_t bytes) {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return QString::number(value, 'f', unit == 0 ? 0 : 1) + " " + units[unit];
}

} // namespace

DownloadTableModel::DownloadTableModel(dlm::DownloadManager& manager, QObject* parent)
    : QAbstractTableModel(parent), manager_(manager) {}

int DownloadTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(jobs_.size());
}

int DownloadTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant DownloadTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(jobs_.size())) {
        return {};
    }
    const dlm::JobInfo& job = jobs_[static_cast<std::size_t>(index.row())];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case ColId: return static_cast<qulonglong>(job.id);
            case ColUrl: return QString::fromStdString(job.url);
            case ColOutput: return QString::fromStdString(job.outputPath);
            case ColState: return stateText(job.state);
            case ColProgress: {
                const int pct = progressPercent(job);
                if (job.state == dlm::JobState::Failed) {
                    return QStringLiteral("—");
                }
                if (pct < 0) {
                    return QStringLiteral("…");
                }
                return QStringLiteral("%1%  (%2 / %3)")
                    .arg(pct)
                    .arg(humanBytes(job.bytesDone))
                    .arg(humanBytes(job.totalBytes));
            }
            default: return {};
        }
    }

    if (role == Qt::UserRole && index.column() == ColProgress) {
        return progressPercent(job);
    }

    if (role == Qt::ToolTipRole) {
        if (index.column() == ColState && job.state == dlm::JobState::Failed) {
            return QString::fromStdString(job.result.error);
        }
        if (index.column() == ColUrl) {
            return QString::fromStdString(job.url);
        }
        if (index.column() == ColOutput) {
            return QString::fromStdString(job.outputPath);
        }
    }

    return {};
}

QVariant DownloadTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
        case ColId: return QStringLiteral("ID");
        case ColUrl: return QStringLiteral("URL");
        case ColOutput: return QStringLiteral("Output file");
        case ColState: return QStringLiteral("State");
        case ColProgress: return QStringLiteral("Progress");
        default: return {};
    }
}

void DownloadTableModel::refresh() {
    std::vector<dlm::JobInfo> fresh = manager_.allJobs();
    std::sort(fresh.begin(), fresh.end(),
              [](const dlm::JobInfo& a, const dlm::JobInfo& b) { return a.id < b.id; });

    // Jobs are only ever added, never removed mid-session, so a full reset
    // is fine here and keeps this simple — DownloadManager doesn't expose
    // a way to drop a finished job from the list anyway.
    const bool rowCountChanged = fresh.size() != jobs_.size();
    jobs_ = std::move(fresh);

    if (rowCountChanged) {
        beginResetModel();
        endResetModel();
    } else if (!jobs_.empty()) {
        emit dataChanged(index(0, 0), index(static_cast<int>(jobs_.size()) - 1, ColumnCount - 1));
    }
}

std::uint64_t DownloadTableModel::jobIdAt(int row) const {
    if (row < 0 || row >= static_cast<int>(jobs_.size())) {
        return 0;
    }
    return jobs_[static_cast<std::size_t>(row)].id;
}

void ProgressBarDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
    if (index.column() != DownloadTableModel::ColProgress) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    const int percent = index.data(Qt::UserRole).toInt();

    QStyleOptionProgressBar bar;
    bar.rect = option.rect.adjusted(2, 2, -2, -2);
    bar.minimum = 0;
    bar.maximum = percent < 0 ? 0 : 100; // min == max renders as indeterminate/busy
    bar.progress = percent < 0 ? 0 : percent;
    bar.text = index.data(Qt::DisplayRole).toString();
    bar.textVisible = true;

    QStyle* style = QApplication::style();
    style->drawControl(QStyle::CE_ProgressBar, &bar, painter);
}
