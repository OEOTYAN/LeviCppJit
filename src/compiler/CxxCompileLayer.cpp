#include "CxxCompileLayer.h"

#include "core/LeviCppJit.h"

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
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetSelect.h>

#pragma comment(lib, "version.lib")

namespace lcj {

class DiagnosticLogger : public clang::DiagnosticConsumer {
public:
    void HandleDiagnostic(clang::DiagnosticsEngine::Level DiagLevel, const clang::Diagnostic& Info)
        override {
        auto        sd   = clang::StoredDiagnostic{DiagLevel, Info};
        auto&       file = sd.getLocation();
        std::string s;
        if (file.hasManager()) {
            s += file.printToString(file.getManager()) + ": ";
        }

        auto& logger = LeviCppJit::getInstance().getSelf().getLogger();

        switch (DiagLevel) {
        case clang::DiagnosticsEngine::Level::Ignored:
            logger.debug("[Ignored] {}{}", s, std::string_view{sd.getMessage()});
            return;
        case clang::DiagnosticsEngine::Level::Note:
            logger.info("[Note] {}{}", s, std::string_view{sd.getMessage()});
            return;
        case clang::DiagnosticsEngine::Level::Remark:
            logger.info("[Remark] {}{}", s, std::string_view{sd.getMessage()});
            return;
        case clang::DiagnosticsEngine::Level::Warning:
            logger.warn("{}{}", s, std::string_view{sd.getMessage()});
            return;
        case clang::DiagnosticsEngine::Level::Error:
            logger.error("{}{}", s, std::string_view{sd.getMessage()});
            return;
        case clang::DiagnosticsEngine::Level::Fatal:
            logger.fatal("{}{}", s, std::string_view{sd.getMessage()});
            return;
        default:
            std::unreachable();
        }
    }
};

struct CxxCompileLayer::Impl {
    std::unique_ptr<clang::CompilerInstance>                compilerInstance;
    llvm::IntrusiveRefCntPtr<clang::DiagnosticsEngine>      diagnosticsEngine;
    llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> inMemoryFileSystem;
};

CxxCompileLayer::CxxCompileLayer() : impl(std::make_unique<Impl>()) {

    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

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
    // codeGenOpts.setDebugInfo(clang::codegenoptions::DebugInfoKind::FullDebugInfo);

    compilerInvocation.getFrontendOpts().ProgramAction = clang::frontend::EmitLLVMOnly;

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
    // langOpts.EHAsynch                      = true;
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

    auto& preprocessorOpts = compilerInvocation.getPreprocessorOpts();

    preprocessorOpts.addMacroDef("_AMD64_");
    preprocessorOpts.addMacroDef("_CRT_SECURE_NO_WARNINGS");
    preprocessorOpts.addMacroDef("NOMINMAX");
    preprocessorOpts.addMacroDef("UNICODE");
    preprocessorOpts.addMacroDef("WIN32_LEAN_AND_MEAN");
    preprocessorOpts.addMacroDef("ENTT_PACKED_PAGE=128");
    preprocessorOpts.addMacroDef("_HAS_CXX23=1");
    preprocessorOpts.addMacroDef("LL_EXPORT");

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