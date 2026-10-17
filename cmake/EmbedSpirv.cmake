# Converte um .spv em lista de inicialização C: {0x07230203,...}
# Uso: cmake -DIN=arquivo.spv -DOUT=arquivo.inc -P EmbedSpirv.cmake
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" len)
math(EXPR rem "${len} % 8")
if(NOT rem EQUAL 0)
  message(FATAL_ERROR "SPIR-V inválido (tamanho não múltiplo de 4): ${IN}")
endif()
# Palavras little-endian: bytes b0 b1 b2 b3 -> 0xb3b2b1b0
string(REGEX REPLACE "([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])" "0x\\4\\3\\2\\1," words "${hex}")
file(WRITE "${OUT}" "{${words}}\n")
