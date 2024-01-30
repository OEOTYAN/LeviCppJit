#include "LeviCppJit.h"

#include "compiler/CxxCompileLayer.h"
#include "compiler/ServerSymbolGenerator.h"

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetSelect.h"

#include <ll/api/plugin/NativePlugin.h>
#include <magic_enum.hpp>

namespace lcj {

struct LeviCppJit::Impl {
    CxxCompileLayer cxxCompileLayer;
};

LeviCppJit::LeviCppJit()  = default;
LeviCppJit::~LeviCppJit() = default;

LeviCppJit& LeviCppJit::getInstance() {
    static LeviCppJit instance;
    return instance;
}

ll::plugin::NativePlugin& LeviCppJit::getSelf() const { return *mSelf; }

bool LeviCppJit::load(ll::plugin::NativePlugin& self) {
    mSelf = std::addressof(self);
    getSelf().getLogger().info("loading...");
    mImpl = std::make_unique<Impl>();
    return true;
}
bool LeviCppJit::unload() {
    getSelf().getLogger().info("unloading...");
    mImpl.reset();
    mSelf = nullptr;
    return true;
}

bool LeviCppJit::enable() {
    getSelf().getLogger().info("enabling...");

    auto tmodule = mImpl->cxxCompileLayer.compileRaw(R"(

#define MCAPI  __declspec(dllimport)
#define MCVAPI  __declspec(dllimport)


    // #include <iostream>

#include <cstdio>

#include "ll/api/service/Bedrock.h"

class LevelData {
    public:
    MCAPI std::string const& getLevelName() const;
};

class Level {
    public:

    [[nodiscard]] std::string const& getLevelName() const { return getLevelData().getLevelName(); }

    MCVAPI class LevelData& getLevelData();
    MCVAPI class LevelData const& getLevelData() const;
};

    // #include "ll/api/Logger.h"

    // ll::Logger logger("RuntimeCpp");

    struct A{
        // __declspec(dllimport) int hi(int);
        int hi(int const& a) const {
        // std::cout<<a<<std::endl;
        // logger.info("{}", a);
            return a-3;
        }
    };
    extern "C" {
    int add1(int x) {
        A a{};

        printf("%s\n", 
ll::service::getLevel()->getLevelName().c_str());

        return a.hi(x+4);
    }
    }

    )");

    if (!tmodule.getModuleUnlocked()) {
        getSelf().getLogger().error("compile failed");
        return false;
    }
    // auto& module = *tmodule.getModuleUnlocked();

    // for (auto& func : module.getFunctionList()) {
    //     getSelf().getLogger().info(
    //         "Linkage: {}, DLLStorageClass: {}, Name: {}",
    //         magic_enum::enum_name(func.getLinkage()),
    //         magic_enum::enum_name(func.getDLLStorageClass()),
    //         std::string_view{func.getName()}
    //     );
    // }

    // std::string s;
    // llvm::raw_string_ostream{s} << module;

    // getSelf().getLogger().info(s);

    llvm::ExitOnError ExitOnErr;

    auto J = ExitOnErr(llvm::orc::LLLazyJITBuilder()
                           .setNumCompileThreads(std::thread::hardware_concurrency())
                           .create());

    ExitOnErr(J->addIRModule(std::move(tmodule)));

    J->getMainJITDylib().addGenerator(
        ExitOnErr(llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
            J->getDataLayout().getGlobalPrefix()
        ))
    );
    J->getMainJITDylib().addGenerator(ExitOnErr(llvm::orc::DynamicLibrarySearchGenerator::Load(
        "msvcp140_1.dll",
        J->getDataLayout().getGlobalPrefix()
    )));

    J->getMainJITDylib().addGenerator(std::make_unique<ServerSymbolGenerator>());

    auto Add1Addr = ExitOnErr(J->lookup("add1"));

    getSelf().getLogger().info("{}", Add1Addr.toPtr<int(int)>()(42));

    return true;
}

bool LeviCppJit::disable() {
    getSelf().getLogger().info("disabling...");

    return true;
}

extern "C" {
_declspec(dllexport) bool ll_plugin_load(ll::plugin::NativePlugin& self) {
    return LeviCppJit::getInstance().load(self);
}

_declspec(dllexport) bool ll_plugin_unload(ll::plugin::NativePlugin&) {
    return LeviCppJit::getInstance().unload();
}

_declspec(dllexport) bool ll_plugin_enable(ll::plugin::NativePlugin&) {
    return LeviCppJit::getInstance().enable();
}

_declspec(dllexport) bool ll_plugin_disable(ll::plugin::NativePlugin&) {
    return LeviCppJit::getInstance().disable();
}
}

} // namespace lcj
