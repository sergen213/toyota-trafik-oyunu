// toyota-trafik-launcher: picks the game folder (or imports the setup CD / .rar), sets the display and graphics
// options, and starts toyota-trafik with them. It only builds a command line; the game validates and imports.
// The Updates tab replaces the running AppImage with the latest GitHub release (sha256-checked) and restarts.
#include <QApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressBar>
#include <QTabWidget>
#include <QVersionNumber>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>

namespace {

QString gameExe() {
    const QString sibling = QCoreApplication::applicationDirPath() + "/toyota-trafik";
    return QFileInfo(sibling).isExecutable() ? sibling : QStandardPaths::findExecutable("toyota-trafik");
}

// The game's profile folder (SDL_GetPrefPath("", "toyota-trafik")): imports land in game/, the last folder in gamedir.txt.
QString profileDir() {
    const QString xdg = qEnvironmentVariable("XDG_DATA_HOME", QDir::homePath() + "/.local/share");
    return xdg + "/toyota-trafik";
}

QComboBox* combo(const QList<QPair<QString, QString>>& items) {  // (label, value)
    auto* c = new QComboBox;
    for (const auto& [label, value] : items) c->addItem(label, value);
    return c;
}

void select(QComboBox* c, const QString& value) {
    const int i = c->findData(value);
    c->setCurrentIndex(i < 0 ? 0 : i);
}

class Launcher : public QWidget {
public:
    Launcher() {
        setWindowTitle("Toyota Trafik Oyunu — Launcher");
        exe_ = gameExe();

        // Game folder
        dir_ = new QLineEdit;
        dir_->setPlaceholderText("Folder containing TrafikOyunu.exe");
        auto* browse = new QPushButton("Browse…");
        auto* importDir = new QPushButton("Import setup folder…");
        auto* importRar = new QPushButton("Import setup .rar…");
        auto* row = new QHBoxLayout;
        row->addWidget(dir_, 1);
        row->addWidget(browse);
        auto* imports = new QHBoxLayout;
        imports->addWidget(importDir);
        imports->addWidget(importRar);
        imports->addStretch();
        gameStatus_ = new QLabel;
        gameStatus_->setWordWrap(true);
        auto* gameBox = new QGroupBox("Game files (your own copy)");
        auto* gl = new QVBoxLayout(gameBox);
        gl->addLayout(row);
        gl->addLayout(imports);
        gl->addWidget(gameStatus_);

        // Display and graphics
        mode_ = combo({{"Windowed", "window"}, {"Fullscreen", "fullscreen"}});
        size_ = combo({{"800 × 600 (original)", "800x600"}, {"1024 × 768", "1024x768"}, {"1280 × 960", "1280x960"},
                       {"1600 × 1200", "1600x1200"}, {"1920 × 1080", "1920x1080"}, {"2560 × 1440", "2560x1440"}});
        renderer_ = combo({{"Automatic (Vulkan, else OpenGL)", "auto"}, {"Vulkan", "vulkan"}, {"OpenGL 3.3", "gl"}});
        rt_ = new QCheckBox("Ray-traced shadows and water reflections (Vulkan)");
        fsr_ = combo({{"Off", "off"}, {"Native AA", "native"}, {"Quality", "quality"}, {"Balanced", "balanced"},
                      {"Performance", "performance"}, {"Ultra performance", "ultra"}});
        framegen_ = new QCheckBox("FSR frame generation (Vulkan)");
        enhanced_ = new QCheckBox("Enhanced graphics: volumetric clouds, parallax relief (not in the original)");
        mute_ = new QCheckBox("Mute audio");
        auto* gfxBox = new QGroupBox("Display and graphics");
        auto* form = new QFormLayout(gfxBox);
        form->addRow("Display", mode_);
        form->addRow("Window size", size_);
        form->addRow("Renderer", renderer_);
        form->addRow("AMD FSR 3.1 upscaling", fsr_);
        form->addRow(rt_);
        form->addRow(framegen_);
        form->addRow(enhanced_);
        form->addRow(mute_);

        play_ = new QPushButton("Play");
        play_->setDefault(true);
        play_->setMinimumHeight(40);
        log_ = new QPlainTextEdit;
        log_->setReadOnly(true);
        log_->setMaximumBlockCount(500);
        log_->setMaximumHeight(120);

        auto* playTab = new QWidget;
        auto* pl = new QVBoxLayout(playTab);
        pl->addWidget(gameBox);
        pl->addWidget(gfxBox);
        pl->addWidget(play_);
        auto* tabs = new QTabWidget;
        tabs->addTab(playTab, "Play");
        tabs->addTab(updatesTab(), "Updates");
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(tabs);
        layout->addWidget(log_);
        resize(620, 560);

        load();
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString d = QFileDialog::getExistingDirectory(this, "Installed game folder", dir_->text());
            if (!d.isEmpty()) setGameDir(d);
        });
        connect(importDir, &QPushButton::clicked, this, [this] {
            const QString d = QFileDialog::getExistingDirectory(this, "Setup CD folder (with Kurulum1/Kurulum2)");
            if (!d.isEmpty()) importFrom(d);
        });
        connect(importRar, &QPushButton::clicked, this, [this] {
            const QString f = QFileDialog::getOpenFileName(this, "Setup archive", QString(), "RAR archive (*.rar)");
            if (!f.isEmpty()) importFrom(f);
        });
        connect(dir_, &QLineEdit::editingFinished, this, [this] { setGameDir(dir_->text()); });
        connect(renderer_, &QComboBox::currentIndexChanged, this, [this] { updateEnabled(); });
        connect(framegen_, &QCheckBox::toggled, this, [this] { updateEnabled(); });
        connect(play_, &QPushButton::clicked, this, [this] { play(); });
        connect(&proc_, &QProcess::readyReadStandardError, this, [this] { log_->appendPlainText(QString::fromUtf8(proc_.readAllStandardError()).trimmed()); });
        connect(&proc_, &QProcess::readyReadStandardOutput, this, [this] { log_->appendPlainText(QString::fromUtf8(proc_.readAllStandardOutput()).trimmed()); });
        connect(&proc_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) { finished(code, st); });
        connect(&proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) finished(-1, QProcess::CrashExit);
        });
        if (exe_.isEmpty()) {
            gameStatus_->setText("toyota-trafik was not found next to the launcher or on PATH.");
            play_->setEnabled(false);
        }
        if (autoUpdate_->isChecked()) checkUpdates(true);
    }

private:
    enum class Job { None, Check, Import, Play };

    static constexpr auto kLatest = "https://api.github.com/repos/sergen213/toyota-trafik-oyunu/releases/latest";
    static constexpr auto kReleases = "https://github.com/sergen213/toyota-trafik-oyunu/releases/latest";

    QWidget* updatesTab() {
        auto* w = new QWidget;
        auto* v = new QVBoxLayout(w);
        v->addWidget(new QLabel("Installed version: " TTO_VERSION));
        autoUpdate_ = new QCheckBox("Update automatically when the launcher starts");
        autoUpdate_->setChecked(QSettings().value("autoUpdate", true).toBool());
        connect(autoUpdate_, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("autoUpdate", on); });
        v->addWidget(autoUpdate_);
        updateStatus_ = new QLabel(appImage().isEmpty()
            ? QString("Updates install only into the AppImage; other builds can check, then download from <a href=\"%1\">%1</a>.").arg(kReleases)
            : "Press “Check for updates”.");
        updateStatus_->setWordWrap(true);
        updateStatus_->setOpenExternalLinks(true);
        v->addWidget(updateStatus_);
        progress_ = new QProgressBar;
        progress_->hide();
        v->addWidget(progress_);
        auto* row = new QHBoxLayout;
        auto* check = new QPushButton("Check for updates");
        apply_ = new QPushButton("Update and restart");
        apply_->setEnabled(false);
        row->addWidget(check);
        row->addWidget(apply_);
        row->addStretch();
        v->addLayout(row);
        v->addStretch();
        connect(check, &QPushButton::clicked, this, [this] { checkUpdates(false); });
        connect(apply_, &QPushButton::clicked, this, [this] { downloadUpdate(); });
        return w;
    }

    // The AppImage runtime exports APPIMAGE (the .AppImage file itself); only that file can be replaced in place.
    static QString appImage() { return qEnvironmentVariable("APPIMAGE"); }

    // GET the latest release; when newer, remember its AppImage and .sha256 assets (and install them if automatic).
    void checkUpdates(bool automatic) {
        updateStatus_->setText("Checking for updates…");
        auto* r = net_.get(QNetworkRequest(QUrl(qEnvironmentVariable("TTO_UPDATE_URL", kLatest))));
        connect(r, &QNetworkReply::finished, this, [this, r, automatic] {
            r->deleteLater();
            const auto rel = QJsonDocument::fromJson(r->readAll()).object();
            const QString tag = rel["tag_name"].toString();
            if (r->error() != QNetworkReply::NoError || tag.isEmpty()) {
                updateStatus_->setText("Update check failed: " + r->errorString());
                return;
            }
            const auto latest = QVersionNumber::fromString(tag.startsWith('v') ? tag.mid(1) : tag);
            if (latest <= QVersionNumber::fromString(TTO_VERSION)) {
                updateStatus_->setText(QString("Up to date (latest release %1).").arg(tag));
                return;
            }
            assetUrl_.clear();
            QUrl sumUrl;
            for (const auto& a : rel["assets"].toArray()) {
                const QString n = a.toObject()["name"].toString();
                const QUrl u(a.toObject()["browser_download_url"].toString());
                if (n.endsWith("-x86_64.AppImage")) assetName_ = n, assetUrl_ = u;
                else if (n.endsWith("-x86_64.AppImage.sha256")) sumUrl = u;
            }
            if (assetUrl_.isEmpty() || sumUrl.isEmpty()) {
                updateStatus_->setText(QString("%1 is available but has no AppImage with a .sha256: <a href=\"%2\">%2</a>").arg(tag, kReleases));
                return;
            }
            if (appImage().isEmpty()) {
                updateStatus_->setText(QString("%1 is available: <a href=\"%2\">%2</a>").arg(tag, kReleases));
                return;
            }
            updateStatus_->setText(QString("%1 is available.").arg(tag));
            // The checksum file is "<hex>  <name>"; fetch it first so the download can be verified as it streams.
            auto* s = net_.get(QNetworkRequest(sumUrl));
            connect(s, &QNetworkReply::finished, this, [this, s, automatic] {
                s->deleteLater();
                expectedSum_ = QString::fromUtf8(s->readAll()).section(' ', 0, 0).trimmed().toLower();
                if (s->error() != QNetworkReply::NoError || expectedSum_.size() != 64) {
                    updateStatus_->setText("Could not read the release checksum: " + s->errorString());
                    return;
                }
                apply_->setEnabled(true);
                if (automatic && proc_.state() == QProcess::NotRunning) downloadUpdate();
            });
        });
    }

    // Download next to the AppImage (same filesystem, so the final rename is atomic), verify, swap, restart.
    void downloadUpdate() {
        apply_->setEnabled(false);
        const QString target = appImage();
        auto* file = new QFile(target + ".part", this);
        if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            updateStatus_->setText("Cannot write " + file->fileName() + ": " + file->errorString());
            delete file;
            return;
        }
        auto* hash = new QCryptographicHash(QCryptographicHash::Sha256);
        updateStatus_->setText("Downloading " + assetName_ + "…");
        progress_->setValue(0);
        progress_->show();
        auto* r = net_.get(QNetworkRequest(assetUrl_));
        connect(r, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
            progress_->setMaximum(total > 0 ? int(total / 1024) : 0);
            progress_->setValue(int(got / 1024));
        });
        connect(r, &QNetworkReply::readyRead, this, [r, file, hash] {
            const QByteArray b = r->readAll();
            hash->addData(b);
            file->write(b);
        });
        connect(r, &QNetworkReply::finished, this, [this, r, file, hash, target] {
            r->deleteLater();
            const QByteArray rest = r->readAll();
            hash->addData(rest);
            file->write(rest);
            const bool written = file->flush() && file->error() == QFileDevice::NoError;
            file->close();
            const QString got = QString::fromLatin1(hash->result().toHex());
            delete hash;
            progress_->hide();
            QString err;
            if (r->error() != QNetworkReply::NoError) err = "Download failed: " + r->errorString();
            else if (!written) err = "Writing the update failed: " + file->errorString();
            else if (got != expectedSum_) err = "Checksum mismatch; the download was discarded.";
            else if (!file->setPermissions(QFileInfo(target).permissions() | QFile::ExeOwner | QFile::ReadOwner))
                err = "Cannot make the update executable.";
            // rename(2) over the running AppImage is safe: the mounted runtime keeps the old inode open.
            else if (std::rename(file->fileName().toLocal8Bit().constData(), target.toLocal8Bit().constData()) != 0)
                err = "Cannot replace " + target + ": " + QString::fromLocal8Bit(std::strerror(errno));
            if (!err.isEmpty()) {
                file->remove();
                file->deleteLater();
                updateStatus_->setText(err);
                apply_->setEnabled(true);
                return;
            }
            file->deleteLater();
            log_->appendPlainText("Updated " + target + " to " + assetName_ + "; restarting.");
            restart(target);
        });
    }

    // Start the new AppImage without this one's mount paths in its environment, then leave.
    void restart(const QString& target) {
        QProcess p;
        auto env = QProcessEnvironment::systemEnvironment();
        for (const char* k : {"LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "APPDIR", "APPIMAGE", "ARGV0", "OWD"}) env.remove(k);
        p.setProcessEnvironment(env);
        p.setProgram(target);
        if (!p.startDetached()) {
            updateStatus_->setText("Updated; restart the launcher to use the new version.");
            return;
        }
        QCoreApplication::quit();
    }

    void load() {
        QSettings s;
        QString dir = s.value("gameDir").toString();
        if (dir.isEmpty()) {  // the folder the game itself last used
            QFile f(profileDir() + "/gamedir.txt");
            if (f.open(QIODevice::ReadOnly)) dir = QString::fromUtf8(f.readLine()).trimmed();
        }
        select(mode_, s.value("display", "window").toString());
        select(size_, s.value("size", "1024x768").toString());
        select(renderer_, s.value("renderer", "auto").toString());
        rt_->setChecked(s.value("rayTracing", true).toBool());
        select(fsr_, s.value("fsr", "off").toString());
        framegen_->setChecked(s.value("frameGeneration", false).toBool());
        enhanced_->setChecked(s.value("enhanced", true).toBool());
        mute_->setChecked(s.value("mute", false).toBool());
        updateEnabled();
        if (!dir.isEmpty()) setGameDir(dir);
        else gameStatus_->setText("Choose the installed game folder, or import the original setup CD or its .rar.");
    }

    void save() {
        QSettings s;
        s.setValue("gameDir", dir_->text());
        s.setValue("display", mode_->currentData());
        s.setValue("size", size_->currentData());
        s.setValue("renderer", renderer_->currentData());
        s.setValue("rayTracing", rt_->isChecked());
        s.setValue("fsr", fsr_->currentData());
        s.setValue("frameGeneration", framegen_->isChecked());
        s.setValue("enhanced", enhanced_->isChecked());
        s.setValue("mute", mute_->isChecked());
    }

    // Ray tracing, FSR and frame generation exist only in the Vulkan renderer.
    void updateEnabled() {
        const bool vk = renderer_->currentData() != "gl";
        rt_->setEnabled(vk);
        fsr_->setEnabled(vk);
        framegen_->setEnabled(vk);
    }

    void setGameDir(const QString& d) {
        dir_->setText(d);
        gameOk_ = false;
        if (d.isEmpty() || exe_.isEmpty()) return;
        run(Job::Check, {"--game", d, "--check"}, "Checking game folder…");
    }

    void importFrom(const QString& source) {
        run(Job::Import, {"--import", source, "--check"}, "Importing the game into " + profileDir() + "/game (one time)…");
    }

    void play() {
        if (!gameOk_) {
            QMessageBox::warning(this, windowTitle(), "Choose a valid game folder first.");
            return;
        }
        save();
        QStringList a{"--game", dir_->text(), "--size", size_->currentData().toString(), "--renderer", renderer_->currentData().toString()};
        if (mode_->currentData() == "fullscreen") a << "--fullscreen";
        if (renderer_->currentData() != "gl") {
            if (!rt_->isChecked()) a << "--no-rt";
            if (fsr_->currentData() != "off") a << "--fsr" << fsr_->currentData().toString();
            if (framegen_->isChecked()) a << "--framegen";
        }
        if (!enhanced_->isChecked()) a << "--no-enhanced";
        if (mute_->isChecked()) a << "--mute";
        run(Job::Play, a, "Running…");
        hide();
    }

    void run(Job j, const QStringList& args, const QString& status) {
        if (proc_.state() != QProcess::NotRunning) return;
        job_ = j;
        setBusy(true);
        gameStatus_->setText(status);
        log_->appendPlainText("$ toyota-trafik " + args.join(' '));
        proc_.start(exe_, args);
    }

    void finished(int code, QProcess::ExitStatus st) {
        const Job j = job_;
        job_ = Job::None;
        setBusy(false);
        const bool ok = st == QProcess::NormalExit && code == 0;
        switch (j) {
            case Job::Check:
                gameOk_ = ok;
                gameStatus_->setText(ok ? "Game folder OK." : "This folder is not a usable game installation (see the log below).");
                if (ok) save();
                break;
            case Job::Import:
                if (ok) {
                    setGameDir(profileDir() + "/game");
                } else {
                    gameStatus_->setText("Import failed (see the log below).");
                }
                break;
            case Job::Play:
                show();
                raise();
                gameStatus_->setText(ok ? "Game folder OK." : QString("The game exited with an error (code %1); see the log below.").arg(code));
                break;
            case Job::None: break;
        }
    }

    void setBusy(bool b) {
        play_->setEnabled(!b && !exe_.isEmpty());
        for (auto* w : findChildren<QPushButton*>())
            if (w != play_ && w != apply_) w->setEnabled(!b);
        apply_->setEnabled(!b && !assetUrl_.isEmpty() && !expectedSum_.isEmpty() && !appImage().isEmpty());
        dir_->setEnabled(!b);
    }

    QString exe_;
    QLineEdit* dir_;
    QLabel* gameStatus_;
    QComboBox *mode_, *size_, *renderer_, *fsr_;
    QCheckBox *rt_, *framegen_, *enhanced_, *mute_;
    QPushButton *play_, *apply_;
    QPlainTextEdit* log_;
    QProcess proc_;
    Job job_ = Job::None;
    bool gameOk_ = false;
    QNetworkAccessManager net_;
    QCheckBox* autoUpdate_;
    QLabel* updateStatus_;
    QProgressBar* progress_;
    QUrl assetUrl_;
    QString assetName_, expectedSum_;
};

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("toyota-trafik");
    QCoreApplication::setApplicationName("launcher");
    QApplication::setDesktopFileName("toyota-trafik");
    Launcher w;
    w.show();
    return QApplication::exec();
}
