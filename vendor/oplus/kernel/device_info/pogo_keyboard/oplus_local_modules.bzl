load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module", "oplus_ddk_get_target", "oplus_ddk_get_variant")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

def define_oplus_local_modules():
    target = oplus_ddk_get_target()
    variant  = oplus_ddk_get_variant()
    kernel_build_variant = "{}_{}".format(target, variant)
    bazel_support_target = oplus_ddk_get_target()

    if bazel_support_target == "canoe" :
        oplus_bsp_boot_projectinfo_ko_deps = [
            "//vendor/oplus/kernel/boot:oplus_bsp_boot_projectinfo",
        ]
        oplus_bsp_kfb_ko_deps = [
            "//vendor/oplus/kernel/dft/bazel:oplus_bsp_dft_kernel_fb",
        ]
        oplus_bsp_dft_olc_ko_deps = [
            "//vendor/oplus/kernel/dft/bazel:oplus_bsp_dft_olc",
        ]
        panel_event_notifier_ko_deps = [
            "//soc-repo:{}/drivers/soc/qcom/panel_event_notifier".format(kernel_build_variant),
        ]
        msm_geni_serial_ko_deps = [
            "//soc-repo:{}/drivers/tty/serial/msm_geni_serial".format(kernel_build_variant),
        ]
    else :
        oplus_bsp_boot_projectinfo_ko_deps = []
        oplus_bsp_kfb_ko_deps = []
        oplus_bsp_dft_olc_ko_deps = []
        panel_event_notifier_ko_deps = []
        msm_geni_serial_ko_deps = []

    define_oplus_ddk_module(
        name = "oplus_bsp_pogo_keyboard",
        srcs = native.glob([
            "**/*.h",
            "pogo_keyboard_core.c",
            "keyboard.c",
            "touchpad.c",
            "pogo_tty_io.c",
            "pogo_ota.c",
            "pogo_healthinfo.c",
            "pogo_exception.c",
        ]),
        ko_deps = oplus_bsp_boot_projectinfo_ko_deps + oplus_bsp_kfb_ko_deps + oplus_bsp_dft_olc_ko_deps + panel_event_notifier_ko_deps + msm_geni_serial_ko_deps,
        includes = ["."],
        conditional_defines = {
            "qcom":  ["CONFIG_QCOM_PANEL_EVENT_NOTIFIER", "CONFIG_OPLUS_FEATURE_OLC"],
            "mtk":  ["CONFIG_OPLUS_FEATURE_OLC"],
        },
    )

    ddk_copy_to_dist_dir(
        name = "oplus_bsp_pogo_keyboard",
        module_list = [
            "oplus_bsp_pogo_keyboard",
        ],
    )
