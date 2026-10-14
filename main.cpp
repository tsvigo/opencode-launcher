// opencode-launcher — сканирует /home/v/projects/ и по клику на кнопку
// запускает Konsole в соответствующей папке проекта с opencode внутри
// (переключатель движка: opencode v1 либо oc2 v2).
//
// Сборка (Qt5 или Qt6, любой из установленных):
//   mkdir build && cd build && cmake .. && make
//
// Запуск:
//   ./opencode-launcher

#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QDir>
#include <QProcess>
#include <QMessageBox>
#include <QStandardPaths>
#include <QFileInfo>
#include <QScreen>
#include <QFontMetrics>
#include <QComboBox>
#include <QHBoxLayout>
#include <QSettings>
#include <QDateTime>
#include <functional>
#include <algorithm>

// ---------------------------------------------------------------------
// Настройки — при необходимости поправьте под свою систему.
// ---------------------------------------------------------------------

// Папка с проектами.
static const QString kProjectsRoot = QStringLiteral("/home/v/projects");

// Путь к бинарнику opencode (согласно вашей настройке — не просто "opencode" в PATH).
static const QString kOpencodeBinary = QStringLiteral("/home/v/.opencode/bin/opencode");

// Аргументы запуска opencode.
static const QStringList kOpencodeArgs = { QStringLiteral("--port"), QStringLiteral("4096") };

// Переменные окружения, которые нужно выставить перед запуском.
static const QStringList kExtraEnv = {
    QStringLiteral("OPENCODE_DB=/home/v/.local/share/opencode/opencode-dev.db"),
    QStringLiteral("OPENCODE_ENABLE_EXA=1"),
};

// Предпочтительные эмуляторы терминала, в порядке приоритета.
// konsole — стандартный терминал KDE Plasma.
static const QStringList kTerminalCandidates = {
    QStringLiteral("konsole"),
    QStringLiteral("xterm"),
    QStringLiteral("gnome-terminal"),
};

// ---------------------------------------------------------------------

// Находит первый доступный в PATH терминал из списка кандидатов.
static QString findAvailableTerminal()
{
    for (const QString &term : kTerminalCandidates) {
        const QString path = QStandardPaths::findExecutable(term);
        if (!path.isEmpty())
            return term;
    }
    return QString();
}

// Режимы движка: 0 — opencode v1, 1 — oc2 v2.
static const int kEngineV1 = 0;
static const int kEngineV2 = 1;

// Собирает команду запуска:
// v1: export ...; cd <path> && <opencode> <args>; exec bash
// v2: cd <path> && oc2-check && oc2 --standalone; exec bash
static QString buildShellCommand(const QString &projectPath, int engineMode)
{
    if (engineMode == kEngineV2) {
        const QString q = QStringLiteral("'%1'").arg(QString(projectPath).replace("'", "'\\''"));
        return QStringLiteral("cd %1 && oc2-check && oc2 --standalone; exec bash").arg(q);
    }

    QStringList parts;
    for (const QString &env : kExtraEnv)
        parts << QStringLiteral("export %1;").arg(env);

    // Ключ xKiro из ~/.bashrc: терминал стартует через "bash -c" (неинтерактивно,
    // bashrc не читается — в нём к тому же guard "case $-"), поэтому подхватываем
    // ключ явно. Секрет не хардкодим — единственным источником остаётся ~/.bashrc.
    // Если ключа там нет — экспорт даст пустую строку, поведение как раньше.
    parts << QStringLiteral("export XKIRO_API_KEY=\"$(sed -n 's/^export XKIRO_API_KEY=\"\\(.*\\)\"$/\\1/p' ~/.bashrc 2>/dev/null | head -1)\";");

    parts << QStringLiteral("cd %1 &&").arg(QString(projectPath).replace("'", "'\\''").prepend("'").append("'"));

    QString opencodeCmd = QStringLiteral("'%1'").arg(QString(kOpencodeBinary).replace("'", "'\\''"));
    for (const QString &arg : kOpencodeArgs)
        opencodeCmd += QStringLiteral(" '%1'").arg(QString(arg).replace("'", "'\\''"));

    parts << opencodeCmd + QStringLiteral(";");
    // exec bash — чтобы терминал не закрывался сразу после выхода из opencode
    parts << QStringLiteral("exec bash");

    return parts.join(' ');
}

// Запускает выбранный терминал в указанной папке проекта с opencode внутри.
static bool launchOpencodeForProject(const QString &projectPath, QWidget *parentForErrors, int engineMode)
{
    const QString terminal = findAvailableTerminal();
    if (terminal.isEmpty()) {
        QMessageBox::critical(parentForErrors, QObject::tr("Ошибка"),
            QObject::tr("Не найден ни один эмулятор терминала (konsole/xterm/gnome-terminal)."));
        return false;
    }

    if (engineMode == kEngineV2) {
        if (QStandardPaths::findExecutable(QStringLiteral("oc2")).isEmpty()
            || QStandardPaths::findExecutable(QStringLiteral("oc2-check")).isEmpty()) {
            QMessageBox::warning(parentForErrors, QObject::tr("Предупреждение"),
                QObject::tr("oc2 или oc2-check не найдены в PATH.\n"
                            "Проверьте установку OpenCode v2."));
            // не прерываем — вдруг PATH в GUI-сессии отличается
        }
    } else if (!QFileInfo::exists(kOpencodeBinary)) {
        QMessageBox::warning(parentForErrors, QObject::tr("Предупреждение"),
            QObject::tr("Бинарник opencode не найден по пути:\n%1\n"
                        "Проверьте путь в исходнике (kOpencodeBinary).").arg(kOpencodeBinary));
        // не прерываем — вдруг путь просто отличается на другой машине
    }

    const QString shellCmd = buildShellCommand(projectPath, engineMode);

    QStringList args;
    if (terminal == QStringLiteral("konsole")) {
        args << QStringLiteral("--workdir") << projectPath << QStringLiteral("-e")
             << QStringLiteral("bash") << QStringLiteral("-c") << shellCmd;
    } else if (terminal == QStringLiteral("gnome-terminal")) {
        args << QStringLiteral("--working-directory") << projectPath << QStringLiteral("--")
             << QStringLiteral("bash") << QStringLiteral("-c") << shellCmd;
    } else { // xterm и прочие, понимающие -e
        args << QStringLiteral("-e") << QStringLiteral("bash") << QStringLiteral("-c") << shellCmd;
    }

    const bool ok = QProcess::startDetached(terminal, args);
    if (!ok) {
        QMessageBox::critical(parentForErrors, QObject::tr("Ошибка"),
            QObject::tr("Не удалось запустить терминал '%1' для проекта:\n%2")
                .arg(terminal, projectPath));
    }
    return ok;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("opencode-launcher"));

    QWidget window;
    window.setWindowTitle(QStringLiteral("Проекты opencode"));

    // Высота окна = высоте доступной области экрана.
    QScreen *screen = QApplication::primaryScreen();
    const QRect availGeom = screen ? screen->availableGeometry() : QRect(0, 0, 800, 600);
    const int screenH = availGeom.height();
    const int screenW = availGeom.width();
    window.resize(420, screenH);

    auto *outerLayout = new QVBoxLayout(&window);

    QDir projectsDir(kProjectsRoot);
    if (!projectsDir.exists()) {
        auto *label = new QLabel(
            QStringLiteral("Папка проектов не найдена:\n%1").arg(kProjectsRoot), &window);
        label->setWordWrap(true);
        outerLayout->addWidget(label);
        window.show();
        return app.exec();
    }

    // Запоминание выбора сортировки + счётчики запусков.
    QSettings settings(QStringLiteral("opencode-launcher"), QStringLiteral("opencode-launcher"));
    int sortMode = settings.value(QStringLiteral("sortMode"), 0).toInt();
    sortMode = qBound(0, sortMode, 2);

    // Dropdown сортировки вверху окна.
    auto *topBar = new QHBoxLayout();
    auto *sortLabel = new QLabel(QStringLiteral("Сортировка:"), &window);
    auto *sortBox = new QComboBox(&window);
    sortBox->addItem(QStringLiteral("По алфавиту"));
    sortBox->addItem(QStringLiteral("По времени создания"));
    sortBox->addItem(QStringLiteral("По частоте использования"));
    sortBox->setCurrentIndex(sortMode);
    topBar->addWidget(sortLabel);
    topBar->addWidget(sortBox, 1);
    outerLayout->addLayout(topBar);

    // Переключатель движка запуска v1/v2 с памятью (QSettings).
    int engineMode = settings.value(QStringLiteral("engineMode"), kEngineV2).toInt();
    engineMode = qBound(kEngineV1, engineMode, kEngineV2);

    auto *modeButton = new QPushButton(&window);
    modeButton->setMinimumHeight(36);
    auto updateModeText = [&]() {
        modeButton->setText(engineMode == kEngineV2
            ? QStringLiteral("Движок: oc2 v2")
            : QStringLiteral("Движок: opencode v1"));
    };
    updateModeText();
    outerLayout->addWidget(modeButton);
    QObject::connect(modeButton, &QPushButton::clicked, [&]() {
        engineMode = (engineMode == kEngineV2) ? kEngineV1 : kEngineV2;
        settings.setValue(QStringLiteral("engineMode"), engineMode);
        updateModeText();
    });

    auto *scrollArea = new QScrollArea(&window);
    scrollArea->setWidgetResizable(true);

    auto *listWidget = new QWidget(scrollArea);
    auto *listLayout = new QVBoxLayout(listWidget);
    listLayout->setAlignment(Qt::AlignTop);
    scrollArea->setWidget(listWidget);
    outerLayout->addWidget(scrollArea);

    std::function<void()> rebuild;
    rebuild = [&]() {
        // Очистить текущий список кнопок.
        QLayoutItem *child = nullptr;
        while ((child = listLayout->takeAt(0)) != nullptr) {
            if (child->widget())
                delete child->widget();
            delete child;
        }

        QStringList names = projectsDir.entryList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::NoSort);

        if (names.isEmpty()) {
            auto *label = new QLabel(
                QStringLiteral("В %1 не найдено ни одной папки.").arg(kProjectsRoot), listWidget);
            label->setWordWrap(true);
            listLayout->addWidget(label);
            return;
        }

        if (sortMode == 0) {
            // По алфавиту.
            std::sort(names.begin(), names.end(),
                [](const QString &a, const QString &b) {
                    return QString::localeAwareCompare(a, b) < 0;
                });
        } else if (sortMode == 1) {
            // По времени создания: новые сверху. birthTime может быть
            // недоступен на некоторых ФС — тогда откат на lastModified.
            std::sort(names.begin(), names.end(),
                [&](const QString &a, const QString &b) {
                    QFileInfo fa(projectsDir.filePath(a));
                    QFileInfo fb(projectsDir.filePath(b));
                    QDateTime ta = fa.birthTime();
                    if (!ta.isValid())
                        ta = fa.lastModified();
                    QDateTime tb = fb.birthTime();
                    if (!tb.isValid())
                        tb = fb.lastModified();
                    if (ta != tb)
                        return ta > tb;
                    return QString::localeAwareCompare(a, b) < 0;
                });
        } else {
            // По частоте использования: чаще запускаемые сверху,
            // при равном счётчике — по алфавиту.
            std::sort(names.begin(), names.end(),
                [&](const QString &a, const QString &b) {
                    const int ca = settings.value(QStringLiteral("usage/%1").arg(a), 0).toInt();
                    const int cb = settings.value(QStringLiteral("usage/%1").arg(b), 0).toInt();
                    if (ca != cb)
                        return ca > cb;
                    return QString::localeAwareCompare(a, b) < 0;
                });
        }

        int maxTextPx = 0;
        int idx = 0;
        for (const QString &name : names) {
            const QString fullPath = projectsDir.filePath(name);
            ++idx;
            const QString btnText = QStringLiteral("%1. %2").arg(idx).arg(name);

            auto *button = new QPushButton(btnText, listWidget);
            button->setMinimumHeight(36);
            // Авторасширение: кнопка тянется по ширине, текст прижат влево,
            // чтобы номера шли ровным столбцом и весь текст влезал.
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            button->setStyleSheet(QStringLiteral("text-align: left; padding-left: 8px; padding-right: 8px;"));
            if (sortMode == 2) {
                const int cnt = settings.value(QStringLiteral("usage/%1").arg(name), 0).toInt();
                button->setToolTip(QStringLiteral("Запусков: %1").arg(cnt));
            }
            maxTextPx = qMax(maxTextPx, button->fontMetrics().horizontalAdvance(btnText));

            QObject::connect(button, &QPushButton::clicked,
                [fullPath, name, &window, &settings, &rebuild, &sortMode]() {
                    const int c = settings.value(QStringLiteral("usage/%1").arg(name), 0).toInt();
                    settings.setValue(QStringLiteral("usage/%1").arg(name), c + 1);
                    launchOpencodeForProject(fullPath, &window);
                    // В режиме «по частоте» сразу перестраиваем список,
                    // чтобы порядок отражал новый счётчик.
                    if (sortMode == 2)
                        rebuild();
                });

            listLayout->addWidget(button);
        }
        // Подгоняем ширину окна под самый длинный текст кнопки:
        // ширина текста + отступы кнопок/лейаута + запас под скроллбар и рамку.
        // Каждая кнопка получает минимальную ширину, чтобы текст точно влез.
        const int neededBtnW = maxTextPx + 40;
        for (QPushButton *b : listWidget->findChildren<QPushButton *>())
            b->setMinimumWidth(neededBtnW);
        const int neededWinW = neededBtnW + 80;
        const int finalW = qBound(420, neededWinW, qMax(screenW, 420));
        window.resize(finalW, screenH);
    };

    QObject::connect(sortBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
        [&](int idx) {
            sortMode = qBound(0, idx, 2);
            settings.setValue(QStringLiteral("sortMode"), sortMode);
            rebuild();
        });

    rebuild();

    window.show();
    return app.exec();
}
