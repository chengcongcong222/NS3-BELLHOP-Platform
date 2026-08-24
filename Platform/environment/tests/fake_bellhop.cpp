#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
  if(argc != 2) {
    return 2;
  }
  const std::string case_name{argv[1]};
  if(case_name.starts_with("timeout")) {
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
  }
  const auto current = std::filesystem::current_path();
  for(const auto* extension : {".env", ".bty", ".ati"}) {
    if(!std::filesystem::is_regular_file(
           current / (case_name + extension))) {
      return 3;
    }
  }
  std::ofstream arrivals{current / (case_name + ".arr"),
                         std::ios::binary | std::ios::trunc};
  arrivals << "'2D'\n"
              "12000\n"
              "1 30\n"
              "1 10\n"
              "1 0\n"
              "1\n"
              "1\n"
              "0.5 0 0.1 0 -1 2 0 0\n";
  return arrivals ? EXIT_SUCCESS : 4;
}
