#include <cstdlib>

consteval int CppStandardProbe() {
  static_assert(__cplusplus > 202002L);
  return 23;
}

int main() {
  static_assert(CppStandardProbe() == 23);
  return EXIT_SUCCESS;
}
