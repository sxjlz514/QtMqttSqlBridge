#include "sqlmanagerwrapper.h"

/*
 * 实现策略：
 * - 该层以“薄封装”为原则：多数接口直接转发到 DbManager。
 * - 仅在 C++/QML 类型鸿沟处做转换，例如 QVariantMap <-> QMap、结果列表打包为 QVariantList。
 */

SqlManagerWrapper::SqlManagerWrapper(QObject *parent) : QObject(parent), sqlManager(DbManager::instance())
{
}

bool SqlManagerWrapper::connect(const QString& host,
                                const QString& username,
                                const QString& password,
                                const QString& database,
                                int port)
{
    return sqlManager.connect(host, username, password, database, port);
}

void SqlManagerWrapper::disconnect()
{
    sqlManager.disconnect();
}

bool SqlManagerWrapper::isConnected() const
{
    return sqlManager.isConnected();
}

QString SqlManagerWrapper::lastError() const
{
    return sqlManager.lastError();
}

bool SqlManagerWrapper::beginTransaction()
{
    return sqlManager.beginTransaction();
}

bool SqlManagerWrapper::commitTransaction()
{
    return sqlManager.commitTransaction();
}

bool SqlManagerWrapper::rollbackTransaction()
{
    return sqlManager.rollbackTransaction();
}

bool SqlManagerWrapper::inTransaction() const
{
    return sqlManager.inTransaction();
}

bool SqlManagerWrapper::createDatabase(const QString& databaseName)
{
    return sqlManager.createDatabase(databaseName);
}

bool SqlManagerWrapper::dropDatabase(const QString& databaseName)
{
    return sqlManager.dropDatabase(databaseName);
}

bool SqlManagerWrapper::useDatabase(const QString& databaseName)
{
    return sqlManager.useDatabase(databaseName);
}

bool SqlManagerWrapper::isDatabaseExists(const QString& databaseName) const
{
    return sqlManager.isDatabaseExists(databaseName);
}

QStringList SqlManagerWrapper::listDatabases()
{
    QStringList databases;
    // SHOW DATABASES 返回单列表结构，这里读取每行首列并映射成字符串列表
    const QList<QMap<QString, QVariant>> result = sqlManager.query(QStringLiteral("SHOW DATABASES"));
    for (const auto &row : result) {
        if (!row.isEmpty())
            databases.append(row.constBegin().value().toString());
    }
    return databases;
}

QStringList SqlManagerWrapper::listTables(const QString &databaseName)
{
    // 允许传空：空字符串表示沿用当前连接已选库
    if (!databaseName.isEmpty() && !sqlManager.useDatabase(databaseName))
        return {};

    QStringList tables;
    const QList<QMap<QString, QVariant>> result = sqlManager.query(QStringLiteral("SHOW TABLES"));
    for (const auto &row : result) {
        if (!row.isEmpty())
            tables.append(row.constBegin().value().toString());
    }
    return tables;
}

bool SqlManagerWrapper::createTable(const QString &tableName, const QVariantMap &fields)
{
    QMap<QString, QString> f;
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
        f[it.key()] = it.value().toString();
    return sqlManager.createTable(tableName, f);
}

bool SqlManagerWrapper::dropTable(const QString &tableName)
{
    return sqlManager.dropTable(tableName);
}

bool SqlManagerWrapper::isTableExists(const QString &tableName) const
{
    return sqlManager.isTableExists(tableName);
}

bool SqlManagerWrapper::insertRecord(const QString &tableName, const QVariantMap &values)
{
    QMap<QString, QVariant> mapValues;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        mapValues.insert(it.key(), it.value());
    return sqlManager.insert(tableName, mapValues);
}

bool SqlManagerWrapper::updateRecord(const QString &tableName,
                                     const QVariantMap &values,
                                     const QString &condition)
{
    QMap<QString, QVariant> mapValues;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        mapValues.insert(it.key(), it.value());
    return sqlManager.update(tableName, mapValues, condition);
}

bool SqlManagerWrapper::insert(const QString &tableName,
                               const QMap<QString, QVariant> &values)
{
    return sqlManager.insert(tableName, values);
}

bool SqlManagerWrapper::insertBatch(const QString &tableName,
                                    const QList<QMap<QString, QVariant>> &valuesList)
{
    return sqlManager.insertBatch(tableName, valuesList);
}

bool SqlManagerWrapper::update(const QString &tableName,
                               const QMap<QString, QVariant> &values,
                               const QString &condition)
{
    return sqlManager.update(tableName, values, condition);
}

bool SqlManagerWrapper::remove(const QString &tableName, const QString &condition)
{
    return sqlManager.remove(tableName, condition);
}

bool SqlManagerWrapper::exec(const QString &sql)
{
    return sqlManager.exec(sql);
}

QVariantList SqlManagerWrapper::query(const QString &sql)
{
    QList<QMap<QString, QVariant>> result = sqlManager.query(sql);
    QVariantList variantList;
    // QVariant(map) 可直接被 QML 识别为 JS 对象，便于前端遍历字段
    for (const auto &map : result) {
        variantList.append(QVariant(map));
    }
    return variantList;
}

QVariantList SqlManagerWrapper::select(
    const QString &tableName,
    const QString &fields,
    const QString &condition,
    const QString &orderBy,
    int limit,
    int offset
    )
{
    QList<QMap<QString, QVariant>> result = sqlManager.select(tableName, fields, condition, orderBy, limit, offset);
    QVariantList variantList;
    for (const auto &map : result) {
        variantList.append(QVariant(map));
    }
    return variantList;
}

QVariant SqlManagerWrapper::lastInsertId() const
{
    return sqlManager.lastInsertId();
}
