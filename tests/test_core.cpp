// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - adapted from Layka (same author)
// Licensed under MIT License.
//
// Tests for the INI parser, config precedence and models-list parsing.
// Plain C++, no framework: exit code 0 = all passed.
#include <cstdio>
#include <map>
#include <string>

#include "core.h"
#include "markdown.h"

static int g_fail = 0;
static int g_run = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_run;                                                             \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                    \
    } while (0)

#define CHECK_STR(got, want)                                                 \
    do {                                                                     \
        ++g_run;                                                             \
        std::string g_ = (got) ? (got) : "(null)";                           \
        if (g_ != (want)) {                                                  \
            ++g_fail;                                                        \
            std::printf("FAIL %s:%d: got '%s' want '%s'\n", __FILE__,        \
                        __LINE__, g_.c_str(), (want));                       \
        }                                                                    \
    } while (0)

static const char *TMP = "sisi_test_tmp.ini";

static IniFile *loadText(const std::string &text) {
    FILE *f = std::fopen(TMP, "wb");
    if (!f) return nullptr;
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
    return ini_load(TMP);
}

static void testIni() {
    // sections, spaces around '=', both comment styles, case-insensitive names
    IniFile *ini = loadText(
        "; leading comment\n"
        "# another comment\n"
        "[sisi]\n"
        "endpoint = http://127.0.0.1:4000\n"
        "  model=haiku  \n"
        "; model = ignored\n"
        "# log = ignored\n"
        "[Other]\n"
        "Key =   spaced value here\n");
    CHECK(ini != nullptr);
    CHECK_STR(ini_get(ini, "sisi", "endpoint"), "http://127.0.0.1:4000");
    CHECK_STR(ini_get(ini, "SISI", "MODEL"), "haiku");
    CHECK(ini_get(ini, "sisi", "log") == nullptr);
    CHECK_STR(ini_get(ini, "other", "key"), "spaced value here");
    CHECK(ini_get(ini, "sisi", "nokey") == nullptr);
    CHECK(ini_get(ini, "nosection", "model") == nullptr);
    ini_free(ini);

    // CRLF line endings, and the last line without a newline
    ini = loadText("[sisi]\r\nmodel = sonnet\r\nlog = C:/x/sisi.log");
    CHECK(ini != nullptr);
    CHECK_STR(ini_get(ini, "sisi", "model"), "sonnet");
    CHECK_STR(ini_get(ini, "sisi", "log"), "C:/x/sisi.log");
    ini_free(ini);

    // UTF-8 BOM before the first section
    ini = loadText("\xEF\xBB\xBF[sisi]\nmodel = opus\n");
    CHECK(ini != nullptr);
    CHECK_STR(ini_get(ini, "sisi", "model"), "opus");
    ini_free(ini);

    // bad lines are ignored, good ones around them still work
    ini = loadText(
        "orphan = before any section\n"
        "[sisi]\n"
        "this line has no equals sign\n"
        "= value without key\n"
        "model = haiku\n"
        "[broken header\n"
        "lost = key of the broken section\n"
        "[sisi2]\n"
        "x = 1\n");
    CHECK(ini != nullptr);
    CHECK_STR(ini_get(ini, "sisi", "model"), "haiku");
    CHECK(ini_get(ini, "sisi", "this line has no equals sign") == nullptr);
    CHECK(ini_get(ini, "sisi", "lost") == nullptr);
    CHECK(ini_get(ini, "broken header", "lost") == nullptr);
    CHECK_STR(ini_get(ini, "sisi2", "x"), "1");
    ini_free(ini);

    // a value containing '=' keeps everything after the first '='
    ini = loadText("[sisi]\nendpoint = http://h/?a=b\n");
    CHECK_STR(ini_get(ini, "sisi", "endpoint"), "http://h/?a=b");
    ini_free(ini);

    // missing file: NULL, and lookups on NULL are safe
    CHECK(ini_load("definitely_missing_sisi_file.ini") == nullptr);
    CHECK(ini_get(nullptr, "sisi", "model") == nullptr);
    ini_free(nullptr);

    std::remove(TMP);
}

static std::map<std::string, std::string> g_env;
static const char *fakeEnv(const char *name) {
    auto it = g_env.find(name);
    return it == g_env.end() ? nullptr : it->second.c_str();
}

static void testConfig() {
    IniFile *ini = loadText("[sisi]\nendpoint = http://ini:4000\nmodel = haiku\n");

    // defaults: no ini, no env
    g_env.clear();
    Config c;
    resolveConfig(c, nullptr, fakeEnv, "python3");
    CHECK_STR(c.endpoint.c_str(), "http://127.0.0.1:4000");
    CHECK_STR(c.model.c_str(), "haiku");
    CHECK_STR(c.workspace.c_str(), ".");
    CHECK_STR(c.python.c_str(), "python3");
    CHECK(c.rexDir.empty() && c.log.empty());

    // ini beats defaults; keys missing from the ini keep their default
    Config c2;
    resolveConfig(c2, ini, fakeEnv, "python3");
    CHECK_STR(c2.endpoint.c_str(), "http://ini:4000");
    CHECK_STR(c2.model.c_str(), "haiku");
    CHECK_STR(c2.workspace.c_str(), ".");

    // env beats ini; an empty env var does not
    g_env["SISI_MODEL"] = "opus";
    g_env["SISI_ENDPOINT"] = "";
    g_env["SISI_LOG"] = "env.log";
    Config c3;
    resolveConfig(c3, ini, fakeEnv, "python3");
    CHECK_STR(c3.model.c_str(), "opus");
    CHECK_STR(c3.endpoint.c_str(), "http://ini:4000");
    CHECK_STR(c3.log.c_str(), "env.log");
    CHECK(c3.report.find("model = opus (env)") != std::string::npos);
    CHECK(c3.report.find("endpoint = http://ini:4000 (ini)") != std::string::npos);
    CHECK(c3.report.find("workspace = . (default)") != std::string::npos);

    // the report never contains unrelated env values (e.g. keys)
    g_env["SISI_API_KEY"] = "secret-value";
    Config c4;
    resolveConfig(c4, ini, fakeEnv, "python3");
    CHECK(c4.report.find("secret-value") == std::string::npos);
    CHECK_STR(c4.apiKey.c_str(), "secret-value");   // read, but never reported in clear
    CHECK(c4.report.find("api_key = *** (env)") != std::string::npos);

    ini_free(ini);
    std::remove(TMP);
}

static void testModels() {
    std::vector<std::string> ids = parseModelIds(
        "{\"object\":\"list\",\"data\":[{\"id\":\"haiku\",\"object\":\"model\"},"
        "{\"id\": \"sonnet\",\"object\":\"model\"},{\"id\" : \"opus\"}]}");
    CHECK(ids.size() == 3);
    if (ids.size() == 3) {
        CHECK(ids[0] == "haiku");
        CHECK(ids[1] == "sonnet");
        CHECK(ids[2] == "opus");
    }

    CHECK(parseModelIds("").empty());
    CHECK(parseModelIds("curl: (7) Failed to connect to 127.0.0.1 port 4000").empty());
    CHECK(parseModelIds("<html>404 not found</html>").empty());
    CHECK(parseModelIds("{\"error\":{\"message\":\"no auth\"}}").empty());
    CHECK(parseModelIds("{\"id\":5}").empty());       // non-string id
    CHECK(parseModelIds("{\"id\":\"unterminated").empty());
}

static void testRedact() {
    CHECK(redactLog("api_key=sk-123 next") == "api_key=*** next");
    CHECK(redactLog("Authorization: token = abc,rest") == "Authorization: token = ***,rest");
    CHECK(redactLog("{\"API_KEY\": \"sk-abc\", \"x\": 1}") == "{\"API_KEY\": \"***\", \"x\": 1}");
    CHECK(redactLog("PASSWORD:hunter2") == "PASSWORD:***");
    CHECK(redactLog("endpoint http://user:pw@host:4000/v1/models") == "endpoint http://***@host:4000/v1/models");
    CHECK(redactLog("http://127.0.0.1:4000/v1") == "http://127.0.0.1:4000/v1");
    // ordinary events must pass unchanged, including the "token" event type
    std::string ev = "rex> {\"type\":\"token\",\"text\":\"hi\"}";
    CHECK(redactLog(ev) == ev);
    CHECK(redactLog("max_tokens 100, keyboard layout") == "max_tokens 100, keyboard layout");
}

static void testFences() {
    const std::string md =
        "Intro\n"
        "```mermaid\n"
        "graph TD; A-->B;\n"
        "```\n"
        "text between ![x](a.png)\n"
        "~~~python extra\n"
        "print(1)\n"
        "```\n"                      // not a closer: wrong character
        "~~~\n"
        "```plantuml\n"
        "@startuml\n"
        "unclosed";
    std::vector<MdSegment> segs = splitFences(md);
    CHECK(segs.size() == 5);
    if (segs.size() == 5) {
        CHECK(!segs[0].fence && segs[0].raw == "Intro\n");
        CHECK(segs[1].fence && segs[1].lang == "mermaid" && segs[1].body == "graph TD; A-->B;\n");
        CHECK(!segs[2].fence && segs[2].raw == "text between ![x](a.png)\n");
        CHECK(segs[3].fence && segs[3].lang == "python" && segs[3].body == "print(1)\n```\n");
        CHECK(segs[4].fence && segs[4].lang == "plantuml" && segs[4].body == "@startuml\nunclosed");
    }
    std::string joined;                      // the raw parts give the input back
    for (const MdSegment &s : segs) joined += s.raw;
    CHECK(joined == md);

    CHECK(splitFences("").empty());
    CHECK(splitFences("no fences\r\nhere\r\n").size() == 1);
    CHECK(splitFences("a ```inline``` code\n").size() == 1);      // not a fence line
    std::vector<MdSegment> crlf = splitFences("```MermaiD\r\nA\r\n```\r\n");
    CHECK(crlf.size() == 1 && crlf[0].fence && crlf[0].lang == "mermaid");
}

static void testImagePaths() {
    CHECK(resolveImagePath("/home/u/ws", "img/a.png") == "/home/u/ws/img/a.png");
    CHECK(resolveImagePath("/home/u/ws/", "a.png") == "/home/u/ws/a.png");
    CHECK(resolveImagePath("C:\\ws", "a.png") == "C:\\ws/a.png");
    CHECK(resolveImagePath("/ws", "a/../b.png") == "/ws/a/../b.png");   // stays inside: allowed
    // everything that could leave the workspace is refused
    CHECK(resolveImagePath("/ws", "../a.png").empty());
    CHECK(resolveImagePath("/ws", "a/../../b.png").empty());
    CHECK(resolveImagePath("/ws", "a\\..\\..\\b.png").empty());
    CHECK(resolveImagePath("/ws", "/abs/a.png").empty());
    CHECK(resolveImagePath("/ws", "D:\\pics\\a.png").empty());
    CHECK(resolveImagePath("/ws", "file:///C:/pics/a.png").empty());
    CHECK(resolveImagePath("/ws", "file:///home/u/a.png").empty());
    CHECK(resolveImagePath("", "a.png").empty());
    CHECK(isLocalImagePath("a.png") && isLocalImagePath("/x/a.png") && isLocalImagePath("file:///x.png"));
    CHECK(!isLocalImagePath("https://x.org/a.png") && !isLocalImagePath("data:image/png;base64,AA"));
    CHECK(!isLocalImagePath(""));
}

int main() {
    testFences();
    testImagePaths();
    testIni();
    testRedact();
    testConfig();
    testModels();
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
