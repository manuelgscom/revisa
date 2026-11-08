#include "doctest.h"

#include "core/fetch/sha256.hpp"
#include "test_util.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using revisa::fetch::Sha256;
using revisa::fetch::sha256_file;
using revisa::fetch::sha256_hex;

TEST_CASE("sha256: FIPS 180-4 vectors") {
    CHECK(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(sha256_hex("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqr"
                     "lmnopqrsmnopqrstnopqrstu") ==
          "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

TEST_CASE("sha256: one million 'a' via incremental updates of odd sizes") {
    const std::string chunk(997, 'a');
    Sha256 h;
    std::size_t left = 1000000;
    std::size_t step = 1;
    while (left > 0) {
        const std::size_t n = std::min({left, step, chunk.size()});
        h.update(chunk.data(), n);
        left -= n;
        step = step * 3 + 1;
        if (step > chunk.size()) step = 1;
    }
    CHECK(h.final() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("sha256_file: temp file and missing file") {
    const revisa::test::TempDir tmp("revisa_sha256_test_");
    const std::string dir = tmp.path;
    const std::string file = dir + "/f.bin";

    // Larger than the 1 MiB read chunk.
    std::string data((1u << 20) + 123, '\0');
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<char>(i * 31 + 7);
    {
        std::ofstream out(file, std::ios::binary);
        out << data;
    }
    CHECK(sha256_file(file) == sha256_hex(data));
    CHECK_FALSE(sha256_file(dir + "/missing.bin").has_value());
}
