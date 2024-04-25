#include "CxxCompileLayer.h"

#include "lcj/compiler/DiagnosticLogger.h"
#include "lcj/core/LeviCppJit.h"

#include <clang/Basic/DiagnosticLex.h>
#include <clang/Basic/DiagnosticSema.h>

#include <clang/Basic/SourceManager.h>
#include <clang/CodeGen/CodeGenAction.h>
#include <clang/CodeGen/ModuleBuilder.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Host.h>

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
    codeGenOpts.CodeModel       = "small";
    codeGenOpts.RelocationModel = llvm::Reloc::PIC_;

    auto& frontendOpts = compilerInvocation.getFrontendOpts();

    frontendOpts.ProgramAction = clang::frontend::EmitLLVMOnly;

    auto& langOpts = compilerInvocation.getLangOpts();

    langOpts.LineComment                = true;
    langOpts.Optimize                   = true;
    langOpts.HexFloats                  = true;
    langOpts.CPlusPlus                  = true;
    langOpts.CPlusPlus11                = true;
    langOpts.CPlusPlus14                = true;
    langOpts.CPlusPlus17                = true;
    langOpts.CPlusPlus20                = true;
    langOpts.CPlusPlus23                = true;
    langOpts.CPlusPlus26                = true;
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


llvm::orc::ThreadSafeModule CxxCompileLayer::compileRaw(std::string_view code) {
    auto& inMemoryFileSystem = *impl->inMemoryFileSystem;
    if (inMemoryFileSystem.exists("main")) {
        inMemoryFileSystem.getBufferForFile("main").get() =
            llvm::MemoryBuffer::getMemBufferCopy(code);
    } else {
        inMemoryFileSystem
            .addFile("main", time(nullptr), llvm::MemoryBuffer::getMemBufferCopy(code));
    }
    auto& frontendOpts = impl->compilerInstance->getInvocation().getFrontendOpts();

    frontendOpts.Inputs.clear();
    frontendOpts.Inputs.push_back(clang::FrontendInputFile{"main", clang::Language::CXX});

    auto context = std::make_unique<llvm::LLVMContext>();

    auto llvmAction = clang::EmitLLVMOnlyAction(context.get());

    if (!impl->compilerInstance->ExecuteAction(llvmAction)) {
        return {};
    }

    return {llvmAction.takeModule(), std::move(context)};
}

} // namespace lcj