load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module", "oplus_ddk_get_kernel_version", "oplus_ddk_get_target", "oplus_ddk_get_variant", "bazel_support_platform")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")
load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")

def define_oplus_uclamp_local_modules():
    target = oplus_ddk_get_target()
    variant  = oplus_ddk_get_variant()
    kernel_build_variant = "{}_{}".format(target, variant)
    kernel_version = oplus_ddk_get_kernel_version()

    if bazel_support_platform == "qcom" :
        ko_deps = [
        ]
        copts = ["-DCONFIG_SCHED_WALT"]
        kconfig = None
        defconfig = None
        ddk_config = "//soc-repo:{}_config".format(kernel_build_variant)
    else :
        ko_deps = [
        ]
        copts = []
        kconfig = "sched/Kconfig"
        defconfig = "build/defconfig/{}/sched/sched_configs".format(target)
        ddk_config = None

    define_oplus_ddk_module(
        name = "oplus_uclamp",
        srcs = native.glob([
            "sched/oplus_uclamp/oplus_uclamp.c",
        ]),
        local_defines = [],
        kconfig = kconfig,
        defconfig = defconfig,
        config = ddk_config,
        conditional_srcs = {
        },
        conditional_defines = {
            "mtk":  ["CONFIG_OPLUS_SYSTEM_KERNEL_MTK"],
            "qcom": ["CONFIG_OPLUS_SYSTEM_KERNEL_QCOM","CONFIG_SCHED_WALT"],
        },
        ko_deps = ["//vendor/oplus/kernel/cpu:oplus_bsp_sched_assist"],
        copts = copts,
        includes = ["."],
    )
