#include "LeviCppJit.h"

#include "lcj/compiler/CxxCompileLayer.h"
#include "lcj/compiler/ServerSymbolGenerator.h"
#include "lcj/utils/LogOnError.h"

#include <llvm/ExecutionEngine/JITLink/EHFrameSupport.h>

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/TargetSelect.h>

#include <ll/api/plugin/NativePlugin.h>
#include <ll/api/plugin/RegisterHelper.h>
#include <ll/api/utils/WinUtils.h>
#include <magic_enum.hpp>

extern "C" void* __stdcall LoadLibraryA(char const* lpLibFileName);

namespace lcj {

struct LeviCppJit::Impl {
    CxxCompileLayer                       cxxCompileLayer;
    std::unique_ptr<llvm::orc::LLLazyJIT> JitEngine;
    Impl() {
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
        auto machineBuilder = CheckExcepted(llvm::orc::JITTargetMachineBuilder::detectHost());

        auto& targetOptions          = machineBuilder.getOptions();
        targetOptions.EmulatedTLS    = true;
        targetOptions.ExceptionModel = llvm::ExceptionHandling::WinEH;

        JitEngine = CheckExcepted(llvm::orc::LLLazyJITBuilder{}
                                      .setJITTargetMachineBuilder(std::move(machineBuilder))
                                      .setExecutionSession(std::move(ES))
                                      .setNumCompileThreads(std::thread::hardware_concurrency())
                                      .create());
    }
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
    return true;
}
bool LeviCppJit::unload() {
    mImpl.reset();
    return true;
}

bool LeviCppJit::enable() {
    auto tmodule = mImpl->cxxCompileLayer.compileRaw(R"(

#define MCAPI  __declspec(dllimport)
#define MCVAPI  __declspec(dllimport)

#pragma comment(lib, "version.lib")

    #include <iostream>
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
        std::cout<<a<<std::endl;
        // logger.info("{}", a);
            return a-3;
        }
        A(){
        std::cout<<"hello A"<<std::endl;
        }
        ~A(){
        std::cout<<"bye A"<<std::endl;
        }
    };
     static   A b{};
    extern "C" {
    int add1(int x) {

        printf("%s\n", 
ll::service::getLevel()->getLevelName().c_str());

     static   A a{};
// try{
//         throw std::runtime_error{"hi"};
// }catch(...){
//         std::cout<<"catched"<<std::endl;
// }
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
    auto& lib = CheckExcepted(mImpl->JitEngine->getExecutionSession().createJITDylib("plugin1"));

    CheckExcepted(mImpl->JitEngine->addIRModule(lib, std::move(tmodule)));

    auto& es = mImpl->JitEngine->getExecutionSession();

    CheckExcepted(lib.define(llvm::orc::absoluteSymbols({
  // {es.intern("__orc_rt_jit_dispatch"),
  //  {es.getExecutorProcessControl().getJITDispatchInfo().JITDispatchFunction,
  //   llvm::JITSymbolFlags::Callable}},
  // {es.intern("__orc_rt_jit_dispatch_ctx"),
  //  {es.getExecutorProcessControl().getJITDispatchInfo().JITDispatchContext,
  //   llvm::JITSymbolFlags::Callable}},
  // {es.intern("__ImageBase"),
  //  {llvm::orc::ExecutorAddr::fromPtr(&ll::win_utils::__ImageBase),
  //   llvm::JITSymbolFlags::Exported}},
        {es.intern("__lljit.platform_support_instance"),
         {llvm::orc::ExecutorAddr::fromPtr(mImpl->JitEngine->getPlatformSupport()),
          llvm::JITSymbolFlags::Exported}}
    })));

    for (auto& str : std::vector<std::string>{
             (getSelf().getDataDir() / u8R"(library\msvc\vcruntime.lib)").string(),
             (getSelf().getDataDir() / u8R"(library\msvc\msvcrt.lib)").string(),
             (getSelf().getDataDir() / u8R"(library\ucrt\ucrt.lib)").string(),
             (getSelf().getDataDir() / u8R"(library\msvc\msvcprt.lib)").string(),
             (getSelf().getDataDir() / u8R"(library\um\Kernel32.lib)").string(),
             (getSelf().getDataDir() / u8R"(library\ll\LeviLamina.lib)").string(),
             (getSelf().getDataDir() / u8R"(library\msvc\clang_rt.builtins-x86_64.lib)").string()
         }) {
        auto libSearcher = CheckExcepted(llvm::orc::StaticLibraryDefinitionGenerator::Load(
            mImpl->JitEngine->getObjLinkingLayer(),
            str.c_str()
        ));

        for (auto& dll : libSearcher->getImportedDynamicLibraries()) {
            getSelf().getLogger().info("dll: {}", dll);

            lib.addGenerator(CheckExcepted(llvm::orc::DynamicLibrarySearchGenerator::Load(
                dll.c_str(),
                mImpl->JitEngine->getDataLayout().getGlobalPrefix()
            )));
            // LoadLibraryA(dll.c_str());
        }

        lib.addGenerator(std::move(libSearcher));
    }
    // lib.addGenerator(CheckExcepted(llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
    //     mImpl->JitEngine->getDataLayout().getGlobalPrefix()
    // )));
    lib.addGenerator(std::make_unique<ServerSymbolGenerator>());

    // lib.addGenerator(llvm::orc::DLLImportDefinitionGenerator::Create(
    //     es,
    //     cast<llvm::orc::ObjectLinkingLayer>(mImpl->JitEngine->getObjLinkingLayer())
    // ));
    CheckExcepted(mImpl->JitEngine->initialize(lib));

    auto Add1Addr = CheckExcepted(mImpl->JitEngine->lookup(lib, "add1"));

    getSelf().getLogger().info("{}", Add1Addr.toPtr<int(int)>()(42));


    CheckExcepted(mImpl->JitEngine->deinitialize(lib));
    CheckExcepted(es.removeJITDylib(lib));

    return true;
}

bool LeviCppJit::disable() { return true; }


} // namespace lcj

LL_REGISTER_PLUGIN(lcj::LeviCppJit, lcj::instance)
