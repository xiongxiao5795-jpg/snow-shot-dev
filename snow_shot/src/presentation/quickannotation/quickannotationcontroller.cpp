#include "snow_shot/presentation/quickannotationcontroller.h"

#include "../globalcanvas/globalcanvasplatform.h"
#include "snow_shot/platform/physicalcursor.h"
#include "snow_shot/platform/screenshotnative.h"
#ifdef Q_OS_MACOS
#include "snow_shot/platform/macos/screenshotsessionfocus.h"
#endif
#include "snow_shot/presentation/screenshotcanvastoolstyles.h"
#include "snow_shot/presentation/screenshotdefaultstyles.h"
#include "snow_shot/storage/applicationstorage.h"
#include "snow_shot/storage/configurationstore.h"
#include "snow_draw_engine_qt/snow_canvas_custom_renderer.h"
#include "snow_draw_engine_qt/snow_canvas_runtime.h"
#include "snow_draw_engine_qt/snow_canvas_widget.h"

#include <QApplication>
#include <QCursor>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QWindow>

#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>
#include <vector>

namespace snow_shot::presentation {
namespace {

class ToolPanel final : public QWidget {
  public:
    explicit ToolPanel(std::function<void(QuickAnnotationTool)> choose)
        : QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
          m_choose(std::move(choose)) {
        setObjectName(QStringLiteral("quickAnnotationToolbar"));
        auto* layout = new QHBoxLayout(this);
        const auto add = [this, layout](QToolButton*& button, QuickAnnotationTool tool) {
            button = new QToolButton(this);
            layout->addWidget(button);
            connect(button, &QToolButton::clicked, this, [this, tool] {
                hide();
                m_choose(tool);
            });
        };
        add(m_brush, QuickAnnotationTool::Brush);
        add(m_arrow, QuickAnnotationTool::Arrow);
        add(m_rectangle, QuickAnnotationTool::Rectangle);
        add(m_ellipse, QuickAnnotationTool::Ellipse);
        retranslate();
    }

  protected:
    void changeEvent(QEvent* event) override {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::LanguageChange)
            retranslate();
    }

  private:
    void retranslate() {
        m_brush->setText(QCoreApplication::translate(
            "snow_shot::presentation::QuickAnnotationController", "Brush"));
        m_arrow->setText(QCoreApplication::translate(
            "snow_shot::presentation::QuickAnnotationController", "Arrow"));
        m_rectangle->setText(QCoreApplication::translate(
            "snow_shot::presentation::QuickAnnotationController", "Rectangle"));
        m_ellipse->setText(QCoreApplication::translate(
            "snow_shot::presentation::QuickAnnotationController", "Ellipse"));
    }

    std::function<void(QuickAnnotationTool)> m_choose;
    QToolButton* m_brush = nullptr;
    QToolButton* m_arrow = nullptr;
    QToolButton* m_rectangle = nullptr;
    QToolButton* m_ellipse = nullptr;
};

class GestureCanvas final : public SnowCanvasWidget {
  public:
    using SnowCanvasWidget::SnowCanvasWidget;
    std::function<void()> pressed;
    std::function<void()> released;
    std::function<void()> cancelled;

  protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) {
            event->accept();
            return;
        }
        if (pressed)
            pressed();
        SnowCanvasWidget::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) {
            event->accept();
            return;
        }
        SnowCanvasWidget::mouseReleaseEvent(event);
        if (released)
            released();
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape && cancelled) {
            cancelled();
            event->accept();
            return;
        }
        SnowCanvasWidget::keyPressEvent(event);
    }
};

class AnnotationWindow final : public QWidget, public SnowCanvasCustomRenderer {
  public:
    explicit AnnotationWindow(QScreen* screen, std::function<void()> onError,
                              std::function<qint64()> clock)
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
          m_screen(screen), m_runtime(SnowCanvasRuntimeConfig{screenshotCanvasToolStyleDefaults()}),
          m_canvas(m_runtime, this), m_onError(std::move(onError)), m_now(std::move(clock)) {
        setObjectName(QStringLiteral("quickAnnotationWindow"));
        m_canvas.setObjectName(QStringLiteral("quickAnnotationCanvas"));
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setWindowFlag(Qt::NoDropShadowWindowHint);
        m_canvas.setClearBackgroundEnabled(false);
        m_canvas.setCustomRenderer(this);
        m_canvas.setWheelZoomEnabled(false);
        m_runtime.setAutomaticallySelectNewlyCreatedElements(false);
        applyScreenshotCanvasToolStyles(m_canvas, screenshotCanvasToolStyleDefaults());
        m_canvas.pressed = [this] { m_state.begin(); };
        m_canvas.released = [this] { completeGesture(); };
        m_canvas.cancelled = [this] { cancel(); };
        m_clock.start();
        if (!m_now)
            m_now = [this] { return m_clock.elapsed(); };
        m_timer.setParent(this);
        m_timer.setObjectName(QStringLiteral("quickAnnotationFadeTimer"));
        m_timer.setInterval(33);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            const auto now = m_now();
            std::erase_if(m_marks, [now](const Mark& mark) {
                return QuickAnnotationState::opacity(now - mark.created) == 0.0;
            });
            ++m_revision;
            m_canvas.update();
            if (m_marks.empty()) {
                m_timer.stop();
                if (m_state.phase() == QuickAnnotationState::Phase::Idle)
                    hide();
            }
        });
        connect(screen, &QScreen::geometryChanged, this, [this] { updateGeometryForScreen(); });
#ifdef Q_OS_MACOS
        m_focus = platform::macos::createScreenshotSessionFocus();
        platform::configureGlobalCanvasWindow(this);
#endif
        updateGeometryForScreen();
    }

    ~AnnotationWindow() override {
        m_canvas.setCustomRenderer(nullptr);
    }
    [[nodiscard]] QScreen* screen() const {
        return m_screen;
    }
    [[nodiscard]] bool drawing() const {
        return m_state.phase() == QuickAnnotationState::Phase::Drawing;
    }

    bool arm(QuickAnnotationTool tool) {
        if (!m_state.arm(tool))
            return false;
        m_canvas.clearDocument();
        SnowCanvasShapeStyle shape;
        shape.shape = tool == QuickAnnotationTool::Ellipse ? SnowCanvasRectangleShape::Ellipse
                                                           : SnowCanvasRectangleShape::Rectangle;
        m_canvas.setCanvasShapeStylePatch(shape, SnowCanvasShapeStylePropertyShape,
                                          SnowCanvasShapeKind::Rectangle);
        const auto canvasTool = tool == QuickAnnotationTool::Brush   ? SnowCanvasTool::FreeDraw
                                : tool == QuickAnnotationTool::Arrow ? SnowCanvasTool::Arrow
                                                                     : SnowCanvasTool::Shape;
        m_canvas.setCanvasTool(canvasTool);
        updateGeometryForScreen();
#ifdef Q_OS_MACOS
        m_focus->prepare();
#endif
        show();
        raise();
        if (!setGlobalCanvasInputTransparent(this, false)) {
            m_state.cancel();
            hide();
#ifdef Q_OS_MACOS
            m_focus->abandonPrepared();
#endif
            m_onError();
            return false;
        }
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        m_canvas.setInteractionEnabled(true);
        activateWindow();
        m_canvas.setFocus(Qt::OtherFocusReason);
#ifdef Q_OS_MACOS
        m_focus->begin(true);
#endif
        return true;
    }

    void cancel() {
        m_state.cancel();
        m_canvas.resetEditingState();
        m_canvas.clearDocument();
        releaseInput();
    }

    std::uint64_t contentRevision() const override {
        return m_revision;
    }
    void renderBeforeCanvas(QPainter& painter, const SnowCanvasRenderContext& context) override {
        painter.save();
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(context.exposedRegion.boundingRect(), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        if (context.purpose == SnowCanvasRenderContext::Purpose::Widget) {
            const auto now = m_now();
            for (const Mark& mark : m_marks) {
                painter.setOpacity(QuickAnnotationState::opacity(now - mark.created));
                painter.drawImage(mark.rect, mark.image);
            }
        }
        painter.restore();
    }

  protected:
    bool event(QEvent* event) override {
        const bool handled = QWidget::event(event);
        if (event->type() == QEvent::WinIdChange && internalWinId() != 0) {
            QTimer::singleShot(0, this, [this] {
                const bool transparent = m_state.phase() == QuickAnnotationState::Phase::Idle;
                if (!setGlobalCanvasInputTransparent(this, transparent)) {
                    cancel();
                    hide();
                    m_marks.clear();
                    m_timer.stop();
                    m_onError();
                }
            });
        }
        if (event->type() == QEvent::WindowDeactivate &&
            m_state.phase() != QuickAnnotationState::Phase::Idle) {
            QTimer::singleShot(0, this, [this] {
                if (!isActiveWindow() && m_state.phase() != QuickAnnotationState::Phase::Idle)
                    cancel();
            });
        }
        return handled;
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        m_canvas.setGeometry(rect());
        m_canvas.setViewportCamera(width() / 2.0, height() / 2.0, 1.0);
    }

  private:
    struct Mark {
        QImage image;
        QRectF rect;
        qint64 created;
    };

    void updateGeometryForScreen() {
        if (!m_screen)
            return;
        if (windowHandle())
            windowHandle()->setScreen(m_screen);
        setGeometry(m_screen->geometry());
    }

    void completeGesture() {
        if (!m_state.finish())
            return;
        if (!releaseInput(true)) {
            m_canvas.resetEditingState();
            m_canvas.clearDocument();
            return;
        }
        if (m_canvas.canvasHistoryState().canUndo) {
            const qreal dpr = devicePixelRatioF();
            const QSize physical(qRound(width() * dpr), qRound(height() * dpr));
            const QImage image = m_canvas.renderContentToImage(physical).convertToFormat(
                QImage::Format_ARGB32_Premultiplied);
            if (!image.isNull()) {
                int left = image.width(), top = image.height(), right = -1, bottom = -1;
                for (int y = 0; y < image.height(); ++y) {
                    const auto* pixels = reinterpret_cast<const QRgb*>(image.constScanLine(y));
                    for (int x = 0; x < image.width(); ++x) {
                        if (qAlpha(pixels[x]) == 0)
                            continue;
                        left = std::min(left, x);
                        right = std::max(right, x);
                        top = std::min(top, y);
                        bottom = y;
                    }
                }
                if (right >= left && bottom >= top) {
                    const QRect bounds(QPoint(left, top), QPoint(right, bottom));
                    m_marks.push_back(
                        {image.copy(bounds),
                         QRectF(left / dpr, top / dpr, bounds.width() / dpr, bounds.height() / dpr),
                         m_now()});
                    m_timer.start();
                }
            }
        }
        m_canvas.resetEditingState();
        m_canvas.clearDocument();
        ++m_revision;
        if (m_marks.empty())
            hide();
        m_canvas.update();
    }

    bool releaseInput(bool keepVisible = false) {
#ifdef Q_OS_MACOS
        m_focus->beforeHide(true);
#endif
        if (!setGlobalCanvasInputTransparent(this, true)) {
            // Hiding is the safe fallback: never leave an invisible input blocker.
            hide();
            m_marks.clear();
            m_timer.stop();
#ifdef Q_OS_MACOS
            m_focus->afterHide();
#endif
            m_onError();
            return false;
        }
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_canvas.setInteractionEnabled(false);
        m_canvas.clearFocus();
#ifdef Q_OS_MACOS
        m_focus->afterHide();
#endif
        if (m_marks.empty() && !keepVisible)
            hide();
        return true;
    }

    QPointer<QScreen> m_screen;
    SnowCanvasRuntime m_runtime;
    GestureCanvas m_canvas;
    QuickAnnotationState m_state;
    std::function<void()> m_onError;
    std::function<qint64()> m_now;
    QElapsedTimer m_clock;
    QTimer m_timer;
    std::vector<Mark> m_marks;
    std::uint64_t m_revision = 0;
#ifdef Q_OS_MACOS
    std::unique_ptr<platform::macos::ScreenshotSessionFocus> m_focus;
#endif
};
} // namespace

class QuickAnnotationController::Impl {
  public:
    Impl(QuickAnnotationController& owner, std::function<qint64()> clock)
        : q(owner), now(std::move(clock)) {
        auto& storage = storage::ApplicationStorage::instance();
        if (storage.isInitialized()) {
            const QString saved = storage.configuration()
                                      .value(QStringLiteral("quick_annotation/last_tool"))
                                      .toString();
            if (saved == QStringLiteral("arrow"))
                lastTool = QuickAnnotationTool::Arrow;
            else if (saved == QStringLiteral("rectangle"))
                lastTool = QuickAnnotationTool::Rectangle;
            else if (saved == QStringLiteral("ellipse"))
                lastTool = QuickAnnotationTool::Ellipse;
        }
        QObject::connect(qApp, &QGuiApplication::screenRemoved, &q, [this](QScreen* screen) {
            for (const auto& window : windows) {
                if (window->screen() != screen)
                    continue;
                QPointer<AnnotationWindow> removed(window.get());
                window->cancel();
                QTimer::singleShot(0, &q, [this, removed] {
                    std::erase_if(windows, [removed](const auto& window) {
                        return window.get() == removed.data();
                    });
                });
            }
        });
    }

    void arm(QuickAnnotationTool tool) {
        if (toolbar)
            toolbar->hide();
        for (const auto& window : windows) {
            if (window->drawing())
                return;
            window->cancel();
        }
        const QPoint position =
            platform::PhysicalCursor().logicalPosition().value_or(QCursor::pos()).toPoint();
        QScreen* screen = QGuiApplication::screenAt(position);
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (!screen)
            return;
        auto it = std::find_if(windows.begin(), windows.end(),
                               [screen](const auto& window) { return window->screen() == screen; });
        if (it == windows.end()) {
            windows.push_back(std::make_unique<AnnotationWindow>(
                screen,
                [this] {
                    emit q.errorOccurred(
                        QuickAnnotationController::tr("Could not change canvas click-through"));
                },
                now));
            it = std::prev(windows.end());
        }
        if ((*it)->arm(tool)) {
            lastTool = tool;
            auto& storage = storage::ApplicationStorage::instance();
            if (storage.isInitialized()) {
                const QString value =
                    tool == QuickAnnotationTool::Arrow       ? QStringLiteral("arrow")
                    : tool == QuickAnnotationTool::Rectangle ? QStringLiteral("rectangle")
                    : tool == QuickAnnotationTool::Ellipse   ? QStringLiteral("ellipse")
                                                             : QStringLiteral("brush");
                storage.configuration().setValue(QStringLiteral("quick_annotation/last_tool"),
                                                 value);
            }
        }
    }

    QuickAnnotationController& q;
    std::function<qint64()> now;
    QuickAnnotationTool lastTool = QuickAnnotationTool::Brush;
    std::vector<std::unique_ptr<AnnotationWindow>> windows;
    std::unique_ptr<ToolPanel> toolbar;
};

QuickAnnotationController::QuickAnnotationController(QObject* parent)
    : QuickAnnotationController(parent, {}) {}
QuickAnnotationController::QuickAnnotationController(QObject* parent, std::function<qint64()> clock)
    : QObject(parent), m_impl(std::make_unique<Impl>(*this, std::move(clock))) {}
QuickAnnotationController::~QuickAnnotationController() = default;
void QuickAnnotationController::arm(QuickAnnotationTool tool) {
    m_impl->arm(tool);
}
void QuickAnnotationController::armLastTool() {
    m_impl->arm(m_impl->lastTool);
}
void QuickAnnotationController::showToolbar() {
    if (std::any_of(m_impl->windows.begin(), m_impl->windows.end(),
                    [](const auto& window) { return window->drawing(); }))
        return;
    if (!m_impl->toolbar)
        m_impl->toolbar =
            std::make_unique<ToolPanel>([this](QuickAnnotationTool tool) { arm(tool); });
    m_impl->toolbar->adjustSize();
    const QPoint cursor =
        platform::PhysicalCursor().logicalPosition().value_or(QCursor::pos()).toPoint();
    QScreen* screen = QGuiApplication::screenAt(cursor);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    QPoint position = cursor;
    if (screen) {
        const QRect bounds = screen->availableGeometry();
        position.setX(
            std::clamp(position.x(), bounds.left(),
                       std::max(bounds.left(), bounds.right() - m_impl->toolbar->width() + 1)));
        position.setY(
            std::clamp(position.y(), bounds.top(),
                       std::max(bounds.top(), bounds.bottom() - m_impl->toolbar->height() + 1)));
    }
    m_impl->toolbar->move(position);
    m_impl->toolbar->show();
}
} // namespace snow_shot::presentation
