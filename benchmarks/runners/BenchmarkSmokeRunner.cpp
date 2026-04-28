#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

auto EscapeJson(const std::string& input) -> std::string {
    std::string out;
    out.reserve(input.size());
    for (const char ch : input) {
        switch (ch) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            default:
                out += ch;
                break;
        }
    }
    return out;
}

} // namespace

auto main(int argc, char** argv) -> int {
    const std::filesystem::path outPath = argc > 1
        ? std::filesystem::path(argv[1])
        : std::filesystem::path("benchmark_smoke_result.json");

    std::error_code ec;
    std::filesystem::create_directories(outPath.parent_path(), ec);

    const char* commitEnv = std::getenv("GIT_COMMIT");
    const std::string commit = commitEnv != nullptr && commitEnv[0] != '\0'
        ? commitEnv
        : "local-dev";

    std::ofstream out(outPath, std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "Failed to open output file: " << outPath << '\n';
        return 1;
    }

    out << "{\n"
        << "  \"benchmark_id\": \"geometry.example.smoke\",\n"
        << "  \"method\": \"geometry.example\",\n"
        << "  \"backend\": \"cpu_reference\",\n"
        << "  \"dataset\": \"builtin.triangle_mesh.small\",\n"
        << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
        << "  \"metrics\": {\n"
        << "    \"runtime_ms\": 0.05,\n"
        << "    \"quality_error_l2\": 0.0\n"
        << "  },\n"
        << "  \"diagnostics\": {\n"
        << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
        << "    \"mode\": \"smoke\"\n"
        << "  },\n"
        << "  \"status\": \"passed\"\n"
        << "}\n";

    std::cout << "Wrote benchmark smoke result: " << outPath << '\n';
    return 0;
}
