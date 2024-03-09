#pragma once

#include <clang/Basic/Diagnostic.h>

namespace lcj {
class DiagnosticLogger : public clang::DiagnosticConsumer {
public:
    void HandleDiagnostic(clang::DiagnosticsEngine::Level DiagLevel, const clang::Diagnostic& Info)
        override;
};
}