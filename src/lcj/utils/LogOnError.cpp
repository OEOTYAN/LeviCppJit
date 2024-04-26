#include "LogOnError.h"

#include "lcj/core/LeviCppJit.h"

namespace lcj {

static constexpr size_t maxSize = 512;


void LogOnError::checkError(llvm::Error Err) {
    if (Err) {
        auto msg = llvm::toString(std::move(Err));
        if (auto size = msg.size(); size > maxSize) {
            msg.resize(maxSize);
            LeviCppJit::getInstance().getSelf().getLogger().fatal(
                "{}..., left {} characters",
                msg,
                size - maxSize
            );
        } else {
            LeviCppJit::getInstance().getSelf().getLogger().fatal(msg);
        }
        throw std::runtime_error("error in llvm");
    }
}
} // namespace lcj
