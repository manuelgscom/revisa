// Fake curl.exe for the Windows download tests (tests/fetch_test.cpp). Never touches the network.
// Its behaviour comes from environment variables:
//   REVISA_FAKE_CURL_ARGS    if set, file where argv is written, one argument per line
//   REVISA_FAKE_CURL_MODE    copy (default): copies the file REVISA_FAKE_CURL_SRC to the -o path
//                            fail: exits with REVISA_FAKE_CURL_EXIT (default 22) and writes nothing
//                            slow: writes REVISA_FAKE_CURL_CHUNKS bytes 'x' to the -o path, one every
//                                  50 ms, reopening the file each time so its size is visible
// Exits 2 without a -o argument or if the source cannot be read, 23 if the output cannot be written.
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

namespace {

std::string env_or_empty(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr ? v : "";
}

}  // namespace

int main(int argc, char** argv) {
    const std::string args_file = env_or_empty("REVISA_FAKE_CURL_ARGS");
    if (!args_file.empty()) {
        std::ofstream args(args_file, std::ios::binary | std::ios::trunc);
        for (int i = 0; i < argc; ++i) args << argv[i] << '\n';
    }

    const std::string mode = env_or_empty("REVISA_FAKE_CURL_MODE");
    if (mode == "fail") {
        const std::string code = env_or_empty("REVISA_FAKE_CURL_EXIT");
        return code.empty() ? 22 : std::atoi(code.c_str());
    }

    std::string out_path;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "-o") out_path = argv[i + 1];
    if (out_path.empty()) return 2;

    if (mode == "slow") {
        const int chunks = std::atoi(env_or_empty("REVISA_FAKE_CURL_CHUNKS").c_str());
        {
            std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
            if (!out) return 23;
        }
        for (int i = 0; i < chunks; ++i) {
            {
                std::ofstream out(out_path, std::ios::binary | std::ios::app);
                out << 'x';
                if (!out) return 23;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return 0;
    }

    std::ifstream in(env_or_empty("REVISA_FAKE_CURL_SRC"), std::ios::binary);
    if (!in) return 2;
    const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.close();
    return out ? 0 : 23;
}
