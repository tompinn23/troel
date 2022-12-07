cmake_minimum_required(VERSION 3.20)

project(troel)

add_library(troel src/memory.c src/tr_obj.c src/tr_vm.c src/tr_value.c src/tr_table.c src/tr_lexer.c src/tr_parser.c src/tr_debug.c)

add_executable(troelc src/troelc.c)
target_link_libraries(troelc troel)
