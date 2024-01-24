#pragma once

#include <ll/api/plugin/PluginManager.h>

namespace lcj {

constexpr std::string_view pluginName{"cppscript"};

class PluginManager : public ll::plugin::PluginManager {
    PluginManager() : ll::plugin::PluginManager(pluginName) {}
};
} // namespace lcj
