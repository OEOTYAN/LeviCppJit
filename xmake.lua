add_rules("mode.release")

add_repositories("liteldev-repo https://github.com/LiteLDev/xmake-repo.git")

add_requires("levilamina")
add_requires("llvm-prebuilt 18.1.1")

set_runtimes("MD")

target("LeviCppJit")
    add_syslinks("wsock32", "version")
    add_cxflags(
        "/EHa", 
        "/utf-8" 
    )
    add_defines(
        "_HAS_CXX23=1",
        "NOMINMAX",
        "UNICODE"
    )
    add_files(
        "src/**.cpp"
    )
    add_includedirs(
        "src"
    )
    add_packages(
        "levilamina",
        "llvm-prebuilt"
    )
    add_shflags(
        "/DELAYLOAD:bedrock_server.dll"
    )
    set_exceptions("none")
    set_kind("shared")
    set_languages("cxx20")
    set_symbols("debug")

    after_build(function (target)
        local plugin_packer = import("scripts.after_build")

        local plugin_define = {
            pluginName = target:name(),
            pluginFile = path.filename(target:targetfile()),
        }
        
        plugin_packer.pack_plugin(target,plugin_define)
    end)
