#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->tableWidget, &QTableWidget::customContextMenuRequested,
            this, &MainWindow::showContextMenu);

    movie = new QMovie(":/loading.gif");
    ui->loadingLabel->setVisible(false);
    movie->setScaledSize(QSize(300, 280));
    ui->loadingLabel->setMovie(movie);
    ui->tableWidget->setColumnWidth(0, 40);
    ui->tableWidget->setColumnWidth(1, 40);

    ui->progressBar->setVisible(false);

    if (!configController.getConfigEntries().isEmpty())
    {
        ui->startButton->setEnabled(true);
        updateAccount();
        refreshScriptPaths();
        loadDropListPaths();
    }

    fetchers_ready = 0;
    sayType = "say";
    auto currentConfig = configController.getCurrentConfigRef();
    timer_interval = (currentConfig) ? getTimerInterval(currentConfig->getPc()):
                                       200;
    lyrics_fetchers.append(new GeniusLyricsFetcher(this));
    lyrics_fetchers.append(new LyricstranslateFetcher(this));
    lyrics_fetchers.append(new MusixmatchFetcher(this));

    for (auto& fet : lyrics_fetchers) {
        connect(dynamic_cast<QObject*>(fet), SIGNAL(listReady(StringPairList)),
                this, SLOT(lyricsListFetched(StringPairList)));
        connect(dynamic_cast<QObject*>(fet), SIGNAL(lyricsReady(QString)),
                this, SLOT(lyricsFetched(QString)));
    }

    QDir root(".");
    root.mkdir("lyrics");
    root.mkdir("songs");
    root.mkdir("config");

    updateManager = new UpdateManager(this);
    connect(updateManager, &UpdateManager::YTDLUpdateReady,
            this, &MainWindow::handleYTDLUpdateResponse);
    connect(updateManager, &UpdateManager::downloadProgress,
            this, &MainWindow::downloadProgress);

    showUpdateNotification();

    setWindowIcon(QIcon(":/icon/favicon.ico"));

    stats.sendLaunch();

    refreshSongList();
}

MainWindow::~MainWindow()
{
    delete ui;
}

bool MainWindow::refreshSongList()
{
    ui->tableWidget->clearContents();

    QDir dir("lyrics");
    QStringList lyrics = dir.entryList(QDir::Files);

    QDir song_dir("songs");
    QStringList songs = song_dir.entryList(QDir::Files);

    ui->tableWidget->setRowCount(lyrics.size() + songs.size());

    for (int i = 0; i < lyrics.size(); i++)
    {
        QString name = lyrics.at(i);

        if (!name.endsWith("txt"))
            continue;

        name = name.left(name.size() - 4);

        QTableWidgetItem *lyrics_checkbox = new QTableWidgetItem("Yes");
        QTableWidgetItem *song_checkbox = new QTableWidgetItem("No");
        ui->tableWidget->setItem(i, 1, lyrics_checkbox);

        if (QFileInfo::exists("songs/" + name + ".wav"))
        {
            song_checkbox->setText("Yes");
            ui->tableWidget->setRowCount(ui->tableWidget->rowCount() - 1);
        }

        ui->tableWidget->setItem(i, 0, song_checkbox);
        ui->tableWidget->setItem(i, 2, new QTableWidgetItem(name));
    }

    int song_pos = lyrics.size();

    for (int i = 0; i < songs.size(); i++)
    {
        QString name = songs.at(i);

        if (!name.endsWith(".wav"))
        {
            ui->tableWidget->setRowCount(ui->tableWidget->rowCount() - 1);
            song_pos--;
            continue;
        }

        name = name.left(name.size() - 4);

        if (!QFileInfo::exists("lyrics/" + name + ".txt"))
        {
            QTableWidgetItem *lyrics_checkbox = new QTableWidgetItem("No");
            QTableWidgetItem *song_checkbox = new QTableWidgetItem("Yes");

            ui->tableWidget->setItem(song_pos + i, 0, song_checkbox);
            ui->tableWidget->setItem(song_pos + i, 1, lyrics_checkbox);
            ui->tableWidget->setItem(song_pos + i, 2,
                                     new QTableWidgetItem(name));
        }
        else
            song_pos--;
    }

    return true;
}

bool MainWindow::createTracklistFile()
{
    QString list("exec lyricsmaster.cfg;\n");
    const char* border =
          "echo \"--------------------------------------------------------\"\n";
    list.append(border);
    for (int i = 0; i < ui->tableWidget->rowCount(); i++)
    {
        QString i_str = QString::number(i + 1);
        list.append("echo \"song"
                    + i_str + ": "
                    + ui->tableWidget->item(i, 2)->text()
                    + "\"\n");
    }
    list.append(border);

    tracklist.write(list.toUtf8());
    return true;
}

bool MainWindow::addSongToConfig(const QString &filename, const QString &id)
{
    QString line;
    QFile source("lyrics/" + filename + ".txt");

    source.open(QIODevice::ReadOnly);

    int i = 0;

    for (i = 0; !source.atEnd(); i++) {
        line = source.readLine();
        line.remove('"');
        line = line.trimmed();

        if (!line.isEmpty()) {
            QString cfg_line = QString("alias song%1lyrics%2 \"%3 ~ %4 ;"
                                       "alias spamycs song%5lyrics%6\"\n")
                 .arg(id, QString::number(i), sayType, line,
                      id, QString::number(i + 1));
            dest.write(cfg_line.toUtf8());
        }

        else i--;
    }

    QString str = QString("alias song%1lyrics%2 %3 \"---THE END---\";\n")
                        .arg(id, QString::number(i), sayType);
    dest.write(str.toUtf8());

    return true;
}

bool MainWindow::createSongIndex(const QString &id)
{
    QChar relay_key = '=';
    QString compatWriteCfg = "host_writeconfig";
    if (configController.getCurrentConfigRef()->getFullName() == "Half-Life")
        compatWriteCfg = "writecfg";

    QString songname = ui->tableWidget->item(id.toInt() - 1, 2)->text();
    QString str = QStringLiteral("alias say_song%2 \"%4 Current Song: %1\";"
                                 "alias song%2 \"alias spamycs song%2lyrics0;"
                                 "bind %3 %2; alias lyrics_current say_song%2;"
                                 "%5 lyrics_trigger;\n")
                .arg(songname, id, relay_key, sayType, compatWriteCfg);

    dest.write(str.toUtf8());
    return true;
}

void MainWindow::on_directoryButton_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Open Directory"),
                                            "/",
                                            QFileDialog::ShowDirsOnly
                                            | QFileDialog::DontResolveSymlinks);

    if (!dir.isEmpty()
            && (QDir(dir).dirName() == "cfg"
                || QDir(dir).entryList().contains("config.cfg")))
    {
        ui->startButton->setEnabled(true);

        configController.addConfig(ConfigEntry(dir));
        refreshScriptPaths();
        loadDropListPaths();
        QTimer::singleShot(200, this, [=](){updateAccount();});
    }
    else
    {
        QMessageBox::warning(this, "Config",
                             "That is not valid source configuration folder.");
    }
}

void MainWindow::on_refreshButton_clicked()
{
    refreshSongList();
}

void MainWindow::loadSong(int songid)
{
    QString s = "songs/"+ ui->tableWidget->item(songid - 1, 2)->text() + ".wav";
    QString d = configController.getCurrentGamePath() + "/voice_input.wav";

    if (QFileInfo::exists(d))
    {
        QFile::remove(d);
    }

    if (!QFile::copy(s, d))
        ui->err->setText("Couldnt copy song to game folder");
}

void MainWindow::checkConfigFile()
{
    QString ud = configController.getUserDataPath();
    if (configController.getCurrentConfigRef()->getFullName() == "Half-Life")
        ud = configController.getCurrentGamePath() + "/lyrics_trigger.cfg";

    if (QFileInfo::exists(ud))
    {
        QFile f(ud);
        f.open(QIODevice::ReadOnly);
        QString cfg = f.readAll();
        int start = cfg.indexOf("bind \"=\" ") + 10;
        int end = cfg.indexOf("\"", start + 1);
        f.remove();
        loadSong(cfg.mid(start, end - start).toInt());
    }
}
void MainWindow::updateConfigSongList()
{
    tracklist.open(QIODevice::WriteOnly | QIODevice::Truncate);
    dest.open(QIODevice::WriteOnly | QIODevice::Truncate);

    auto keys = configController.getCurrentConfigRef()->getKeyBindings();
    QString voice_command;
    QString lyrics_command;
    for (auto& key : keys) {
        if (key.first == "Voice")
            voice_command = key.second;
        else if (key.first == "Lyrics")
            lyrics_command = key.second;
    }
    dest.write(QString("alias spamycs say_team \"type exec lyrics_list.cfg in "
                       "the console to see list with available songs\"\nalias "
                       "karaoke_play karaoke_play_on\nalias karaoke_play_on "
                       "\"alias karaoke_play karaoke_play_off;"
                       "voice_inputfromfile 1;voice_loopback 1;+voicerecord\"\n"
                       "alias karaoke_play_off \"-voicerecord; "
                       "voice_inputfromfile 0; voice_loopback 0; alias "
                       "karaoke_play karaoke_play_on\";"
                       "bind " + voice_command + " \"karaoke_play\";"
                       "bind " + lyrics_command + " spamycs\n").toUtf8());
    createTracklistFile();

    for (int i = 0; i < ui->tableWidget->rowCount(); i++)
    {
        addSongToConfig(ui->tableWidget->item(i, 2)->text(),
                        QString::number(i + 1));
        createSongIndex(QString::number(i+1));
    }

    tracklist.close();
    dest.close();
}

void MainWindow::on_startButton_clicked()
{
    if (ui->startButton->text() == "Start")
    {
        timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &MainWindow::checkConfigFile);
        timer->start(200);

        ui->startButton->setText("Stop");
        updateConfigSongList();
    }
    else
    {
        QString gamepath = configController.getCurrentGamePath();
        if (QFileInfo::exists(gamepath + "/voice_input.wav"))
        {
            QFile rm(gamepath + "/voice_input.wav");
            rm.remove();
        }
        ui->startButton->setText("Start");
        timer->stop();
    }
}

void MainWindow::on_addSongButton_clicked()
{
    ui->err->setText("");

    QStringList songs_list = QFileDialog::getOpenFileNames(this,
                                                   tr("Open Directory"),
                                                   "/",
                                                   tr("Text Files (*.txt)"));
    for (int i = 0; i < songs_list.size(); i++)
    {
        QString song = songs_list.at(i);
        int pos = song.lastIndexOf(QChar('/'));
        QString song_newpath = "lyrics" + song.right(song.size() - pos);

        QFile song_file(song);
        if (!song_file.copy(song_newpath))
            ui->err->setText(song_file.errorString());
    }
    refreshSongList();
}

void MainWindow::handleYTDLUpdateResponse(Response response)
{
    switch (response) {
    case Response::UPDATED:
        QMessageBox::information(this, "YTDL Update",
                                 "yt-dl.exe has been updated!");
        break;
    case Response::UP_TO_DATE:
        QMessageBox::information(this, "YTDL Update",
                                 "Skipping. Already up to date!");
        break;
    case Response::FAILED:
        QMessageBox::critical(this, "YTDL Update",
                                 "Update failed!");
    }
    ui->progressBar->setVisible(false);
}

void MainWindow::on_deleteSongButton_clicked()
{
    ui->err->setText("");

    QList<QTableWidgetItem*> items = ui->tableWidget->selectedItems();

    if (items.size() == 0) {
        QMessageBox::information(this,
                                 "Delete songs",
                                 "You haven't selected any songs.");
        return;
    }

    QString question_content =
            QString("Are you sure you want to delete %1 song's lyrics?")
            .arg(QString::number(items.size()/3));

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Delete songs", question_content,
                                  QMessageBox::Yes|QMessageBox::No);

    if (reply == QMessageBox::Yes)
    {
        for (int i = 0; i < items.size()/3; i++)
        {
            QString name = items.at(i * 3 + 2)->text();
            if (QFileInfo::exists("lyrics/" + name + ".txt"))
                if (!QFile::remove("lyrics/" + name + ".txt"))
                    ui->err->setText("Permission error!");
            if (QFileInfo::exists("songs/" + name + ".wav"))
                QFile::remove("songs/" + name + ".wav");
        }
        refreshSongList();
    }
}

void MainWindow::on_searchOnlineButton_clicked()
{
    bool ok;
    QString text = QInputDialog::getText(this, tr("Online Search"),
                            tr("Type song name, artist or part of the song"),
                            QLineEdit::Normal,
                            QDir::home().dirName(), &ok);
    search_string = text;

    if (ok && !text.isEmpty())
    {
        // Fetch list
        for (auto &fetcher : lyrics_fetchers) {
            fetcher->fetchList(text);
        }
        search_string = "";
    }
}

void MainWindow::songCooked()
{
    if (timeout_timer->isActive())
        timeout_timer->stop();

    QDir root;
    root.setNameFilters(QStringList() << "*.wav" << "*.webm.part" << "*.webm");
    root.setFilter(QDir::Files);

    bool success = false;

    if (root.count() > 0)
    {
        QStringList ffmpegsux = root.entryList();

        for (int i = 0; i < ffmpegsux.size(); i++)
        {
            QString filename = ffmpegsux.at(i);
            if (filename.endsWith(".wav"))
            {
                dl_file_name.replace(QChar(0x00A0), " ");
                QFile::rename(filename, "songs/" + dl_file_name + ".wav");
                success = true;
            }
            else if (QFileInfo::exists(filename))
                QFile::remove(filename);
        }
    }

    movie->stop();
    ui->loadingLabel->setVisible(false);

    QString text;
    if (success)
    {
        text = "Song Downloaded!";
        QTimer::singleShot(100, this, &MainWindow::on_startButton_clicked);
        QTimer::singleShot(200, this, &MainWindow::on_startButton_clicked);
    }
    else
        text = "Song Failed to download!";

    QMessageBox::information(this, "Song Download", text);

    dl_file_timer->stop();

    refreshSongList();
}

void MainWindow::downloadFinished(int exitCode)
{
    dl_file_timer = new QTimer(this);
    dl_file_timer->start(800);
    connect(dl_file_timer, &QTimer::timeout, this, &MainWindow::songCooked);
}

void MainWindow::downloadSongYoutube(QString &song_name)
{
    QProcess *process = new QProcess(this);

    QString search_str;

    if (song_name.startsWith("https://"))
        search_str = song_name;
    else
    {
        song_name.replace(QRegularExpression("[%.\\/: ]"), " ");
        search_str = "\"ytsearch: " + song_name + "\"";
    }
    QString program = "yt-dlp.exe -x --extract-audio --audio-format wav "
                      + search_str +
                      " --ppa \"ffmpeg: -bitexact -ac 1 -ab 352k -ar 22050\"";

    dl_file_name = song_name;
    timeout_timer = new QTimer(this);
    timeout_timer->setInterval(10000);
    timeout_timer->setSingleShot(true);
    connect(timeout_timer, &QTimer::timeout, this, &MainWindow::songCooked);
    connect(process, &QProcess::finished, this, &MainWindow::downloadFinished);
    timeout_timer->start();
    movie->start();
    ui->loadingLabel->setVisible(true);
    process->start(program);
}

void MainWindow::on_youtubeButton_clicked()
{
    bool ok;
    QString text = QInputDialog::getText(this, tr("Youtube download"),
                                         tr("Type song name or link"),
                                         QLineEdit::Normal,
                                         QDir::home().dirName(), &ok);
    if (ok && !text.isEmpty())
    {
        downloadSongYoutube(text);
    }
}

void MainWindow::loadDropListPaths()
{
    ui->dropList->clear();
    foreach (const ConfigEntry &config, configController.getConfigEntries())
    {
        ui->dropList->addItem(config.getFullName(), config.getName());
    }

    int index = ui->dropList->findData(configController
                                       .getCurrentConfigRef()
                                       ->getName());
    if ( index != -1 ) {
        ui->dropList->setCurrentIndex(index);
    }
}

void MainWindow::refreshScriptPaths()
{
    ConfigEntry *config = configController.getCurrentConfigRef();

    tracklist.setFileName(config->getPath() + "/lyrics_list.cfg");
    dest.setFileName(config->getPath() + "/lyricsmaster.cfg");
}

const QStringList MainWindow::getMostRecentUser() const
{
    QString udpath = configController.getUserDataPath();
    if (udpath.isNull() || udpath.isEmpty())
        return QStringList();
    QString steamPath = udpath.left(udpath.indexOf("/Steam/") + 7);
    QString steamConfig = steamPath + "/config/loginusers.vdf";
    QFile steamConfigFile(steamConfig);

    if (!steamConfigFile.open(QIODevice::ReadOnly)) {
        qWarning("Couldn't open steam config file.");
        return QStringList();
    }

    QByteArray file_data = steamConfigFile.readAll();

    QStringList result;

    QString regex_str = "\"(?<steamid64>\\d+)\"\\s+?{\\s+?\"AccountName\"\\"
                        "s+?\"(?<username>.+?)\".+?\"MostRecent\"\\"
                        "s+\"(?<mostrecent>\\d)\".+?}";
    QRegularExpression regex = QRegularExpression( regex_str,
                               QRegularExpression::DotMatchesEverythingOption);

    QRegularExpressionMatchIterator i = regex.globalMatch(file_data);

    while (true)
    {
        if (i.hasNext()) {
            QRegularExpressionMatch match = i.next();
            QString steamid64 = match.captured("steamid64");
            QString username = match.captured("username");
            QString mostrecent = match.captured("mostrecent");

            if (mostrecent == "1")
            {
                qlonglong num = (steamid64.toLongLong() & 0xFFFFFFFF);
                result << username << QString::number(num);
                break;
            }
        }
        else
            break;
    }

    return result;
}

void MainWindow::updateAccount()
{
    QStringList user = getMostRecentUser();
    if (user.isEmpty()) return;
    ui->account->setText(user.at(0));
    configController.setAccountId(user.at(1));
}

void MainWindow::on_tsayCheckBox_stateChanged(int state)
{
    if (state)
        sayType = "say_team";
    else
        sayType = "say";
}

void MainWindow::on_actionUpdate_YTDL_triggered()
{
    updateManager->updateYTDL();
}


void MainWindow::on_actionUpdate_account_info_triggered()
{
    updateAccount();
}


void MainWindow::on_actionAbout_triggered()
{
    QMessageBox::about(this, "About", "This is Karaoke master version " VERSION
                                      ". Made by Victor G.");
}

void MainWindow::on_actionGuide_triggered()
{
    QMessageBox::information(this, "Guide",
    "Press 'Choose config' and choose your game cfg folder to begin.<br>"
    "Type exec lyrics_list.cfg in the console to see the current loaded songs."
    "<br>Use the settings menu to choose which keys you want to bind.<br>"
    "Press Start and become a DJ.<br>More info "
    "<a href='https://github.com/Catishere/karaoke-master'>here</a>.");
}

void MainWindow::on_actionUpdate_client_triggered()
{
    if (QFileInfo::exists("karaoke-master-update.exe")) {
        auto const conn = new QMetaObject::Connection;
        *conn = connect(updateManager, &UpdateManager::finished,
                        this, [this, conn](const QByteArray info) {
            QObject::disconnect(*conn);
            delete conn;
            QJsonDocument doc = QJsonDocument::fromJson(info);

            if (doc["tag_name"].toString() == VERSION) {
                QMessageBox::information(this, "Update",
                                         "You are running the latest update.");
                return;
            }

            configController.setUpdateNotification(false);
            configController.saveConfig();

            qApp->quit();
            QProcess::startDetached("karaoke-master-update.exe");
        });
        updateManager->getUpdateInfo();
    } else {
        QMessageBox::information(this, "Update",
            "You don't have karaoke-master-update.exe. "
            "You can download the package "
            "<a href='https://github.com/Catishere/karaoke-master/"
            "releases/latest'>here</a>.");
    }
}


void MainWindow::on_actionKey_bindings_triggered()
{
    auto config = configController.getCurrentConfigRef();
    if (config == nullptr) {
        QMessageBox::warning(this, "Key bindings",
                             "Choose your game cfg folder first.");
        return;
    }

    bool ok;
    InputDialog id(this, config->getKeyBindings(), "Key Bindings");
    StringPairList list = id.getStrings(&ok);
    if (ok) {
        config->setKeyBindings(list);
        configController.saveConfig();
    }
}

void MainWindow::showContextMenu(const QPoint &pos)
{
    QMenu contextMenu(tr("Context menu"), ui->tableWidget);

    QAction action("Delete selected songs", ui->tableWidget);
    QAction action2("Add song with lyrics", ui->tableWidget);
    QAction action3("Add song without lyrics", ui->tableWidget);
    QAction action4("Add song from local folder", ui->tableWidget);

    connect(&action, &QAction::triggered,
            this, &MainWindow::on_deleteSongButton_clicked);
    connect(&action2, &QAction::triggered,
            this, &MainWindow::on_searchOnlineButton_clicked);
    connect(&action3, &QAction::triggered,
            this, &MainWindow::on_youtubeButton_clicked);
    connect(&action4, &QAction::triggered,
            this, &MainWindow::on_addSongButton_clicked);

    contextMenu.addAction(&action);
    contextMenu.addAction(&action2);
    contextMenu.addAction(&action3);
    contextMenu.addAction(&action4);
    contextMenu.exec(ui->tableWidget->pos() + mapToGlobal(pos) + QPoint(0, 45));
}

int MainWindow::getTimerInterval(const QString pc)
{
    if (pc == "Potato")
        return 2000;
    if (pc == "Slow")
        return  1000;
    if (pc ==  "Average")
        return 500;
    if (pc == "Fast")
        return 200;
    if (pc == "Alien")
        return 200;
    return 500;
}

void MainWindow::on_actionOptions_triggered()
{
    auto config = configController.getCurrentConfigRef();
    if (config == nullptr) {
        QMessageBox::warning(this, "Options",
                             "Choose your game cfg folder first.");
        return;
    }

    QDialog *dialog = new QDialog(this);

    QVBoxLayout *lytMain = new QVBoxLayout(dialog);
    QHBoxLayout *hbox = new QHBoxLayout();

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok |
                                                       QDialogButtonBox::Close);

    QCheckBox *checkbox = new QCheckBox("Always download song", dialog);
    QGroupBox *groupBox = new QGroupBox(tr("Your PC speed"), dialog);
    QList<QRadioButton *> buttons = {
        new QRadioButton(tr("Potato"), dialog),
        new QRadioButton(tr("Slow"), dialog),
        new QRadioButton(tr("Average"), dialog),
        new QRadioButton(tr("Fast"), dialog),
        new QRadioButton(tr("Alien"), dialog)
    };

    QString currentPC = config->getPc();
    checkbox->setChecked(config->getAlwaysDownload());

    for (auto button : buttons) {
        hbox->addWidget(button);
        if (button->text() == currentPC)
            button->setChecked(true);
    }

    hbox->addStretch(1);
    groupBox->setLayout(hbox);
    lytMain->addWidget(groupBox);
    lytMain->addWidget(checkbox);
    lytMain->addWidget(buttonBox);
    dialog->setWindowTitle("Options");
    dialog->setLayout(lytMain);

    connect(buttonBox, &QDialogButtonBox::accepted, this, [=]() {
        config->setAlwaysDownload(checkbox->isChecked());
        for (auto& button : buttons) {
            if (button->isChecked()) {
                config->setPc(button->text());
                configController.saveConfig();
            }
        }
        dialog->close();
    });

    connect(buttonBox, &QDialogButtonBox::rejected,
            this, [=]() { dialog->close(); });

    dialog->exec();
}

void MainWindow::allLyricsListsFetched()
{
    fetchers_ready = 0;

    if (search_list.isEmpty()) {
        QMessageBox::information(this, "Lyrics",
                                 "Couldn't find these lyrics online!");
        return;
    }

    QDialog *dialog = new QDialog(this);
    QGridLayout *layout = new QGridLayout(dialog);
    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok
                                          | QDialogButtonBox::Cancel);
    auto comboBox = new QComboBox(dialog);

    for (auto &pair : search_list) {
        comboBox->addItem(QIcon(":icon/" + pair.first.right(2) + ".ico"),
                          pair.first.chopped(2));
    }

    layout->addWidget(comboBox);
    layout->addWidget(buttonBox);
    dialog->setWindowTitle("Online Search");
    dialog->setLayout(layout);

    connect(buttonBox, &QDialogButtonBox::accepted, this, [=](){
        QString item = comboBox->currentText();
        qsizetype item_index = comboBox->currentIndex();
        if (!item.isEmpty())
        {
            if (!temp_lyrics_name.isEmpty()) {
                ui->err->setText("Aborted. Another download in progress.");
                return;
            }

            temp_lyrics_name = item;

            for (auto &fetcher : lyrics_fetchers)
                fetcher->fetchLyrics(search_list.at(item_index).second);

            search_list.clear();

            if (configController.getCurrentConfigRef()->getAlwaysDownload()
                || QMessageBox::question(this,"Download song?","Download song?",
                   QMessageBox::Yes|QMessageBox::No) == QMessageBox::Yes)
                downloadSongYoutube(item);
        }
        dialog->close();
    });

    connect(buttonBox, &QDialogButtonBox::rejected, this, [=](){
        search_list.clear();
        dialog->close();
    });

    dialog->exec();
}

void MainWindow::addListWithPriorty(const StringPairList &list)
{
    if (list.isEmpty())
        return;
    auto url = list.at(0).second;

    if (url.startsWith(lyrics_fetchers.at(0)->getEndpoint())) {
        StringPairList copy(list);
        copy.append(search_list);
        search_list = copy;
    } else
        search_list.append(list);
}

void MainWindow::showUpdateNotification()
{
    auto const conn = new QMetaObject::Connection;
    *conn = connect(updateManager, &UpdateManager::finished,
                          this, [this, conn](const QByteArray info) {
        QObject::disconnect(*conn);
        delete conn;
        QJsonDocument doc = QJsonDocument::fromJson(info);
        QString version = doc["tag_name"].toString();
        QString changes = doc["body"].toString();

        if (version == VERSION) {
            if (configController.isUpdateNotification()) {
                configController.setUpdateNotification(true);
                configController.saveConfig();
                QMessageBox::information(this, "Updated",
                                         "The application is successfully"
                                         " updated to version " VERSION
                                         ".\n New things: \n" + changes);
            }
            return;
        }
        auto reply = QMessageBox::question(this, "Update available",
                                           "Update for version " + version + " "
                                           "is available (Current " VERSION ")."
                                           " Do you want to update?",
                                           QMessageBox::Yes | QMessageBox::No,
                                           QMessageBox::Yes);
        if (reply == QMessageBox::Yes)
            on_actionUpdate_client_triggered();
    });

    updateManager->getUpdateInfo();
}

void MainWindow::lyricsListFetched(const StringPairList &list)
{
    addListWithPriorty(list);
    fetchers_ready++;
    if (fetchers_ready >= lyrics_fetchers.size())
        allLyricsListsFetched();
}

void MainWindow::lyricsFetched(const QString& lyrics)
{
    qDebug() << "Saving lyrics...";
    QFile lyrics_file;

    temp_lyrics_name.replace(QChar(0xA0), " ");
    temp_lyrics_name.replace(QRegularExpression("[%.\\/:]"), " ");

    lyrics_file.setFileName("lyrics/" + temp_lyrics_name + ".txt");

    if (lyrics_file.open(QIODevice::WriteOnly))
    {
        lyrics_file.write(lyrics.toUtf8());
        lyrics_file.close();
    }
    else
        qDebug() << lyrics_file.errorString();

    refreshSongList();

    temp_lyrics_name = "";
}


void MainWindow::downloadProgress(qint64 ist, qint64 max)
{
    ui->progressBar->setVisible(true);
    ui->progressBar->setRange(0, max);
    ui->progressBar->setValue(ist);
    if(max < 0) ui->progressBar->setVisible(false);
}


void MainWindow::on_dropList_textActivated(const QString &arg1)
{
    configController.choose(arg1);
    refreshScriptPaths();
}

