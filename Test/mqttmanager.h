#ifndef MQTTMANAGER_H
#define MQTTMANAGER_H

#include <QObject>
#include <QString>
#include <QMqttClient>
#include <QMqttSubscription>
#include <QVariantList>
#include <QHash>

/*
 * 为什么需要 MqttManager：
 * - QML 侧需要一个稳定、可调用的 MQTT 客户端入口（连接/订阅/发布/错误回调）。
 * - 直接在 QML 使用底层 MQTT API 会导致状态管理分散，难以与数据库流程联动。
 * - 该类统一维护连接状态和订阅对象生命周期，减少重复订阅和信号处理分叉。
 */
class MqttManager : public QObject
{
    Q_OBJECT

public:
    explicit MqttManager(QObject *parent = nullptr);
    ~MqttManager();

    Q_INVOKABLE void connectToBroker(const QString &host, int port, const QString &clientId, const QString &username, const QString &password);
    Q_INVOKABLE void disconnectFromBroker();
    Q_INVOKABLE void subscribe(const QString &topic);
    Q_INVOKABLE void unsubscribe(const QString &topic);
    Q_INVOKABLE void publish(const QString &topic, const QString &message);

    Q_INVOKABLE bool isConnected() const;

signals:
    void connected();
    void disconnected();
    void messageReceived(const QString &topic, const QString &message, int qos, bool retained, bool duplicated);
    void error(const QString &errorMessage);

private slots:
    void onConnected();
    void onDisconnected();
    void onErrorChanged(QMqttClient::ClientError error);

private:
    QMqttClient *m_client; // MQTT 会话核心对象，负责网络连接和协议收发
    QHash<QString, QMqttSubscription *> m_subscriptions; // topic -> 订阅句柄，避免重复订阅并支持后续取消
};

#endif // MQTTMANAGER_H