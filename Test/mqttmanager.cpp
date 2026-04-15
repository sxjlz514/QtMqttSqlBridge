#include "mqttmanager.h"
#include <QDebug>
#include <QMqttMessage>

/*
 * 实现说明：
 * - 把 QMqttClient 的底层信号转换为更适合 UI/业务消费的语义信号。
 * - 在订阅管理中保留 topic->subscription 映射，防止重复 subscribe 带来的重复消息回调。
 */

MqttManager::MqttManager(QObject *parent) : QObject(parent)
{
    m_client = new QMqttClient(this);

    connect(m_client, &QMqttClient::connected, this, &MqttManager::onConnected);
    connect(m_client, &QMqttClient::disconnected, this, &MqttManager::onDisconnected);
    // QOverload 用于显式指定重载信号签名，避免函数指针推导歧义
    connect(m_client, QOverload<QMqttClient::ClientError>::of(&QMqttClient::errorChanged), this, &MqttManager::onErrorChanged);
}

MqttManager::~MqttManager()
{
    if (m_client->state() == QMqttClient::Connected)
        m_client->disconnectFromHost();
}

void MqttManager::connectToBroker(const QString &host, int port, const QString &clientId, const QString &username, const QString &password)
{
    m_client->setHostname(host);
    m_client->setPort(port);
    m_client->setClientId(clientId);

    if (!username.isEmpty())
        m_client->setUsername(username);

    if (!password.isEmpty())
        m_client->setPassword(password);

    m_client->connectToHost();
    qDebug() << "Connecting to MQTT broker:" << host << "port:" << port;
}

void MqttManager::disconnectFromBroker()
{
    if (m_client->state() == QMqttClient::Connected)
        m_client->disconnectFromHost();
}

void MqttManager::subscribe(const QString &topic)
{
    if (m_client->state() == QMqttClient::Connected) {
        if (m_subscriptions.contains(topic)) {
            qDebug() << "Already subscribed to topic:" << topic;
            return;
        }

        QMqttSubscription *subscription = m_client->subscribe(QMqttTopicFilter(topic));
        if (!subscription) {
            qDebug() << "Subscribe failed for topic:" << topic;
            emit error("Subscribe failed for topic: " + topic);
            return;
        }

        m_subscriptions.insert(topic, subscription);
        connect(subscription, &QMqttSubscription::messageReceived, this,
                [this](const QMqttMessage &mqttMessage) {
            // Lambda 捕获 this：在收到消息时直接访问成员并向外发统一信号
            const QString topicName = mqttMessage.topic().name();
            const QString payload = QString::fromUtf8(mqttMessage.payload());
            qDebug() << "Received message from topic:" << topicName
                     << "qos:" << mqttMessage.qos()
                     << "retained:" << mqttMessage.retain()
                     << "duplicated:" << mqttMessage.duplicate()
                     << "message:" << payload;
            emit messageReceived(topicName,
                                 payload,
                                 mqttMessage.qos(),
                                 mqttMessage.retain(),
                                 mqttMessage.duplicate());
        });
        qDebug() << "Subscribed to topic:" << topic;
    }
    else
    {
        qDebug() << "Cannot subscribe: not connected to broker";
        emit error("Cannot subscribe: not connected to broker");
    }
}

void MqttManager::unsubscribe(const QString &topic)
{
    if (m_client->state() == QMqttClient::Connected) {
        m_client->unsubscribe(QMqttTopicFilter(topic));
        m_subscriptions.remove(topic);
        qDebug() << "Unsubscribed from topic:" << topic;
    } else {
        qDebug() << "Cannot unsubscribe: not connected to broker";
        emit error("Cannot unsubscribe: not connected to broker");
    }
}

void MqttManager::publish(const QString &topic, const QString &message)
{
    if (m_client->state() == QMqttClient::Connected)
    {
        m_client->publish(QMqttTopicName(topic), message.toUtf8());
        qDebug() << "Published message to topic:" << topic << "message:" << message;
    }
    else
    {
        qDebug() << "Cannot publish: not connected to broker";
        emit error("Cannot publish: not connected to broker");
    }
}

bool MqttManager::isConnected() const
{
    return m_client->state() == QMqttClient::Connected;
}

void MqttManager::onConnected()
{
    qDebug() << "Connected to MQTT broker";
    emit connected();
}

void MqttManager::onDisconnected()
{
    m_subscriptions.clear();
    qDebug() << "Disconnected from MQTT broker";
    emit disconnected();
}

void MqttManager::onErrorChanged(QMqttClient::ClientError Error)
{
    QString errorMessage;
    switch (Error)
    {
    case QMqttClient::NoError:
        errorMessage = "No error";
        break;
    case QMqttClient::InvalidProtocolVersion:
        errorMessage = "Invalid protocol version";
        break;
    case QMqttClient::IdRejected:
        errorMessage = "Client ID rejected";
        break;
    case QMqttClient::ServerUnavailable:
        errorMessage = "Server unavailable";
        break;
    case QMqttClient::BadUsernameOrPassword:
        errorMessage = "Bad username or password";
        break;
    case QMqttClient::NotAuthorized:
        errorMessage = "Not authorized";
        break;
    case QMqttClient::TransportInvalid:
        errorMessage = "Network error";
        break;
    case QMqttClient::ProtocolViolation:
        errorMessage = "The underlying transmission causes errors. For example, the connection may be unexpectedly interrupted.";
        break;
    case QMqttClient::UnknownError:
        errorMessage = "Unknown error";
        break;
    default:
        errorMessage = "Unknown error";
        break;
    }
    qDebug() << "MQTT error:" << errorMessage;
    emit error(errorMessage);
}