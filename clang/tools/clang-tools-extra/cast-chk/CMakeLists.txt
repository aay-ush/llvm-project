set(LLVM_LINK_COMPONENTS support)

add_clang_executable(cast-chk
    CastChk.cpp
)

target_link_libraries(cast-chk
    PRIVATE
    clangAST
    clangASTMatchers
    clangBasic
    clangFrontend
    clangSerialization
    clangTooling
)

