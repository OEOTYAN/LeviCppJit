#pragma once

#include <ll/api/plugin/NativePlugin.h>

namespace lcj {

class LeviCppJit {
public:
     LeviCppJit();

    LeviCppJit(LeviCppJit&&)                 = delete;
    LeviCppJit(const LeviCppJit&)            = delete;
    LeviCppJit& operator=(LeviCppJit&&)      = delete;
    LeviCppJit& operator=(const LeviCppJit&) = delete;

    ~LeviCppJit();

    static LeviCppJit& getInstance();

    [[nodiscard]] ll::plugin::NativePlugin& getSelf() const;

    bool load(ll::plugin::NativePlugin&);

    bool enable();

    bool disable();

    bool unload();

private:
    ll::plugin::NativePlugin* mSelf{};
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace lcj
