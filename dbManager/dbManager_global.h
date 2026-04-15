#ifndef DBMANAGER_GLOBAL_H
#define DBMANAGER_GLOBAL_H

#include <QtCore/qglobal.h>

/*
 * 为什么需要这个头文件：
 * - dbManager 可作为独立库被其他模块链接（如 Test）。
 * - Windows 下导出/导入符号需要显式声明，避免链接阶段找不到类符号。
 * - 通过 DBMANAGER_LIBRARY 宏区分“构建库本身”和“使用该库”两种场景。
 */
#if defined(DBMANAGER_LIBRARY)
#define DBMANAGER_EXPORT Q_DECL_EXPORT
#else
#define DBMANAGER_EXPORT Q_DECL_IMPORT
#endif

#endif // DBMANAGER_GLOBAL_H
