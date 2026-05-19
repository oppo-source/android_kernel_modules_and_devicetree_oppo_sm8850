load(":synx_modules.bzl", "synx_modules")
load(":synx_module_build.bzl", "define_consolidate_perf_modules")

def define_autogvm():
    define_consolidate_perf_modules(
        target = "autogvm",
        registry = synx_modules,
        modules = [
            "synx-stub",
        ],
    )
