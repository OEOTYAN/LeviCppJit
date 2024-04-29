#include "lcj/engine/LazyJitEngine.h"
#include "lcj/core/LeviCppJit.h"
#include "lcj/engine/ServerSymbolGenerator.h"
#include "lcj/utils/LogOnError.h"

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Host.h>

#include <ll/api/base/MsvcPredefine.h>

namespace lcj {

// __declspec(noreturn
// ) extern "C" void __stdcall CxxThrowException(void* pExceptionObject, _ThrowInfo* pThrowInfo) {
//     LeviCppJit::getInstance()
//         .getLogger()
//         .warn("Exception thrown {} {} {}", _ReturnAddress(), pExceptionObject,
//         (void*)pThrowInfo);
//     // _CxxThrowException(pExceptionObject, pThrowInfo);
//     throw std::runtime_error{"hh"};
// }

unsigned char _subborrow_u64(
    unsigned char     Borrow,
    unsigned __int64  Source1,
    unsigned __int64  Source2,
    unsigned __int64* Destination
) {
    unsigned __int64 Diff        = Source1 - Source2 - (Borrow != 0);
    unsigned __int64 CarryVector = (Diff & Source2) ^ ((Diff ^ Source2) & ~Source1);
    *Destination                 = Diff;
    return CarryVector >> 63;
}
unsigned char _addcarry_u64(
    unsigned char     Carry,
    unsigned __int64  Source1,
    unsigned __int64  Source2,
    unsigned __int64* Destination
) {
    unsigned __int64 Sum         = (Carry != 0) + Source1 + Source2;
    unsigned __int64 CarryVector = (Source1 & Source2) ^ ((Source1 ^ Source2) & ~Sum);
    *Destination                 = Sum;
    return CarryVector >> 63;
}

struct LazyJitEngine::Impl {
    std::unique_ptr<llvm::orc::LLLazyJIT> JitEngine;
};

LazyJitEngine::LazyJitEngine() : impl(std::make_unique<Impl>()) {
    auto ES = std::make_unique<llvm::orc::ExecutionSession>(
        CheckExcepted(llvm::orc::SelfExecutorProcessControl::Create())
    );
    ES->setErrorReporter([](llvm::Error err) {
        auto l = ll::Logger::lock();
        LeviCppJit::getInstance().getLogger().error(
            "[ExecutionSession] {}",
            llvm::toString(std::move(err))
        );
    });
    auto machineBuilder = CheckExcepted(llvm::orc::JITTargetMachineBuilder::detectHost());

    auto& targetOptions               = machineBuilder.getOptions();
    targetOptions.EmulatedTLS         = true;
    targetOptions.ExplicitEmulatedTLS = true;
    targetOptions.ExceptionModel      = llvm::ExceptionHandling::WinEH;

    impl->JitEngine = CheckExcepted(
        llvm::orc::LLLazyJITBuilder{}
            .setJITTargetMachineBuilder(std::move(machineBuilder))
            .setExecutionSession(std::move(ES))
            //   .setObjectLinkingLayerCreator([&](llvm::orc::ExecutionSession& ES,
            //                                     const llvm::Triple&          TT) {
            //       return std::make_unique<llvm::orc::ObjectLinkingLayer>(
            //           ES,
            //           CheckExcepted(llvm::jitlink::InProcessMemoryManager::Create())
            //       );
            //   })
            .setNumCompileThreads(std::thread::hardware_concurrency())
            .create()
    );
}
LazyJitEngine::~LazyJitEngine() = default;

struct Dylib::Impl {
    llvm::orc::JITDylib& lib;
    llvm::orc::LLJIT&    jit;
};
Dylib LazyJitEngine::createDylib(std::string_view name) {
    Dylib res{CheckExcepted(impl->JitEngine->createJITDylib(std::string{name})), *impl->JitEngine};
    return res;
}
Dylib::Dylib(llvm::orc::JITDylib& lib, llvm::orc::LLJIT& jit)
: impl(std::make_unique<Impl>(lib, jit)) {

    auto& es = impl->jit.getExecutionSession();

    // CheckExcepted(lib.define(llvm::orc::absoluteSymbols({
    //     {es.intern("_CxxThrowException"),
    //      llvm::JITEvaluatedSymbol::fromPointer(
    //          CxxThrowException, llvm::JITSymbolFlags::Exported | llvm::JITSymbolFlags::Callable
    //      )}
    // })));

    CheckExcepted(lib.define(llvm::orc::absoluteSymbols({
        {es.intern("__orc_rt_jit_dispatch"),
         {es.getExecutorProcessControl().getJITDispatchInfo().JITDispatchFunction.getValue(),
          llvm::JITSymbolFlags::Exported | llvm::JITSymbolFlags::Callable}},
        {es.intern("__orc_rt_jit_dispatch_ctx"),
         {es.getExecutorProcessControl().getJITDispatchInfo().JITDispatchContext.getValue(),
          llvm::JITSymbolFlags::Exported}},
        {es.intern("__ImageBase"),
         llvm::JITEvaluatedSymbol::fromPointer(
             &ll::win_utils::__ImageBase,
         llvm::JITSymbolFlags::Exported
         )},
        {es.intern("__lljit.platform_support_instance"),
         llvm::JITEvaluatedSymbol::fromPointer(
             jit.getPlatformSupport(),
         llvm::JITSymbolFlags::Exported
         )},
        {es.intern("_subborrow_u64"),
         llvm::JITEvaluatedSymbol::fromPointer(_subborrow_u64, llvm::JITSymbolFlags::Exported)},
        {es.intern("_addcarry_u64"),
         llvm::JITEvaluatedSymbol::fromPointer(_addcarry_u64, llvm::JITSymbolFlags::Exported)}
    })));
    for (auto& str : std::vector<std::string>{
             (LeviCppJit::getInstance().getDataDir() / u8R"(library\msvc\vcruntime.lib)").string(),
             (LeviCppJit::getInstance().getDataDir() / u8R"(library\ucrt\ucrt.lib)").string(),
             (LeviCppJit::getInstance().getDataDir() / u8R"(library\msvc\msvcprt.lib)").string(),
             (LeviCppJit::getInstance().getDataDir() / u8R"(library\um\Kernel32.lib)").string(),
             (LeviCppJit::getInstance().getDataDir() / u8R"(library\ll\LeviLamina.lib)").string(),
             (LeviCppJit::getInstance().getDataDir()
              / u8R"(library\msvc\clang_rt.builtins-x86_64.lib)")
                 .string()
         }) {
        auto libSearcher = CheckExcepted(
            llvm::orc::StaticLibraryDefinitionGenerator::Load(jit.getObjLinkingLayer(), str.c_str())
        );
        for (auto& dll : libSearcher->getImportedDynamicLibraries()) {
            // LeviCppJit::getInstance().getLogger().info("dll: {}", dll);
            lib.addGenerator(CheckExcepted(llvm::orc::DynamicLibrarySearchGenerator::Load(
                dll.c_str(),
                jit.getDataLayout().getGlobalPrefix()
            )));
        }
        lib.addGenerator(std::move(libSearcher));
    }
    // lib.addGenerator(CheckExcepted(llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
    //     jit.getDataLayout().getGlobalPrefix()
    // )));
    lib.addGenerator(std::make_unique<ServerSymbolGenerator>());

    // lib.addGenerator(llvm::orc::DLLImportDefinitionGenerator::Create(
    //     es,
    //     cast<llvm::orc::ObjectLinkingLayer>(jit.getObjLinkingLayer())
    // ));
}

Dylib::~Dylib() { CheckExcepted(impl->jit.getExecutionSession().removeJITDylib(impl->lib)); }
Dylib::Dylib(Dylib&&) noexcept            = default;
Dylib& Dylib::operator=(Dylib&&) noexcept = default;

void Dylib::initialize() { CheckExcepted(impl->jit.initialize(impl->lib)); }
void Dylib::deinitialize() { CheckExcepted(impl->jit.deinitialize(impl->lib)); }

void Dylib::addModule(llvm::orc::ThreadSafeModule&& module) {
    // tmodule.withModuleDo([&, this](llvm::Module& module) {
    //     for (auto node : module.getNamedMetadata("llvm.linker.options")->operands()) {
    //         if (auto tuple = dyn_cast<llvm::MDTuple>(node)) {
    //             if (tuple->getNumOperands() == 0) {
    //                 continue;
    //             }
    //             if (auto str = dyn_cast<llvm::MDString>(tuple->getOperand(0).get())) {
    //                 auto opt = str->getString();
    //                 if (!opt.consume_front("/DEFAULTLIB:")) {
    //                     continue;
    //                 }
    //                 getLogger().info(opt);
    //             }
    //         }
    //     }
    // });
    CheckExcepted(impl->jit.addIRModule(impl->lib, std::move(module)));
}
void* Dylib::lookupImpl(std::string_view name) {
    return CheckExcepted(impl->jit.lookup(impl->lib, name)).toPtr<void*>();
}
} // namespace lcj
