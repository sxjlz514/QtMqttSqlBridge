import QtQuick 6.10
import QtQuick.Controls 6.10
import QtQuick.Layouts 6.10

/*
 * 页面设计说明（为什么这样设计）：
 * 1) 左侧侧栏负责连接与上下文选择（MQTT/DB/当前库表），减少操作路径和状态切换成本。
 * 2) 右侧工作区按 Tab 拆分“订阅落库 / 建表 / SQL 控制台 / CRUD”，让测试流程可循序推进。
 * 3) 底部固定展示 SQL 结果与日志，保证每一步动作都有可观察反馈，便于联调排错。
 */
ApplicationWindow
{
    id: root
    visible: true
    width: 1100
    height: 820
    title: "MQTT Message To Database Tester"
    minimumWidth: 980
    minimumHeight: 760

    // 关键状态变量：驱动 UI 按钮可用性、状态显示和流程判断
    property bool mqttConnected: false // MQTT 当前连接状态（由 mqttManager 信号同步）
    property bool dbConnected: false // 数据库当前连接状态（由 connectDatabase 维护）
    property string statusLog: "" // 操作日志缓存，统一显示在底部 Logs 区域
    property var databaseList: [] // 当前可选数据库列表，作为 databaseSelector 模型
    property var tableList: [] // 当前数据库下的表列表，作为 tableSelector 模型
    property string sqlResultText: "" // SQL/CRUD 结果文本，显示在 SQL Result 区域
    property string dialogTitleText: "" // 反馈弹窗标题
    property string dialogMessageText: "" // 反馈弹窗内容

    function appendLog() {
        // 用 arguments 支持可变参数，减少大量字符串拼接模板代码
        var parts = []
        for (var i = 0; i < arguments.length; ++i)
            parts.push(String(arguments[i]))
        var line = parts.join(" ")
        console.log(line)
        statusLog += (statusLog === "" ? "" : "\n") + line
    }

    function showMessage(title, message)
    {
        dialogTitleText = String(title)
        dialogMessageText = String(message)
        feedbackDialog.open()
    }

    function parsePort(textValue, fallbackValue)
    {
        var parsed = parseInt(textValue, 10)
        return isNaN(parsed) ? fallbackValue : parsed
    }

    function parseIntOrDefault(textValue, fallbackValue)
    {
        var parsed = parseInt(textValue, 10)
        return isNaN(parsed) ? fallbackValue : parsed
    }

    function currentDatabaseName()
    {
        var comboText = databaseSelector.editText ? databaseSelector.editText.trim() : ""
        return comboText !== "" ? comboText : dbNameField.text.trim()
    }

    function currentTableName()
    {
        return tableSelector.editText ? tableSelector.editText.trim() : ""
    }

    function setCurrentTableName(tableName)
    {
        var target = tableName ? tableName.trim() : ""
        if (tableSelector.editText !== target)
            tableSelector.editText = target

        var tableIndex = tableList.indexOf(target)
        tableSelector.currentIndex = tableIndex
        syncBridgeSettings()
    }

    function setSqlResult(text)
    {
        sqlResultText = String(text)
        appendLog("[sql-result] updated")
    }

    function formatRows(rows)
    {
        if (!rows || rows.length === 0)
            return "No rows returned."

        var lines = []
        for (var i = 0; i < rows.length; ++i)
        {
            var row = rows[i]
            var parts = []
            for (var key in row)
                parts.push(key + "=" + row[key])
            lines.push(parts.join(" | "))
        }
        return lines.join("\n")
    }

    function parseScalar(textValue)
    {
        // 将文本输入尽量恢复为原始类型，避免全部按字符串写入数据库
        var value = textValue.trim()
        if (value === "")
            return ""
        if (value === "null")
            return null
        if (value === "true")
            return true
        if (value === "false")
            return false

        var numberValue = Number(value)
        if (!isNaN(numberValue) && value !== "")
            return numberValue

        return value
    }

    function parseKeyValueLines(textValue)
    {
        var result = {}
        var lines = textValue.split("\n")
        for (var i = 0; i < lines.length; ++i)
        {
            var line = lines[i].trim()
            if (line === "")
                continue

            var separator = line.indexOf("=")
            if (separator === -1)
                separator = line.indexOf(":")
            if (separator === -1)
                continue

            var key = line.substring(0, separator).trim()
            var value = line.substring(separator + 1).trim()
            if (key !== "")
                result[key] = parseScalar(value)
        }
        return result
    }

    function refreshDatabases()
    {
        if (!dbConnected)
        {
            appendLog("Please connect the database first")
            return
        }

        databaseList = sqlManager.listDatabases()
        appendLog("Databases refreshed:", databaseList.length)
    }

    function refreshTables()
    {
        if (!dbConnected)
        {
            appendLog("Please connect the database first")
            return
        }

        var databaseName = currentDatabaseName()
        if (databaseName === "")
        {
            appendLog("Please choose a database first")
            return
        }

        if (!sqlManager.useDatabase(databaseName))
        {
            appendLog("Use database failed:", sqlManager.lastError())
            return
        }

        var previousTableName = currentTableName()
        dbNameField.text = databaseName
        tableList = sqlManager.listTables(databaseName)
        setCurrentTableName(previousTableName)
        appendLog("Tables refreshed for", databaseName + ":", tableList.length)
    }

    function useSelectedDatabase()
    {
        var databaseName = currentDatabaseName()
        if (databaseName === "")
        {
            appendLog("Database name cannot be empty")
            return
        }

        dbNameField.text = databaseName
        if (!dbConnected)
        {
            appendLog("Database name prepared:", databaseName)
            return
        }

        if (sqlManager.useDatabase(databaseName))
        {
            appendLog("Using database:", databaseName)
            refreshTables()
        }
        else
        {
            appendLog("Use database failed:", sqlManager.lastError())
        }
    }

    function ensureWorkingDatabase()
    {
        var databaseName = currentDatabaseName()
        if (databaseName === "")
        {
            appendLog("Please choose a database first")
            return false
        }

        dbNameField.text = databaseName
        if (!sqlManager.useDatabase(databaseName))
        {
            appendLog("Use database failed:", sqlManager.lastError())
            return false
        }

        return true
    }

    function currentTargetTableExists()
    {
        var tableName = currentTableName()
        var databaseName = currentDatabaseName()
        if (!dbConnected || tableName === "" || databaseName === "")
            return false

        if (!sqlManager.useDatabase(databaseName))
            return false

        return sqlManager.isTableExists(tableName)
    }

    function executeRawSql()
    {
        var sql = sqlEditor.text.trim()
        if (sql === "")
        {
            appendLog("SQL text cannot be empty")
            return
        }

        appendLog("[qml] executeRawSql()", sql)

        // 用正则快速判断查询语句；\b 表示单词边界，避免误匹配前缀
        var isQuery = /^(select|show|desc|describe|explain|with)\b/i.test(sql)
        if (isQuery)
        {
            var rows = sqlManager.query(sql)
            setSqlResult(formatRows(rows))
            appendLog("Query executed, rows:", rows.length)
        }
        else
        {
            var ok = sqlManager.exec(sql)
            if (ok)
            {
                setSqlResult("Execution succeeded.")
                appendLog("SQL executed successfully")
                refreshDatabases()
                if (currentDatabaseName() !== "")
                    refreshTables()
            }
            else
            {
                setSqlResult("Execution failed:\n" + sqlManager.lastError())
                appendLog("SQL execution failed:", sqlManager.lastError())
            }
        }
    }

    function createTableFromUi()
    {
        var tableName = newTableNameField.text.trim()
        var columnsText = newTableColumnsField.text.trim()
        if (tableName === "" || columnsText === "")
        {
            appendLog("Table name and column definitions are required")
            return
        }

        var sql = "CREATE TABLE IF NOT EXISTS `" + tableName + "` (\n" + columnsText + "\n)"
        sqlEditor.text = sql
        executeRawSql()
    }

    function runCrudHelper()
    {
        var operation = crudOperationBox.currentText
        var tableName = currentTableName()
        if (tableName === "") {
            appendLog("Please choose a table first")
            return
        }

        appendLog("[qml] runCrudHelper()", "op=", operation, "table=", tableName)

        // CRUD Helper 通过前端分支把统一输入映射到不同 C++ 接口
        if (operation === "Select")
        {
            var rows = sqlManager.select(
                tableName,
                crudFieldsField.text.trim() === "" ? "*" : crudFieldsField.text.trim(),
                crudConditionField.text.trim(),
                crudOrderByField.text.trim(),
                parseIntOrDefault(crudLimitField.text, -1),
                0
            )
            setSqlResult(formatRows(rows))
            appendLog("Select executed, rows:", rows.length)
            return
        }

        if (operation === "Insert")
        {
            var insertValues = parseKeyValueLines(crudValuesField.text)
            var insertOk = sqlManager.insertRecord(tableName, insertValues)
            if (insertOk)
            {
                setSqlResult("Insert succeeded.")
                appendLog("Insert succeeded")
            }
            else
            {
                setSqlResult("Insert failed:\n" + sqlManager.lastError())
                appendLog("Insert failed:", sqlManager.lastError())
            }
            return
        }

        if (operation === "Update")
        {
            var updateValues = parseKeyValueLines(crudValuesField.text)
            var updateOk = sqlManager.updateRecord(tableName, updateValues, crudConditionField.text.trim())
            if (updateOk)
            {
                setSqlResult("Update succeeded.")
                appendLog("Update succeeded")
            }
            else
            {
                setSqlResult("Update failed:\n" + sqlManager.lastError())
                appendLog("Update failed:", sqlManager.lastError())
            }
            return
        }

        if (operation === "Delete")
        {
            var deleteOk = sqlManager.remove(tableName, crudConditionField.text.trim())
            if (deleteOk)
            {
                setSqlResult("Delete succeeded.")
                appendLog("Delete succeeded")
            }
            else
            {
                setSqlResult("Delete failed:\n" + sqlManager.lastError())
                appendLog("Delete failed:", sqlManager.lastError())
            }
        }
    }

    function syncBridgeSettings()
    {
        // 设计目的：保持 UI 当前目标表 与 mqttBridge.messageLogTable 一致，防止消息写入错误表
        var targetTable = currentTableName()
        if (mqttBridge && mqttBridge.messageLogTable !== targetTable)
            mqttBridge.messageLogTable = targetTable
    }

    function connectMqtt()
    {
        appendLog("[qml] connectMqtt() clicked",
                  "connected=", mqttConnected,
                  "host=", mqttHostField.text.trim(),
                  "port=", mqttPortField.text)
        if (mqttConnected)
        {
            mqttManager.disconnectFromBroker()
            return
        }

        mqttManager.connectToBroker(
            mqttHostField.text.trim(),
            parsePort(mqttPortField.text, 1883),
            mqttClientIdField.text.trim(),
            mqttUserField.text.trim(),
            mqttPasswordField.text
        )
    }

    function connectDatabase()
    {
        appendLog("[qml] connectDatabase() clicked",
                  "connected=", dbConnected,
                  "host=", dbHostField.text.trim(),
                  "port=", dbPortField.text,
                  "database=", dbNameField.text.trim(),
                  "user=", dbUserField.text.trim())
        if (dbConnected)
        {
            sqlManager.disconnect()
            dbConnected = false
            appendLog("Database disconnected")
            return
        }

        var ok = sqlManager.connect(
            dbHostField.text.trim(),
            dbUserField.text.trim(),
            dbPasswordField.text,
            dbNameField.text.trim(),
            parsePort(dbPortField.text, 3306)
        )

        dbConnected = ok
        if (ok)
        {
            appendLog("Database connected")
            refreshDatabases()
            if (dbNameField.text.trim() !== "")
                refreshTables()
        }
        else
            appendLog("Database connect failed:", sqlManager.lastError())
    }

    function createLogTable()
    {
        var tableName = currentTableName()
        appendLog("[qml] createLogTable() clicked",
                  "table=", tableName,
                  "dbConnected=", dbConnected)
        syncBridgeSettings()

        if (!mqttBridge)
        {
            appendLog("mqttBridge is not ready")
            return
        }

        if (!dbConnected)
        {
            appendLog("Please connect the database first")
            return
        }

        if (tableName === "")
        {
            appendLog("Please choose or enter a target table first")
            return
        }

        if (!ensureWorkingDatabase())
            return

        if (currentTargetTableExists())
        {
            appendLog("Create target table skipped: table already exists")
            showMessage("Table already exists", "The current target table already exists in the selected database. Please choose another table name or use the existing table directly.")
            return
        }

        if (mqttBridge.createDefaultMqttLogTable(tableName))
        {
            appendLog("Target table is ready:", tableName)
            refreshTables()
            setCurrentTableName(tableName)
        }
        else
            appendLog("Create log table failed:", sqlManager.lastError())
    }

    function addSubscription()
    {
        var topic = topicFilterField.text.trim()
        var tableName = currentTableName()
        appendLog("[qml] addSubscription() clicked",
                  "topic=", topic,
                  "table=", tableName,
                  "mqttConnected=", mqttConnected,
                  "dbConnected=", dbConnected)
        if (!mqttBridge)
        {
            appendLog("mqttBridge is not ready")
            return
        }
        if (topic === "")
        {
            appendLog("Topic filter cannot be empty")
            return
        }

        if (!mqttConnected)
        {
            appendLog("Please connect MQTT before subscribing")
            showMessage("MQTT not connected", "Connect to the MQTT broker before subscribing to a topic.")
            return
        }

        if (!dbConnected)
        {
            appendLog("Please connect the database first")
            return
        }

        if (tableName === "")
        {
            appendLog("Please choose or enter a target table first")
            return
        }

        if (!ensureWorkingDatabase())
            return

        syncBridgeSettings()

        if (mqttBridge.subscribeLogTopic(topic))
        {
            appendLog("Subscribed topic:", topic)
            topicFilterField.clear()
        }
        else
        {
            appendLog("Subscribe failed. Check the topic filter format.")
        }
    }

    Connections
    {
        target: mqttManager
        // Connections 语法用于集中订阅 C++ 对象信号，避免在控件里分散写连接逻辑

        function onConnected()
        {
            mqttConnected = true
            appendLog("[qml] signal mqttManager.connected")
            appendLog("MQTT connected")
        }

        function onDisconnected()
        {
            mqttConnected = false
            appendLog("[qml] signal mqttManager.disconnected")
            appendLog("MQTT disconnected")
        }

        function onError(errorMessage)
        {
            mqttConnected = false
            appendLog("[qml] signal mqttManager.error", errorMessage)
            appendLog("MQTT error:", errorMessage)
        }
    }

    Dialog
    {
        id: feedbackDialog
        modal: true
        anchors.centerIn: parent
        title: root.dialogTitleText
        standardButtons: Dialog.Ok

        contentItem: Label
        {
            text: root.dialogMessageText
            wrapMode: Text.WordWrap
            width: 320
        }
    }

    // 根布局采用左右分栏：左侧连接与选择，右侧操作工作台
    RowLayout
    {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        ColumnLayout
        {
            Layout.preferredWidth: 270
            Layout.minimumWidth: 250
            Layout.maximumWidth: 300
            Layout.fillHeight: true
            spacing: 8

            GroupBox
            {
                title: qsTr("MqttConnection")
                Layout.fillWidth: true
                padding: 8

                Component.onCompleted: root.appendLog("[qml] MqttConnection group created")

                ColumnLayout
                {
                    anchors.fill: parent
                    spacing: 6

                    Label
                    {
                        text: root.mqttConnected ? qsTr("Status: Connected") : qsTr("Status: Disconnected")
                        font.bold: true
                    }

                    GridLayout
                    {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 6

                        Label
                        {
                            text: qsTr("Host")
                        }
                        TextField
                        {
                            id: mqttHostField
                            Layout.fillWidth: true
                            text: "127.0.0.1"
                        }

                        Label
                        {
                            text: qsTr("Port")
                        }
                        TextField
                        {
                            id: mqttPortField
                            Layout.fillWidth: true
                            text: "1883"
                            inputMethodHints: Qt.ImhDigitsOnly
                        }

                        Label
                        {
                            text: qsTr("Username")
                        }
                        TextField
                        {
                            id: mqttUserField
                            Layout.fillWidth: true
                        }

                        Label
                        {
                            text: qsTr("Password")
                        }
                        TextField
                        {
                            id: mqttPasswordField
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                        }

                        Label
                        {
                            text: qsTr("ClientId")
                        }
                        TextField
                        {
                            id: mqttClientIdField
                            Layout.fillWidth: true
                            text: "appTestClient"
                        }
                    }

                    Button
                    {
                        text: root.mqttConnected ? qsTr("Disconnect") : qsTr("Connect")
                        Layout.alignment: Qt.AlignRight
                        onClicked: root.connectMqtt()
                    }
                }
            }

            GroupBox
            {
                title: qsTr("DbConnection")
                Layout.fillWidth: true
                padding: 8

                Component.onCompleted: root.appendLog("[qml] DbConnection group created")

                ColumnLayout
                {
                    anchors.fill: parent
                    spacing: 6

                    Label
                    {
                        text: root.dbConnected ? qsTr("Status: Connected") : qsTr("Status: Disconnected")
                        font.bold: true
                    }

                    GridLayout
                    {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 6

                        Label
                        {
                            text: qsTr("Host")
                        }
                        TextField
                        {
                            id: dbHostField
                            Layout.fillWidth: true
                            text: "127.0.0.1"
                        }

                        Label
                        {
                            text: qsTr("Port")
                        }
                        TextField
                        {
                            id: dbPortField
                            Layout.fillWidth: true
                            text: "3306"
                            inputMethodHints: Qt.ImhDigitsOnly
                        }

                        Label
                        {
                            text: qsTr("Database")
                        }
                        TextField
                        {
                            id: dbNameField
                            Layout.fillWidth: true
                        }

                        Label
                        {
                            text: qsTr("Username")
                        }
                        TextField
                        {
                            id: dbUserField
                            Layout.fillWidth: true
                            text: "root"
                        }

                        Label
                        {
                            text: qsTr("Password")
                        }
                        TextField
                        {
                            id: dbPasswordField
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                        }
                    }

                    Button
                    {
                        text: root.dbConnected ? qsTr("Disconnect") : qsTr("Connect")
                        Layout.alignment: Qt.AlignRight
                        onClicked: root.connectDatabase()
                    }
                }
            }

            GroupBox
            {
                title: qsTr("Database Browser")
                Layout.fillWidth: true
                padding: 8

                ColumnLayout
                {
                    anchors.fill: parent
                    spacing: 6

                    Label
                    {
                        text: qsTr("Choose working database and table.")
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Label
                    {
                        text: qsTr("Database")
                    }
                    ComboBox
                    {
                        id: databaseSelector
                        Layout.fillWidth: true
                        editable: true
                        model: root.databaseList
                        onActivated:
                        {
                            dbNameField.text = currentText
                            root.useSelectedDatabase()
                        }
                    }

                    RowLayout
                    {
                        Layout.fillWidth: true
                        Button
                        {
                            text: qsTr("Refresh DBs")
                            Layout.fillWidth: true
                            onClicked: root.refreshDatabases()
                        }
                        Button
                        {
                            text: qsTr("Use DB")
                            Layout.fillWidth: true
                            onClicked: root.useSelectedDatabase()
                        }
                    }

                    Label
                    {
                        text: qsTr("Table")
                    }
                    ComboBox
                    {
                        id: tableSelector
                        Layout.fillWidth: true
                        editable: true
                        model: root.tableList
                        onActivated: root.syncBridgeSettings()
                        onEditTextChanged: root.syncBridgeSettings()
                    }

                    Button
                    {
                        text: qsTr("Refresh Tables")
                        Layout.fillWidth: true
                        onClicked: root.refreshTables()
                    }
                }
            }

            Item
            {
                Layout.fillHeight: true
            }
        }

        ColumnLayout
        {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            GroupBox
            {
                title: qsTr("Data Workspace")
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 340
                padding: 12

                ColumnLayout
                {
                    anchors.fill: parent
                    spacing: 10

                    TabBar
                    {
                        id: workspaceTabs
                        Layout.fillWidth: true

                        TabButton { text: qsTr("Subscription") }
                        TabButton { text: qsTr("Create Table") }
                        TabButton { text: qsTr("SQL Console") }
                        TabButton { text: qsTr("CRUD Helper") }
                    }

                    // StackLayout 与 TabBar.currentIndex 绑定，实现“单页多工具”切换
                    StackLayout
                    {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: workspaceTabs.currentIndex

                        Item
                        {
                            // Subscription 页：配置 topic 过滤并把消息落库到目标表
                            ColumnLayout
                            {
                                anchors.fill: parent
                                spacing: 10

                                Label
                                {
                                    text: qsTr("Subscribe topic filters and write incoming MQTT messages into the current target table.")
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }

                                RowLayout
                                {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Label
                                    {
                                        text: qsTr("Target table")
                                        Layout.preferredWidth: 80
                                    }

                                    TextField
                                    {
                                        Layout.fillWidth: true
                                        readOnly: true
                                        text: root.currentTableName() === "" ? qsTr("Choose or enter a table above") : root.currentTableName()
                                    }

                                    Button
                                    {
                                        text: qsTr("Create target table")
                                        enabled: root.dbConnected && root.currentTableName() !== ""
                                        onClicked: root.createLogTable()
                                    }
                                }

                                RowLayout
                                {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Label
                                    {
                                        text: qsTr("Topic filter")
                                        Layout.preferredWidth: 80
                                    }

                                    TextField
                                    {
                                        id: topicFilterField
                                        Layout.fillWidth: true
                                        placeholderText: "example/topic or sensors/#"
                                        onAccepted: root.addSubscription()
                                    }

                                    Button
                                    {
                                        text: qsTr("Subscribe")
                                        enabled: root.mqttConnected && root.dbConnected && root.currentTableName() !== ""
                                        onClicked: root.addSubscription()
                                    }
                                }

                                Label
                                {
                                    text: qsTr("Current subscriptions")
                                    font.bold: true
                                }

                                Rectangle
                                {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    border.color: "#d0d0d0"
                                    color: "transparent"
                                    radius: 4

                                    ListView
                                    {
                                        id: subscriptionList
                                        anchors.fill: parent
                                        anchors.margins: 6
                                        clip: true
                                        spacing: 6
                                        model: mqttBridge ? mqttBridge.logSubscriptions : []

                                        delegate: Rectangle
                                        {
                                            width: subscriptionList.width
                                            height: 36
                                            color: "#f6f6f6"
                                            radius: 4
                                            border.color: "#e0e0e0"

                                            RowLayout
                                            {
                                                anchors.fill: parent
                                                anchors.margins: 6
                                                spacing: 8

                                                Label
                                                {
                                                    text: modelData
                                                    Layout.fillWidth: true
                                                    elide: Text.ElideRight
                                                }

                                                Button
                                                {
                                                    text: qsTr("Remove")
                                                    onClicked:
                                                    {
                                                        if (mqttBridge)
                                                            mqttBridge.unsubscribeLogTopic(modelData)
                                                    }
                                                }
                                            }
                                        }

                                        ScrollBar.vertical: ScrollBar {}
                                    }
                                }
                            }
                        }

                        Item
                        {
                            // Create Table 页：根据列定义快速生成建表 SQL 并执行
                            ColumnLayout
                            {
                                anchors.fill: parent
                                spacing: 10

                                Label
                                {
                                    text: qsTr("Enter the table name and column definitions, then create the table.")
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }

                                RowLayout
                                {
                                    Layout.fillWidth: true

                                    Label
                                    {
                                        text: qsTr("Table name")
                                        Layout.preferredWidth: 90
                                    }

                                    TextField
                                    {
                                        id: newTableNameField
                                        Layout.fillWidth: true
                                        placeholderText: "mqtt_messages"
                                    }
                                }

                                Label
                                {
                                    text: qsTr("Column definitions (one line per column)")
                                    font.bold: true
                                }

                                ScrollView
                                {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Layout.minimumHeight: 140

                                    TextArea
                                    {
                                        id: newTableColumnsField
                                        width: parent.width
                                        wrapMode: TextArea.Wrap
                                        placeholderText: "id INT AUTO_INCREMENT PRIMARY KEY,\ntopic VARCHAR(512) NOT NULL,\npayload JSON NOT NULL,\nqos TINYINT UNSIGNED NOT NULL DEFAULT 0,\nretained BOOLEAN NOT NULL DEFAULT FALSE,\nduplicated BOOLEAN NOT NULL DEFAULT FALSE,\npayload_size INT NOT NULL DEFAULT 0,\nreceived_at DATETIME DEFAULT CURRENT_TIMESTAMP"
                                    }
                                }

                                RowLayout
                                {
                                    Layout.fillWidth: true
                                    Button
                                    {
                                        text: qsTr("Generate & Execute")
                                        onClicked: root.createTableFromUi()
                                    }
                                }
                            }
                        }

                        Item
                        {
                            // SQL Console 页：保留直接执行 SQL 的能力，便于调试和高级操作
                            ColumnLayout
                            {
                                anchors.fill: parent
                                spacing: 10

                                Label
                                {
                                    text: qsTr("Execute any SQL statement. Query results will be shown below.")
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }

                                ScrollView
                                {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Layout.minimumHeight: 140

                                    TextArea
                                    {
                                        id: sqlEditor
                                        width: parent.width
                                        wrapMode: TextArea.Wrap
                                        placeholderText: "SELECT * FROM my_table LIMIT 10;\n\nUPDATE my_table SET topic='demo' WHERE id=1;"
                                    }
                                }

                                RowLayout
                                {
                                    Layout.fillWidth: true
                                    Button
                                    {
                                        text: qsTr("Execute SQL")
                                        onClicked: root.executeRawSql()
                                    }
                                    Button
                                    {
                                        text: qsTr("Load selected table")
                                        onClicked:
                                        {
                                            var tableName = root.currentTableName()
                                            if (tableName === "")
                                            {
                                                root.appendLog("Please choose a table first")
                                                return
                                            }
                                            sqlEditor.text = "SELECT * FROM `" + tableName + "` LIMIT 50"
                                        }
                                    }
                                }
                            }
                        }

                        ScrollView
                        {
                            // CRUD Helper 页：面向非 SQL 熟练用户的结构化操作入口
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true

                            ColumnLayout
                            {
                                width: parent.availableWidth
                                spacing: 10

                                RowLayout
                                {
                                    Layout.fillWidth: true
                                    Label
                                    {
                                        text: qsTr("Operation")
                                        Layout.preferredWidth: 80
                                    }
                                    ComboBox
                                    {
                                        id: crudOperationBox
                                        Layout.preferredWidth: 140
                                        model: ["Select", "Insert", "Update", "Delete"]
                                    }
                                    Label
                                    {
                                        text: qsTr("Target table")
                                        Layout.preferredWidth: 90
                                    }
                                    TextField
                                    {
                                        Layout.fillWidth: true
                                        text: root.currentTableName()
                                        readOnly: true
                                    }
                                }

                                RowLayout
                                {
                                    Layout.fillWidth: true
                                    Label
                                    {
                                        text: qsTr("Fields")
                                        Layout.preferredWidth: 80
                                    }
                                    TextField
                                    {
                                        id: crudFieldsField
                                        Layout.fillWidth: true
                                        placeholderText: "* or id,name,topic"
                                    }
                                    Label
                                    {
                                        text: qsTr("Limit")
                                        Layout.preferredWidth: 50
                                    }
                                    TextField
                                    {
                                        id: crudLimitField
                                        Layout.preferredWidth: 80
                                        placeholderText: "-1"
                                    }
                                }

                                RowLayout
                                {
                                    Layout.fillWidth: true
                                    Label
                                    {
                                        text: qsTr("Condition")
                                        Layout.preferredWidth: 80
                                    }
                                    TextField
                                    {
                                        id: crudConditionField
                                        Layout.fillWidth: true
                                        placeholderText: "id = 1"
                                    }
                                }

                                RowLayout
                                {
                                    Layout.fillWidth: true
                                    Label
                                    {
                                        text: qsTr("Order by")
                                        Layout.preferredWidth: 80
                                    }
                                    TextField
                                    {
                                        id: crudOrderByField
                                        Layout.fillWidth: true
                                        placeholderText: "id DESC"
                                    }
                                }

                                Label
                                {
                                    id: valuesHelpLabel
                                    text: qsTr("Values (key=value, one per line)")
                                    font.bold: true
                                    ToolTip.visible: valuesHelpHandler.hovered
                                    ToolTip.text: qsTr("Used by Insert and Update. Enter one field assignment per line, such as topic=test/value or qos=1.")
                                    HoverHandler
                                    {
                                        id: valuesHelpHandler
                                    }
                                }

                                ScrollView
                                {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 160
                                    Layout.minimumHeight: 120

                                    TextArea
                                    {
                                        id: crudValuesField
                                        width: parent.width
                                        wrapMode: TextArea.Wrap
                                        placeholderText: "topic=test/value\npayload=hello world\npriority=1"
                                    }
                                }

                                RowLayout
                                {
                                    Layout.fillWidth: true

                                    Button
                                    {
                                        text: qsTr("Run CRUD")
                                        onClicked: root.runCrudHelper()
                                    }
                                }
                            }
                        }
                    }
                }
            }

            GroupBox
            {
                title: qsTr("SQL Result")
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                Layout.minimumHeight: 100
                padding: 8

                ScrollView
                {
                    anchors.fill: parent

                    TextArea
                    {
                        id: sqlResultArea
                        width: parent.width
                        readOnly: true
                        text: root.sqlResultText
                        wrapMode: TextArea.Wrap
                        selectByMouse: true
                    }
                }
            }

            GroupBox
            {
                title: qsTr("Logs")
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 140
                padding: 8

                ScrollView
                {
                    anchors.fill: parent
                    clip: true

                    TextArea
                    {
                        id: logArea
                        width: parent.width
                        readOnly: true
                        wrapMode: TextArea.WrapAnywhere
                        text: root.statusLog
                        font.family: "Consolas"
                        font.pointSize: 9
                        selectByMouse: true
                    }
                }
            }
        }
    }

    Component.onCompleted:
    {
        // 启动时同步一次 C++ 层状态，避免 UI 默认值与真实连接状态不一致
        appendLog("[qml] ApplicationWindow completed")
        appendLog("[qml] initial size", width, height)
        root.mqttConnected = mqttManager.isConnected()
        root.dbConnected = sqlManager.isConnected()
        root.syncBridgeSettings()
        appendLog("Application started")
    }
}
