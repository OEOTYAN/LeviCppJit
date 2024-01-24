#pragma once

#include <ll/api/plugin/NativePlugin.h>

namespace lcj {

class LeviCppJit {
public:
    explicit LeviCppJit(ll::plugin::NativePlugin& self);

    LeviCppJit(LeviCppJit&&)                 = delete;
    LeviCppJit(const LeviCppJit&)            = delete;
    LeviCppJit& operator=(LeviCppJit&&)      = delete;
    LeviCppJit& operator=(const LeviCppJit&) = delete;

    ~LeviCppJit();

    static LeviCppJit& getInstance();

    [[nodiscard]] ll::plugin::NativePlugin& getSelf() const;

    bool enable();

    bool disable();

private:
    ll::plugin::NativePlugin& mSelf;
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace lcj
