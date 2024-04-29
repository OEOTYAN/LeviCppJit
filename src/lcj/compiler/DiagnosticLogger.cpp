#include "DiagnosticLogger.h"

#include "lcj/core/LeviCppJit.h"

namespace lcj {

void DiagnosticLogger::HandleDiagnostic(
    clang::DiagnosticsEngine::Level DiagLevel,
    const clang::Diagnostic&        Info
) {
    auto        sd   = clang::StoredDiagnostic{DiagLevel, Info};
    auto&       file = sd.getLocation();
    std::string s;
    if (file.hasManager()) {
        s += file.printToString(file.getManager()) + ": ";
    }

    auto& logger = LeviCppJit::getInstance().getLogger();

    switch (DiagLevel) {
    case clang::DiagnosticsEngine::Level::Ignored:
        logger.debug("Ignored: {}{}", s, std::string_view{sd.getMessage()});
        return;
    case clang::DiagnosticsEngine::Level::Note:
        logger.info("{}{}", s, std::string_view{sd.getMessage()});
        return;
    case clang::DiagnosticsEngine::Level::Remark:
        logger.info("Remark: {}{}", s, std::string_view{sd.getMessage()});
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
} // namespace lcj