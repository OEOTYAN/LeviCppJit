#include "CxxCompileLayer.h"

#include "core/LeviCppJit.h"

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
            logger.debug("<Ignored> {}{}", s, std::string_view{sd.getMessage()});
            return;
        case clang::DiagnosticsEngine::Level::Note:
            logger.info("<Note> {}{}", s, std::string_view{sd.getMessage()});
            return;
        case clang::DiagnosticsEngine::Level::Remark:
            logger.info("<Remark> {}{}", s, std::string_view{sd.getMessage()});
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

    langOpts.LineComment                   = true;
    langOpts.Optimize                      = true;
    langOpts.HexFloats                     = true;
    langOpts.CPlusPlus                     = true;
    langOpts.CPlusPlus11                   = true;
    langOpts.CPlusPlus14                   = true;
    langOpts.CPlusPlus17                   = true;
    langOpts.CPlusPlus20                   = true;
    langOpts.CPlusPlus2b                   = true;
    langOpts.EncodeCXXClassTemplateSpec    = true;
    langOpts.CXXExceptions                 = true;
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
    llvm::VersionTuple VT;
    if (!VT.tryParse("14.38.33130")) // TODO
    {
        langOpts.MSCompatibilityVersion = (VT.getMajor() + 5) * 10000000
                                        + VT.getMinor().value_or(0) * 100000
                                        + VT.getSubminor().value_or(0);
    }

    std::cout << langOpts.MSCompatibilityVersion << std::endl;

    auto& preprocessorOpts = compilerInvocation.getPreprocessorOpts();
    preprocessorOpts.addMacroDef("_AMD64_");
    preprocessorOpts.addMacroDef("_CRT_SECURE_NO_WARNINGS");
    preprocessorOpts.addMacroDef("_ENABLE_CONSTEXPR_MUTEX_CONSTRUCTOR");
    preprocessorOpts.addMacroDef("NOMINMAX");
    preprocessorOpts.addMacroDef("UNICODE");
    preprocessorOpts.addMacroDef("WIN32_LEAN_AND_MEAN");
    preprocessorOpts.addMacroDef("ENTT_PACKED_PAGE=128");
    preprocessorOpts.addMacroDef("_HAS_CXX23=1");
    preprocessorOpts.addMacroDef("LL_EXPORT");

    auto& headerSearchOpts = compilerInvocation.getHeaderSearchOpts();

    headerSearchOpts.AddPath(
        R"(D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.38.33130\include)",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );
    headerSearchOpts.AddPath(
        R"(C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\cppwinrt)",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );
    headerSearchOpts.AddPath(
        R"(C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\shared)",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );
    headerSearchOpts.AddPath(
        R"(C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\ucrt)",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );
    headerSearchOpts.AddPath(
        R"(C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\um)",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );
    headerSearchOpts.AddPath(
        R"(C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\winrt)",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );


    headerSearchOpts.AddPath(
        "C:\\Users\\OEOTYAN\\AppData\\Local\\.xmake\\packages\\l\\levilamina\\0.5."
        "1\\519580a937b043468db373b3d1caf7ee\\include",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );
    headerSearchOpts.AddPath(
        "C:\\Users\\OEOTYAN\\AppData\\Local\\.xmake\\packages\\c\\ctre\\3.8."
        "1\\38526533895541a6a7943676aa1a6a13\\include",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );
    headerSearchOpts.AddPath(
        "C:\\Users\\OEOTYAN\\AppData\\Local\\.xmake\\packages\\e\\entt\\v3.12."
        "2\\5748c075ebf044f9b53ef5be15be7e68\\include",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );
    headerSearchOpts.AddPath(
        "C:\\Users\\OEOTYAN\\AppData\\Local\\.xmake\\packages\\f\\fmt\\10.1."
        "1\\d6bf0eee3327450f979207097834d942\\include",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );
    headerSearchOpts.AddPath(
        "C:\\Users\\OEOTYAN\\AppData\\Local\\.xmake\\packages\\g\\gsl\\v4.0."
        "0\\d827b849ba7b4981a0dd19e7b3d83179\\include",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );
    headerSearchOpts.AddPath(
        "C:\\Users\\OEOTYAN\\AppData\\Local\\.xmake\\packages\\l\\leveldb\\1."
        "23\\e75a071b4bcf4c7ba6ed1d50ed645b69\\include",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );
    headerSearchOpts.AddPath(
        "C:\\Users\\OEOTYAN\\AppData\\Local\\.xmake\\packages\\s\\snappy\\1.1."
        "10\\ce2eea8d5eb6433781586827e06057c2\\include",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );
    headerSearchOpts.AddPath(
        "C:\\Users\\OEOTYAN\\AppData\\Local\\.xmake\\packages\\m\\magic_enum\\v0.9."
        "0\\fe30b4c97c064545940a9e7c58e584e8\\include",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );
    headerSearchOpts.AddPath(
        "C:\\Users\\OEOTYAN\\AppData\\Local\\.xmake\\packages\\n\\nlohmann_json\\v3.11."
        "2\\5211ffe4b91e417b97051c2472dcca02\\include",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );
    headerSearchOpts.AddPath(
        "C:\\Users\\OEOTYAN\\AppData\\Local\\.xmake\\packages\\r\\rapidjson\\v1.1."
        "0\\d77ef886f1564da79362114528b6d272\\include",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );
    headerSearchOpts.AddPath(
        "C:\\Users\\OEOTYAN\\AppData\\Local\\.xmake\\packages\\p\\pcg_cpp\\v1.0."
        "0\\c51ba936dd56460fae7ffd07212bbc14\\include",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );
    headerSearchOpts.AddPath(
        "C:\\Users\\OEOTYAN\\AppData\\Local\\.xmake\\packages\\p\\pfr\\2.1."
        "1\\8df44852ec4d4625a1bbcec8caf26c0d\\include",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );
    headerSearchOpts.AddPath(
        "C:\\Users\\OEOTYAN\\AppData\\Local\\.xmake\\packages\\e\\expected-lite\\v0.6."
        "3\\dcc02bd4ed7c4da8a8dde5f4c36e03d1\\include",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );
    headerSearchOpts.AddPath(
        "C:\\Users\\OEOTYAN\\AppData\\Local\\.xmake\\packages\\p\\preloader\\v1.4."
        "0\\581696c0cba348b198c3d069593df3c6\\include",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );
    headerSearchOpts.AddPath(
        "C:\\Users\\OEOTYAN\\AppData\\Local\\.xmake\\packages\\s\\symbolprovider\\v1.1."
        "0\\355ae41e03ab4536a9e1f6767963f2f4\\include",
        clang::frontend::IncludeDirGroup::System,
        false,
        false
    );
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