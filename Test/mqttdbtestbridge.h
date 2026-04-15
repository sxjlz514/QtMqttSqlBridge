#ifndef MQTTDBTESTBRIDGE_H
#define MQTTDBTESTBRIDGE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMqttTopicFilter>

class MqttManager;
class SqlManagerWrapper;

/*
 * 为什么需要 MqttDbTestBridge：
 * - 项目核心目标是“MQTT 消息 -> 数据库存储/查询控制”，该类即两者的桥接中枢。
 * - 它把 MQTT 命令主题中的 JSON 指令翻译为 SQL 操作，并把执行结果回发到响应主题。
 * - 同时支持按 topic filter 记录消息日志到指定表，形成可观测的测试闭环。
 */
class MqttDbTestBridge : public QObject
{
    Q_OBJECT
    // Q_PROPERTY 使属性可被 QML 绑定与读写，NOTIFY 信号用于触发界面自动刷新
    Q_PROPERTY(QString commandTopic READ commandTopic WRITE setCommandTopic NOTIFY commandTopicChanged)
    Q_PROPERTY(QString responseTopic READ responseTopic WRITE setResponseTopic NOTIFY responseTopicChanged)
    Q_PROPERTY(QString messageLogTable READ messageLogTable WRITE setMessageLogTable NOTIFY messageLogTableChanged)
    Q_PROPERTY(QStringList logSubscriptions READ logSubscriptions NOTIFY logSubscriptionsChanged)

public:
    explicit MqttDbTestBridge(MqttManager *mqtt, SqlManagerWrapper *sql, QObject *parent = nullptr);

    QString commandTopic() const { return m_commandTopic; }
    void setCommandTopic(const QString &t);

    QString responseTopic() const { return m_responseTopic; }
    void setResponseTopic(const QString &t);

    QString messageLogTable() const { return m_messageLogTable; }
    void setMessageLogTable(const QString &t);

    QStringList logSubscriptions() const;

    Q_INVOKABLE bool subscribeLogTopic(const QString &topicFilter);
    Q_INVOKABLE bool unsubscribeLogTopic(const QString &topicFilter);
    Q_INVOKABLE bool createDefaultMqttLogTable(const QString &tableName);

signals:
    void commandTopicChanged();
    void responseTopicChanged();
    void messageLogTableChanged();
    void logSubscriptionsChanged();

private slots:
    void onMqttConnected();
    void onMqttMessage(const QString &topic, const QString &message, int qos, bool retained, bool duplicated);

private:
    void subscribeAllTopics();
    bool handleCommandJson(const QString &message);
    void publishResponse(const QJsonObject &obj);
    bool topicMatchesLogFilter(const QString &topic) const;
    bool insertMessageLog(const QString &topic, const QString &message, int qos, bool retained, bool duplicated);
    static QMap<QString, QVariant> jsonObjectToVariantMap(const QJsonObject &o);
    static QJsonValue variantToJson(const QVariant &v);
    static QJsonArray variantListToJson(const QVariantList &list);

    MqttManager *m_mqtt = nullptr;            // MQTT 适配器，负责收发消息
    SqlManagerWrapper *m_sql = nullptr;       // DB 适配器，负责执行数据库操作
    QString m_commandTopic;                   // 控制指令主题：监听 JSON 命令（exec/query/select/...）
    QString m_responseTopic;                  // 响应主题：发布命令执行结果
    QString m_messageLogTable;                // 日志落库目标表（记录普通订阅消息）
    QList<QMqttTopicFilter> m_logTopicFilters; // MQTT 过滤器列表，支持通配符匹配后批量落库
};

#endif // MQTTDBTESTBRIDGE_H
