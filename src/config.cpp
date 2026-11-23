// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - config loading and log file
// Licensed under MIT License.
#include "config.h"

#include <cstdlib>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>

Config g_cfg;

void logLine(const std::string &text) {
    if (g_cfg.log.empty()) return;
    QFile f(QString::fromStdString(g_cfg.log));
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    f.write(ts.toUtf8());
    f.write(" ");
    f.write(QByteArray::fromStdString(redactLog(text)));
    f.write("\n");
}

void loadConfig() {
    const QString exeDir = QCoreApplication::applicationDirPath();
    const char *envCfg = std::getenv("SISI_CONFIG");
    const bool fromEnv = envCfg && *envCfg;
    std::string path = fromEnv ? std::string(envCfg) : exeDir.toStdString() + "/sisi.ini";
    IniFile *ini = ini_load(path.c_str());   // a missing file is fine
#ifndef _WIN32
    // Linux and macOS: also look in ~/.config/sisi/ when there is no sisi.ini next to the exe.
    if (!ini && !fromEnv) {
        const char *home = std::getenv("HOME");
        if (home && *home) {
            const std::string alt = std::string(home) + "/.config/sisi/sisi.ini";
            ini = ini_load(alt.c_str());
            if (ini) path = alt;
        }
    }
#endif
    if (ini) g_cfg.iniPath = path;
#ifdef _WIN32
    const char *pyDefault = "python";
#else
    const char *pyDefault = "python3";
#endif
    resolveConfig(g_cfg, ini,
                  [](const char *n) -> const char * { return std::getenv(n); }, pyDefault);
    if (ini) ini_free(ini);

    // Defaults that need Qt: workspace = current directory, rex = folder "rex" next to the exe.
    if (g_cfg.workspace == ".") g_cfg.workspace = QDir::currentPath().toStdString();
    if (g_cfg.rexDir.empty()) {
        const QString cand = exeDir + QStringLiteral("/rex");
        if (QDir(cand).exists()) g_cfg.rexDir = cand.toStdString();
    }

    logLine("---- sisi start ----");
    logLine("config file: " + (g_cfg.iniPath.empty() ? "(none, tried " + path + ")" : g_cfg.iniPath));
    logLine("config used:\n" + g_cfg.report);
}
