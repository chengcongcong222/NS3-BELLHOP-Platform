#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include "internal/woss_cache_loader.hpp"

using namespace ns3_factory;

namespace {

auto Check(bool condition) -> void {
  if(!condition) {
    std::abort();
  }
}

[[nodiscard]] auto Quote(std::string_view argument) -> std::string {
  Check(argument.find('"') == std::string_view::npos);
  return '"' + std::string{argument} + '"';
}

class TemporaryAssetRoot final {
 public:
  TemporaryAssetRoot() {
    const auto suffix = std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count();
    path_ = std::filesystem::temp_directory_path() /
            ("platform-raw-environment-import-" +
             std::to_string(suffix));
    Check(std::filesystem::create_directories(path_));
  }

  ~TemporaryAssetRoot() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  [[nodiscard]] auto path() const noexcept
      -> const std::filesystem::path& {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

}  // namespace

int main(int argc, char** argv) {
  Check(argc == 6);
  TemporaryAssetRoot output;
  const auto command_body =
      Quote(argv[1]) + " " + Quote(argv[2]) +
      " --woa-temperature " + Quote(argv[3]) +
      " --woa-salinity " + Quote(argv[4]) +
      " --latitude 18 --longitude 130 --depth-limit-m 500" +
      " --bearing-deg 90 --range-max-m 1000 --sample-count 3" +
      " --asset-id raw-import-fixture" +
      " --asset-name " + Quote("raw import fixture") +
      " --time-label " + Quote("WOA23 month-04 climatology") +
      " --woa-dataset 2023-month-04-1deg" +
      " --gebco-dataset 2020-recorded-response" +
      " --gebco-response-file " + Quote(argv[5]) +
      " --output-root " + Quote(output.path().string());
#ifdef _WIN32
  // cmd.exe requires an additional outer quote when the command begins with
  // a quoted executable path. POSIX shells do not.
  const auto command = '"' + command_body + '"';
#else
  const auto& command = command_body;
#endif
  Check(std::system(command.c_str()) == 0);

  const auto manifest = output.path() / "data" / "woss_sources" /
                        "raw-import-fixture.json";
  const auto loaded =
      environment::WossCacheLoader::Load(manifest, output.path());
  Check(loaded.has_value());
  Check(loaded->manifest.source_id == "raw-import-fixture");
  Check(loaded->manifest.mode == "raw-import");
  Check(loaded->manifest.source_kind ==
        "woa23-gebco2020-raw-import");
  Check(loaded->manifest.datasets.at("woa") ==
        "2023-month-04-1deg");
  Check(loaded->manifest.datasets.at("gebco") ==
        "2020-recorded-response");
  Check(loaded->sound_speed_profile.samples().size() >= 2U);
  Check(loaded->bathymetry_profile.ranges_meters().size() == 3U);
  Check(loaded->bathymetry_profile.depths_meters()[0] == 200.0);
  Check(loaded->bathymetry_profile.depths_meters()[1] == 220.0);
  Check(loaded->bathymetry_profile.depths_meters()[2] == 240.0);
  Check(loaded->provenance.content_digest.starts_with("fnv1a64:"));
  return EXIT_SUCCESS;
}
