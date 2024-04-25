#include "LeviCppJit.h"

#include "lcj/compiler/CxxCompileLayer.h"
#include "lcj/compiler/ServerSymbolGenerator.h"
#include "lcj/utils/LogOnError.h"
#include "lcj/compiler/CoffHeaderMaterializationUnit.h"

#include <llvm/ExecutionEngine/Orc/EPCEHFrameRegistrar.h>
#include <llvm/ExecutionEngine/Orc/COFFPlatform.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/TargetSelect.h>

#include <ll/api/plugin/NativePlugin.h>
#include <magic_enum.hpp>

#include <ll/api/utils/StacktraceUtils.h>

extern "C" void* __stdcall LoadLibraryA(char const* lpLibFileName);

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

    if (!tmodule) {
        getSelf().getLogger().error("compile failed");
        return false;
    }

    tmodule.withModuleDo([&, this](llvm::Module& module) {
        for (auto node : module.getNamedMetadata("llvm.linker.options")->operands()) {
            if (auto tuple = dyn_cast<llvm::MDTuple>(node)) {
                if (tuple->getNumOperands() == 0) {
                    continue;
                }
                if (auto str = dyn_cast<llvm::MDString>(tuple->getOperand(0).get())) {
                    auto opt = str->getString();
                    if (!opt.consume_front("/DEFAULTLIB:")) {
                        continue;
                    }
                    getSelf().getLogger().info(opt);
                }
            }
        }
    });

    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    auto Builder = llvm::orc::LLLazyJITBuilder();

    Builder.setJITTargetMachineBuilder(CheckExcepted(llvm::orc::JITTargetMachineBuilder::detectHost(
    )));

    Builder.getJITTargetMachineBuilder()
        ->setRelocationModel(llvm::Reloc::PIC_)
        .setCodeModel(llvm::CodeModel::Small);

    auto ES = std::make_unique<llvm::orc::ExecutionSession>(
        CheckExcepted(llvm::orc::SelfExecutorProcessControl::Create())
    );
    ES->setErrorReporter([](llvm::Error err) {
        auto l = ll::Logger::lock();
        LeviCppJit::getInstance().getSelf().getLogger().error(
            "[ExecutionSession] {}",
            llvm::toString(std::move(err))
        );
    });
    Builder.setExecutionSession(std::move(ES));

    auto J = CheckExcepted(
        Builder
            .setNumCompileThreads(std::thread::hardware_concurrency())
            // .setPlatformSetUp(llvm::orc::ExecutorNativePlatform{ll::string_utils::wstr2str(
            //     (getSelf().getDataDir() / u8R"(library\orc\orc_rt-x86_64.lib)").wstring()
            // )})
            .setObjectLinkingLayerCreator([](llvm::orc::ExecutionSession& ES, llvm::Triple const&) {
                auto L = std::make_unique<llvm::orc::ObjectLinkingLayer>(ES);

                L->addPlugin(std::make_unique<llvm::orc::EHFrameRegistrationPlugin>(
                    ES,
                    CheckExcepted(llvm::orc::EPCEHFrameRegistrar::Create(ES))
                ));
                return L;
            })
            .create()
    );

    auto imageBaseSymbol = J->getExecutionSession().intern("__ImageBase");

    CheckExcepted(J->addIRModule(std::move(tmodule)));

    CheckExcepted(J->getMainJITDylib().define(
        std::make_unique<COFFHeaderMaterializationUnit>(*J, imageBaseSymbol)
    ));

    for (auto& str : std::vector<std::string>{
             //  (getSelf().getDataDir() / u8R"(library\msvc\vcruntime.lib)").string(),
             //  (getSelf().getDataDir() / u8R"(library\msvc\msvcrt.lib)").string(),
             (getSelf().getDataDir() / u8R"(library\ucrt\ucrt.lib)").string(),
             (getSelf().getDataDir() / u8R"(library\msvc\msvcprt.lib)").string(),
         }) {
        auto libSearcher = CheckExcepted(
            llvm::orc::StaticLibraryDefinitionGenerator::Load(J->getObjLinkingLayer(), str.c_str())
        );

        for (auto& dll : libSearcher->getImportedDynamicLibraries()) {
            getSelf().getLogger().info("dll: {}", dll);
            LoadLibraryA(dll.c_str());
        }

        J->getMainJITDylib().addGenerator(std::move(libSearcher));
    }

    J->getMainJITDylib().addGenerator(std::make_unique<ServerSymbolGenerator>());

    J->getMainJITDylib().addGenerator(llvm::orc::DLLImportDefinitionGenerator::Create(
        J->getExecutionSession(),
        cast<llvm::orc::ObjectLinkingLayer>(J->getObjLinkingLayer())
    ));

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
