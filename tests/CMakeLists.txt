find_package(Qt6 REQUIRED COMPONENTS Test)

# Each tst_*.cpp is a standalone Qt Test executable registered with ctest.
function(locallens_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE locallens_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

locallens_add_test(tst_tokenizer)
locallens_add_test(tst_textextractor)
