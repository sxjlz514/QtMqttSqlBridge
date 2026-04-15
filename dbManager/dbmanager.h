#ifndef DBMANAGER_H
#define DBMANAGER_H

#include "dbManager_global.h"
#include <QtSql/QSqlDatabase>
#include <QString>
#include <QMap>
#include <QList>
#include <QVariant>

/*
 * DbManager 设计目标：
 * 1) 统一封装 Qt SQL 的常用数据库操作，避免业务层散落原生 SQL 连接细节。
 * 2) 对外提供更稳定的事务、建库建表、CRUD 接口，降低 QML/上层模块使用门槛。
 * 3) 通过单例模式保证连接名唯一，避免重复 addDatabase 导致的连接冲突。
 */

// 兼容导出/导入宏（如果 dbManager_global.h 未定义，补充兜底逻辑）
#ifndef DBMANAGER_EXPORT
#  if defined(DBMANAGER_LIBRARY)
#    define DBMANAGER_EXPORT Q_DECL_EXPORT
#  else
#    define DBMANAGER_EXPORT Q_DECL_IMPORT
#  endif
#endif

class QSqlError;

/*
 * 事务特性说明：
 * 原子性（Atomicity）：要么全执行，要么全回滚
 * 一致性（Consistency）：事务前后数据库状态合法
 * 隔离性（Isolation）：多事务互不干扰
 * 持久性（Durability）：提交后数据永久存储
*/

/*
 * 为什么增加 DbManager 类：
 * - 项目同时服务 QML 测试界面和 MQTT 指令桥接，需要一个稳定的数据库能力中台。
 * - 如果直接在各处 new QSqlQuery/QSqlDatabase，容易出现连接复用混乱与事务边界不一致。
 * - 该类将错误信息、事务状态和 SQL 执行策略集中管理，便于排错和后续扩展。
 */
class DBMANAGER_EXPORT DbManager
{
public:
    // 单例获取接口
    static DbManager& instance();
    // 禁用拷贝/赋值
    DbManager(const DbManager&) = delete;
    DbManager& operator=(const DbManager&) = delete;

    // 检查Qt MySQL驱动是否可用
    static bool isDriverAvailable();

    // 数据库连接
    bool connect(const QString& host,
                 const QString& username,
                 const QString& password,
                 const QString& database = "",
                 int port = 3306);
    void disconnect();
    bool isConnected() const;

    // 错误信息
    QString lastError() const;

    // 事务管理
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();
    bool inTransaction() const;

    // 数据库管理
    bool createDatabase(const QString& databaseName);
    bool dropDatabase(const QString& databaseName);
    bool useDatabase(const QString& databaseName);
    bool isDatabaseExists(const QString& databaseName) const;

    // 表管理
    bool createTable(const QString &tableName,
                     const QMap<QString, QString> &fields);
    bool dropTable(const QString &tableName);
    bool isTableExists(const QString &tableName) const;

    // 字段元信息：用于把 SHOW COLUMNS 的结果转换为结构化数据，便于 UI/业务层展示
    struct FieldInfo
    {
        QString name;           // 字段名
        QString type;           // 数据库类型（如 INT/VARCHAR(255)）
        bool isPrimaryKey;      // true 表示该列参与主键约束
        bool isAutoIncrement;   // true 表示插入时通常可省略该列（由数据库自增）
        bool isNullable;        // true 表示该列可写入 NULL
        QVariant defaultValue;  // 列默认值（无默认值时可能为空 QVariant）
    };
    QList<FieldInfo> getTableFields(const QString &tableName) const;

    // 数据操作 - 插入
    bool insert(const QString &tableName,
                const QMap<QString, QVariant> &values);
    // 批量插入（性能优化）
    bool insertBatch(const QString &tableName,
                     const QList<QMap<QString, QVariant>> &valuesList);

    // 数据操作 - 更新
    bool update(const QString &tableName,
                const QMap<QString, QVariant> &values,
                const QString &condition);

    // 数据操作 - 删除
    bool remove(const QString &tableName, const QString &condition);

    // 执行任意SQL
    bool exec(const QString &sql);

    // 原生查询（返回结果集）
    QList<QMap<QString, QVariant>> query(const QString &sql);

    // 封装查询（排序/分页）
    QList<QMap<QString, QVariant>> select(
        const QString &tableName,
        const QString &fields = "*",
        const QString &condition = "",
        const QString &orderBy = "",   // 示例: "id DESC, create_time ASC"
        int limit = -1,                 // -1表示不限
        int offset = 0                  // 分页偏移量
        );

    // 获取最后插入的自增ID
    QVariant lastInsertId() const;

private:
    // 单例构造/析构
    DbManager();
    ~DbManager();

    // Pimpl 模式：隐藏内部成员，减小头文件依赖并提高 ABI 稳定性
    class Private;
    Private* d; // 私有实现指针，集中保存连接对象/错误状态/事务标记
};

#endif // DBMANAGER_H