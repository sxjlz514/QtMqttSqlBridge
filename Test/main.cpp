#include <QGuiApplication>
#include <QDebug>
#include <QQmlError>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "sqlmanagerwrapper.h"
#include "mqttmanager.h"
#include "mqttdbtestbridge.h"

int main(int argc, char *argv[])
{
    // 应用入口：创建 QML 引擎并把 C++ 服务对象注入到 QML 上下文
    QGuiApplication app(argc, argv);
    qDebug() << "[main] Application starting";
    qDebug() << "[main] Qt version:" << QT_VERSION_STR;

    QQmlApplicationEngine engine;
    qDebug() << "[main] QQmlApplicationEngine created";

    SqlManagerWrapper sqlManager; // 提供数据库能力给 QML
    engine.rootContext()->setContextProperty("sqlManager", &sqlManager);
    qDebug() << "[main] Context property registered: sqlManager";

    MqttManager mqttManager; // 提供 MQTT 能力给 QML
    engine.rootContext()->setContextProperty("mqttManager", &mqttManager);
    qDebug() << "[main] Context property registered: mqttManager";

    MqttDbTestBridge mqttBridge(&mqttManager, &sqlManager); // MQTT 与 DB 的桥接层
    engine.rootContext()->setContextProperty("mqttBridge", &mqttBridge);
    qDebug() << "[main] Context property registered: mqttBridge";

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::warnings,
        &app,
        [](const QList<QQmlError> &warnings) {
            qWarning() << "[main] QML warnings count:" << warnings.size();
            for (const QQmlError &warning : warnings)
                qWarning().noquote() << "[main] QML warning:" << warning.toString();
        });

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [](QObject *object, const QUrl &url) {
            if (!object) {
                qCritical() << "[main] Root object creation failed for:" << url;
                return;
            }
            qDebug() << "[main] Root object created:" << object << "from" << url;
        },
        Qt::QueuedConnection); // QueuedConnection 可避免加载时机下直接回调引发的重入问题

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() {
            qCritical() << "[main] objectCreationFailed emitted, exiting application";
            QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);

    qDebug() << "[main] Loading QML module Test/Main";
    engine.loadFromModule("Test", "Main");
    qDebug() << "[main] engine.loadFromModule returned, rootObjects:" << engine.rootObjects().size();

    qDebug() << "[main] Entering event loop";
    return QCoreApplication::exec();
}


// #include <QGuiApplication>
// #include <QQmlApplicationEngine>
// #include <QDebug>

// int main(int argc, char *argv[]) {
//     QGuiApplication app(argc, argv);
//     QQmlApplicationEngine engine;

//     // 监听QML加载错误
//     QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
//                      &app, [](QObject *obj, const QUrl &objUrl) {
//                          if (!obj) {
//                              qCritical() << "QML加载失败：" << objUrl;
//                          } else {
//                              qInfo() << "QML加载成功：" << objUrl;
//                          }
//                      }, Qt::QueuedConnection);

//     engine.load(QUrl(QStringLiteral("qrc:/Main.qml"))); // 确认路径是否正确（文件路径/资源路径）
//     if (engine.rootObjects().isEmpty()) {
//         qCritical() << "QML根对象为空！";
//         return -1;
//     }

//     return app.exec();
// }