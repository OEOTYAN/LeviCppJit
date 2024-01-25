#pragma once

#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"

namespace lcj {

class ServerSymbolGenerator : public llvm::orc::DefinitionGenerator {
    char globalPrefix{};

public:
    llvm::Error tryToGenerate(
        llvm::orc::LookupState&           LS,
        llvm::orc::LookupKind             K,
        llvm::orc::JITDylib&              JD,
        llvm::orc::JITDylibLookupFlags    JDLookupFlags,
        llvm::orc::SymbolLookupSet const& Symbols
    ) override;
};

} // namespace lcj