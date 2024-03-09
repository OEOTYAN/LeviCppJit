#pragma once

#include <llvm/Support/Error.h>

namespace lcj {

class LogOnError {
public:
    void operator()(llvm::Error Err) const { checkError(std::move(Err)); }

    template <typename T>
    T operator()(llvm::Expected<T>&& E) const {
        checkError(E.takeError());
        return std::move(*E);
    }

    template <typename T>
    T& operator()(llvm::Expected<T&>&& E) const {
        checkError(E.takeError());
        return *E;
    }

private:
    static void checkError(llvm::Error Err);
};

static inline LogOnError CheckExcepted;

} // namespace lcj
