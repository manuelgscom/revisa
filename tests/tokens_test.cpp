#include "doctest.h"

#include "core/tokens/tokens.hpp"
#include "test_util.hpp"

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace revisa::tokens;
namespace fs = std::filesystem;

namespace {

const std::string kHash = std::string(64, 'a');

bool contains(const std::string& s, std::string_view part) { return s.find(part) != std::string::npos; }

// A temporary directory, removed on destruction.
struct TempDir {
    std::string dir;
    TempDir() : dir(revisa::test::make_temp_dir("revisa-tokens-")) {}
    ~TempDir() { revisa::test::remove_tree(dir); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

}  // namespace

TEST_CASE("tokens: valid names") {
    CHECK(valid_name("firefox"));
    CHECK(valid_name("chrome-1.2_x"));
    CHECK(valid_name(std::string(kMaxNameLength, 'a')));
    CHECK_FALSE(valid_name(""));
    CHECK_FALSE(valid_name(std::string(kMaxNameLength + 1, 'a')));
    CHECK_FALSE(valid_name("con espacio"));
    CHECK_FALSE(valid_name("extensión"));
    CHECK_FALSE(valid_name("a/b"));
}

TEST_CASE("tokens: parse and serialize round trip") {
    const std::string text = "# comment\n\n" + kHash + " firefox 2025-03-14T10:00:00Z\r\n" +
                             std::string(64, '0') + "\tchrome   2026-01-02T03:04:05Z";
    const LoadResult r = parse(text, "tokens");
    REQUIRE(r.entries);
    REQUIRE(r.entries->size() == 2);
    CHECK((*r.entries)[0].hash == kHash);
    CHECK((*r.entries)[0].name == "firefox");
    CHECK((*r.entries)[0].created == "2025-03-14T10:00:00Z");
    CHECK((*r.entries)[1].name == "chrome");

    const LoadResult again = parse(serialize(*r.entries), "tokens");
    REQUIRE(again.entries);
    REQUIRE(again.entries->size() == 2);
    CHECK((*again.entries)[1].hash == std::string(64, '0'));
    CHECK((*again.entries)[1].created == "2026-01-02T03:04:05Z");
}

TEST_CASE("tokens: malformed lines report file:line") {
    const auto err = [](const std::string& text) {
        const LoadResult r = parse(text, "f");
        CHECK_FALSE(r.entries);
        return r.error;
    };
    CHECK(err("\n" + kHash + " a").rfind("f:2: ", 0) == 0);                                // two fields
    CHECK(err(kHash + " a 2025-03-14T10:00:00Z extra").rfind("f:1: ", 0) == 0);           // four
    CHECK(err(std::string(63, 'a') + " a 2025-03-14T10:00:00Z").rfind("f:1: ", 0) == 0);   // short hash
    CHECK(err(std::string(64, 'A') + " a 2025-03-14T10:00:00Z").rfind("f:1: ", 0) == 0);   // uppercase
    CHECK(err(kHash + " a$ 2025-03-14T10:00:00Z").rfind("f:1: ", 0) == 0);                 // name
    CHECK(err(kHash + " a 2025-03-14").rfind("f:1: ", 0) == 0);                            // date
    const std::string dup = err(kHash + " a 2025-03-14T10:00:00Z\n" + kHash + " a 2025-03-14T10:00:00Z");
    CHECK(dup.rfind("f:2: ", 0) == 0);
    CHECK(contains(dup, "repetido"));
}

TEST_CASE("tokens: new tokens are random hex and only their hash matches") {
    const std::string a = new_token(), b = new_token();
    CHECK(a.size() == 64);
    CHECK(a != b);
    CHECK(a.find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK(hash_token("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    const std::vector<Entry> entries = {{hash_token("one"), "x", "2025-03-14T10:00:00Z"},
                                        {hash_token(a), "y", "2025-03-14T10:00:00Z"}};
    CHECK(verify(entries, a));
    CHECK(verify(entries, "one"));
    CHECK_FALSE(verify(entries, b));
    CHECK_FALSE(verify(entries, ""));
    CHECK_FALSE(verify(entries, hash_token(a)));  // the stored hash is not a token
    CHECK_FALSE(verify({}, a));
}

TEST_CASE("tokens: now_utc has the stored format") {
    const std::string t = now_utc();
    CHECK(t.size() == 20);
    CHECK(t[10] == 'T');
    CHECK(t.back() == 'Z');
    CHECK(parse(kHash + " a " + t, "f").entries);
}

TEST_CASE("tokens: add, list, revoke on disk") {
    TempDir tmp;
    const std::string path = tmp.dir + "/conf/revisa/tokens";  // the directory does not exist yet

    LoadResult r = load(path);
    REQUIRE(r.entries);
    CHECK(r.entries->empty());

    const Change c = add(path, "firefox");
    REQUIRE(c.status == Status::Ok);
    CHECK(c.token.size() == 64);
    CHECK(add(path, "firefox").status == Status::Duplicate);
    CHECK(add(path, "mal nombre").status == Status::InvalidName);
    REQUIRE(add(path, "chrome").status == Status::Ok);

#ifndef _WIN32
    struct stat st {};
    REQUIRE(stat(path.c_str(), &st) == 0);
    CHECK((st.st_mode & 0777) == 0600);
    REQUIRE(stat((tmp.dir + "/conf/revisa").c_str(), &st) == 0);
    CHECK((st.st_mode & 0777) == 0700);
#endif

    std::string text;
    {
        // Closed before revoke(): Windows cannot replace a file that is still open.
        std::ifstream f(path, std::ios::binary);
        text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    CHECK_FALSE(contains(text, c.token));  // only the hash is stored
    CHECK(contains(text, hash_token(c.token)));

    r = load(path);
    REQUIRE(r.entries);
    REQUIRE(r.entries->size() == 2);
    CHECK(verify(*r.entries, c.token));

    CHECK(revoke(path, "nadie").status == Status::NotFound);
    CHECK(revoke(path, "a b").status == Status::InvalidName);
    CHECK(revoke(path, "firefox").status == Status::Ok);
    r = load(path);
    REQUIRE(r.entries);
    REQUIRE(r.entries->size() == 1);
    CHECK((*r.entries)[0].name == "chrome");
    CHECK_FALSE(verify(*r.entries, c.token));

    // No temporary files left behind.
    int files = 0;
    for ([[maybe_unused]] const auto& e : fs::directory_iterator(tmp.dir + "/conf/revisa")) ++files;
    CHECK(files == 1);
}

TEST_CASE("tokens: a malformed file is an error, not an empty list") {
    TempDir tmp;
    const std::string path = tmp.dir + "/tokens";
    std::ofstream(path, std::ios::binary) << "basura\n";
    const LoadResult r = load(path);
    CHECK_FALSE(r.entries);
    CHECK(r.error.rfind(path + ":1: ", 0) == 0);
    const Change c = add(path, "x");
    CHECK(c.status == Status::FileError);
    CHECK(c.error == r.error);
}

TEST_CASE("tokens: unique names for pairing") {
    const std::vector<Entry> entries = {{kHash, "Firefox", "2025-03-14T10:00:00Z"},
                                        {kHash, "Firefox-2", "2025-03-14T10:00:00Z"}};
    CHECK(unique_name({}, "Chrome") == "Chrome");
    CHECK(unique_name({}, "") == "cliente");
    CHECK(unique_name({}, "a b/c") == "a-b-c");
    CHECK(unique_name(entries, "Firefox") == "Firefox-3");
    CHECK(unique_name({}, std::string(100, 'x')).size() == kMaxNameLength);
    const std::vector<Entry> long_taken = {{kHash, std::string(kMaxNameLength, 'x'), "2025-03-14T10:00:00Z"}};
    const std::string n = unique_name(long_taken, std::string(100, 'x'));
    CHECK(n.size() == kMaxNameLength);
    CHECK(valid_name(n));
    CHECK(n.substr(n.size() - 2) == "-2");
}

TEST_CASE("tokens: the file lives next to the config") {
#ifdef _WIN32
    revisa::test::EnvGuard appdata("APPDATA");
    appdata.set("C:\\Users\\ana\\AppData\\Roaming");
    CHECK(tokens_path() == "C:\\Users\\ana\\AppData\\Roaming\\revisa\\tokens");
#else
    revisa::test::EnvGuard xdg("XDG_CONFIG_HOME");
    xdg.set("/tmp/xdg");
    CHECK(tokens_path() == "/tmp/xdg/revisa/tokens");
#endif
}
