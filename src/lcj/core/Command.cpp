#include "lcj/core/LeviCppJit.h"


#include <ll/api/command/CommandHandle.h>
#include <ll/api/command/CommandRegistrar.h>
#include <ll/api/command/runtime/RuntimeOverload.h>
#include <ll/api/reflection/Reflection.h>

namespace lcj {

void registerTestCommand() {
    auto& reg = ll::command::CommandRegistrar::getInstance();
    auto& cmd = reg.getOrCreateCommand("cppjit", "cppjit");
    cmd.runtimeOverload()
        .text("run")
        .required("code", ll::command::ParamKind::RawText)
        .execute(
            [](CommandOrigin const&, CommandOutput& output, ll::command::RuntimeCommand const& rc) {
                output.success(
                    "type: {}",
                    LeviCppJit::getInstance().simpleEval(
                        rc["code"].get<ll::command::ParamKind::RawText>().text
                    )
                );
            }
        );
}
} // namespace lcj
