#include "LogOnError.h"

#include "lcj/core/LeviCppJit.h"

namespace lcj {
void LogOnError::checkError(llvm::Error Err) {
    if (Err) {
        LeviCppJit::getInstance().getSelf().getLogger().fatal(llvm::toString(std::move(Err)));
        throw std::runtime_error("errored in llvm");
    }
}
} // namespace lcj
