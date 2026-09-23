# Turn a binary file into a C array: cmake -DIN=file -DOUT=file.c -DNAME=symbol -P embed.cmake
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" n)
math(EXPR len "${n} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," arr "${hex}")
string(REGEX REPLACE "((0x..,){32})" "\\1\n" arr "${arr}")
file(WRITE "${OUT}" "/* generated from ${IN} */\nconst unsigned char ${NAME}[] = {\n${arr}\n};\nconst unsigned int ${NAME}_len = ${len};\n")
