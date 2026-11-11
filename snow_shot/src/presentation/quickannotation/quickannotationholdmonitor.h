#pragma once

#include <QString>
#include <functional>
#include <memory>

namespace snow_shot::presentation {
// macOS-only passive modifier monitoring; it never consumes keyboard events.
class QuickAnnotationHoldMonitor final {
  public:
    explicit QuickAnnotationHoldMonitor(std::function<void(bool)> changed);
    ~QuickAnnotationHoldMonitor();
    void setChord(const QString& chord);

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace snow_shot::presentation
