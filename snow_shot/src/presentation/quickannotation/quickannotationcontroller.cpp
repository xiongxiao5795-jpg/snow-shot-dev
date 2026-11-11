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
#include <QGraphicsDropShadowEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
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

class ToolPanel final : public QWidget {
  public:
    explicit ToolPanel(QuickAnnotationOptions options,
                       std::function<void(QuickAnnotationTool)> choose,
                       std::function<void(QuickAnnotationOptions)> changeOptions,
                       std::function<void()> exitSession)
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
          m_options(options), m_choose(std::move(choose)),
          m_changeOptions(std::move(changeOptions)), m_exitSession(std::move(exitSession)) {
        setObjectName(QStringLiteral("quickAnnotationToolbar"));
        auto* layout = new QHBoxLayout(this);
        const auto add = [this, layout](QToolButton*& button, QuickAnnotationTool tool) {
            button = new QToolButton(this);
            layout->addWidget(button);
            connect(button, &QToolButton::clicked, this, [this, tool] { m_choose(tool); });
        };
        add(m_brush, QuickAnnotationTool::Brush);
        add(m_arrow, QuickAnnotationTool::Arrow);
        add(m_rectangle, QuickAnnotationTool::Rectangle);
        add(m_roundedRectangle, QuickAnnotationTool::RoundedRectangle);
        add(m_ellipse, QuickAnnotationTool::Ellipse);
        m_color = new QComboBox(this);
        m_color->setObjectName(QStringLiteral("quickAnnotationColor"));
        for (const auto* color : {"#f5222d", "#faad14", "#52c41a", "#1890ff", "#000000", "#ffffff"})
            m_color->addItem(QString(), QString::fromLatin1(color));
        int colorIndex = m_color->findData(m_options.color.name());
        if (colorIndex < 0) {
            m_color->addItem(m_options.color.name().toUpper(), m_options.color.name());
            colorIndex = m_color->count() - 1;
        }
        m_color->setCurrentIndex(colorIndex);
        layout->addWidget(m_color);
        m_width = new QSpinBox(this);
        m_width->setObjectName(QStringLiteral("quickAnnotationWidth"));
        m_width->setRange(1, 20);
        m_width->setValue(m_options.width);
        layout->addWidget(m_width);
        m_hold = new QDoubleSpinBox(this);
        m_hold->setObjectName(QStringLiteral("quickAnnotationHoldSeconds"));
        m_hold->setDecimals(1);
        m_hold->setRange(0.5, 30.0);
        m_hold->setSingleStep(0.5);
        m_hold->setValue(m_options.holdMs / 1000.0);
        layout->addWidget(m_hold);
        connect(m_color, &QComboBox::currentIndexChanged, this, [this](int) {
            m_options.color = QColor(m_color->currentData().toString());
            m_changeOptions(m_options);
        });
        connect(m_width, &QSpinBox::valueChanged, this, [this](int value) {
            m_options.width = value;
            m_changeOptions(m_options);
        });
        connect(m_hold, &QDoubleSpinBox::valueChanged, this, [this](double value) {
            m_options.holdMs = qRound(value * 1000.0);
            m_changeOptions(m_options);
        });
        m_persistent = new QToolButton(this);
        m_persistent->setObjectName(QStringLiteral("quickAnnotationPersistent"));
        m_persistent->setCheckable(true);
        m_persistent->setChecked(m_options.persistent);
        layout->addWidget(m_persistent);
        connect(m_persistent, &QToolButton::toggled, this, [this](bool enabled) {
            m_options.persistent = enabled;
            m_changeOptions(m_options);
        });
        m_close = new QToolButton(this);
        m_close->setObjectName(QStringLiteral("quickAnnotationClose"));
        layout->addWidget(m_close);
        connect(m_close, &QToolButton::clicked, this, [this] { m_exitSession(); });
        m_color->setVisible(m_options.showColor);
        m_width->setVisible(m_options.showWidth);
        m_hold->setVisible(m_options.showHold);
#ifdef Q_OS_MACOS
        platform::configureGlobalCanvasToolbarWindow(this);
#endif
        retranslate();
    }

    void setOptions(const QuickAnnotationOptions& options) {
        m_options = options;
        const QSignalBlocker colorBlocker(m_color), widthBlocker(m_width), holdBlocker(m_hold),
            persistentBlocker(m_persistent);
        int colorIndex = m_color->findData(options.color.name());
        if (colorIndex < 0) {
            m_color->addItem(options.color.name().toUpper(), options.color.name());
            colorIndex = m_color->count() - 1;
        }
        m_color->setCurrentIndex(colorIndex);
        m_width->setValue(options.width);
        m_hold->setValue(options.holdMs / 1000.0);
        m_persistent->setChecked(options.persistent);
        m_color->setVisible(options.showColor);
        m_width->setVisible(options.showWidth);
        m_hold->setVisible(options.showHold);
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
        m_roundedRectangle->setText(QCoreApplication::translate(
            "snow_shot::presentation::QuickAnnotationController", "Rounded rectangle"));
        m_persistent->setText(QCoreApplication::translate(
            "snow_shot::presentation::QuickAnnotationController", "Keep marks"));
        m_close->setText(QCoreApplication::translate(
            "snow_shot::presentation::QuickAnnotationController", "Close"));
        m_ellipse->setText(QCoreApplication::translate(
            "snow_shot::presentation::QuickAnnotationController", "Ellipse"));
        m_color->setItemText(0, QCoreApplication::translate(
                                    "snow_shot::presentation::QuickAnnotationController", "Red"));
        m_color->setItemText(
            1, QCoreApplication::translate("snow_shot::presentation::QuickAnnotationController",
                                           "Orange"));
        m_color->setItemText(2, QCoreApplication::translate(
                                    "snow_shot::presentation::QuickAnnotationController", "Green"));
        m_color->setItemText(3, QCoreApplication::translate(
                                    "snow_shot::presentation::QuickAnnotationController", "Blue"));
        m_color->setItemText(4, QCoreApplication::translate(
                                    "snow_shot::presentation::QuickAnnotationController", "Black"));
        m_color->setItemText(5, QCoreApplication::translate(
                                    "snow_shot::presentation::QuickAnnotationController", "White"));
        m_color->setAccessibleName(QCoreApplication::translate(
            "snow_shot::presentation::QuickAnnotationController", "Stroke color"));
        m_color->setToolTip(m_color->accessibleName());
        m_width->setAccessibleName(QCoreApplication::translate(
            "snow_shot::presentation::QuickAnnotationController", "Stroke width"));
        m_width->setToolTip(m_width->accessibleName());
        m_hold->setAccessibleName(QCoreApplication::translate(
            "snow_shot::presentation::QuickAnnotationController", "Hold time in seconds"));
        m_hold->setToolTip(m_hold->accessibleName());
        m_hold->setSuffix(QCoreApplication::translate(
            "snow_shot::presentation::QuickAnnotationController", " s"));
    }

    QuickAnnotationOptions m_options;
    std::function<void(QuickAnnotationTool)> m_choose;
    std::function<void(QuickAnnotationOptions)> m_changeOptions;
    std::function<void()> m_exitSession;
    QToolButton* m_brush = nullptr;
    QToolButton* m_arrow = nullptr;
    QToolButton* m_rectangle = nullptr;
    QToolButton* m_roundedRectangle = nullptr;
    QToolButton* m_persistent = nullptr;
    QToolButton* m_close = nullptr;
    QToolButton* m_ellipse = nullptr;
    QComboBox* m_color = nullptr;
    QSpinBox* m_width = nullptr;
    QDoubleSpinBox* m_hold = nullptr;
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
                              std::function<qint64()> clock,
                              std::function<bool(QWidget*, bool)> setInputTransparent,
                              std::function<void()> onComplete, std::function<void()> onExit,
                              std::function<bool()> toolbarActive)
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
          m_screen(screen), m_runtime(SnowCanvasRuntimeConfig{screenshotCanvasToolStyleDefaults()}),
          m_canvas(m_runtime, this), m_onError(std::move(onError)), m_now(std::move(clock)),
          m_setInputTransparent(std::move(setInputTransparent)),
          m_onComplete(std::move(onComplete)), m_onExit(std::move(onExit)),
          m_toolbarActive(std::move(toolbarActive)) {
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
        m_canvas.cancelled = [this] { m_onExit(); };
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
    [[nodiscard]] bool drawing() const {
        return m_state.phase() == QuickAnnotationState::Phase::Drawing;
    }

    bool arm(QuickAnnotationTool tool, const QuickAnnotationOptions& options) {
        if (!m_state.arm(tool))
            return false;
        m_canvas.clearDocument();
        m_holdMs = options.holdMs;
        m_fadeMs = options.fadeMs;
        m_shadowRadius = options.shadowRadius;
        m_persistent = options.persistent;
        auto defaults = screenshotCanvasToolStyleDefaults();
        for (auto* style : {&defaults.rectangle, &defaults.arrow, &defaults.freeDraw}) {
            style->stroke = options.color;
            style->strokeWidth = options.width;
        }
        defaults.rectangle.fillStyle = SnowCanvasFillStyle::Border;
        defaults.rectangle.cornerRadii = tool == QuickAnnotationTool::RoundedRectangle
                                             ? SnowCanvasCornerRadii{8, 8, 8, 8}
                                             : SnowCanvasCornerRadii{};
        defaults.freeDraw.fill = options.color;
        applyScreenshotCanvasToolStyles(m_canvas, defaults);
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
        activateWindow();
        m_canvas.setFocus(Qt::OtherFocusReason);
#ifdef Q_OS_MACOS
        m_focus->begin(true);
#endif
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
        if (event->type() == QEvent::WindowDeactivate &&
            m_state.phase() != QuickAnnotationState::Phase::Idle) {
            QTimer::singleShot(0, this, [this] {
                if (!isActiveWindow() && !m_toolbarActive() &&
                    m_state.phase() != QuickAnnotationState::Phase::Idle)
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
            m_onComplete();
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
                    QImage markImage = image.copy(bounds);
                    QRectF markRect(left / dpr, top / dpr, bounds.width() / dpr,
                                    bounds.height() / dpr);
                    if (m_shadowRadius > 0) {
                        const int margin = qCeil(m_shadowRadius * dpr * 2);
                        QGraphicsScene scene;
                        QPixmap pixmap = QPixmap::fromImage(markImage);
                        pixmap.setDevicePixelRatio(1);
                        auto* item = scene.addPixmap(pixmap);
                        auto* shadow = new QGraphicsDropShadowEffect;
                        shadow->setBlurRadius(m_shadowRadius * dpr);
                        shadow->setOffset(0, 0);
                        shadow->setColor(QColor(0, 0, 0, 180));
                        item->setGraphicsEffect(shadow);
                        const QRectF source(-margin, -margin, bounds.width() + margin * 2,
                                            bounds.height() + margin * 2);
                        QImage withShadow(source.size().toSize(),
                                          QImage::Format_ARGB32_Premultiplied);
                        withShadow.fill(Qt::transparent);
                        QPainter shadowPainter(&withShadow);
                        scene.render(&shadowPainter, QRectF(withShadow.rect()), source);
                        shadowPainter.end();
                        markImage = withShadow;
                        markRect = QRectF((left - margin) / dpr, (top - margin) / dpr,
                                          withShadow.width() / dpr, withShadow.height() / dpr);
                    }
                    m_marks.push_back(
                        {markImage, markRect, m_now(), m_holdMs, m_fadeMs, m_persistent});
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
        m_onComplete();
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
    int m_holdMs = 1500;
    int m_fadeMs = 600;
    int m_shadowRadius = 0;
    bool m_persistent = false;
    std::function<void()> m_onError;
    std::function<qint64()> m_now;
    std::function<bool(QWidget*, bool)> m_setInputTransparent;
    std::function<void()> m_onComplete;
    std::function<void()> m_onExit;
    std::function<bool()> m_toolbarActive;
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
    Impl(QuickAnnotationController& owner, std::function<qint64()> clock,
         std::function<bool(QWidget*, bool)> setInputTransparent)
        : q(owner), now(std::move(clock)), input(std::move(setInputTransparent)) {
        if (!input)
            input = setGlobalCanvasInputTransparent;
        auto& storage = storage::ApplicationStorage::instance();
        if (storage.isInitialized()) {
            options = QuickAnnotationOptions::fromJson(
                storage.configuration()
                    .value(QStringLiteral("quick_annotation/options"))
                    .toObject());
            QObject::connect(&storage.configuration(), &storage::ConfigurationStore::valueChanged,
                             &q, [this](const QString& key, const QJsonValue& value) {
                                 if (key != QStringLiteral("quick_annotation/options"))
                                     return;
                                 options = QuickAnnotationOptions::fromJson(value.toObject());
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
    }
    void exitSession() {
        holdActive = false;
        holdKeyDown = false;
        if (toolbar)
            toolbar->hide();
        for (const auto& window : windows)
            window->cancel();
        fadeMarks();
    }
    QuickAnnotationTool defaultTool() const {
        return options.defaultTool == QStringLiteral("brush")       ? QuickAnnotationTool::Brush
               : options.defaultTool == QStringLiteral("arrow")     ? QuickAnnotationTool::Arrow
               : options.defaultTool == QStringLiteral("rectangle") ? QuickAnnotationTool::Rectangle
               : options.defaultTool == QStringLiteral("ellipse")
                   ? QuickAnnotationTool::Ellipse
                   : QuickAnnotationTool::RoundedRectangle;
    }
    void arm(QuickAnnotationTool tool) {
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
                now, input,
                [this] {
                    if (toolbar)
                        toolbar->hide();
                    if (holdActive)
                        arm(lastTool);
                },
                [this] { exitSession(); },
                [this] {
                    QWidget* active = QApplication::activeWindow();
                    return toolbar && active &&
                           (active == toolbar.get() || toolbar->isAncestorOf(active));
                }));
            it = std::prev(windows.end());
        }
        if ((*it)->arm(tool, options)) {
            lastTool = tool;
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

    QuickAnnotationController& q;
    std::function<qint64()> now;
    std::function<bool(QWidget*, bool)> input;
    QuickAnnotationTool lastTool = QuickAnnotationTool::RoundedRectangle;
    QuickAnnotationOptions options;
    bool holdActive = false;
    bool holdKeyDown = false;
    std::vector<std::unique_ptr<AnnotationWindow>> windows;
    std::unique_ptr<ToolPanel> toolbar;
};

QuickAnnotationController::QuickAnnotationController(QObject* parent)
    : QuickAnnotationController(parent, {}) {}
QuickAnnotationController::QuickAnnotationController(QObject* parent, std::function<qint64()> clock)
    : QuickAnnotationController(parent, std::move(clock), {}) {}
QuickAnnotationController::QuickAnnotationController(
    QObject* parent, std::function<qint64()> clock,
    std::function<bool(QWidget*, bool)> setInputTransparent)
    : QObject(parent),
      m_impl(std::make_unique<Impl>(*this, std::move(clock), std::move(setInputTransparent))) {}
QuickAnnotationController::~QuickAnnotationController() = default;
void QuickAnnotationController::arm(QuickAnnotationTool tool) {
    showToolbar(false);
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
void QuickAnnotationController::endHold() {
    m_impl->holdKeyDown = false;
    if (!m_impl->options.persistent)
        m_impl->exitSession();
}
void QuickAnnotationController::exitMode() {
    m_impl->exitSession();
}
void QuickAnnotationController::showToolbar(bool armDefault) {
    if (std::any_of(m_impl->windows.begin(), m_impl->windows.end(),
                    [](const auto& window) { return window->drawing(); }))
        return;
    if (!m_impl->toolbar)
        m_impl->toolbar = std::make_unique<ToolPanel>(
            m_impl->options, [this](QuickAnnotationTool tool) { m_impl->arm(tool); },
            [this](QuickAnnotationOptions options) {
                const bool endingPersistent = m_impl->options.persistent && !options.persistent;
                m_impl->options = options;
                if (endingPersistent)
                    m_impl->fadeMarks();
                auto& storage = storage::ApplicationStorage::instance();
                if (storage.isInitialized())
                    storage.configuration().setValue(QStringLiteral("quick_annotation/options"),
                                                     options.toJson());
                if (endingPersistent && m_impl->holdActive && !m_impl->holdKeyDown) {
                    m_impl->exitSession();
                    return;
                }
                m_impl->arm(m_impl->lastTool);
            },
            [this] { m_impl->exitSession(); });
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
    if (armDefault)
        m_impl->arm(m_impl->defaultTool());
}
} // namespace snow_shot::presentation
