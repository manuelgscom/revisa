# Embeds one data file as a C++ byte array (catalog data, see src/core/catalog/catalog.hpp).
#   cmake -DIN=<file> -DOUT=<file.cpp> -DNAME=<symbol> -P cmake/embed.cmake
# Defines revisa::catalog::detail::<NAME> (bytes, plus a trailing 0) and <NAME>Size.
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" hex_len)
math(EXPR size "${hex_len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," body "${hex}")
# A line break every 32 bytes keeps the generated file readable for compilers and editors.
string(REGEX REPLACE "((0x..,){32})" "\\1\n" body "${body}")
file(WRITE "${OUT}.tmp"
  "// Generated from ${IN} by cmake/embed.cmake. Do not edit.\n"
  "#include <cstddef>\n"
  "namespace revisa::catalog::detail {\n"
  "extern const unsigned char ${NAME}[] = {\n${body}0};\n"
  "extern const std::size_t ${NAME}Size = ${size};\n"
  "}  // namespace revisa::catalog::detail\n")
file(RENAME "${OUT}.tmp" "${OUT}")
