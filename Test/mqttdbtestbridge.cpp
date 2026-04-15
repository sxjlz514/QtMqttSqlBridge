#include "mqttdbtestbridge.h"
#include "mqttmanager.h"
#include "sqlmanagerwrapper.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QDebug>

/*
 * 桥接流程总览：
 * 1) 接收 MQTT 消息；
 * 2) 命中 commandTopic 时按 JSON 指令执行数据库操作并回发响应；
 * 3) 命中日志过滤器时把消息结构化写入 messageLogTable。
 */

namespace
{
QByteArray encodeJsonStringValue(const QString &value)
{
    QJsonArray wrapper;
    wrapper.append(value);
    const QByteArray json = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
    return json.mid(1, json.size() - 2);
}

QByteArray jsonValueToCompactJson(const QJsonValue &value)
{
    switch (value.type())
    {
    case QJsonValue::Object:
        return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    case QJsonValue::Array:
        return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
    case QJsonValue::Bool:
        return value.toBool() ? QByteArrayLiteral("true") : QByteArrayLiteral("false");
    case QJsonValue::Double:
    {
        QJsonArray wrapper;
        wrapper.append(value);
        const QByteArray json = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
        return json.mid(1, json.size() - 2);
    }
    case QJsonValue::Null:
        return QByteArrayLiteral("null");
    case QJsonValue::String:
        return encodeJsonStringValue(value.toString());
    case QJsonValue::Undefined:
    default:
        return {};
    }
}

QByteArray normalizePayloadForJsonStorage(const QString &message)
{
    // 目标：尽量把 payload 存成合法 JSON（对象/数组/标量），失败时回退为 JSON 字符串
    QJsonParseError parseError{};
    const QByteArray rawPayload = message.toUtf8();
    const QJsonDocument document = QJsonDocument::fromJson(rawPayload, &parseError);
    if (parseError.error == QJsonParseError::NoError && !document.isNull())
        return document.toJson(QJsonDocument::Compact);

    const QByteArray trimmedPayload = message.trimmed().toUtf8();
    if (!trimmedPayload.isEmpty())
    {
        const QJsonDocument wrappedDocument = QJsonDocument::fromJson(QByteArrayLiteral("[") + trimmedPayload + QByteArrayLiteral("]"), &parseError);
        if (parseError.error == QJsonParseError::NoError && wrappedDocument.isArray() && wrappedDocument.array().size() == 1)
            return jsonValueToCompactJson(wrappedDocument.array().at(0));
    }

    return encodeJsonStringValue(message);
}
}

MqttDbTestBridge::MqttDbTestBridge(MqttManager *mqtt, SqlManagerWrapper *sql, QObject *parent)
    : QObject(parent)
    , m_mqtt(mqtt)
    , m_sql(sql)
    , m_commandTopic(QStringLiteral("db/test/cmd"))
    , m_responseTopic(QStringLiteral("db/test/resp"))
    , m_messageLogTable(QStringLiteral("mqtt_inbox"))
{
    qDebug() << "[bridge] MqttDbTestBridge created"
             << "commandTopic=" << m_commandTopic
             << "responseTopic=" << m_responseTopic
             << "messageLogTable=" << m_messageLogTable;
    connect(m_mqtt, &MqttManager::connected, this, &MqttDbTestBridge::onMqttConnected);
    connect(m_mqtt, &MqttManager::messageReceived, this, &MqttDbTestBridge::onMqttMessage);
}

void MqttDbTestBridge::setCommandTopic(const QString &t)
{
    if (m_commandTopic == t)
        return;
    qDebug() << "[bridge] commandTopic changed from" << m_commandTopic << "to" << t;
    m_commandTopic = t;
    emit commandTopicChanged();
}

void MqttDbTestBridge::setResponseTopic(const QString &t)
{
    if (m_responseTopic == t)
        return;
    qDebug() << "[bridge] responseTopic changed from" << m_responseTopic << "to" << t;
    m_responseTopic = t;
    emit responseTopicChanged();
}

void MqttDbTestBridge::setMessageLogTable(const QString &t)
{
    if (m_messageLogTable == t)
        return;
    qDebug() << "[bridge] messageLogTable changed from" << m_messageLogTable << "to" << t;
    m_messageLogTable = t;
    emit messageLogTableChanged();
}

QStringList MqttDbTestBridge::logSubscriptions() const
{
    QStringList out;
    out.reserve(m_logTopicFilters.size());
    for (const QMqttTopicFilter &f : m_logTopicFilters)
        out.append(f.filter());
    return out;
}

bool MqttDbTestBridge::subscribeLogTopic(const QString &topicFilter)
{
    qDebug() << "[bridge] subscribeLogTopic called with" << topicFilter;
    QMqttTopicFilter f(topicFilter);
    if (!f.isValid()) {
        qWarning() << "Invalid MQTT topic filter:" << topicFilter;
        return false;
    }
    for (const QMqttTopicFilter &existing : m_logTopicFilters)
    {
        if (existing.filter() == f.filter())
        {
            qDebug() << "[bridge] topic already subscribed:" << topicFilter;
            return true;
        }
    }
    m_logTopicFilters.append(f);
    qDebug() << "[bridge] topic appended, total subscriptions:" << m_logTopicFilters.size();
    emit logSubscriptionsChanged();
    if (m_mqtt->isConnected())
        m_mqtt->subscribe(f.filter());
    return true;
}

bool MqttDbTestBridge::unsubscribeLogTopic(const QString &topicFilter)
{
    qDebug() << "[bridge] unsubscribeLogTopic called with" << topicFilter;
    for (int i = 0; i < m_logTopicFilters.size(); ++i) {
        if (m_logTopicFilters.at(i).filter() == topicFilter) {
            m_logTopicFilters.removeAt(i);
            qDebug() << "[bridge] topic removed, remaining subscriptions:" << m_logTopicFilters.size();
            emit logSubscriptionsChanged();
            if (m_mqtt->isConnected())
                m_mqtt->unsubscribe(topicFilter);
            return true;
        }
    }
    return false;
}

bool MqttDbTestBridge::createDefaultMqttLogTable(const QString &tableName)
{
    qDebug() << "[bridge] createDefaultMqttLogTable called with" << tableName;
    if (!m_sql->isConnected()) {
        qWarning() << "createDefaultMqttLogTable: database not connected";
        return false;
    }
    const QString t = tableName.isEmpty() ? m_messageLogTable : tableName;
    QVariantMap fields;
    fields.insert(QStringLiteral("id"), QStringLiteral("INT AUTO_INCREMENT PRIMARY KEY"));
    fields.insert(QStringLiteral("topic"), QStringLiteral("VARCHAR(512) NOT NULL"));
    fields.insert(QStringLiteral("payload"), QStringLiteral("JSON NOT NULL"));
    fields.insert(QStringLiteral("qos"), QStringLiteral("TINYINT UNSIGNED NOT NULL DEFAULT 0"));
    fields.insert(QStringLiteral("retained"), QStringLiteral("BOOLEAN NOT NULL DEFAULT FALSE"));
    fields.insert(QStringLiteral("duplicated"), QStringLiteral("BOOLEAN NOT NULL DEFAULT FALSE"));
    fields.insert(QStringLiteral("payload_size"), QStringLiteral("INT NOT NULL DEFAULT 0"));
    fields.insert(QStringLiteral("received_at"), QStringLiteral("DATETIME DEFAULT CURRENT_TIMESTAMP"));
    const bool ok = m_sql->createTable(t, fields);
    qDebug() << "[bridge] createDefaultMqttLogTable result:" << ok << "table=" << t;
    return ok;
}

void MqttDbTestBridge::onMqttConnected()
{
    qDebug() << "[bridge] MQTT connected, subscribing all topics";
    subscribeAllTopics();
}

void MqttDbTestBridge::subscribeAllTopics()
{
    qDebug() << "[bridge] subscribeAllTopics entered. mqttConnected=" << m_mqtt->isConnected()
             << "commandTopic=" << m_commandTopic
             << "logSubscriptionCount=" << m_logTopicFilters.size();
    if (!m_mqtt->isConnected())
        return;
    if (!m_commandTopic.isEmpty())
        m_mqtt->subscribe(m_commandTopic);
    for (const QMqttTopicFilter &f : m_logTopicFilters)
        m_mqtt->subscribe(f.filter());
}

void MqttDbTestBridge::onMqttMessage(const QString &topic, const QString &message, int qos, bool retained, bool duplicated)
{
    qDebug() << "[bridge] onMqttMessage topic=" << topic
             << "payloadLength=" << message.size()
             << "qos=" << qos
             << "retained=" << retained
             << "duplicated=" << duplicated;
    if (!m_commandTopic.isEmpty() && topic == m_commandTopic) {
        if (handleCommandJson(message))
            return;
    }
    if (topicMatchesLogFilter(topic)) {
        insertMessageLog(topic, message, qos, retained, duplicated);
    }
}

bool MqttDbTestBridge::handleCommandJson(const QString &message)
{
    qDebug() << "[bridge] handleCommandJson payload:" << message;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &err);
    if (!doc.isObject()) {
        QJsonObject r;
        r[QStringLiteral("ok")] = false;
        r[QStringLiteral("error")] = QStringLiteral("invalid_json: %1").arg(err.errorString());
        publishResponse(r);
        return true;
    }
    const QJsonObject o = doc.object();
    const QString op = o.value(QStringLiteral("op")).toString();
    qDebug() << "[bridge] command op =" << op;
    QJsonObject resp;
    resp[QStringLiteral("op")] = op;

    auto fail = [&](const QString &msg) {
        // 使用 lambda 统一失败响应结构，避免每个分支重复拼装 JSON
        resp[QStringLiteral("ok")] = false;
        resp[QStringLiteral("error")] = msg;
        if (m_sql)
            resp[QStringLiteral("sqlError")] = m_sql->lastError();
        publishResponse(resp);
    };

    auto okEmpty = [&]() {
        // 使用 lambda 统一成功响应结构
        resp[QStringLiteral("ok")] = true;
        publishResponse(resp);
    };

    if (!m_sql->isConnected()) {
        fail(QStringLiteral("database_not_connected"));
        return true;
    }

    if (op == QLatin1String("exec")) {
        const QString sql = o.value(QStringLiteral("sql")).toString();
        if (sql.isEmpty()) {
            fail(QStringLiteral("missing_sql"));
            return true;
        }
        if (m_sql->exec(sql))
            okEmpty();
        else
            fail(QStringLiteral("exec_failed"));
        return true;
    }

    if (op == QLatin1String("query")) {
        const QString sql = o.value(QStringLiteral("sql")).toString();
        if (sql.isEmpty()) {
            fail(QStringLiteral("missing_sql"));
            return true;
        }
        const QVariantList rows = m_sql->query(sql);
        resp[QStringLiteral("ok")] = true;
        resp[QStringLiteral("data")] = variantListToJson(rows);
        publishResponse(resp);
        return true;
    }

    if (op == QLatin1String("select")) {
        const QString table = o.value(QStringLiteral("table")).toString();
        if (table.isEmpty()) {
            fail(QStringLiteral("missing_table"));
            return true;
        }
        const QString fields = o.value(QStringLiteral("fields")).toString(QStringLiteral("*"));
        const QString condition = o.value(QStringLiteral("condition")).toString();
        const QString orderBy = o.value(QStringLiteral("orderBy")).toString();
        const int limit = o.contains(QStringLiteral("limit")) ? o.value(QStringLiteral("limit")).toInt(-1) : -1;
        const int offset = o.contains(QStringLiteral("offset")) ? o.value(QStringLiteral("offset")).toInt(0) : 0;
        const QVariantList rows = m_sql->select(table, fields, condition, orderBy, limit, offset);
        resp[QStringLiteral("ok")] = true;
        resp[QStringLiteral("data")] = variantListToJson(rows);
        publishResponse(resp);
        return true;
    }

    if (op == QLatin1String("insert")) {
        const QString table = o.value(QStringLiteral("table")).toString();
        const QJsonObject valuesObj = o.value(QStringLiteral("values")).toObject();
        if (table.isEmpty() || valuesObj.isEmpty()) {
            fail(QStringLiteral("missing_table_or_values"));
            return true;
        }
        if (m_sql->insert(table, jsonObjectToVariantMap(valuesObj)))
            okEmpty();
        else
            fail(QStringLiteral("insert_failed"));
        return true;
    }

    if (op == QLatin1String("update")) {
        const QString table = o.value(QStringLiteral("table")).toString();
        const QJsonObject valuesObj = o.value(QStringLiteral("values")).toObject();
        const QString condition = o.value(QStringLiteral("condition")).toString();
        if (table.isEmpty() || valuesObj.isEmpty() || condition.isEmpty()) {
            fail(QStringLiteral("missing_table_values_or_condition"));
            return true;
        }
        if (m_sql->update(table, jsonObjectToVariantMap(valuesObj), condition))
            okEmpty();
        else
            fail(QStringLiteral("update_failed"));
        return true;
    }

    if (op == QLatin1String("delete") || op == QLatin1String("remove")) {
        const QString table = o.value(QStringLiteral("table")).toString();
        const QString condition = o.value(QStringLiteral("condition")).toString();
        if (table.isEmpty() || condition.isEmpty()) {
            fail(QStringLiteral("missing_table_or_condition"));
            return true;
        }
        if (m_sql->remove(table, condition))
            okEmpty();
        else
            fail(QStringLiteral("delete_failed"));
        return true;
    }

    if (op == QLatin1String("createTable")) {
        const QString table = o.value(QStringLiteral("table")).toString();
        const QJsonObject fieldsObj = o.value(QStringLiteral("fields")).toObject();
        if (table.isEmpty() || fieldsObj.isEmpty()) {
            fail(QStringLiteral("missing_table_or_fields"));
            return true;
        }
        QVariantMap fmap;
        for (auto it = fieldsObj.begin(); it != fieldsObj.end(); ++it)
            fmap[it.key()] = it.value().toString();
        if (m_sql->createTable(table, fmap))
            okEmpty();
        else
            fail(QStringLiteral("createTable_failed"));
        return true;
    }

    fail(QStringLiteral("unknown_op"));
    return true;
}

void MqttDbTestBridge::publishResponse(const QJsonObject &obj)
{
    if (m_responseTopic.isEmpty() || !m_mqtt->isConnected()) {
        qDebug() << "[bridge] publishResponse skipped. responseTopicEmpty=" << m_responseTopic.isEmpty()
                 << "mqttConnected=" << m_mqtt->isConnected();
        return;
    }
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    qDebug() << "[bridge] publishResponse topic=" << m_responseTopic << "payload=" << payload;
    m_mqtt->publish(m_responseTopic, QString::fromUtf8(payload));
}

bool MqttDbTestBridge::topicMatchesLogFilter(const QString &topic) const
{
    for (const QMqttTopicFilter &f : m_logTopicFilters) {
        if (f.match(topic))
            return true;
    }
    return false;
}

bool MqttDbTestBridge::insertMessageLog(const QString &topic, const QString &message, int qos, bool retained, bool duplicated)
{
    // payload 先规范化为紧凑 JSON 字符串，保证写入 JSON 列时格式稳定
    const QString normalizedPayload = QString::fromUtf8(normalizePayloadForJsonStorage(message));
    qDebug() << "[bridge] insertMessageLog topic=" << topic
             << "payloadLength=" << message.size()
             << "normalizedPayloadLength=" << normalizedPayload.size()
             << "qos=" << qos
             << "retained=" << retained
             << "duplicated=" << duplicated
             << "table=" << m_messageLogTable
             << "dbConnected=" << m_sql->isConnected();
    if (!m_sql->isConnected() || m_messageLogTable.isEmpty())
        return false;
    QMap<QString, QVariant> row;
    row[QStringLiteral("topic")] = topic;
    row[QStringLiteral("payload")] = normalizedPayload;
    row[QStringLiteral("qos")] = qos;
    row[QStringLiteral("retained")] = retained;
    row[QStringLiteral("duplicated")] = duplicated;
    row[QStringLiteral("payload_size")] = message.toUtf8().size();
    row[QStringLiteral("received_at")] = QDateTime::currentDateTime();
    if (!m_sql->insert(m_messageLogTable, row)) {
        qWarning() << "insertMessageLog failed:" << m_sql->lastError();
        return false;
    }
    qDebug() << "[bridge] insertMessageLog succeeded for topic" << topic;
    return true;
}

QMap<QString, QVariant> MqttDbTestBridge::jsonObjectToVariantMap(const QJsonObject &o)
{
    QMap<QString, QVariant> m;
    for (auto it = o.begin(); it != o.end(); ++it)
        m[it.key()] = it.value().toVariant();
    return m;
}

QJsonValue MqttDbTestBridge::variantToJson(const QVariant &v)
{
    // 针对常见数据库类型做显式转换，避免 QJsonValue 推导为字符串造成语义丢失
    switch (v.typeId())
    {
    case QMetaType::UnknownType:
        return QJsonValue::Null;
    case QMetaType::Bool:
        return QJsonValue(v.toBool());
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong:
        return QJsonValue(v.toLongLong());
    case QMetaType::Double:
        return QJsonValue(v.toDouble());
    default:
        return QJsonValue(v.toString());
    }
}

QJsonArray MqttDbTestBridge::variantListToJson(const QVariantList &list)
{
    QJsonArray arr;
    for (const QVariant &item : list) {
        const QVariantMap map = item.toMap();
        if (!map.isEmpty()) {
            QJsonObject row;
            for (auto it = map.begin(); it != map.end(); ++it)
                row[it.key()] = variantToJson(it.value());
            arr.append(row);
        } else {
            arr.append(variantToJson(item));
        }
    }
    return arr;
}
