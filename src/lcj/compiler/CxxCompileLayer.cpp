#include "CxxCompileLayer.h"

#include "lcj/compiler/DiagnosticLogger.h"
#include "lcj/core/LeviCppJit.h"

#include <clang/Basic/DiagnosticLex.h>
#include <clang/Basic/DiagnosticSema.h>
#include <clang/Basic/SourceManager.h>
#include <clang/CodeGen/CodeGenAction.h>
#include <clang/CodeGen/ModuleBuilder.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetSelect.h>

namespace lcj {
struct CxxCompileLayer::Impl {
    std::unique_ptr<clang::CompilerInstance>                compilerInstance;
    llvm::IntrusiveRefCntPtr<clang::DiagnosticsEngine>      diagnosticsEngine;
    llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> inMemoryFileSystem;
};

CxxCompileLayer::CxxCompileLayer() : impl(std::make_unique<Impl>()) {
    impl->compilerInstance  = std::make_unique<clang::CompilerInstance>();
    impl->diagnosticsEngine = std::make_unique<clang::DiagnosticsEngine>(
        std::make_unique<clang::DiagnosticIDs>(),
        std::make_unique<clang::DiagnosticOptions>(),
        new DiagnosticLogger{}
    );
    impl->compilerInstance->setDiagnostics(impl->diagnosticsEngine.get());

    auto overlay = std::make_unique<llvm::vfs::OverlayFileSystem>(llvm::vfs::getRealFileSystem());

    impl->inMemoryFileSystem = std::make_unique<llvm::vfs::InMemoryFileSystem>();

    overlay->pushOverlay(impl->inMemoryFileSystem);

    impl->compilerInstance->createSourceManager(
        *impl->compilerInstance->createFileManager(std::move(overlay))
    );
    auto& compilerInvocation = impl->compilerInstance->getInvocation();

    compilerInvocation.getTargetOpts().Triple = llvm::sys::getProcessTriple();

    auto& codeGenOpts           = compilerInvocation.getCodeGenOpts();
    codeGenOpts.CodeModel       = "default";
    codeGenOpts.RelocationModel = llvm::Reloc::PIC_;
    codeGenOpts.EmulatedTLS     = true;

    auto& frontendOpts = compilerInvocation.getFrontendOpts();

    frontendOpts.ProgramAction = clang::frontend::EmitLLVMOnly;

    auto& langOpts = *compilerInvocation.getLangOpts();

    langOpts.LineComment                = true;
    langOpts.Optimize                   = true;
    langOpts.HexFloats                  = true;
    langOpts.CPlusPlus                  = true;
    langOpts.CPlusPlus11                = true;
    langOpts.CPlusPlus14                = true;
    langOpts.CPlusPlus17                = true;
    langOpts.CPlusPlus20                = true;
    langOpts.CPlusPlus2b                = true;
    langOpts.EncodeCXXClassTemplateSpec = true;
    langOpts.CXXExceptions              = true;
    // langOpts.EHAsynch                = true;
    langOpts.Digraphs                      = true;
    langOpts.CXXOperatorNames              = true;
    langOpts.Bool                          = true;
    langOpts.WChar                         = true;
    langOpts.Char8                         = true;
    langOpts.Coroutines                    = true;
    langOpts.CoroAlignedAllocation         = true;
    langOpts.DllExportInlines              = true;
    langOpts.RelaxedTemplateTemplateArgs   = true;
    langOpts.ExperimentalLibrary           = true;
    langOpts.DoubleSquareBracketAttributes = true;
    langOpts.CPlusPlusModules              = true;
    langOpts.EmitAllDecls                  = true;
    langOpts.MSVCCompat                    = true;
    langOpts.MicrosoftExt                  = true;
    langOpts.AsmBlocks                     = true;
    langOpts.DeclSpecKeyword               = true;
    langOpts.MSBitfields                   = true;
    langOpts.MSVolatile                    = true;
    langOpts.GNUMode                       = false;
    langOpts.GNUKeywords                   = false;
    langOpts.GNUAsm                        = false;

    langOpts.MSCompatibilityVersion = 193833130;

    impl->diagnosticsEngine->setSeverity(
        clang::diag::warn_unhandled_ms_attribute_ignored,
        clang::diag::Severity::Ignored,
        {}
    );
    impl->diagnosticsEngine->setSeverity(
        clang::diag::warn_pragma_diagnostic_unknown_warning,
        clang::diag::Severity::Ignored,
        {}
    );

    auto& preprocessorOpts = compilerInvocation.getPreprocessorOpts();

    preprocessorOpts.addMacroDef("_AMD64_");
    preprocessorOpts.addMacroDef("_CRT_SECURE_NO_WARNINGS");
    preprocessorOpts.addMacroDef("NOMINMAX");
    preprocessorOpts.addMacroDef("UNICODE");
    preprocessorOpts.addMacroDef("WIN32_LEAN_AND_MEAN");
    preprocessorOpts.addMacroDef("ENTT_PACKED_PAGE=128");
    preprocessorOpts.addMacroDef("_HAS_CXX23=1");
    preprocessorOpts.addMacroDef("LL_EXPORT");
    preprocessorOpts.addMacroDef("_MT");
    preprocessorOpts.addMacroDef("_DLL");

    auto& headerSearchOpts = compilerInvocation.getHeaderSearchOpts();

    for (auto& header : std::filesystem::directory_iterator(
             LeviCppJit::getInstance().getSelf().getDataDir() / u8"header"
         )) {
        headerSearchOpts.AddPath(
            header.path().string(),
            clang::frontend::IncludeDirGroup::System,
            false,
            false
        );
    }
}
CxxCompileLayer::~CxxCompileLayer() = default;


llvm::orc::ThreadSafeModule
CxxCompileLayer::compileRaw(std::string_view code, std::string_view name) {
    // EmulatedTLS
    std::string codeBuffer{R"(
extern "C" {
void                 Sleep(unsigned long dwMilliseconds);
inline unsigned int           _tls_index         = 0;
inline int                    _Init_global_epoch = (-2147483647i32 - 1);
inline __declspec(thread) int _Init_thread_epoch = (-2147483647i32 - 1);

inline void _Init_thread_header(volatile int* ptss) {
    while (true) {
        if (_InterlockedCompareExchange(reinterpret_cast<volatile long*>(ptss), -1, 0) == -1) {
            Sleep(0);
            continue;
        }
        break;
    }
}
inline void _Init_thread_footer(int* ptss) {
    *ptss = _InterlockedIncrement(reinterpret_cast<long*>(&_Init_global_epoch));
}
inline void _Init_thread_abort(volatile int* ptss) {
    _InterlockedAnd(reinterpret_cast<volatile long*>(ptss), 0);
}
}
#include <__msvc_all_public_headers.hpp>
)"};
    codeBuffer += code;

    auto buffer = llvm::MemoryBuffer::getMemBuffer(codeBuffer);

    auto& frontendOpts = impl->compilerInstance->getInvocation().getFrontendOpts();

    frontendOpts.Inputs.clear();
    frontendOpts.Inputs.push_back(clang::FrontendInputFile{*buffer, clang::Language::CXX});

    auto context = std::make_unique<llvm::LLVMContext>();

    auto llvmAction = clang::EmitLLVMOnlyAction(context.get());

    if (!impl->compilerInstance->ExecuteAction(llvmAction)) {
        return {};
    }
    return {llvmAction.takeModule(), std::move(context)};
}
void CxxCompileLayer::generatePch(std::string_view code, std::filesystem::path const& outFile) {
    auto&       compilerInvocation = impl->compilerInstance->getInvocation();
    auto&       frontendOpts       = compilerInvocation.getFrontendOpts();
    std::string prevFile           = std::move(frontendOpts.OutputFile);
    frontendOpts.OutputFile        = ll::string_utils::u8str2str(outFile.u8string());

    auto buffer = llvm::MemoryBuffer::getMemBuffer(code);

    // keep a copy of the current program action:
    auto prevAction            = frontendOpts.ProgramAction;
    frontendOpts.ProgramAction = clang::frontend::GeneratePCH;

    frontendOpts.Inputs.clear();
    frontendOpts.Inputs.push_back(clang::FrontendInputFile{*buffer, clang::Language::CXX});

    auto action = clang::GeneratePCHAction{};

    if (!impl->compilerInstance->ExecuteAction(action)) {
        std::terminate();
    }
    // Restore the previous values:
    frontendOpts.OutputFile    = std::move(prevFile);
    frontendOpts.ProgramAction = prevAction;

    auto& opts              = compilerInvocation.getPreprocessorOpts();
    opts.ImplicitPCHInclude = ll::string_utils::u8str2str(outFile.u8string());
}
} // namespace lcj
