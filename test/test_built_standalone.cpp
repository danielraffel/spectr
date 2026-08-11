#include <catch2/catch_test_macros.hpp>

#include <pulp/platform/child_process.hpp>
#include <pulp/view/screenshot_compare.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {

class ScopedEnv {
public:
  explicit ScopedEnv(std::string name) : name_(std::move(name)) {
    if (const char *value = std::getenv(name_.c_str()))
      previous_ = value;
  }

  ~ScopedEnv() {
    if (previous_)
      set(*previous_);
    else
      unset();
  }

  void set(const std::string &value) {
#if defined(_WIN32)
    _putenv_s(name_.c_str(), value.c_str());
#else
    ::setenv(name_.c_str(), value.c_str(), 1);
#endif
  }

  void unset() {
#if defined(_WIN32)
    _putenv_s(name_.c_str(), "");
#else
    ::unsetenv(name_.c_str());
#endif
  }

private:
  std::string name_;
  std::optional<std::string> previous_;
};

struct ScratchDir {
  std::filesystem::path path;

  ScratchDir() {
    const auto suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    path = std::filesystem::temp_directory_path() /
           ("spectr-standalone-artifact-" + suffix);
    std::filesystem::create_directories(path);
  }

  ~ScratchDir() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

std::vector<std::uint8_t> read_bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("Built Spectr Standalone renders headlessly without opening audio") {
  namespace fs = std::filesystem;
  const fs::path standalone = SPECTR_TEST_STANDALONE_PATH;
  REQUIRE(fs::exists(standalone));

  ScratchDir scratch;
  const auto screenshot = scratch.path / "spectr.png";
  ScopedEnv headless("PULP_HEADLESS");
  ScopedEnv screenshot_path("PULP_SCREENSHOT");
  ScopedEnv frames("PULP_FRAMES");
  ScopedEnv keep_audio("PULP_SCREENSHOT_KEEP_AUDIO");
  headless.set("1");
  screenshot_path.set(screenshot.string());
  frames.set("120");
  keep_audio.unset();

  pulp::platform::ProcessOptions options;
  options.timeout_ms = 30'000;
  const auto result =
      pulp::platform::ChildProcess::run(standalone.string(), {}, options);
  INFO("stdout=" << result.stdout_output);
  INFO("stderr=" << result.stderr_output);
  REQUIRE_FALSE(result.timed_out);
  REQUIRE(result.exit_code == 0);

  const auto logs = result.stdout_output + result.stderr_output;
  CHECK(logs.find("no audio device created, opened, or started") !=
        std::string::npos);
  REQUIRE(fs::exists(screenshot));
  const auto png = read_bytes(screenshot);
  REQUIRE_FALSE(png.empty());
  const auto content = pulp::view::analyze_screenshot_content(png);
  INFO("content error=" << content.error);
  CHECK(content.width == 2640);
  CHECK(content.height == 1720);
  CHECK(content.passes_content_floor());
}
