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

#include <QTimer>

#include "mqtt_cfg.h"   // MQTT服务器地址 (私有配置, 已gitignore不入库)

// ---- OTA 分块参数 (与设备端 ota.h 一致, 改动需同步) ----
#define OTA_CHUNK_SIZE  128     // 每块原始字节数
#define OTA_TOPIC       "ota/fw"    // 上位机 -> 设备
#define OTA_ACK_TOPIC   "ota/ack"   // 设备 -> 上位机

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

    // ---- OTA ----
    void onOtaBrowseClicked();
    void onOtaStartClicked();
    void onOtaAckReceived(const QByteArray &message, const QMqttTopicName &topic);
    void onOtaTimeout();

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

    // ---- OTA 状态机 ----
    enum class OtaState {
        Idle,        // 空闲
        WaitBegin,   // 已发 B, 等待 R,BEGIN
        Sending,     // 已发 D,seq, 等待 R,OK,seq
        WaitDone     // 已发 E, 等待 R,DONE / R,FAIL
    };

    void otaSendBegin();                     // 发 B,size,crc,ver
    void otaSendChunk(int seq);              // 发 D,seq,hex
    void otaSendEnd();                       // 发 E
    void otaPublish(const QString &payload); // 发布到 OTA_TOPIC
    void otaSetProgress(int done, int total);
    void otaLog(const QString &msg);         // 状态栏日志
    void otaAbort(const QString &reason);
    void otaResetUi();

    OtaState otaState = OtaState::Idle;
    QByteArray otaFirmware;                  // 固件内容
    quint32    otaCrc = 0;                   // 固件CRC32
    quint32    otaVer = 0;                   // 版本号
    int        otaTotalChunks = 0;           // 总块数
    int        otaNextSeq = 0;               // 下一块待发序号
    int        otaPendingSeq = 0;            // 当前等待确认的序号
    QTimer    *otaTimer = nullptr;           // 重发定时器
    static constexpr int OTA_TIMEOUT_MS = 3000;         // 数据块/结束帧确认超时
    static constexpr int OTA_BEGIN_TIMEOUT_MS = 25000;  // 等待设备擦除暂存区(约8~16秒), 给足时间
};
#endif // MAINWINDOW_H
