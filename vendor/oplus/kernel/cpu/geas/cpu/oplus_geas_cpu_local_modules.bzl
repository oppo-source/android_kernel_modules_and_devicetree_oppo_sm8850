load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module", "oplus_ddk_get_target")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

def define_oplus_geas_cpu_local_modules():
    target = oplus_ddk_get_target()
    if target == "k6993v1_64" :
        kernelconfig = "geas/cpu/kernel_config/k6993v1_64/geas_configs"
    elif target == "canoe" :
        kernelconfig = "geas/cpu/kernel_config/canoe/geas_configs"
    else :
        kernelconfig = "geas/cpu/kernel_config/default/geas_configs"

    define_oplus_ddk_module(
        name = "oplus_bsp_geas_cpu",
        srcs = native.glob([
            "geas/cpu/geas.h",
        ]),
        kconfig = "geas/cpu/Kconfig",
        defconfig = kernelconfig,
        conditional_srcs = {
            "CONFIG_OPLUS_FEATURE_GEAS_CPU": {
                True:["geas/cpu/geas_cpu.c"],
                False:["geas/cpu/empty.c"],
            },
        },
    )

