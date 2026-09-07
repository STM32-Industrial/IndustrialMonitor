#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlError>
#include <QSqlRecord>
#include <QVariant>
#include <QCoreApplication>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 初始化折线图（历史趋势页）
    setupCharts();

    // 初始化数据库，并回放历史数据
    if (initDatabase())
        loadHistoryFromDb();

    // 初始化 MQTT 客户端
    mqttClient = new QMqttClient(this);
    connect(mqttClient, &QMqttClient::connected, [this]() {
        mqttClient->subscribe(QMqttTopicFilter("sensor/data"), 1);
        mqttClient->subscribe(QMqttTopicFilter("sensor/status"), 1);
        setConnectionState(true);
    });
    connect(mqttClient, &QMqttClient::disconnected, [this]() {
        setConnectionState(false);
    });
    connect(mqttClient, &QMqttClient::messageReceived, this, &MainWindow::onMessageReceived);

    mqttClient->setHostname(brokerAddress);
    mqttClient->setPort(brokerPort);
    mqttClient->connectToHost();

    connect(ui->valveBtnOpen, &QPushButton::clicked, this, &MainWindow::onValveOpenClicked);
    connect(ui->valveBtnClose, &QPushButton::clicked, this, &MainWindow::onValveCloseClicked);
}

MainWindow::~MainWindow()
{
    if (db.isOpen())
        db.close();
    delete ui;
}

// ---------- SQLite 初始化 ----------
bool MainWindow::initDatabase()
{
    if (QSqlDatabase::contains("industrial_conn"))
        db = QSqlDatabase::database("industrial_conn");
    else
        db = QSqlDatabase::addDatabase("QSQLITE", "industrial_conn");

    // 数据库存放到可执行文件同目录，保证路径稳定可预期
    db.setDatabaseName(QCoreApplication::applicationDirPath() + "/industrial_data.db");
    if (!db.open()) {
        qWarning() << "数据库打开失败:" << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    bool ok = query.exec(
        "CREATE TABLE IF NOT EXISTS sensor_data ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  temp REAL,"
        "  humi REAL,"
        "  smoke INTEGER,"
        "  rpm INTEGER,"
        "  pressure REAL,"
        "  vibration REAL,"
        "  status INTEGER"
        ")");
    if (!ok)
        qWarning() << "建表失败:" << query.lastError().text();
    return ok;
}

void MainWindow::insertRecord(double temp, double humi, double smoke,
                              int rpm, double pressure, double vibration, int status)
{
    if (!db.isOpen())
        return;

    if (!db.transaction()) {
        qWarning() << "开启事务失败:" << db.lastError().text();
        return;
    }

    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO sensor_data (temp, humi, smoke, rpm, pressure, vibration, status) "
        "VALUES (:temp, :humi, :smoke, :rpm, :pressure, :vibration, :status)");
    query.bindValue(":temp", temp);
    query.bindValue(":humi", humi);
    query.bindValue(":smoke", qRound(smoke));
    query.bindValue(":rpm", rpm);
    query.bindValue(":pressure", pressure);
    query.bindValue(":vibration", vibration);
    query.bindValue(":status", status);

    if (!query.exec())
        qWarning() << "插入失败:" << query.lastError().text();

    db.commit();
}

void MainWindow::loadHistoryFromDb()
{
    qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - TIME_WINDOW_MS;
    // SQLite CURRENT_TIMESTAMP 存的是 UTC，故 cutoff 也转成 UTC 再比较
    QString cutStr = QDateTime::fromMSecsSinceEpoch(cutoff)
                         .toUTC().toString("yyyy-MM-dd HH:mm:ss");
    QSqlQuery query(db);
    query.prepare("SELECT timestamp, temp, humi, smoke, rpm, pressure, vibration, status"
                  " FROM sensor_data WHERE timestamp >= :cut ORDER BY id ASC LIMIT 1000");
    query.bindValue(":cut", cutStr);
    query.exec();
    while (query.next()) {
        QDateTime ts = QDateTime::fromString(
                           query.value("timestamp").toString(),
                           "yyyy-MM-dd HH:mm:ss");
        ts.setTimeSpec(Qt::UTC);
        if (!ts.isValid())
            continue;
        qint64 ms = ts.toMSecsSinceEpoch();
        double temp = query.value("temp").toDouble();
        double humi = query.value("humi").toDouble();
        double smoke = query.value("smoke").toDouble();
        int rpm = query.value("rpm").toInt();
        double press = query.value("pressure").toDouble();
        double vib = query.value("vibration").toDouble();
        int status = query.value("status").toInt();

        appendDataPoint(ms, temp, humi, smoke);
        appendStatusPoint(ms, status);
        // 仅回放图表；rpm/press/vib 历史不再回放（主界面只显示最新实时值）
        Q_UNUSED(rpm); Q_UNUSED(press); Q_UNUSED(vib);
    }
}

// ---------- 折线图初始化 ----------
void MainWindow::setupCharts()
{
    const QColor bg(0x14, 0x1b, 0x26);
    const QColor gridColor(0x23, 0x2d, 0x3d);
    const QColor textColor(0x8b, 0x94, 0x9e);

    auto makeChart = [&](const QString &title) -> QChart * {
        QChart *chart = new QChart();
        chart->setTitle(title);
        chart->setBackgroundBrush(bg);
        chart->setBackgroundRoundness(8);
        chart->setTitleBrush(QColor(0xe6, 0xed, 0xf3));
        QFont tf = chart->titleFont();
        tf.setPointSize(11);
        chart->setTitleFont(tf);
        chart->legend()->setLabelColor(textColor);
        chart->legend()->setBrush(bg);
        chart->legend()->setBorderColor(gridColor);
        chart->setMargins(QMargins(4, 4, 4, 4));
        return chart;
    };

    auto makeTimeAxis = [&]() -> QDateTimeAxis * {
        QDateTimeAxis *axis = new QDateTimeAxis();
        axis->setFormat("HH:mm:ss");
        axis->setLabelsColor(textColor);
        axis->setLinePenColor(gridColor);
        axis->setGridLineColor(gridColor);
        axis->setMinorGridLineColor(gridColor);
        axis->setTitleBrush(textColor);
        axis->setTitleText("时间");
        axis->setTickCount(3);
        return axis;
    };

    auto makeValueAxis = [&](const QString &title) -> QValueAxis * {
        QValueAxis *axis = new QValueAxis();
        axis->setTitleText(title);
        axis->setLabelsColor(textColor);
        axis->setLinePenColor(gridColor);
        axis->setGridLineColor(gridColor);
        axis->setTitleBrush(textColor);
        return axis;
    };

    // --- 温湿度图 ---
    tempSeries = new QLineSeries();
    tempSeries->setName("温度 (°C)");
    tempSeries->setColor(QColor(0x4f, 0xc3, 0xf7));
    humiSeries = new QLineSeries();
    humiSeries->setName("湿度 (%)");
    humiSeries->setColor(QColor(0x34, 0xd3, 0x99));

    QChart *thChart = makeChart("温湿度趋势");
    thChart->addSeries(tempSeries);
    thChart->addSeries(humiSeries);
    timeAxisTH = makeTimeAxis();
    valueAxisTH = makeValueAxis("数值");
    thChart->addAxis(timeAxisTH, Qt::AlignBottom);
    thChart->addAxis(valueAxisTH, Qt::AlignLeft);
    tempSeries->attachAxis(timeAxisTH);
    tempSeries->attachAxis(valueAxisTH);
    humiSeries->attachAxis(timeAxisTH);
    humiSeries->attachAxis(valueAxisTH);

    tempHumiChartView = new QChartView(thChart);
    tempHumiChartView->setRenderHint(QPainter::Antialiasing);
    ui->tempHumiChartLayout->addWidget(tempHumiChartView);

    // --- 烟雾图 ---
    smokeSeries = new QLineSeries();
    smokeSeries->setName("烟雾");
    smokeSeries->setColor(QColor(0xfb, 0xbf, 0x24));

    QChart *smokeChart = makeChart("烟雾趋势");
    smokeChart->addSeries(smokeSeries);
    timeAxisSmoke = makeTimeAxis();
    valueAxisSmoke = makeValueAxis("烟雾值");
    smokeChart->addAxis(timeAxisSmoke, Qt::AlignBottom);
    smokeChart->addAxis(valueAxisSmoke, Qt::AlignLeft);
    smokeSeries->attachAxis(timeAxisSmoke);
    smokeSeries->attachAxis(valueAxisSmoke);

    smokeChartView = new QChartView(smokeChart);
    smokeChartView->setRenderHint(QPainter::Antialiasing);
    ui->smokeChartLayout->addWidget(smokeChartView);

    // --- 设备状态图 ---
    statusSeries = new QLineSeries();
    statusSeries->setName("状态 (0=正常 1=报警)");
    statusSeries->setColor(QColor(0xa7, 0x8b, 0xfa));

    QChart *statusChart = makeChart("设备状态");
    statusChart->addSeries(statusSeries);
    timeAxisStatus = makeTimeAxis();
    valueAxisStatus = makeValueAxis("状态");
    valueAxisStatus->setRange(0, 1);
    valueAxisStatus->setTickCount(2);
    valueAxisStatus->setLabelFormat("%d");
    statusChart->addAxis(timeAxisStatus, Qt::AlignBottom);
    statusChart->addAxis(valueAxisStatus, Qt::AlignLeft);
    statusSeries->attachAxis(timeAxisStatus);
    statusSeries->attachAxis(valueAxisStatus);

    statusChartView = new QChartView(statusChart);
    statusChartView->setRenderHint(QPainter::Antialiasing);
    ui->statusChartLayout->addWidget(statusChartView);
}

// ---------- 数据追加（仅来自 MQTT 真实消息） ----------
void MainWindow::appendDataPoint(qint64 ms, double temp, double humi, double smoke)
{
    tempSeries->append(ms, temp);
    humiSeries->append(ms, humi);
    smokeSeries->append(ms, smoke);

    trimSeries(tempSeries);
    trimSeries(humiSeries);
    trimSeries(smokeSeries);

    updateTimeAxis(timeAxisTH);
    updateTimeAxis(timeAxisSmoke);

    // 温湿度纵轴自适应
    double minV = qMin(tempSeries->points().first().y(), humiSeries->points().first().y());
    double maxV = qMax(tempSeries->points().first().y(), humiSeries->points().first().y());
    for (const QPointF &p : tempSeries->points()) { minV = qMin(minV, p.y()); maxV = qMax(maxV, p.y()); }
    for (const QPointF &p : humiSeries->points())  { minV = qMin(minV, p.y()); maxV = qMax(maxV, p.y()); }
    double pad = qMax(1.0, (maxV - minV) * 0.15);
    valueAxisTH->setRange(minV - pad, maxV + pad);

    // 烟雾纵轴自适应
    double sMin = smokeSeries->points().first().y();
    double sMax = sMin;
    for (const QPointF &p : smokeSeries->points()) { sMin = qMin(sMin, p.y()); sMax = qMax(sMax, p.y()); }
    double sPad = qMax(1.0, (sMax - sMin) * 0.15);
    valueAxisSmoke->setRange(qMax(0.0, sMin - sPad), sMax + sPad);
}

void MainWindow::appendStatusPoint(qint64 ms, int status)
{
    statusSeries->append(ms, status);
    trimSeries(statusSeries);
    updateTimeAxis(timeAxisStatus);
}

void MainWindow::trimSeries(QLineSeries *series)
{
    while (series->count() > MAX_POINTS)
        series->removePoints(0, 1);
}

void MainWindow::updateTimeAxis(QDateTimeAxis *axis)
{
    // 时间轴固定显示最近 TIME_WINDOW_MS 的窗口，始终跟随最新点，避免历史数据把跨度拉大
    const auto *series = axis == timeAxisTH ? tempSeries
                       : (axis == timeAxisSmoke ? smokeSeries : statusSeries);
    if (series->count() < 1) return;
    qint64 last = static_cast<qint64>(series->points().last().x());
    qint64 begin = last - TIME_WINDOW_MS;
    axis->setRange(QDateTime::fromMSecsSinceEpoch(begin),
                   QDateTime::fromMSecsSinceEpoch(last));
}

void MainWindow::setConnectionState(bool connected)
{
    if (connected) {
        ui->connDot->setStyleSheet("background-color:#34d399; border-radius:6px;");
        ui->connStateLabel->setText("已连接");
    } else {
        ui->connDot->setStyleSheet("background-color:#f87171; border-radius:6px;");
        ui->connStateLabel->setText("已断开");
    }
}

// ---------- MQTT 消息处理 ----------
void MainWindow::onMessageReceived(const QByteArray &message, const QMqttTopicName &topic) {
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(message, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return;

    QJsonObject obj = doc.object();
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (topic.name() == "sensor/data") {
        double temp = obj["temp"].toDouble();
        double humi = obj["humi"].toDouble();
        double smoke = obj["smoke"].toDouble();
        int rpm = obj["rpm"].toInt();
        double press = obj["press"].toDouble();
        double vib = obj["vib"].toDouble();
        int state = obj["state"].toInt(-1);

        // 更新环境卡片
        ui->tempValue->setText(QString::number(temp, 'f', 1));
        ui->humiValue->setText(QString::number(humi, 'f', 1));
        ui->smokeValue->setText(QString::number(smoke, 'f', 1));

        // 更新机器参数卡片
        ui->rpmValue->setText(QString::number(rpm));
        ui->pressValue->setText(QString::number(press, 'f', 2));
        ui->vibValue->setText(QString::number(vib, 'f', 2));

        // 更新运行状态（state: 0=停止 1=运行 2=空闲）
        if (state >= 0) {
            if (state == 1) {          // 运行
                ui->statusValue->setText("运行");
                ui->statusValue->setStyleSheet(
                    "color:#34d399; background-color:#12291f; border:1px solid #1f4d38;");
            } else if (state == 2) {   // 空闲
                ui->statusValue->setText("空闲");
                ui->statusValue->setStyleSheet(
                    "color:#fbbf24; background-color:#2d2416; border:1px solid #5a4d28;");
            } else {                   // 停止
                ui->statusValue->setText("停止");
                ui->statusValue->setStyleSheet(
                    "color:#8b949e; background-color:#1c2735; border:1px solid #33415c;");
            }
        }

        // 更新阀门状态（valve: 0=关阀 1=开阀）
        int valve = obj["valve"].toInt(-1);
        if (valve >= 0) {
            if (valve == 1) {
                ui->valveValue->setText("开阀");
                ui->valveValue->setStyleSheet(
                    "color:#34d399; background-color:#12291f; border:1px solid #1f4d38;");
            } else {
                ui->valveValue->setText("关阀");
                ui->valveValue->setStyleSheet(
                    "color:#f87171; background-color:#2b1318; border:1px solid #6b1f2b;");
            }
        }

        // 追加到折线图（真实数据）
        appendDataPoint(now, temp, humi, smoke);

        // 写入 SQLite（status 存设备原始 state：0=停止 1=运行 2=空闲）
        insertRecord(temp, humi, smoke, rpm, press, vib, state);
    }
}

void MainWindow::onValveOpenClicked() {
    mqttClient->publish(QMqttTopicName("sensor/ctrl"), QByteArray("1"));
}

void MainWindow::onValveCloseClicked() {
    mqttClient->publish(QMqttTopicName("sensor/ctrl"), QByteArray("0"));
}
