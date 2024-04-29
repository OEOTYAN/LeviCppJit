#include "LeviCppJit.h"

#include "lcj/compiler/CxxCompileLayer.h"
#include "lcj/engine/LazyJitEngine.h"
#include "lcj/utils/LogOnError.h"

#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/TargetSelect.h>

#include <ll/api/plugin/NativePlugin.h>
#include <ll/api/plugin/RegisterHelper.h>
#include <ll/api/utils/WinUtils.h>
#include <magic_enum.hpp>

namespace lcj {
void registerTestCommand();
struct LeviCppJit::Impl {
    CxxCompileLayer cxxCompileLayer;
    LazyJitEngine   jitEngine;
};

LeviCppJit::LeviCppJit(ll::plugin::NativePlugin& p) : mSelf(p) {}
LeviCppJit::~LeviCppJit() = default;

static std::unique_ptr<LeviCppJit> instance;

LeviCppJit& LeviCppJit::getInstance() { return *instance; }

ll::plugin::NativePlugin& LeviCppJit::getSelf() const { return mSelf; }

bool LeviCppJit::load() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    mImpl = std::make_unique<Impl>();

    mImpl->cxxCompileLayer.generatePch(
        R"(
#include <__msvc_all_public_headers.hpp>
#define LL_MEMORY_OPERATORS
namespace std
{
    enum class align_val_t : size_t {};
}
#include "ll/api/memory/MemoryOperators.h" // IWYU pragma: keep
    )",
        getDataDir() / u8"pch"
    );

    return true;
}
bool LeviCppJit::unload() {
    llvm::llvm_shutdown_obj s{};
    mImpl.reset();
    return true;
}

std::string LeviCppJit::simpleEval(std::string_view code) {
    auto module = mImpl->cxxCompileLayer.compileRaw(
        std::string(R"(
#line 1
decltype(auto) evalImpl(){
    )")
                .append(code.contains("return ") ? "" : "return ")
                .append(code)
            + R"(;
}
template<auto F>
std::any evalImpl2() {
    if constexpr (std::is_void_v<std::invoke_result_t<decltype(F)>>) {
        F();
        return {};
    } else {
        return F();
    }
}
std::any eval() {
    return evalImpl2<evalImpl>();
}
)",
        "<eval>"
    );
    std::string res;
    if (module) {
        auto lib = mImpl->jitEngine.createDylib("<eval>");
        lib.addModule(std::move(module));
        lib.initialize();
        res = lib.lookup<std::any()>("?eval@@YA?AVany@std@@XZ")().type().name();
        lib.deinitialize();
    }
    return res;
}

bool LeviCppJit::enable() {
    registerTestCommand();
    return true;
}

bool LeviCppJit::disable() { return true; }


} // namespace lcj

LL_REGISTER_PLUGIN(lcj::LeviCppJit, lcj::instance)
