#pragma once
#include <QAbstractFileIconProvider>
#include <QFileInfo>
#include <QIcon>

namespace gridlock::ui {

class FileIconProvider : public QAbstractFileIconProvider {
public:
  FileIconProvider();
  ~FileIconProvider() override = default;

  QIcon icon(const QFileInfo &info) const override;
  QIcon icon(IconType type) const override;
};

} // namespace gridlock::ui
