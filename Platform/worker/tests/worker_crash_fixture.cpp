#include <csignal>

auto main() -> int {
  (void)::raise(SIGKILL);
  return 127;
}
