// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - config loading and log file
// Licensed under MIT License.
//
// Config precedence (see core.h): env SISI_* > sisi.ini > built-in defaults.
#ifndef SISI_CONFIG_H
#define SISI_CONFIG_H

#include <string>

#include "core.h"

extern Config g_cfg;

// Reads sisi.ini and the environment into g_cfg. Call after QApplication exists
// (the exe directory comes from Qt).
void loadConfig();

// Appends a timestamped line to the log file, if one is configured.
// Keys, tokens and URL credentials are masked (redactLog in core.h).
void logLine(const std::string &text);

#endif
