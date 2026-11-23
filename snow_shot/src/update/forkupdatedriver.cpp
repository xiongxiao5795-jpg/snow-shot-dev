#include "macosupdatedriver_p.h"

#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

namespace snow_shot::update::detail {
namespace {
class ForkDriver final : public MacOSUpdateDriver {
    const QString resources = QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/fork-update/");
    QTemporaryDir temp;
    QProcess process;
    QTimer timer;
    UpdateStatus status;
    QString mode;
    bool checking = false;
    void publish(UpdateState state) {
        status.state = state;
        if (changed)
            changed(status, true, false, false);
    }
    void fail() {
        status.error = QCoreApplication::translate("UpdateService", "Could not update the application. Please try again");
        publish(UpdateState::Failed);
    }
    void run(const QString& command) {
        process.start(QStringLiteral("/bin/bash"), {resources + QStringLiteral("update.sh"), command, temp.path()});
    }
  public:
    ForkDriver() {
        QObject::connect(&timer, &QTimer::timeout, &timer, [this] { check(false); });
        QObject::connect(&process, &QProcess::errorOccurred, &process, [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) fail();
        });
        QObject::connect(&process, &QProcess::finished, &process, [this](int code, QProcess::ExitStatus exit) {
            if (code || exit != QProcess::NormalExit) {
                fail();
            } else if (checking) {
                QFile file(temp.filePath(QStringLiteral("manifest")));
                if (!file.open(QIODevice::ReadOnly)) { fail(); return; }
                const auto fields = file.readAll().trimmed().split(' ');
                if (fields.size() != 4 || fields[0].toLongLong() <= 0) { fail(); return; }
                status.version = QString::fromLatin1(fields[2]);
                if (fields[0].toLongLong() <= SNOW_FORK_BUILD) {
                    publish(UpdateState::Idle);
                } else {
                    publish(UpdateState::Available);
                    if (available) available(status.version);
                    if (mode == u"next_launch") download();
                }
            } else {
                publish(UpdateState::Ready);
            }
            if (finished) finished(QStringLiteral("check"), code ? QStringLiteral("failed") : QStringLiteral("success"));
        });
    }
    void start(const QString& value) override {
        publish(UpdateState::Idle);
        setMode(value);
        if (mode != u"manual") QTimer::singleShot(30000, &timer, [this] { check(false); });
    }
    void setMode(const QString& value) override {
        mode = value;
        if (mode == u"manual") timer.stop();
        else timer.start(std::chrono::hours(24));
    }
    void check(bool manual) override {
        if ((!manual && mode == u"manual") || process.state() != QProcess::NotRunning || status.state == UpdateState::Applying) return;
        checking = true;
        status.error.clear();
        publish(UpdateState::Checking);
        run(QStringLiteral("check"));
    }
    void download() override {
        if (status.state != UpdateState::Available) return;
        checking = false;
        publish(UpdateState::Downloading);
        run(QStringLiteral("download"));
    }
    void install() override {
        if (status.state != UpdateState::Ready) return;
        const QString installer = temp.filePath(QStringLiteral("installer.sh"));
        QFile::remove(installer);
        if (!QFile::copy(resources + QStringLiteral("install-snow-shot-macos.sh"), installer)) { fail(); return; }
        const QString script = QStringLiteral("cd \"$1\" || exit 1; /bin/bash ./installer.sh --dmg \"$1/update.dmg\" >install.log 2>&1 && /bin/rm -rf \"$1\"");
        if (!QProcess::startDetached(QStringLiteral("/bin/bash"), {QStringLiteral("-c"),script,QStringLiteral("snow-shot-update"),temp.path()})) { fail(); return; }
        temp.setAutoRemove(false);
        publish(UpdateState::Applying);
        if (committed) committed();
    }
    void cancel() override {}
    void retranslate() override { if (status.state == UpdateState::Failed) fail(); }
};
}
std::unique_ptr<MacOSUpdateDriver> makeSparkleUpdateDriver() { return std::make_unique<ForkDriver>(); }
}
