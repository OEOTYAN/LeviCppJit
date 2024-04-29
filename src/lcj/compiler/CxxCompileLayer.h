#pragma once

#include <memory>
#include <string_view>
#include <filesystem>

#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>

namespace lcj {
class CxxCompileLayer {
    struct Impl;
    std::unique_ptr<Impl> impl;

public:
    CxxCompileLayer();
    ~CxxCompileLayer();

    llvm::orc::ThreadSafeModule compileRaw(std::string_view code, std::string_view name = "main");

    void generatePch(std::string_view code, std::filesystem::path const& outFile);
};
} // namespace lcj
