#pragma once

#include "snow_shot/presentation/quickannotationstate.h"

#include <QObject>
#include <functional>
#include <memory>

namespace snow_shot::presentation {

class QuickAnnotationController final : public QObject {
    Q_OBJECT
  public:
    explicit QuickAnnotationController(QObject* parent = nullptr);
    QuickAnnotationController(QObject* parent, std::function<qint64()> clock);
    ~QuickAnnotationController() override;

    void arm(QuickAnnotationTool tool);
    void armLastTool();
    void showToolbar();

  signals:
    void errorOccurred(const QString& message);

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace snow_shot::presentation
