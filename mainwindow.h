#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMqttClient>
#include <QJsonDocument>
#include <QJsonObject>

#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QDateTime>

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>

#include "mqtt_cfg.h"   // MQTT服务器地址 (私有配置, 已gitignore不入库)

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onMessageReceived(const QByteArray &message, const QMqttTopicName &topic);
    void onValveOpenClicked();
    void onValveCloseClicked();

private:
    void setupCharts();
    void appendDataPoint(qint64 ms, double temp, double humi, double smoke);
    void appendStatusPoint(qint64 ms, int status);
    void trimSeries(QLineSeries *series);
    void updateTimeAxis(QDateTimeAxis *axis);
    void setConnectionState(bool connected);

    // ---- SQLite ----
    bool initDatabase();
    void insertRecord(double temp, double humi, double smoke,
                      int rpm, double pressure, double vibration, int status);
    void loadHistoryFromDb();

    Ui::MainWindow *ui;

    QSqlDatabase db;

    QMqttClient *mqttClient;
    QString brokerAddress = MQTT_BROKER_IP;   // 从 mqtt_cfg.h 读取
    quint16 brokerPort = MQTT_BROKER_PORT;    // 从 mqtt_cfg.h 读取

    // ---- 折线图（历史趋势） ----
    QChartView *tempHumiChartView = nullptr;
    QChartView *smokeChartView = nullptr;
    QChartView *statusChartView = nullptr;

    QLineSeries *tempSeries = nullptr;
    QLineSeries *humiSeries = nullptr;
    QLineSeries *smokeSeries = nullptr;
    QLineSeries *statusSeries = nullptr;

    QDateTimeAxis *timeAxisTH = nullptr;
    QDateTimeAxis *timeAxisSmoke = nullptr;
    QDateTimeAxis *timeAxisStatus = nullptr;

    QValueAxis *valueAxisTH = nullptr;
    QValueAxis *valueAxisSmoke = nullptr;
    QValueAxis *valueAxisStatus = nullptr;

    static const int MAX_POINTS = 600;        // 每条曲线最多保留的数据点数
    static const qint64 TIME_WINDOW_MS = 30 * 60 * 1000;  // 时间轴固定显示的最近窗口(30分钟)
};
#endif // MAINWINDOW_H
