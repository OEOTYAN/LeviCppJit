#pragma once

#include <any>

#include <ll/api/plugin/NativePlugin.h>

namespace lcj {

class LeviCppJit {
public:
    LeviCppJit(ll::plugin::NativePlugin&);

    LeviCppJit(LeviCppJit&&)                 = delete;
    LeviCppJit(const LeviCppJit&)            = delete;
    LeviCppJit& operator=(LeviCppJit&&)      = delete;
    LeviCppJit& operator=(const LeviCppJit&) = delete;

    ~LeviCppJit();

    static LeviCppJit& getInstance();

    [[nodiscard]] ll::plugin::NativePlugin& getSelf() const;

    [[nodiscard]] decltype(auto) getLogger() const { return (getSelf().getLogger()); }
    [[nodiscard]] decltype(auto) getDataDir() const { return (getSelf().getDataDir()); }

    std::string simpleEval(std::string_view code);

    bool load();

    bool enable();

    bool disable();

    bool unload();

private:
    ll::plugin::NativePlugin& mSelf;
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace lcj
