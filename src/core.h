// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - copied from Layka (same author): config resolution, models parsing, log redaction
// Licensed under MIT License.
//
// Pure logic with no UI or OS dependencies, so tests/test_core.cpp can run it.
#ifndef SISI_CORE_H
#define SISI_CORE_H

#include <cmath>
#include <deque>
#include <string>
#include <vector>

#include "ini.h"

struct Config {
    std::string endpoint = "http://127.0.0.1:4000";
    std::string model = "haiku";
    std::string workspace = ".";
    std::string python;
    std::string rexDir;
    std::string apiKey;      // optional; never written to the report in clear
    std::string merman;      // path of merman-cli for ```mermaid blocks; empty = MERMAN_HOME / PATH
    std::string log;
    std::string iniPath;    // file that was read, empty if none
    std::string report;      // "key = value (source)" lines for the log
};

// Environment lookup; getenv in production, a fake map in tests.
typedef const char *(*EnvFn)(const char *);

// Sets `out` from env var, else ini key ([sisi] section), else leaves the default.
inline void cfgPick(Config &cfg, std::string &out, EnvFn env, const IniFile *ini,
                    const char *envName, const char *iniKey) {
    const char *src = "default";
    const char *e = envName ? env(envName) : nullptr;
    const char *v = ini ? ini_get(ini, "sisi", iniKey) : nullptr;
    if (e && *e) { out = e; src = "env"; }
    else if (v && *v) { out = v; src = "ini"; }
    // Only the fixed keys are ever reported, never arbitrary env values.
    const bool secret = std::string(iniKey) == "api_key";
    cfg.report += std::string("  ") + iniKey + " = " + (secret && !out.empty() ? "***" : out) +
                  " (" + src + ")\n";
}

// Precedence: env vars > ini > built-in defaults (pythonDefault is per OS).
inline void resolveConfig(Config &cfg, const IniFile *ini, EnvFn env,
                          const char *pythonDefault) {
    cfg.python = pythonDefault;
    cfg.report.clear();
    cfgPick(cfg, cfg.endpoint, env, ini, "SISI_ENDPOINT", "endpoint");
    cfgPick(cfg, cfg.model, env, ini, "SISI_MODEL", "model");
    cfgPick(cfg, cfg.workspace, env, ini, "SISI_WORKSPACE", "workspace");
    cfgPick(cfg, cfg.python, env, ini, "SISI_PYTHON", "python");
    cfgPick(cfg, cfg.rexDir, env, ini, "SISI_REX_DIR", "rex_dir");
    cfgPick(cfg, cfg.apiKey, env, ini, "SISI_API_KEY", "api_key");
    cfgPick(cfg, cfg.merman, env, ini, nullptr, "merman");   // env MERMAN_HOME is handled by the renderer
    cfgPick(cfg, cfg.log, env, ini, "SISI_LOG", "log");
}

// Extracts the model ids from an OpenAI-style /v1/models reply:
// every  "id" : "value"  pair. Anything else yields an empty list.
inline std::vector<std::string> parseModelIds(const std::string &out) {
    std::vector<std::string> ids;
    size_t p = 0;
    while (true) {
        size_t k = out.find("\"id\"", p);
        if (k == std::string::npos) break;
        size_t i = k + 4;
        while (i < out.size() && (out[i] == ' ' || out[i] == '\t')) ++i;
        if (i >= out.size() || out[i] != ':') { p = k + 4; continue; }
        ++i;
        while (i < out.size() && (out[i] == ' ' || out[i] == '\t')) ++i;
        if (i >= out.size() || out[i] != '"') { p = k + 4; continue; }
        size_t q2 = out.find('"', i + 1);
        if (q2 == std::string::npos) break;
        std::string id = out.substr(i + 1, q2 - i - 1);
        if (!id.empty()) ids.push_back(id);
        p = q2 + 1;
    }
    return ids;
}

// Masks secrets before a line goes to the log file:
//  - the value after key / token / secret / password followed by '=' or ':'
//    (case-insensitive; also JSON style  "api_key": "...");
//  - the user:password part of URLs (scheme://user:pass@host -> scheme://***@host).
inline std::string redactLog(const std::string &in) {
    static const char *words[] = {"key", "token", "secret", "password"};
    std::string low = in;
    for (size_t i = 0; i < low.size(); ++i)
        if (low[i] >= 'A' && low[i] <= 'Z') low[i] = (char)(low[i] - 'A' + 'a');

    std::string out;
    size_t i = 0;
    while (i < in.size()) {
        size_t wl = 0;
        for (const char *w : words) {
            size_t n = std::string(w).size();
            if (low.compare(i, n, w) == 0) { wl = n; break; }
        }
        if (wl == 0) { out += in[i++]; continue; }
        size_t j = i + wl;
        if (j < in.size() && (in[j] == '"' || in[j] == '\'')) ++j;
        while (j < in.size() && (in[j] == ' ' || in[j] == '\t')) ++j;
        if (j >= in.size() || (in[j] != '=' && in[j] != ':')) {
            out.append(in, i, wl);
            i += wl;
            continue;
        }
        ++j;
        while (j < in.size() && (in[j] == ' ' || in[j] == '\t')) ++j;
        out.append(in, i, j - i);
        if (j < in.size() && (in[j] == '"' || in[j] == '\'')) {
            char q = in[j];
            out += q;
            out += "***";
            size_t e = in.find(q, j + 1);
            j = (e == std::string::npos) ? in.size() : e;   // keep the closing quote
        } else {
            out += "***";
            while (j < in.size() && in[j] != ' ' && in[j] != '\t' && in[j] != ',' &&
                   in[j] != '&' && in[j] != '}' && in[j] != ';' && in[j] != '"')
                ++j;
        }
        i = j;
    }

    // URL userinfo
    std::string res;
    size_t p = 0;
    while (true) {
        size_t s = out.find("://", p);
        if (s == std::string::npos) { res.append(out, p, std::string::npos); break; }
        s += 3;
        size_t e = s;
        while (e < out.size() && std::string(" \t\"'/?#)<>").find(out[e]) == std::string::npos) ++e;
        size_t at = (e > s) ? out.rfind('@', e - 1) : std::string::npos;
        res.append(out, p, s - p);
        if (at != std::string::npos && at >= s && at < e) {
            res += "***";
            p = at;
        } else {
            p = s;
        }
    }
    return res;
}

// ---------------------------------------------------------------------------
// Permission requests from rex
// ---------------------------------------------------------------------------

enum class PermDecision { AllowOnce, AllowAlways, Deny };

inline const char *decisionName(PermDecision d) {
    switch (d) {
    case PermDecision::AllowOnce: return "allow_once";
    case PermDecision::AllowAlways: return "allow_always";
    default: return "deny";
    }
}

struct PermissionReq {
    std::string id, tool, kind, summary, detail;
};

// Cuts `s` to at most `max` bytes without splitting a UTF-8 sequence; adds a marker if cut.
inline std::string clipText(const std::string &s, size_t max) {
    if (s.size() <= max) return s;
    size_t n = max;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    return s.substr(0, n) + "
[... truncated]";
}

// Detail is shown in full up to 20000 characters, the summary is one short line.
inline PermissionReq makePermissionReq(const std::string &id, const std::string &tool,
                                       const std::string &kind, const std::string &summary,
                                       const std::string &detail) {
    PermissionReq r;
    r.id = id;
    r.tool = tool.empty() ? "?" : tool;
    r.kind = kind;
    r.summary = clipText(summary, 2000);
    r.detail = clipText(detail, 20000);
    return r;
}

// Requests wait here until the user answered; pop() hands out an id exactly once,
// so a request can never get two replies.
class PermQueue {
public:
    // False (and ignored) when the id is empty or already waiting.
    bool push(const PermissionReq &r) {
        if (r.id.empty()) return false;
        for (const PermissionReq &q : q_)
            if (q.id == r.id) return false;
        q_.push_back(r);
        return true;
    }
    bool empty() const { return q_.empty(); }
    size_t size() const { return q_.size(); }
    const PermissionReq &front() const { return q_.front(); }
    // Removes the first request and returns its id; empty string if none is waiting.
    std::string pop() {
        if (q_.empty()) return std::string();
        std::string id = q_.front().id;
        q_.pop_front();
        return id;
    }
private:
    std::deque<PermissionReq> q_;
};

// Class of one line of a unified diff: '+' added, '-' removed, '@' hunk header,
// 'h' file header (--- / +++), ' ' anything else.
inline char classifyDiffLine(const std::string &line) {
    if (line.compare(0, 4, "+++ ") == 0 || line.compare(0, 4, "--- ") == 0) return 'h';
    if (line.compare(0, 2, "@@") == 0) return '@';
    if (!line.empty() && line[0] == '+') return '+';
    if (!line.empty() && line[0] == '-') return '-';
    return ' ';
}

// True when the text looks like a unified diff: some line is a hunk header "@@ ... @@".
inline bool looksLikeDiff(const std::string &text) {
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('
', p);
        if (e == std::string::npos) e = text.size();
        if (e - p >= 4 && text.compare(p, 2, "@@") == 0) {
            size_t m = text.find("@@", p + 2);
            if (m != std::string::npos && m < e) return true;
        }
        p = e + 1;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Context usage from rex
// ---------------------------------------------------------------------------

// "ctx 83%" for the status bar; percent is clamped to 0..999 and rounded.
inline std::string ctxLabel(double percent) {
    if (!(percent >= 0)) percent = 0;            // also catches NaN
    if (percent > 999) percent = 999;
    return "ctx " + std::to_string(static_cast<int>(std::floor(percent + 0.5))) + "%";
}

// Orange when at 80 % or more, or when rex flags a warning.
inline bool ctxIsHigh(double percent, bool warning) { return warning || percent >= 80.0; }

#endif
