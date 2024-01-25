#pragma once

#include <memory>
#include <string_view>

#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>

namespace llvm::orc {
class ThreadSafeModule;
}

namespace lcj {
class CxxCompileLayer {
    struct Impl;
    std::unique_ptr<Impl> impl;

public:
    CxxCompileLayer();
    ~CxxCompileLayer();

    llvm::orc::ThreadSafeModule compileRaw(std::string_view code);
};
} // namespace lcj
