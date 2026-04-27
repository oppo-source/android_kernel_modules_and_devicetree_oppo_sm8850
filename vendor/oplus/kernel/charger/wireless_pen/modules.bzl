load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl",
    "define_oplus_ddk_module", "oplus_ddk_get_kernel_version",
    "bazel_support_platform")
load(":kleaf-scripts/targets.bzl", "oplus_modules_get_target_variant")
load(":kleaf-scripts/filter_target.bzl", "filter_deps_map")
load(":kleaf-scripts/version.bzl", "version_compare")

conditional_ko_deps = {
    "CONFIG_OPLUS_ADSP_CHARGER": {
        True: [
            "//soc-repo:{target_variant}/drivers/soc/qcom/panel_event_notifier",
            "//soc-repo:{target_variant}/drivers/soc/qcom/qti_pmic_glink",
        ],
    },
    "CONFIG_OPLUS_CHARGER_MTK": {
        True: [
                "//kernel_device_modules-{}/drivers/soc/oplus/device_info:device_info".format(oplus_ddk_get_kernel_version()),
                "//kernel_device_modules-{}/drivers/soc/oplus/boot:oplus_bsp_boot_projectinfo".format(oplus_ddk_get_kernel_version()),
                "//kernel_device_modules-{}/drivers/misc/mediatek/boot_common:mtk_boot_common".format(oplus_ddk_get_kernel_version()),
                "//kernel_device_modules-{}/drivers/base/kernelFwUpdate:oplus_bsp_fw_update".format(oplus_ddk_get_kernel_version()),
        ],
    },
    "CONFIG_DISABLE_OPLUS_FUNCTION": {
        False: [
            "//vendor/oplus/kernel/device_info/device_info/bazel:device_info",
            "//vendor/oplus/kernel/boot:oplus_bsp_bootmode",
            "//vendor/oplus/kernel/boot:oplus_bsp_boot_projectinfo",
            "//vendor/oplus/kernel/touchpanel/kernelFwUpdate/bazel:oplus_bsp_fw_update",
        ],
    },
}

def define_oplus_wireless_pen_mt5806_module():
    module_list = []

    target = oplus_modules_get_target_variant()
    kernel_version = oplus_ddk_get_kernel_version()
    ko_deps = filter_deps_map(target, conditional_ko_deps)

    if bazel_support_platform == "qcom":
        kconfig = None
        defconfig = None
        if version_compare(kernel_version, "6.12") :
            ddk_config = "//soc-repo:{}_config".format(target)
    else:
        kconfig = ":kconfig.oplus_chg.generated"
        defconfig = ":oplus_chg_{}_defconfig".format(target)
        if version_compare(kernel_version, "6.12") :
            ddk_config = None

    ddk_srcs = native.glob([
        "wireless_pen/*.h",
        "wireless_pen/oplus_mt5806.c",
        "wireless_pen/oplus_wireless_pen_glink.c",
    ])

    ddk_includes = [
        "wireless_pen/*.h"
    ]

    if version_compare(kernel_version, "6.12") :
        define_oplus_ddk_module(
            name = "{}_oplus_wireless_pen_mt5806".format(target),
            out = "oplus_wireless_pen_mt5806.ko",
            srcs = ddk_srcs,
            conditional_srcs = None,
            includes = ddk_includes,
            ko_deps = ko_deps,
            local_defines = [],
            conditional_defines = {
            },
            hdrs = [],
            kconfig = kconfig,
            defconfig = defconfig,
            config = ddk_config,
        )
    else:
        define_oplus_ddk_module(
            name = "{}_oplus_wireless_pen_mt5806".format(target),
            out = "oplus_wireless_pen_mt5806.ko",
            srcs = ddk_srcs,
            conditional_srcs = None,
            includes = ddk_includes,
            ko_deps = ko_deps,
            local_defines = [],
            conditional_defines = {
            },
            hdrs = [],
            kconfig = kconfig,
            defconfig = defconfig,
        )

    module_list.extend(filter_deps_map(target, {
        "CONFIG_OPLUS_WIRELESS_PEN": "{}_oplus_wireless_pen_mt5806".format(target)
    }))

    ddk_headers(
        name = "wireless_pen_headers",
        hdrs  = native.glob([
            "wireless_pen/*.h"
        ]),
        includes = [
            "wireless_pen/*.h",
        ]
    )

    return module_list

def define_oplus_wireless_pen_cps8601_module():
    module_list = []

    target = oplus_modules_get_target_variant()
    kernel_version = oplus_ddk_get_kernel_version()
    ko_deps = filter_deps_map(target, conditional_ko_deps)

    if bazel_support_platform == "qcom":
        kconfig = None
        defconfig = None
        if version_compare(kernel_version, "6.12") :
            ddk_config = "//soc-repo:{}_config".format(target)
    else:
        kconfig = ":kconfig.oplus_chg.generated"
        defconfig = ":oplus_chg_{}_defconfig".format(target)
        if version_compare(kernel_version, "6.12") :
            ddk_config = None

    ddk_srcs = native.glob([
        "wireless_pen/*.h",
        "wireless_pen/oplus_cps8601.c",
        "wireless_pen/oplus_wireless_pen_glink.c",
    ])

    ddk_includes = [
        "wireless_pen/*.h"
    ]

    if version_compare(kernel_version, "6.12") :
        define_oplus_ddk_module(
            name = "{}_oplus_wireless_pen_cps8601".format(target),
            out = "oplus_wireless_pen_cps8601.ko",
            srcs = ddk_srcs,
            conditional_srcs = None,
            includes = ddk_includes,
            ko_deps = ko_deps,
            local_defines = [],
            conditional_defines = {
            },
            hdrs = [],
            kconfig = kconfig,
            defconfig = defconfig,
            config = ddk_config,
        )
    else:
        define_oplus_ddk_module(
            name = "{}_oplus_wireless_pen_cps8601".format(target),
            out = "oplus_wireless_pen_cps8601.ko",
            srcs = ddk_srcs,
            conditional_srcs = None,
            includes = ddk_includes,
            ko_deps = ko_deps,
            local_defines = [],
            conditional_defines = {
            },
            hdrs = [],
            kconfig = kconfig,
            defconfig = defconfig,
        )

    module_list.extend(filter_deps_map(target, {
        "CONFIG_OPLUS_WIRELESS_PEN": "{}_oplus_wireless_pen_cps8601".format(target)
    }))

    return module_list
