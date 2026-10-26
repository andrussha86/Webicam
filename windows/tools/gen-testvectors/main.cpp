// Writes the shared protocol test vectors (shared/testvectors/*.bin).
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "webicam/testvectors.h"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: gen-testvectors <output-dir>\n");
    return 2;
  }
  const std::filesystem::path dir = argv[1];
  std::filesystem::create_directories(dir);
  for (const auto& vector : webicam::proto::TestVectors()) {
    const auto bytes = webicam::proto::Encode(vector.message);
    std::ofstream file(dir / vector.file, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    std::printf("%s:", vector.file);
    for (auto b : bytes) std::printf(" %02X", b);
    std::printf("\n");
  }
  return 0;
}
