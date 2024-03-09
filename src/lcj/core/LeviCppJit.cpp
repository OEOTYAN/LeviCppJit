#include "LeviCppJit.h"

#include "lcj/compiler/CxxCompileLayer.h"
#include "lcj/compiler/ServerSymbolGenerator.h"
#include "lcj/utils/LogOnError.h"

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetSelect.h>


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

#pragma comment(lib, "version.lib")

    // #include <iostream>
// #include "mc/world/level/storage/LevelData.h"
// #include "mc/world/level/Level.h"

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
    auto& module = *tmodule.getModuleUnlocked();

    for (auto node : module.getOrInsertNamedMetadata("llvm.linker.options")->operands()) {
        if (node->getMetadataID() != llvm::Metadata::MDTupleKind) {
            continue;
        }
        auto tuple = cast<llvm::MDTuple>(node);
        if (tuple->getNumOperands() == 0) {
            continue;
        }
        auto data = tuple->getOperand(0).get();
        if (data->getMetadataID() != llvm::Metadata::MDStringKind) {
            continue;
        }
        auto opt = cast<llvm::MDString>(data)->getString();
        if (!opt.consume_front("/DEFAULTLIB:")) {
            continue;
        }
        getSelf().getLogger().info(opt);
    }
    for (auto node : module.getOrInsertNamedMetadata("llvm.linker.options")->operands()) {
        node->dumpTree();
    }

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

    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    auto J = CheckExcepted(llvm::orc::LLLazyJITBuilder()
                               .setNumCompileThreads(std::thread::hardware_concurrency())
                               .create());

    CheckExcepted(J->addIRModule(std::move(tmodule)));

    J->getMainJITDylib().addGenerator(std::make_unique<ServerSymbolGenerator>());

    J->getMainJITDylib().addGenerator(
        CheckExcepted(llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
            J->getDataLayout().getGlobalPrefix()
        ))
    );
    for (auto& lb :
         {"vcruntime140_1.dll",
          "vcruntime140.dll",
          "msvcp140.dll",
          "msvcp140_codecvt_ids.dll",
          "msvcp140_atomic_wait.dll",
          "msvcp140_2.dll",
          "msvcp140_codecvt_ids.dll",
          "msvcp140_1.dll",
          "concrt140.dll",
          "ucrtbase.dll"}) {
        J->getMainJITDylib().addGenerator(CheckExcepted(
            llvm::orc::DynamicLibrarySearchGenerator::Load(lb, J->getDataLayout().getGlobalPrefix())
        ));
    }

    auto Add1Addr = CheckExcepted(J->lookup("add1"));

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
