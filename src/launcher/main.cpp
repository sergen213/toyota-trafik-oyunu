// toyota-trafik-launcher: picks the game folder (or imports the setup CD / .rar), sets the display and graphics
// options, and starts toyota-trafik with them. It only builds a command line; the game validates and imports.
#include <QApplication>
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
        mute_ = new QCheckBox("Mute audio");
        auto* gfxBox = new QGroupBox("Display and graphics");
        auto* form = new QFormLayout(gfxBox);
        form->addRow("Display", mode_);
        form->addRow("Window size", size_);
        form->addRow("Renderer", renderer_);
        form->addRow("AMD FSR 3.1 upscaling", fsr_);
        form->addRow(rt_);
        form->addRow(framegen_);
        form->addRow(mute_);

        play_ = new QPushButton("Play");
        play_->setDefault(true);
        play_->setMinimumHeight(40);
        log_ = new QPlainTextEdit;
        log_->setReadOnly(true);
        log_->setMaximumBlockCount(500);
        log_->setMaximumHeight(120);

        auto* layout = new QVBoxLayout(this);
        layout->addWidget(gameBox);
        layout->addWidget(gfxBox);
        layout->addWidget(play_);
        layout->addWidget(log_);
        resize(620, 520);

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
    }

private:
    enum class Job { None, Check, Import, Play };

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
            if (w != play_) w->setEnabled(!b);
        dir_->setEnabled(!b);
    }

    QString exe_;
    QLineEdit* dir_;
    QLabel* gameStatus_;
    QComboBox *mode_, *size_, *renderer_, *fsr_;
    QCheckBox *rt_, *framegen_, *mute_;
    QPushButton* play_;
    QPlainTextEdit* log_;
    QProcess proc_;
    Job job_ = Job::None;
    bool gameOk_ = false;
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
