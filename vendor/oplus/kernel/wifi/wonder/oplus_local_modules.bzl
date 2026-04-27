# SPDX-License-Identifier: GPL-2.0-or-later

# Google Wonder WiFi Virtual Device driver.
load("//build/kernel/kleaf:kernel.bzl", "checkpatch", "ddk_headers", "ddk_module")
load("//build/kernel/oplus:oplus_modules_define.bzl", "oplus_ddk_get_target", "define_oplus_ddk_module", "bazel_support_platform", "oplus_ddk_get_variant")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

def define_oplus_local_modules():
    checkpatch(
        name = "checkpatch",
        checkpatch_pl = "//common:scripts/checkpatch.pl",
    )

    if bazel_support_platform == "qcom" :

        _KERNEL_BUILD_VARIANT = "{}_{}".format(oplus_ddk_get_target(), oplus_ddk_get_variant())

        ddk_module(
            name = "wonder",
            srcs = native.glob([
                "**/*.h",
            ]) + [
                "band_config.c",
                "mac80211.c",
                "mac80211_txs.c",
                "main.c",
                "nl80211_ven_cmd.c",
                "ssr.c",
                "wondertap.c",
            ],
            out = "wonder.ko",
            conditional_srcs = {
                "CONFIG_DEBUG_FS": {
                    True: [
                        "debugfs.c",
                    ],
                },
            },
            # Resolve exported symbols from cfg80211/mac80211 during modpost by pulling in their Module.symvers.
            # These modules are generated under //soc-repo:{target}_{variant}/... by the QCOM registry.
            deps = select({
                "//build/kernel/kleaf:socrepo_true": [
                    "//soc-repo:{}/net/wireless/cfg80211".format(_KERNEL_BUILD_VARIANT),
                    "//soc-repo:{}/net/mac80211/mac80211".format(_KERNEL_BUILD_VARIANT),
                ],
                "//conditions:default": [],
            }),
            kernel_build = "//common:kernel_aarch64",
        )

        ddk_headers(
            name = "wonder_headers",
            hdrs = native.glob([
                "**/*.h",
            ]) + [
                "Makefile.include",
            ],
        )
    else :
        define_oplus_ddk_module(
            name = "wonder",
            srcs = native.glob([
                "**/*.h",
            ]) + [
                "band_config.c",
                "mac80211.c",
                "mac80211_txs.c",
                "main.c",
                "nl80211_ven_cmd.c",
                "ssr.c",
                "wondertap.c",
            ],
            out = "wonder.ko",
            conditional_srcs = {
                "CONFIG_DEBUG_FS": {
                    True: [
                        "debugfs.c",
                    ],
                },
            },
        )

        ddk_headers(
            name = "wonder_headers",
            hdrs = native.glob([
                "include/**/*.h",
            ]) + [
                "Makefile.include",
            ],
            includes = [
                "include",
            ],
        )

        ddk_copy_to_dist_dir(
            name = "oplus_wonder",
            module_list = ["wonder"],
        )
