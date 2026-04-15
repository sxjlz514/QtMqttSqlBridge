#include "dbmanager.h"

#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QtSql/QSqlRecord>
#include <QtSql/QSqlField>
#include <QDebug>

/*
 * 实现说明：
 * - 本文件负责 DbManager 的具体数据库行为，头文件只暴露稳定接口。
 * - 重点保障：连接复用、事务状态一致、参数化执行降低 SQL 注入风险。
 */

// 私有实现类
class DbManager::Private
{
public:
    QSqlDatabase db;               // 数据库连接对象（Qt 连接句柄）
    QString lastErrorText;         // 最近一次失败原因，供上层 UI/日志展示
    bool inTransactionFlag = false; // 应用层事务标记（避免重复 begin/commit）

    // 全局唯一连接名
    static const QString CONNECTION_NAME; // 命名连接键：保证同进程内复用同一连接
};

// 初始化静态连接名
const QString DbManager::Private::CONNECTION_NAME = "ybb";

// 单例构造
DbManager::DbManager() : d(new Private)
{
    // 检查连接是否已存在，避免重复创建
    if (QSqlDatabase::contains(Private::CONNECTION_NAME))
    {
        d->db = QSqlDatabase::database(Private::CONNECTION_NAME);
    }
    else
    {
        // 创建MySQL连接
        d->db = QSqlDatabase::addDatabase("QMYSQL", Private::CONNECTION_NAME);
    }
}

// 单例析构
DbManager::~DbManager()
{
    // 析构时清理资源
    if (d->db.isOpen())
    {
        // 事务中则回滚
        if (d->inTransactionFlag)
        {
            d->db.rollback();
        }
        d->db.close();
    }
    delete d;
}

// 获取单例实例
DbManager& DbManager::instance()
{
    // C++11 起局部静态变量初始化是线程安全的，适合实现懒加载单例
    static DbManager singletonInstance;
    return singletonInstance;
}

// 检查MySQL驱动是否可用
bool DbManager::isDriverAvailable()
{
    return QSqlDatabase::drivers().contains("QMYSQL");
}

// 数据库连接
bool DbManager::connect(const QString& host,
                        const QString& username,
                        const QString& password,
                        const QString& database,
                        int port)
{
    // 先断开已有连接
    if (d->db.isOpen())
    {
        disconnect();
    }

    // 设置连接参数
    d->db.setHostName(host);
    d->db.setPort(port);
    d->db.setUserName(username);
    d->db.setPassword(password);
    if (!database.isEmpty())
    {
        d->db.setDatabaseName(database);
    }

    // 禁用SSL连接，解决SSL连接错误问题
    d->db.setConnectOptions("MYSQL_OPT_SSL_MODE=DISABLED");

    // 打开连接
    if (!d->db.open())
    {
        d->lastErrorText = d->db.lastError().text();
        return false;
    }

    d->lastErrorText.clear();
    return true;
}

// 断开数据库连接
void DbManager::disconnect()
{
    if (d->db.isOpen())
    {
        if (d->inTransactionFlag)
        {
            rollbackTransaction();
        }
        d->db.close();
    }
    d->lastErrorText.clear();
}

// 检查是否已连接
bool DbManager::isConnected() const
{
    return d->db.isOpen();
}

// 获取最后一次错误信息
QString DbManager::lastError() const
{
    return d->lastErrorText;
}

// 开启事务
bool DbManager::beginTransaction()
{
    if (!d->db.isOpen())
    {
        d->lastErrorText = "Error: Database not connected";
        return false;
    }

    if (d->inTransactionFlag)
    {
        d->lastErrorText = "Error: Nested transactions are not supported";
        return false;
    }

    bool success = d->db.transaction();
    if (success)
    {
        d->inTransactionFlag = true;
        d->lastErrorText.clear();
    }
    else
    {
        d->lastErrorText = d->db.lastError().text();
    }
    return success;
}

// 提交事务
bool DbManager::commitTransaction()
{
    if (!d->inTransactionFlag)
    {
        d->lastErrorText = "Error: No active transaction to commit";
        return false;
    }

    bool success = d->db.commit();
    if (success)
    {
        d->inTransactionFlag = false;
        d->lastErrorText.clear();
    }
    else
    {
        d->lastErrorText = d->db.lastError().text();
    }
    return success;
}

// 回滚事务
bool DbManager::rollbackTransaction()
{
    if (!d->inTransactionFlag)
    {
        d->lastErrorText = "Error: No active transaction to rollback";
        return false;
    }

    bool success = d->db.rollback();
    if (success)
    {
        d->inTransactionFlag = false;
        d->lastErrorText.clear();
    }
    else
    {
        d->lastErrorText = d->db.lastError().text();
    }
    return success;
}

// 检查是否在事务中
bool DbManager::inTransaction() const
{
    return d->inTransactionFlag;
}

// 创建数据库
bool DbManager::createDatabase(const QString& databaseName)
{
    QSqlQuery query(d->db);
    // 反引号包裹避免关键字冲突
    QString sql = QString("CREATE DATABASE IF NOT EXISTS `%1`").arg(databaseName);

    if (!query.exec(sql))
    {
        d->lastErrorText = query.lastError().text();
        return false;
    }
    d->lastErrorText.clear();
    return true;
}

// 删除数据库
bool DbManager::dropDatabase(const QString& databaseName)
{
    QSqlQuery query(d->db);
    // 修复原参考代码的语法错误：IF EXISTS + 反引号
    QString sql = QString("DROP DATABASE IF EXISTS `%1`").arg(databaseName);

    if (!query.exec(sql))
    {
        d->lastErrorText = query.lastError().text();
        return false;
    }
    d->lastErrorText.clear();
    return true;
}

// 切换数据库
bool DbManager::useDatabase(const QString &databaseName)
{
    // 直接更新QSqlDatabase的databaseName属性
    d->db.setDatabaseName(databaseName);
    qDebug() << "Switched to database:" << databaseName;
    
    // 重新打开连接以应用新的数据库名称
    if (d->db.isOpen()) {
        d->db.close();
    }
    
    if (!d->db.open()) {
        d->lastErrorText = d->db.lastError().text();
        qDebug() << "Failed to open database:" << d->lastErrorText;
        return false;
    }
    
    // 测试查询，确认数据库切换成功
    QSqlQuery testQuery(d->db);
    if (testQuery.exec("SELECT DATABASE()")) {
        if (testQuery.next()) {
            QString currentDb = testQuery.value(0).toString();
            qDebug() << "Current database after switch:" << currentDb;
        }
    }
    
    d->lastErrorText.clear();
    return true;
}

// 检查数据库是否存在
bool DbManager::isDatabaseExists(const QString &databaseName) const
{
    QSqlQuery query(d->db);
    // 参数化查询避免注入；'?' 为位置占位符，addBindValue 按顺序绑定
    query.prepare("SELECT SCHEMA_NAME FROM information_schema.SCHEMATA WHERE SCHEMA_NAME = ?");
    query.addBindValue(databaseName);

    if (!query.exec())
    {
        d->lastErrorText = query.lastError().text();
        return false;
    }
    return query.next();
}

// 创建表
bool DbManager::createTable(const QString &tableName,
                            const QMap<QString, QString> &fields)
{
    if (fields.isEmpty())
    {
        d->lastErrorText = "Error: Fields definition is empty";
        return false;
    }

    // QMap 迭代按 key 排序，生成 SQL 时字段顺序稳定，便于调试与比较
    QStringList fieldDefs;
    for (auto it = fields.begin(); it != fields.end(); ++it)
    {
        fieldDefs << QString("`%1` %2").arg(it.key(), it.value());
    }

    QString sql = QString("CREATE TABLE IF NOT EXISTS `%1` (%2)").arg(tableName, fieldDefs.join(", "));
    QSqlQuery query(d->db);

    if (!query.exec(sql))
    {
        d->lastErrorText = query.lastError().text();
        return false;
    }
    d->lastErrorText.clear();
    return true;
}

// 删除表
bool DbManager::dropTable(const QString &tableName)
{
    QSqlQuery query(d->db);
    QString sql = QString("DROP TABLE IF EXISTS `%1`").arg(tableName);

    if (!query.exec(sql))
    {
        d->lastErrorText = query.lastError().text();
        return false;
    }
    d->lastErrorText.clear();
    return true;
}

// 检查表是否存在
bool DbManager::isTableExists(const QString &tableName) const
{
    return d->db.tables().contains(tableName, Qt::CaseInsensitive);
}

// 获取表字段信息
QList<DbManager::FieldInfo> DbManager::getTableFields(const QString &tableName) const
{
    QList<FieldInfo> result;
    QSqlQuery query(d->db);
    QString sql = QString("SHOW COLUMNS FROM `%1`").arg(tableName);

    if (!query.exec(sql))
    {
        d->lastErrorText = query.lastError().text();
        return result;
    }

    while (query.next())
    {
        FieldInfo info;
        info.name = query.value("Field").toString();
        info.type = query.value("Type").toString();
        info.isPrimaryKey = (query.value("Key").toString() == "PRI");
        info.isAutoIncrement = query.value("Extra").toString().contains("auto_increment", Qt::CaseInsensitive);
        info.isNullable = (query.value("Null").toString() == "YES");
        info.defaultValue = query.value("Default");

        result.append(info);
    }

    d->lastErrorText.clear();
    return result;
}

// 单条插入
bool DbManager::insert(const QString &tableName,
                       const QMap<QString, QVariant> &values)
{
    if (values.isEmpty())
    {
        d->lastErrorText = "Error: Insert values map is empty";
        return false;
    }

    QStringList keys = values.keys();
    QStringList placeholders;
    for (const QString &key : keys)
    {
        placeholders << QString(":%1").arg(key);
    }

    // 组装插入 SQL：字段与占位符一一对应，避免字符串拼接直接带值
    QString sql = QString("INSERT INTO `%1` (`%2`) VALUES (%3)")
                      .arg(tableName, keys.join("`, `"), placeholders.join(", "));

    QSqlQuery query(d->db);
    query.prepare(sql);

    // 绑定参数（防SQL注入）
    for (auto it = values.begin(); it != values.end(); ++it)
    {
        query.bindValue(QString(":%1").arg(it.key()), it.value());
    }

    if (!query.exec())
    {
        d->lastErrorText = query.lastError().text();
        return false;
    }

    d->lastErrorText.clear();
    return true;
}

// 批量插入
bool DbManager::insertBatch(const QString &tableName,
                            const QList<QMap<QString, QVariant>> &valuesList)
{
    if (valuesList.isEmpty())
    {
        d->lastErrorText = "Error: Batch insert values list is empty";
        return false;
    }

    // 取第一条数据的字段名（所有行字段需一致）
    const QMap<QString, QVariant> &firstRow = valuesList.first();
    QStringList keys = firstRow.keys();
    if (keys.isEmpty())
    {
        d->lastErrorText = "Error: Batch insert row is empty";
        return false;
    }

    // 拼接字段名和占位符模板
    QStringList placeholders;
    for (const QString &key : keys)
    {
        placeholders << QString(":%1").arg(key);
    }
    QString sql = QString("INSERT INTO `%1` (`%2`) VALUES (%3)")
                      .arg(tableName, keys.join("`, `"), placeholders.join(", "));

    QSqlQuery query(d->db);
    query.prepare(sql);

    // 批量绑定+执行
    d->db.transaction(); // 批量操作建议手动加事务
    bool allSuccess = true;
    for (const QMap<QString, QVariant> &row : valuesList)
    {
        // 校验字段一致性
        if (row.keys() != keys)
        {
            allSuccess = false;
            d->lastErrorText = "Error: Batch insert row fields mismatch";
            break;
        }

        // 绑定当前行参数
        for (auto it = row.begin(); it != row.end(); ++it)
        {
            query.bindValue(QString(":%1").arg(it.key()), it.value());
        }

        if (!query.exec())
        {
            allSuccess = false;
            d->lastErrorText = query.lastError().text();
            break;
        }
    }

    if (allSuccess)
    {
        d->db.commit();
        d->lastErrorText.clear();
    }
    else
    {
        d->db.rollback();
    }

    return allSuccess;
}

// 更新数据
bool DbManager::update(const QString &tableName,
                       const QMap<QString, QVariant> &values,
                       const QString &condition)
{
    if (values.isEmpty())
    {
        d->lastErrorText = "Error: Update values map is empty";
        return false;
    }

    // 拼接 SET 子句，使用 :field 的命名绑定参数语法
    QStringList setClauses;
    for (const QString &key : values.keys())
    {
        setClauses << QString("`%1` = :%1").arg(key);
    }

    QString sql = QString("UPDATE `%1` SET %2").arg(tableName, setClauses.join(", "));
    if (!condition.isEmpty())
    {
        sql += QString(" WHERE %1").arg(condition);
    }

    QSqlQuery query(d->db);
    query.prepare(sql);

    // 绑定参数
    for (auto it = values.begin(); it != values.end(); ++it)
    {
        query.bindValue(QString(":%1").arg(it.key()), it.value());
    }

    if (!query.exec())
    {
        d->lastErrorText = query.lastError().text();
        return false;
    }

    return true;
}

// 删除数据
bool DbManager::remove(const QString &tableName, const QString &condition)
{
    QString sql = QString("DELETE FROM `%1`").arg(tableName);
    if (!condition.isEmpty())
    {
        sql += QString(" WHERE %1").arg(condition);
    }

    QSqlQuery query(d->db);
    if (!query.exec(sql))
    {
        d->lastErrorText = query.lastError().text();
        return false;
    }

    return true;
}

// 执行任意SQL
bool DbManager::exec(const QString &sql)
{
    QSqlQuery query(d->db);
    if (!query.exec(sql))
    {
        d->lastErrorText = query.lastError().text();
        return false;
    }
    return true;
}

// 原生查询
QList<QMap<QString, QVariant>> DbManager::query(const QString &sql)
{
    QList<QMap<QString, QVariant>> resultList;
    QSqlQuery query(d->db);

    qDebug() << "Executing query:" << sql;
    qDebug() << "Current database:" << d->db.databaseName();

    if (!query.exec(sql))
    {
        d->lastErrorText = query.lastError().text();
        qDebug() << "Query execution failed:" << d->lastErrorText;
        return resultList;
    }

    // 解析结果集
    QSqlRecord record = query.record();
    int colCount = record.count();
    qDebug() << "Number of columns:" << colCount;

    for (int i = 0; i < colCount; ++i)
    {
        qDebug() << "Column" << i << ":" << record.fieldName(i);
    }

    int rowCount = 0;
    while (query.next())
    {
        QMap<QString, QVariant> rowMap;
        for (int i = 0; i < colCount; ++i)
        {
            QString colName = record.fieldName(i);
            QVariant value = query.value(i);
            rowMap[colName] = value;
            qDebug() << "Row" << rowCount << ", Column" << colName << ":" << value;
        }
        resultList.append(rowMap);
        rowCount++;
    }

    qDebug() << "Query completed. Rows returned:" << rowCount;
    return resultList;
}

// 封装查询（排序/分页）
QList<QMap<QString, QVariant>> DbManager::select(const QString &tableName,
                                                 const QString &fields,
                                                 const QString &condition,
                                                 const QString &orderBy,
                                                 int limit, int offset)
{
    QString sql = QString("SELECT %1 FROM `%2`").arg(fields, tableName);

    if (!condition.isEmpty())
        sql += QString(" WHERE %1").arg(condition);
    if (!orderBy.isEmpty())
        sql += QString(" ORDER BY %1").arg(orderBy);
    if (limit > 0)
    {
        sql += QString(" LIMIT %1").arg(limit);
        if (offset > 0)
        {
            sql += QString(" OFFSET %1").arg(offset);
        }
    }

    return query(sql);
}

// 获取最后插入的ID
QVariant DbManager::lastInsertId() const
{
    QSqlQuery query(d->db);
    return query.lastInsertId();
}