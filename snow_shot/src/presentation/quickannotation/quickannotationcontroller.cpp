#include "snow_shot/presentation/quickannotationcontroller.h"

#include "../globalcanvas/globalcanvasplatform.h"
#include "quickannotationoptions.h"
#include "snow_shot/platform/physicalcursor.h"
#include "snow_shot/platform/screenshotnative.h"
#ifdef Q_OS_MACOS
#include "snow_shot/platform/macos/screenshotsessionfocus.h"
#endif
#include "snow_shot/presentation/screenshotcanvastoolstyles.h"
#include "snow_shot/presentation/screenshotdefaultstyles.h"
#include "snow_shot/presentation/screenshotfloatingtoolpalettewindow.h"
#include "snow_shot/presentation/screenshottoolpalette.h"
#include "snow_shot/storage/settingsadapters.h"
#include "snow_draw_engine_qt/snow_canvas_style_edit.h"
#include "snow_shot/storage/applicationstorage.h"
#include "snow_shot/storage/configurationstore.h"
#include "snow_draw_engine_qt/snow_canvas_custom_renderer.h"
#include "snow_draw_engine_qt/snow_canvas_runtime.h"
#include "snow_draw_engine_qt/snow_canvas_widget.h"

#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QWindow>
#include <QtMath>

#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>
#include <vector>

namespace snow_shot::presentation {
namespace {

SnowCanvasStyleDefaults annotationStyleDefaults(const QuickAnnotationOptions& options,
                                                QuickAnnotationTool tool) {
    auto defaults = screenshotCanvasToolStyleDefaults();
    for (auto* style : {&defaults.rectangle, &defaults.arrow, &defaults.freeDraw}) {
        style->stroke = options.color;
        style->strokeWidth = options.width;
    }
    defaults.rectangle.fillStyle = SnowCanvasFillStyle::Border;
    defaults.arrow.arrowType = SnowCanvasArrowType::Curve;
    defaults.rectangle.cornerRadii = tool == QuickAnnotationTool::RoundedRectangle
                                         ? SnowCanvasCornerRadii{8, 8, 8, 8}
                                         : SnowCanvasCornerRadii{};
    defaults.freeDraw.fill = options.color;
    return defaults;
}

ScreenshotToolPalette::Options quickPaletteOptions(const QuickAnnotationOptions& options) {
    ScreenshotToolPalette::Options result;
    result.showDragHandle = true;
    result.showSelectTool = false;
    result.showSpeechBubbleTool = false;
    result.showDistanceTool = false;
    result.showAngleTool = false;
    result.showBracketTool = false;
    result.showShapeTool = true;
    result.showArrowTool = true;
    result.showBrushTool = true;
    result.showQuickAnnotationActions = true;
    result.toolbarLayout = storage::ScreenshotToolbarSettings().layout(
        storage::ScreenshotToolbarLayoutKind::QuickAnnotationTools);
    result.styleDefaults = annotationStyleDefaults(options, QuickAnnotationTool::RoundedRectangle);
    return result;
}

class ToolPanel final : public ScreenshotFloatingToolPaletteWindow {
  public:
    explicit ToolPanel(QuickAnnotationOptions options,
                       std::function<void(QuickAnnotationTool)> choose,
                       std::function<void(QuickAnnotationTool)> selectShape,
                       std::function<void(QuickAnnotationOptions, bool)> changeOptions,
                       std::function<void(const SnowCanvasStyleEdit&)> styleEdited,
                       std::function<void(bool)> keepFading,
                       std::function<void()> exitSession, std::function<void()> undo,
                       std::function<void()> redo)
        : ScreenshotFloatingToolPaletteWindow(quickPaletteOptions(options)), m_options(options),
          m_choose(std::move(choose)), m_selectShape(std::move(selectShape)),
          m_changeOptions(std::move(changeOptions)), m_styleEdited(std::move(styleEdited)) {
        setObjectName(QStringLiteral("quickAnnotationToolbar"));
        setStyleToolbarAboveMain(true);
        auto* controls = palette();
        connect(controls, &ScreenshotToolPalette::shapeRequested, this,
                [this] { m_choose(QuickAnnotationTool::RoundedRectangle); });
        connect(controls, &ScreenshotToolPalette::arrowRequested, this,
                [this] { m_choose(QuickAnnotationTool::Arrow); });
        connect(controls, &ScreenshotToolPalette::brushRequested, this,
                [this] { m_choose(QuickAnnotationTool::Brush); });
        connect(controls, &ScreenshotToolPalette::quickAnnotationPersistentChanged, this,
                [this](bool enabled) {
                    m_options.persistent = enabled;
                    m_changeOptions(m_options, true);
                });
        connect(controls, &ScreenshotToolPalette::quickAnnotationKeepFadingChanged, this,
                [keepFading = std::move(keepFading)](bool enabled) { keepFading(enabled); });
        connect(controls, &ScreenshotToolPalette::quickAnnotationExitRequested, this,
                [exitSession = std::move(exitSession)] { exitSession(); });
        connect(controls, &ScreenshotToolPalette::undoRequested, this, std::move(undo));
        connect(controls, &ScreenshotToolPalette::redoRequested, this, std::move(redo));
        connect(this, &ScreenshotFloatingToolPaletteWindow::dragFinished, this, [this] {
            m_moved = true;
            m_placementScreen = QGuiApplication::screenAt(contentPosition());
        });
        controls->setStyleEditHandler([this](const SnowCanvasStyleEdit& edit) {
            if (!m_canvas || !m_canvas->commitStyleEdit(edit))
                return false;
            palette()->rememberStyleEdit(edit);
            m_styleEdited(edit);
            if (const auto color = snowCanvasCreationColorEdit(edit); color && color->isValid())
                m_options.color = *color;
            if (const auto* shape = std::get_if<SnowCanvasShapeEdit>(&edit)) {
                if (shape->properties & SnowCanvasShapeStylePropertyStrokeWidth)
                    m_options.width = std::clamp(qRound(shape->style.strokeWidth), 1, 20);
                if (shape->kind == SnowCanvasShapeKind::Rectangle &&
                    (shape->properties & SnowCanvasShapeStylePropertyShape)) {
                    m_selectShape(shape->style.shape == SnowCanvasRectangleShape::Ellipse
                                      ? QuickAnnotationTool::Ellipse
                                      : QuickAnnotationTool::RoundedRectangle);
                }
            }
            m_changeOptions(m_options, false);
            return true;
        });
        setOptions(options);
#ifdef Q_OS_MACOS
        platform::configureGlobalCanvasToolbarWindow(this);
#endif
    }
    void setOptions(const QuickAnnotationOptions& options) {
        m_options = options;
        const QSignalBlocker blocker(palette());
        palette()->setQuickAnnotationPersistent(options.persistent);
    }
    void setKeepFading(bool enabled) {
        const QSignalBlocker blocker(palette());
        palette()->setQuickAnnotationKeepFading(enabled);
    }
    void setCanvas(SnowCanvasWidget* canvas, QuickAnnotationTool tool,
                   const SnowCanvasStyleDefaults& defaults) {
        disconnect(m_styleConnection);
        m_canvas = canvas;
        palette()->setCreationStyleDefaults(defaults);
        const auto active = tool == QuickAnnotationTool::Arrow ? ScreenshotToolPalette::Tool::Arrow
                            : tool == QuickAnnotationTool::Brush
                                ? ScreenshotToolPalette::Tool::Brush
                                : ScreenshotToolPalette::Tool::Shape;
        palette()->setActiveTool(active);
        if (canvas) {
            palette()->setStyleToolbarState(canvas->canvasStyleToolbarState());
            m_styleConnection =
                connect(canvas, &SnowCanvasWidget::styleToolbarStateChanged, this, [this] {
                    if (m_canvas)
                        palette()->setStyleToolbarState(m_canvas->canvasStyleToolbarState());
                });
        }
    }
    void place(QScreen* screen) {
        if (m_moved && m_placementScreen)
            return;
        if (!screen)
            return;
        const QRect bounds = screen->availableGeometry();
        setPlacementContext(screen, bounds);
        const QSize size = contentSizeHint();
        moveContentTo(
            QPoint(bounds.right() - size.width() + 1, bounds.bottom() - size.height() + 1));
        m_placementScreen = screen;
    }

  private:
    QuickAnnotationOptions m_options;
    std::function<void(QuickAnnotationTool)> m_choose;
    std::function<void(QuickAnnotationTool)> m_selectShape;
    std::function<void(QuickAnnotationOptions, bool)> m_changeOptions;
    std::function<void(const SnowCanvasStyleEdit&)> m_styleEdited;
    QPointer<SnowCanvasWidget> m_canvas;
    QMetaObject::Connection m_styleConnection;
    QPointer<QScreen> m_placementScreen;
    bool m_moved = false;
};

class GestureCanvas final : public SnowCanvasWidget {
  public:
    using SnowCanvasWidget::SnowCanvasWidget;
    std::function<void()> pressed;
    std::function<void()> released;
    std::function<void()> cancelled;
    std::function<bool()> ignoreModifiers;

  protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) {
            event->accept();
            return;
        }
        if (pressed)
            pressed();
        dispatchMouse(event, [this](QMouseEvent* e) { SnowCanvasWidget::mousePressEvent(e); });
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        dispatchMouse(event, [this](QMouseEvent* e) { SnowCanvasWidget::mouseMoveEvent(e); });
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) {
            event->accept();
            return;
        }
        dispatchMouse(event, [this](QMouseEvent* e) { SnowCanvasWidget::mouseReleaseEvent(e); });
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

  private:
    void dispatchMouse(QMouseEvent* event, const std::function<void(QMouseEvent*)>& handler) {
        if (!ignoreModifiers || !ignoreModifiers()) {
            handler(event);
            return;
        }
        QMouseEvent unmodified(event->type(), event->position(), event->globalPosition(),
                               event->button(), event->buttons(), Qt::NoModifier);
        handler(&unmodified);
        event->setAccepted(unmodified.isAccepted());
    }
};

class AnnotationWindow final : public QWidget, public SnowCanvasCustomRenderer {
  public:
    explicit AnnotationWindow(QScreen* screen, std::function<void()> onError,
                              std::function<qint64()> clock,
                              std::function<bool(QWidget*, bool)> setInputTransparent,
                              std::function<void(bool)> onComplete, std::function<void()> onExit,
                              std::function<bool()> ignoreModifiers,
                              std::function<void(AnnotationWindow*)> onPersistentMark,
                              std::function<void(AnnotationWindow*)> onPress)
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
          m_screen(screen), m_runtime(SnowCanvasRuntimeConfig{screenshotCanvasToolStyleDefaults()}),
          m_canvas(m_runtime, this), m_onError(std::move(onError)), m_now(std::move(clock)),
          m_setInputTransparent(std::move(setInputTransparent)),
          m_onComplete(std::move(onComplete)), m_onExit(std::move(onExit)),
          m_onPersistentMark(std::move(onPersistentMark)), m_onPress(std::move(onPress)) {
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
        m_canvas.pressed = [this] { m_onPress(this); m_state.begin(); };
        m_canvas.released = [this] { completeGesture(); };
        m_canvas.cancelled = [this] { m_onExit(); };
        m_canvas.ignoreModifiers = std::move(ignoreModifiers);
        m_clock.start();
        if (!m_now)
            m_now = [this] { return m_clock.elapsed(); };
        m_timer.setParent(this);
        m_timer.setObjectName(QStringLiteral("quickAnnotationFadeTimer"));
        m_timer.setInterval(33);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            const auto now = m_now();
            std::erase_if(m_marks, [now](const Mark& mark) {
                return !mark.persistent && QuickAnnotationState::opacity(
                                               now - mark.created, mark.holdMs, mark.fadeMs) == 0.0;
            });
            ++m_revision;
            m_canvas.update();
            if (std::none_of(m_marks.begin(), m_marks.end(),
                             [](const Mark& mark) { return !mark.persistent; }))
                m_timer.stop();
            if (m_marks.empty() && m_state.phase() == QuickAnnotationState::Phase::Idle)
                hide();
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
    [[nodiscard]] SnowCanvasWidget* canvas() {
        return &m_canvas;
    }
    [[nodiscard]] bool drawing() const {
        return m_state.phase() == QuickAnnotationState::Phase::Drawing;
    }

    bool arm(QuickAnnotationTool tool, const QuickAnnotationOptions& options,
             const SnowCanvasStyleDefaults& defaults, bool focus) {
        if (!m_state.arm(tool))
            return false;
        m_canvas.clearDocument();
        m_holdMs = options.holdMs;
        m_fadeMs = options.fadeMs;
        m_persistent = options.persistent;
        applyScreenshotCanvasToolStyles(m_canvas, defaults);
        const auto canvasTool = tool == QuickAnnotationTool::Brush   ? SnowCanvasTool::FreeDraw
                                : tool == QuickAnnotationTool::Arrow ? SnowCanvasTool::Arrow
                                                                     : SnowCanvasTool::Shape;
        m_canvas.setCanvasTool(canvasTool);
        updateGeometryForScreen();
#ifdef Q_OS_MACOS
        if (focus)
            m_focus->prepare();
#endif
        show();
        raise();
        if (!m_setInputTransparent(this, false)) {
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
        if (focus) {
            activateWindow();
            m_canvas.setFocus(Qt::OtherFocusReason);
#ifdef Q_OS_MACOS
            m_focus->begin(true);
#endif
        }
        return true;
    }

    void fadeMarks() {
        const auto now = m_now();
        for (auto& mark : m_marks) {
            mark.persistent = false;
            mark.created = now;
            mark.holdMs = 0;
        }
        if (!m_marks.empty())
            m_timer.start();
        m_redoMarks.clear();
    }

    bool undoPersistentMark() {
        const auto it = std::find_if(m_marks.rbegin(), m_marks.rend(),
                                     [](const auto& mark) { return mark.persistent; });
        if (it == m_marks.rend())
            return false;
        const auto forward = std::next(it).base();
        m_redoMarks.push_back(std::move(*forward));
        m_marks.erase(forward);
        ++m_revision;
        m_canvas.update();
        if (m_marks.empty() && m_state.phase() == QuickAnnotationState::Phase::Idle)
            hide();
        return true;
    }
    bool redoPersistentMark() {
        if (m_redoMarks.empty())
            return false;
        m_marks.push_back(std::move(m_redoMarks.back()));
        m_redoMarks.pop_back();
        ++m_revision;
        show();
        m_canvas.update();
        return true;
    }
    void clearRedoMarks() {
        m_redoMarks.clear();
    }

    void cancel() {
        if (m_state.phase() == QuickAnnotationState::Phase::Idle)
            return;
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
                painter.setOpacity((mark.persistent
                                        ? 1.0
                                        : QuickAnnotationState::opacity(now - mark.created,
                                                                        mark.holdMs, mark.fadeMs)));
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
                if (!m_setInputTransparent(this, transparent)) {
                    cancel();
                    hide();
                    m_marks.clear();
                    m_timer.stop();
                    m_onError();
                }
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
        int holdMs;
        int fadeMs;
        bool persistent;
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
            m_onComplete(false);
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
                    const QImage markImage = image.copy(bounds);
                    const QRectF markRect(left / dpr, top / dpr, bounds.width() / dpr,
                                          bounds.height() / dpr);
                    m_marks.push_back(
                        {markImage, markRect, m_now(), m_holdMs, m_fadeMs, m_persistent});
                    if (m_persistent)
                        m_onPersistentMark(this);
                    if (!m_persistent)
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
        m_onComplete(true);
    }

    bool releaseInput(bool keepVisible = false) {
#ifdef Q_OS_MACOS
        m_focus->beforeHide(true);
#endif
        if (!m_setInputTransparent(this, true)) {
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
    int m_holdMs = 300;
    int m_fadeMs = 500;
    bool m_persistent = false;
    std::function<void()> m_onError;
    std::function<qint64()> m_now;
    std::function<bool(QWidget*, bool)> m_setInputTransparent;
    std::function<void(bool)> m_onComplete;
    std::function<void()> m_onExit;
    std::function<void(AnnotationWindow*)> m_onPersistentMark;
    std::function<void(AnnotationWindow*)> m_onPress;
    QElapsedTimer m_clock;
    QTimer m_timer;
    std::vector<Mark> m_marks;
    std::vector<Mark> m_redoMarks;
    std::uint64_t m_revision = 0;
#ifdef Q_OS_MACOS
    std::unique_ptr<platform::macos::ScreenshotSessionFocus> m_focus;
#endif
};
} // namespace

class QuickAnnotationController::Impl {
  public:
    Impl(QuickAnnotationController& owner, std::function<qint64()> clock,
         std::function<bool(QWidget*, bool)> setInputTransparent,
         std::function<QScreen*()> screenAtCursor)
        : q(owner), now(std::move(clock)), input(std::move(setInputTransparent)),
          cursorScreen(std::move(screenAtCursor)) {
        if (!input)
            input = setGlobalCanvasInputTransparent;
        auto& storage = storage::ApplicationStorage::instance();
        if (storage.isInitialized()) {
            options = QuickAnnotationOptions::fromJson(
                storage.configuration()
                    .value(QStringLiteral("quick_annotation/options"))
                    .toObject());
            QObject::connect(
                &storage.configuration(), &storage::ConfigurationStore::valueChanged, &q,
                [this](const QString& key, const QJsonValue& value) {
                    if (key == QStringLiteral("quick_annotation/toolbar_layout")) {
                        if (toolbar)
                            toolbar->palette()->setToolbarLayout(
                                storage::ScreenshotToolbarSettings().layout(
                                    storage::ScreenshotToolbarLayoutKind::QuickAnnotationTools));
                        return;
                    }
                    if (key != QStringLiteral("quick_annotation/options"))
                        return;
                    const bool persistent = options.persistent;
                    options = QuickAnnotationOptions::fromJson(value.toObject());
                    options.persistent = persistent;
                    for (auto* style : {&styles.rectangle, &styles.arrow, &styles.freeDraw}) {
                        style->stroke = options.color;
                        style->strokeWidth = options.width;
                    }
                    if (toolbar)
                        toolbar->setOptions(options);
                });
            const QString saved = storage.configuration()
                                      .value(QStringLiteral("quick_annotation/last_tool"))
                                      .toString();
            if (saved == QStringLiteral("brush"))
                lastTool = QuickAnnotationTool::Brush;
            else if (saved == QStringLiteral("arrow"))
                lastTool = QuickAnnotationTool::Arrow;
            else if (saved == QStringLiteral("rectangle"))
                lastTool = QuickAnnotationTool::Rectangle;
            else if (saved == QStringLiteral("ellipse"))
                lastTool = QuickAnnotationTool::Ellipse;
        }
        options.persistent = false;
        styles = annotationStyleDefaults(options, QuickAnnotationTool::RoundedRectangle);
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

    void fadeMarks() {
        for (const auto& window : windows)
            window->fadeMarks();
        undoHistory.clear();
        redoHistory.clear();
        updateHistory();
    }
    void updateHistory() {
        std::erase_if(undoHistory, [](const auto& window) { return window.isNull(); });
        std::erase_if(redoHistory, [](const auto& window) { return window.isNull(); });
        if (toolbar)
            toolbar->palette()->setHistoryState({!undoHistory.empty(), !redoHistory.empty()});
    }
    void recordPersistentMark(AnnotationWindow* window) {
        for (const auto& candidate : windows)
            candidate->clearRedoMarks();
        redoHistory.clear();
        undoHistory.emplace_back(window);
        updateHistory();
    }
    void undo() {
        updateHistory();
        if (undoHistory.empty())
            return;
        const QPointer<AnnotationWindow> window = undoHistory.back();
        undoHistory.pop_back();
        if (window && window->undoPersistentMark())
            redoHistory.push_back(window);
        updateHistory();
    }
    void redo() {
        updateHistory();
        if (redoHistory.empty())
            return;
        const QPointer<AnnotationWindow> window = redoHistory.back();
        redoHistory.pop_back();
        if (window && window->redoPersistentMark())
            undoHistory.push_back(window);
        updateHistory();
    }
    void exitSession() {
        holdActive = false;
        holdKeyDown = false;
        keepMode = false;
        options.persistent = false;
        if (toolbar) {
            toolbar->setOptions(options);
            toolbar->setKeepFading(false);
            toolbar->hide();
        }
        for (const auto& window : windows)
            window->cancel();
        fadeMarks();
    }
    void suspendForScreenshot() {
        holdActive = false;
        holdKeyDown = false;
        if (toolbar)
            toolbar->hide();
        for (const auto& window : windows)
            window->cancel();
    }
    QuickAnnotationTool defaultTool() const {
        return options.defaultTool == QStringLiteral("brush")       ? QuickAnnotationTool::Brush
               : options.defaultTool == QStringLiteral("arrow")     ? QuickAnnotationTool::Arrow
               : options.defaultTool == QStringLiteral("rectangle") ? QuickAnnotationTool::Rectangle
               : options.defaultTool == QStringLiteral("ellipse")
                   ? QuickAnnotationTool::Ellipse
                   : QuickAnnotationTool::RoundedRectangle;
    }
    void selectToolShape(QuickAnnotationTool tool) {
        if (tool == QuickAnnotationTool::Ellipse) {
            styles.rectangle.shape = SnowCanvasRectangleShape::Ellipse;
        } else if (tool == QuickAnnotationTool::Rectangle ||
                   tool == QuickAnnotationTool::RoundedRectangle) {
            styles.rectangle.shape = SnowCanvasRectangleShape::Rectangle;
            if (tool == QuickAnnotationTool::Rectangle)
                styles.rectangle.cornerRadii = {};
            else if (styles.rectangle.cornerRadii.topLeft <= 0)
                styles.rectangle.cornerRadii = {8, 8, 8, 8};
        }
    }
    void arm(QuickAnnotationTool tool) {
        for (const auto& window : windows) {
            if (window->drawing())
                return;
            window->cancel();
        }
        const QPoint position =
            platform::PhysicalCursor().logicalPosition().value_or(QCursor::pos()).toPoint();
        QScreen* screen = cursorScreen ? cursorScreen() : QGuiApplication::screenAt(position);
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (!screen)
            return;
        const auto screens = QGuiApplication::screens();
        for (QScreen* target : screens) {
        auto it = std::find_if(windows.begin(), windows.end(),
                               [target](const auto& window) { return window->screen() == target; });
        if (it == windows.end()) {
            windows.push_back(std::make_unique<AnnotationWindow>(
                target,
                [this] {
                    emit q.errorOccurred(
                        QuickAnnotationController::tr("Could not change canvas click-through"));
                },
                now, input,
                [this](bool success) {
                    if (!success) {
                        exitSession();
                        return;
                    }
                    if (keepMode || holdActive) {
                        arm(lastTool);
                        if (toolbar)
                            toolbar->show();
                    } else {
                        for (const auto& window : windows)
                            window->cancel();
                        if (toolbar)
                            toolbar->hide();
                    }
                },
                [this] { exitSession(); },
                [this] { return holdActive; },
                [this](AnnotationWindow* window) { recordPersistentMark(window); },
                [this](AnnotationWindow* window) {
                    if (toolbar)
                        toolbar->setCanvas(window->canvas(), lastTool, styles);
                }));
            it = std::prev(windows.end());
        }
        if ((*it)->arm(tool, options, styles, target == screen)) {
            lastTool = tool;
            if (toolbar && target == screen)
                toolbar->setCanvas((*it)->canvas(), tool, styles);
            auto& storage = storage::ApplicationStorage::instance();
            if (storage.isInitialized()) {
                const QString value =
                    tool == QuickAnnotationTool::Arrow       ? QStringLiteral("arrow")
                    : tool == QuickAnnotationTool::Rectangle ? QStringLiteral("rectangle")
                    : tool == QuickAnnotationTool::Ellipse   ? QStringLiteral("ellipse")
                    : tool == QuickAnnotationTool::RoundedRectangle
                        ? QStringLiteral("rounded_rectangle")
                        : QStringLiteral("brush");
                storage.configuration().setValue(QStringLiteral("quick_annotation/last_tool"),
                                                 value);
            }
        }
        }
    }

    QuickAnnotationController& q;
    std::function<qint64()> now;
    std::function<bool(QWidget*, bool)> input;
    std::function<QScreen*()> cursorScreen;
    QuickAnnotationTool lastTool = QuickAnnotationTool::RoundedRectangle;
    QuickAnnotationOptions options;
    SnowCanvasStyleDefaults styles;
    bool keepMode = false;
    bool holdActive = false;
    bool holdKeyDown = false;
    std::vector<std::unique_ptr<AnnotationWindow>> windows;
    std::vector<QPointer<AnnotationWindow>> undoHistory;
    std::vector<QPointer<AnnotationWindow>> redoHistory;
    std::unique_ptr<ToolPanel> toolbar;
};

QuickAnnotationController::QuickAnnotationController(QObject* parent)
    : QuickAnnotationController(parent, {}) {}
QuickAnnotationController::QuickAnnotationController(QObject* parent, std::function<qint64()> clock)
    : QuickAnnotationController(parent, std::move(clock), {}) {}
QuickAnnotationController::QuickAnnotationController(
    QObject* parent, std::function<qint64()> clock,
    std::function<bool(QWidget*, bool)> setInputTransparent)
    : QuickAnnotationController(parent, std::move(clock), std::move(setInputTransparent), {}) {}
QuickAnnotationController::QuickAnnotationController(
    QObject* parent, std::function<qint64()> clock,
    std::function<bool(QWidget*, bool)> setInputTransparent,
    std::function<QScreen*()> screenAtCursor)
    : QObject(parent),
      m_impl(std::make_unique<Impl>(*this, std::move(clock), std::move(setInputTransparent),
                                    std::move(screenAtCursor))) {}
QuickAnnotationController::~QuickAnnotationController() = default;
void QuickAnnotationController::arm(QuickAnnotationTool tool) {
    showToolbar(false);
    m_impl->selectToolShape(tool);
    m_impl->arm(tool);
}
void QuickAnnotationController::armLastTool() {
    showToolbar();
}
void QuickAnnotationController::beginHold() {
    m_impl->holdActive = true;
    m_impl->holdKeyDown = true;
    showToolbar();
}
void QuickAnnotationController::beginHold(QuickAnnotationTool tool) {
    m_impl->holdActive = true;
    m_impl->holdKeyDown = true;
    showToolbar(false);
    m_impl->selectToolShape(tool);
    m_impl->arm(tool);
}
void QuickAnnotationController::endHold() {
    m_impl->holdKeyDown = false;
    m_impl->holdActive = false;
    if (!m_impl->keepMode)
        m_impl->exitSession();
}
void QuickAnnotationController::exitMode() {
    m_impl->exitSession();
}
void QuickAnnotationController::suspendForScreenshot() {
    m_impl->suspendForScreenshot();
}
void QuickAnnotationController::showToolbar(bool armDefault) {
    if (std::any_of(m_impl->windows.begin(), m_impl->windows.end(),
                    [](const auto& window) { return window->drawing(); }))
        return;
    if (!m_impl->toolbar)
        m_impl->toolbar = std::make_unique<ToolPanel>(
            m_impl->options, [this](QuickAnnotationTool tool) { m_impl->arm(tool); },
            [this](QuickAnnotationTool tool) { m_impl->lastTool = tool; },
            [this](QuickAnnotationOptions options, bool rearm) {
                const bool endingPersistent = m_impl->options.persistent && !options.persistent;
                m_impl->options = options;
                m_impl->keepMode = options.persistent;
                if (options.persistent && m_impl->toolbar)
                    m_impl->toolbar->setKeepFading(false);
                if (endingPersistent)
                    m_impl->fadeMarks();
                auto& storage = storage::ApplicationStorage::instance();
                if (storage.isInitialized()) {
                    auto saved = options;
                    saved.persistent = false;
                    storage.configuration().setValue(QStringLiteral("quick_annotation/options"),
                                                     saved.toJson());
                }
                if (endingPersistent && !m_impl->holdKeyDown) {
                    m_impl->exitSession();
                    return;
                }
                if (rearm)
                    m_impl->arm(m_impl->lastTool);
            },
            [this](const SnowCanvasStyleEdit& edit) {
                snowCanvasMergeStyleEdit(m_impl->styles, edit);
                for (const auto& window : m_impl->windows)
                    window->canvas()->commitStyleEdit(edit);
            },
            [this](bool enabled) {
                const bool wasPersistent = m_impl->options.persistent;
                m_impl->keepMode = enabled;
                m_impl->options.persistent = false;
                if (m_impl->toolbar)
                    m_impl->toolbar->setOptions(m_impl->options);
                if (wasPersistent)
                    m_impl->fadeMarks();
                if (!enabled && !m_impl->holdKeyDown) {
                    m_impl->exitSession();
                    return;
                }
                m_impl->arm(m_impl->lastTool);
            },
            [this] { m_impl->exitSession(); }, [this] { m_impl->undo(); },
            [this] { m_impl->redo(); });
    m_impl->updateHistory();
    m_impl->toolbar->place(QGuiApplication::primaryScreen());
    m_impl->toolbar->show();
    if (armDefault) {
        const auto tool = m_impl->defaultTool();
        m_impl->selectToolShape(tool);
        m_impl->arm(tool);
    }
    m_impl->toolbar->place(QGuiApplication::primaryScreen());
}
} // namespace snow_shot::presentation
