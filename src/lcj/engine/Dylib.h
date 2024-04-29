#pragma once

#include <memory>
#include <string_view>

#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>

namespace llvm::orc {
class JITDylib;
class LLJIT;
}; // namespace llvm::orc

namespace lcj {
class Dylib {

    struct Impl;
    std::unique_ptr<Impl> impl;

    void* lookupImpl(std::string_view name);

public:
    Dylib(llvm::orc::JITDylib& lib, llvm::orc::LLJIT& jit);

    Dylib(Dylib&&) noexcept;
    Dylib& operator=(Dylib&&) noexcept;

    Dylib(Dylib const&)            = delete;
    Dylib& operator=(Dylib const&) = delete;

    ~Dylib();
    void initialize();
    void deinitialize();

    void addModule(llvm::orc::ThreadSafeModule&& module);

    template <class T>
    T* lookup(std::string_view name) {
        return reinterpret_cast<T*>(lookupImpl(name));
    }
};
} // namespace lcj
