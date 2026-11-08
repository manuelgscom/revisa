#include "doctest.h"

#include "core/fetch/fetch.hpp"
#include "core/fetch/model_info.hpp"
#include "core/fetch/model_resolve.hpp"
#include "core/fetch/sha256.hpp"
#include "test_util.hpp"

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

using namespace revisa::fetch;

namespace {

using revisa::test::EnvGuard;

// Own temporary directory, removed on exit.
struct TempDir : revisa::test::TempDir {
    TempDir() : revisa::test::TempDir("revisa_fetch_test_") {}
};

// mode is only applied on POSIX.
void write_file(const std::string& p, const std::string& text, [[maybe_unused]] unsigned mode = 0644) {
    {
        std::ofstream out(p, std::ios::binary);
        out << text;
    }
#ifndef _WIN32
    ::chmod(p.c_str(), static_cast<mode_t>(mode));
#endif
}

std::size_t entries(const std::string& dir) {
    std::size_t n = 0;
    for ([[maybe_unused]] const auto& e : std::filesystem::directory_iterator(dir)) ++n;
    return n;
}

const std::string kUrl = "https://example.invalid/m.gguf";

#ifdef _WIN32
// The fake curl.exe (tests/win/fake_curl.cpp) is driven by these variables; all of them are
// cleared on construction and restored on destruction.
struct FakeCurlEnv {
    EnvGuard mode{"REVISA_FAKE_CURL_MODE"};
    EnvGuard src{"REVISA_FAKE_CURL_SRC"};
    EnvGuard exit_code{"REVISA_FAKE_CURL_EXIT"};
    EnvGuard chunks{"REVISA_FAKE_CURL_CHUNKS"};
    EnvGuard args{"REVISA_FAKE_CURL_ARGS"};
    FakeCurlEnv() {
        for (EnvGuard* g : {&mode, &src, &exit_code, &chunks, &args}) g->unset();
    }
};
#endif

// Fake curl: copies src to the -o path. Never touches the network. On POSIX the source path is
// baked into a shell script; on Windows it reaches the fake curl.exe through the environment.
struct FakeCurl {
#ifdef _WIN32
    FakeCurlEnv env;
#else
    TempDir bin;
#endif
    TempDir src_dir;
    std::string src;
    std::string data = "fake model contents\n";

    FakeCurl() {
        src = src_dir.path + "/model.src";
        write_file(src, data);
#ifdef _WIN32
        env.mode.set("copy");
        env.src.set(src);
#else
        write_file(bin.path + "/curl",
                   "#!/bin/sh\n"
                   "out=\n"
                   "while [ $# -gt 0 ]; do\n"
                   "    case \"$1\" in\n"
                   "        -o) out=$2; shift ;;\n"
                   "    esac\n"
                   "    shift\n"
                   "done\n"
                   "[ -n \"$out\" ] || exit 2\n"
                   "cat '" + src + "' > \"$out\"\n",
                   0755);
#endif
    }

    DownloadOptions options() const {
        DownloadOptions o;
#ifdef _WIN32
        o.path_env = REVISA_FAKE_CURL_DIR;
#else
        o.path_env = bin.path;
#endif
        return o;
    }
};

#ifndef _WIN32
// Fake curl script that writes `chunks` bytes to the -o file, one every 50 ms (then exits 0).
std::string slow_curl(const std::string& bin, int chunks) {
    write_file(bin + "/curl",
               "#!/bin/sh\n"
               "out=\n"
               "while [ $# -gt 0 ]; do\n"
               "    case \"$1\" in\n"
               "        -o) out=$2; shift ;;\n"
               "    esac\n"
               "    shift\n"
               "done\n"
               "[ -n \"$out\" ] || exit 2\n"
               ": > \"$out\"\n"
               "i=0\n"
               "while [ $i -lt " + std::to_string(chunks) + " ]; do\n"
               "    printf x >> \"$out\"\n"
               "    sleep 0.05\n"
               "    i=$((i + 1))\n"
               "done\n",
               0755);
    return bin;
}
#endif

// Fake curl that writes `chunks` bytes to the -o file, one every 50 ms (then exits 0).
struct SlowCurl {
#ifdef _WIN32
    FakeCurlEnv env;
#else
    TempDir bin;
#endif
    explicit SlowCurl(int chunks) {
#ifdef _WIN32
        env.mode.set("slow");
        env.chunks.set(std::to_string(chunks));
#else
        slow_curl(bin.path, chunks);
#endif
    }
    std::string path_env() const {
#ifdef _WIN32
        return REVISA_FAKE_CURL_DIR;
#else
        return bin.path;
#endif
    }
};

}  // namespace

TEST_CASE("download: success leaves the file in place with 0644 and no temp") {
    FakeCurl curl;
    TempDir out;
    const std::string dir = out.path + "/a/b";  // created with parents
    const DownloadResult r = download(kUrl, dir, "m.gguf", curl.data.size(), sha256_hex(curl.data), curl.options());
    REQUIRE(r.status == DownloadResult::Status::Ok);
    CHECK(r.error.empty());
    CHECK(r.path == revisa::test::join(dir, "m.gguf"));
#ifndef _WIN32
    struct stat st {};
    REQUIRE(stat(r.path.c_str(), &st) == 0);
    CHECK((st.st_mode & 07777) == 0644);
#endif
    CHECK(sha256_file(r.path) == sha256_hex(curl.data));
    CHECK(entries(dir) == 1);
}

TEST_CASE("download: wrong SHA-256 fails and leaves nothing") {
    FakeCurl curl;
    TempDir out;
    const DownloadResult r =
        download(kUrl, out.path, "m.gguf", curl.data.size(), sha256_hex("other"), curl.options());
    CHECK(r.status == DownloadResult::Status::Failed);
    CHECK(r.error == "el fichero descargado no es íntegro");
    CHECK(entries(out.path) == 0);
}

TEST_CASE("download: wrong size fails and leaves nothing") {
    FakeCurl curl;
    TempDir out;
    const DownloadResult r =
        download(kUrl, out.path, "m.gguf", curl.data.size() + 1, sha256_hex(curl.data), curl.options());
    CHECK(r.status == DownloadResult::Status::Failed);
    CHECK(r.error == "el fichero descargado no es íntegro");
    CHECK(entries(out.path) == 0);
}

TEST_CASE("download: the tool's exit code is reported and nothing is left") {
    TempDir out;
    DownloadOptions o;
#ifdef _WIN32
    FakeCurlEnv env;
    env.mode.set("fail");
    env.exit_code.set("22");
    o.path_env = REVISA_FAKE_CURL_DIR;
#else
    TempDir bin;
    write_file(bin.path + "/curl", "#!/bin/sh\nexit 22\n", 0755);
    o.path_env = bin.path;
#endif
    const DownloadResult r = download(kUrl, out.path, "m.gguf", 10, sha256_hex(""), o);
    CHECK(r.status == DownloadResult::Status::Failed);
    CHECK(r.error == "la descarga ha fallado: curl salió con código 22");
    CHECK(entries(out.path) == 0);
}

TEST_CASE("download: neither curl nor wget in PATH") {
    TempDir empty_bin;
    TempDir out;
    DownloadOptions o;
    o.path_env = empty_bin.path;
    const DownloadResult r = download(kUrl, out.path, "m.gguf", 10, "abc", o);
    CHECK(r.status == DownloadResult::Status::Failed);
#ifdef _WIN32
    CHECK(r.error.rfind("no se encuentra curl; ", 0) == 0);
#else
    CHECK(r.error.rfind("no se encuentra curl ni wget", 0) == 0);
#endif
    CHECK(r.error.find(kUrl) != std::string::npos);
    CHECK(entries(out.path) == 0);
}

TEST_CASE("download: not enough free space") {
    FakeCurl curl;
    TempDir out;
    DownloadOptions o = curl.options();
    std::string asked;
    o.free_space = [&](const std::string& d) -> std::optional<std::uint64_t> {
        asked = d;
        return std::uint64_t{1} << 20;  // 1 MiB free
    };
    const DownloadResult r = download(kUrl, out.path, "m.gguf", 10u << 20, sha256_hex(curl.data), o);
    CHECK(r.status == DownloadResult::Status::Failed);
    CHECK(asked == out.path);
    CHECK(r.error == "no hay espacio suficiente en " + out.path + ": hacen falta 11 MiB y hay 1 MiB libres");
    CHECK(entries(out.path) == 0);

    // Unknown free space does not block the download.
    o.free_space = [](const std::string&) { return std::optional<std::uint64_t>{}; };
    CHECK(download(kUrl, out.path, "m.gguf", curl.data.size(), sha256_hex(curl.data), o).status ==
          DownloadResult::Status::Ok);
}

#ifdef _WIN32

TEST_CASE("models_dir: LOCALAPPDATA") {
    EnvGuard local("LOCALAPPDATA");
    local.set("C:\\Users\\ana\\AppData\\Local\\");
    CHECK(models_dir() == "C:\\Users\\ana\\AppData\\Local\\revisa\\models");
    local.unset();
    CHECK(models_dir().empty());
}

TEST_CASE("download: curl.exe gets the fixed argv, with paths that need quoting") {
    FakeCurl curl;
    TempDir out;
    TempDir rec;
    const std::string args_file = rec.path + "\\args.txt";
    curl.env.args.set(args_file);
    const std::string dir = out.path + "\\con espacio ñ";
    const DownloadResult r = download(kUrl, dir, "m.gguf", curl.data.size(), sha256_hex(curl.data), curl.options());
    REQUIRE(r.status == DownloadResult::Status::Ok);

    std::vector<std::string> argv;
    {
        std::ifstream in(args_file, std::ios::binary);
        for (std::string line; std::getline(in, line);) argv.push_back(line);
    }
    REQUIRE(argv.size() == 13);
    std::error_code ec;
    CHECK(std::filesystem::equivalent(argv[0], std::filesystem::path(REVISA_FAKE_CURL_DIR) / "curl.exe", ec));
    CHECK(std::vector<std::string>(argv.begin() + 1, argv.begin() + 11) ==
          std::vector<std::string>{"-fL", "--proto", "=https", "--tlsv1.2", "--retry", "3", "--connect-timeout",
                                   "20", "-sS", "-o"});
    CHECK(argv[11].rfind(dir + "\\.m.gguf.part-", 0) == 0);
    CHECK(argv[12] == kUrl);
}

#else

TEST_CASE("models_dir: XDG_DATA_HOME and HOME") {
    EnvGuard xdg("XDG_DATA_HOME");
    EnvGuard home("HOME");
    home.set("/home/ana");
    xdg.set("/xdg/");
    CHECK(models_dir() == "/xdg/revisa/models");
    xdg.set("");
    CHECK(models_dir() == "/home/ana/.local/share/revisa/models");
    xdg.unset();
    CHECK(models_dir() == "/home/ana/.local/share/revisa/models");
    home.unset();
    CHECK(models_dir().empty());
}

#endif  // _WIN32

TEST_CASE("download: on_progress sees the temp file grow") {
    SlowCurl curl(20);  // ~1 s
    TempDir out;
    DownloadOptions o;
    o.path_env = curl.path_env();
    o.ignore_interrupts = false;
    std::vector<std::uint64_t> seen;
    o.on_progress = [&](std::uint64_t done) {
        seen.push_back(done);
        return true;
    };
    const std::string data(20, 'x');
    const DownloadResult r = download(kUrl, out.path, "m.gguf", data.size(), sha256_hex(data), o);
    REQUIRE(r.status == DownloadResult::Status::Ok);
    REQUIRE(seen.size() >= 2);
    for (std::size_t i = 1; i < seen.size(); ++i) CHECK(seen[i] >= seen[i - 1]);
    CHECK(seen.back() > seen.front());
    CHECK(seen.back() <= data.size());
}

TEST_CASE("download: on_progress returning false cancels and removes the temp file") {
    SlowCurl curl(200);  // ~10 s if not cancelled
    TempDir out;
    DownloadOptions o;
    o.path_env = curl.path_env();
    o.ignore_interrupts = false;
    int calls = 0;
    std::uint64_t last = 0;
    o.on_progress = [&](std::uint64_t done) {
        last = done;
        return ++calls < 3;
    };
    const auto t0 = std::chrono::steady_clock::now();
    const DownloadResult r = download(kUrl, out.path, "m.gguf", 200, sha256_hex(std::string(200, 'x')), o);
    const auto secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(r.status == DownloadResult::Status::Cancelled);
    CHECK(r.error == "descarga cancelada");
    CHECK(calls == 3);
    CHECK(last > 0);
    CHECK(secs < 5.0);
    CHECK(entries(out.path) == 0);
}

// --- Model resolution -----------------------------------------------------------------------

TEST_CASE("model_resolve: resolution, name and size") {
    TempDir tmp;
    const ModelSpec spec{"nombre", "m.gguf", "https://example.invalid/m.gguf", 5, "x"};
    using S = ModelResolution::Status;
    CHECK(resolve_model("", "", spec).status == S::NoDir);
    CHECK(resolve_model(tmp.path + "/no.gguf", tmp.path, spec).status == S::Missing);
    CHECK(resolve_model("", tmp.path, spec).status == S::NeedsDownload);
    write_file(tmp.path + "/m.gguf", "1234");  // wrong size
    CHECK(resolve_model("", tmp.path, spec).status == S::NeedsDownload);
    write_file(tmp.path + "/m.gguf", "12345");
    const auto r = resolve_model("", tmp.path, spec);
    CHECK(r.status == S::Ready);
    CHECK(r.path == revisa::test::join(tmp.path, "m.gguf"));
    CHECK(resolve_model(tmp.path + "/m.gguf", "", spec).status == S::Ready);  // explicit: no dir needed
    std::filesystem::create_directory(tmp.path + "/d.gguf");                  // a directory is not the model
    const ModelSpec dir_spec{"nombre", "d.gguf", "", 0, "x"};
    CHECK(resolve_model("", tmp.path, dir_spec).status == S::NeedsDownload);
    CHECK(model_name("", spec) == "nombre");
    CHECK(model_name("/a/b/otro.gguf", spec) == "otro.gguf");
    CHECK(model_name("otro.gguf", spec) == "otro.gguf");
    CHECK(size_mb(529297888ULL) == 529);
    CHECK(size_mb(11) == 1);
    CHECK(size_mb(0) == 0);
}

TEST_CASE("model_resolve: default spec and the test-hook overrides") {
    EnvGuard url("REVISA_TEST_MODEL_URL");
    EnvGuard size("REVISA_TEST_MODEL_SIZE");
    EnvGuard sha("REVISA_TEST_MODEL_SHA256");
    url.unset();
    ModelSpec s = default_model_spec();
    CHECK(s.name == kDefaultModel.name);
    CHECK(s.filename == kDefaultModel.filename);
    CHECK(s.url == kDefaultModel.url);
    CHECK(s.size == kDefaultModel.size);
    CHECK(s.sha256 == kDefaultModel.sha256);

    url.set("https://example.invalid/t.gguf");
    size.set("11");
    sha.set("abc");
    s = default_model_spec();
#if REVISA_TEST_HOOKS
    CHECK(s.url == "https://example.invalid/t.gguf");
    CHECK(s.size == 11);
    CHECK(s.sha256 == "abc");
#else
    CHECK(s.url == kDefaultModel.url);
#endif
    CHECK(s.name == kDefaultModel.name);  // name and file name never change
    CHECK(s.filename == kDefaultModel.filename);
}

#ifdef _WIN32
// The real curl.exe that Windows 10/11 ships, over HTTPS, through the same download() the app
// uses. Needs the network, so it only runs with REVISA_TEST_NETWORK=1. The file is doctest's
// LICENSE.txt at the commit of third_party/doctest (1086 bytes).
TEST_CASE("download: the system curl.exe over HTTPS (REVISA_TEST_NETWORK=1)") {
    const char* net = std::getenv("REVISA_TEST_NETWORK");
    if (net == nullptr || std::string(net) != "1") {
        MESSAGE("REVISA_TEST_NETWORK not set: skipped");
        return;
    }
    revisa::test::TempDir tmp;
    const std::string dir = tmp.path + "\\models con ñ";
    const DownloadResult r = download(
        "https://raw.githubusercontent.com/doctest/doctest/2d0a9359a60c51affe2a9bebb1be1dca47868151/LICENSE.txt",
        dir, "LICENSE.txt", 1086, "0fe0b331fa1513dcce8604ff1fa925f32d1cea17d8aeb1c2471fad40d291adc5");
    INFO(r.error);
    REQUIRE(r.status == DownloadResult::Status::Ok);
    CHECK(r.path == dir + "\\LICENSE.txt");
    CHECK(std::filesystem::file_size(std::filesystem::path(r.path)) == 1086);  // UTF-8 code page (manifest)
}
#endif
