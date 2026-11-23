// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - Markdown helpers
// Licensed under MIT License.
//
// Pure helpers for the chat renderer, no Qt, so tests/test_core.cpp can run them:
//  - splitFences: cuts a Markdown text into plain parts and fenced code blocks
//    (so diagrams can be found and images are only rewritten outside code);
//  - resolveImagePath: turns the path of a Markdown image into a file path.
#ifndef SISI_MARKDOWN_H
#define SISI_MARKDOWN_H

#include <cctype>
#include <string>
#include <vector>

struct MdSegment {
    bool fence = false;     // true: a fenced code block
    std::string lang;       // fence only: first word of the info string, lower case
    std::string body;       // fence only: the code between the fence lines
    std::string raw;        // the exact source text; all raws joined give the input back
};

inline std::vector<MdSegment> splitFences(const std::string &md) {
    std::vector<MdSegment> out;
    MdSegment text, fence;
    bool inFence = false;
    char fenceCh = 0;
    size_t fenceLen = 0;

    size_t pos = 0;
    while (pos < md.size()) {
        size_t nl = md.find('\n', pos);
        size_t next = (nl == std::string::npos) ? md.size() : nl + 1;
        const std::string line = md.substr(pos, next - pos);
        pos = next;

        std::string t = line;                                  // the line without its ending
        while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
        size_t i = 0;
        while (i < t.size() && i < 3 && t[i] == ' ') ++i;      // up to 3 spaces of indent
        char ch = 0;
        size_t run = 0;
        if (i < t.size() && (t[i] == '`' || t[i] == '~')) {
            ch = t[i];
            while (i + run < t.size() && t[i + run] == ch) ++run;
        }

        if (!inFence) {
            bool opens = run >= 3;
            if (opens && ch == '`' && t.find('`', i + run) != std::string::npos) opens = false;
            if (!opens) { text.raw += line; continue; }
            if (!text.raw.empty()) { out.push_back(text); text = MdSegment(); }
            inFence = true;
            fenceCh = ch;
            fenceLen = run;
            fence = MdSegment();
            fence.fence = true;
            fence.raw = line;
            size_t a = t.find_first_not_of(" \t", i + run);    // info string: first word is the language
            if (a != std::string::npos) {
                size_t b = t.find_first_of(" \t", a);
                fence.lang = t.substr(a, b == std::string::npos ? std::string::npos : b - a);
                for (char &c : fence.lang) c = (char)std::tolower((unsigned char)c);
            }
        } else {
            fence.raw += line;
            const bool closes = ch == fenceCh && run >= fenceLen &&
                                t.find_first_not_of(" \t", i + run) == std::string::npos;
            if (closes) { out.push_back(fence); inFence = false; }
            else fence.body += line;
        }
    }
    if (inFence) out.push_back(fence);                         // unclosed fence runs to the end
    if (!text.raw.empty()) out.push_back(text);
    return out;
}

// True for a path that points to a file on this machine (not http, data: or our own names).
inline bool isLocalImagePath(const std::string &p) {
    if (p.empty()) return false;
    static const char *remote[] = {"http://", "https://", "data:", "sisi://"};
    for (const char *r : remote)
        if (p.compare(0, std::string(r).size(), r) == 0) return false;
    return true;
}

// Relative paths are taken from the workspace. A file:// prefix is removed.
inline std::string resolveImagePath(const std::string &workspace, std::string p) {
    if (p.compare(0, 7, "file://") == 0) {
        p.erase(0, 7);
        if (p.size() >= 3 && p[0] == '/' && std::isalpha((unsigned char)p[1]) && p[2] == ':')
            p.erase(0, 1);                                     // file:///C:/x -> C:/x
    }
    const bool absolute = !p.empty() && (p[0] == '/' || p[0] == '\\' ||
                          (p.size() >= 2 && std::isalpha((unsigned char)p[0]) && p[1] == ':'));
    if (absolute || workspace.empty()) return p;
    std::string base = workspace;
    while (!base.empty() && (base.back() == '/' || base.back() == '\\')) base.pop_back();
    return base + "/" + p;
}

#endif
