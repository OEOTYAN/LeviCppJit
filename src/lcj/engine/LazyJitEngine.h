#pragma once

#include <memory>
#include <string_view>

#include "lcj/engine/Dylib.h"

namespace lcj {
class LazyJitEngine {
    struct Impl;
    std::unique_ptr<Impl> impl;

public:
    LazyJitEngine();
    ~LazyJitEngine();

    Dylib createDylib(std::string_view name);
};
} // namespace lcj
