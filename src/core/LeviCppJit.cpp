#include "LeviCppJit.h"

#include <ll/api/plugin/NativePlugin.h>

namespace lcj {

struct LeviCppJit::Impl {};

LeviCppJit::~LeviCppJit() { mSelf.getLogger().info("unloading..."); }

static std::unique_ptr<LeviCppJit> plugin{};

LeviCppJit& LeviCppJit::getInstance() { return *plugin; }

LeviCppJit::LeviCppJit(ll::plugin::NativePlugin& self)
: mSelf(self),
  mImpl(std::make_unique<Impl>()) {
    mSelf.getLogger().info("loading...");
}

ll::plugin::NativePlugin& LeviCppJit::getSelf() const { return mSelf; }

bool LeviCppJit::enable() {
    mSelf.getLogger().info("enabling...");

    return true;
}

bool LeviCppJit::disable() {
    mSelf.getLogger().info("disabling...");

    return true;
}

extern "C" {
_declspec(dllexport) bool ll_plugin_load(ll::plugin::NativePlugin& self) {
    plugin = std::make_unique<LeviCppJit>(self);
    return true;
}

_declspec(dllexport) bool ll_plugin_unload(ll::plugin::NativePlugin&) {
    plugin.reset();
    return true;
}

_declspec(dllexport) bool ll_plugin_enable(ll::plugin::NativePlugin&) { return plugin->enable(); }

_declspec(dllexport) bool ll_plugin_disable(ll::plugin::NativePlugin&) { return plugin->disable(); }
}

} // namespace lcj
