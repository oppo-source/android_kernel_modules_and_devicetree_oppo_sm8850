load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl",
    "define_oplus_ddk_module", "oplus_ddk_get_kernel_version",
    "bazel_support_platform")
load(":kleaf-scripts/defconfig.bzl", "oplus_modules_get_config")
load(":kleaf-scripts/targets.bzl", "oplus_modules_get_target_variant")
load(":kleaf-scripts/filter_target.bzl", "filter_deps_map")
load(":kleaf-scripts/version.bzl", "version_compare")

def _oplus_pd_ext_enabled():
    target = oplus_modules_get_target_variant()
    cfg = oplus_modules_get_config(target)
    if cfg == None:
        return False
    return cfg.get("CONFIG_OPLUS_PD_EXT_SUPPORT", "n") in ["y", "m"]

def define_pd_ext_modules():
    if not _oplus_pd_ext_enabled():
        return []
    modules = []
    modules += define_tcpc_class_module()
    modules += define_tcpci_late_sync_module()
    modules += define_pd_dbg_info_module()
    modules += define_rt_regmap_module()
    modules += define_tcpc_rt1711h_module()
    return modules

def define_tcpc_class_module():
    module_list = []

    target = oplus_modules_get_target_variant()
    kernel_version = oplus_ddk_get_kernel_version()

    if bazel_support_platform == "qcom":
        kconfig = None
        defconfig = None
        if version_compare(kernel_version, "6.12"):
            ddk_config = "//soc-repo:{}_config".format(target)
    else:
        kconfig = ":kconfig.oplus_chg.generated"
        defconfig = ":oplus_chg_{}_defconfig".format(target)
        if version_compare(kernel_version, "6.12"):
            ddk_config = None

    ddk_srcs = native.glob([
        "pd_ext/*.h",
        "pd_ext/inc/*.h",
        "pd_ext/tcpci_core.c",
        "pd_ext/tcpci_typec.c",
        "pd_ext/tcpci_timer.c",
        "pd_ext/tcpm.c",
        "pd_ext/tcpci.c",
        "pd_ext/tcpci_alert.c",
    ])

    ddk_conditional_srcs = {
        "CONFIG_USB_POWER_DELIVERY": {
            True: [
                "pd_ext/tcpci_event.c",
                "pd_ext/pd_core.c",
                "pd_ext/pd_policy_engine.c",
                "pd_ext/pd_process_evt.c",
                "pd_ext/pd_dpm_core.c",
                "pd_ext/pd_dpm_uvdm.c",
                "pd_ext/pd_dpm_alt_mode_dp.c",
                "pd_ext/pd_dpm_pdo_select.c",
                "pd_ext/pd_dpm_reaction.c",
                "pd_ext/pd_process_evt_snk.c",
                "pd_ext/pd_process_evt_src.c",
                "pd_ext/pd_process_evt_vdm.c",
                "pd_ext/pd_process_evt_drs.c",
                "pd_ext/pd_process_evt_prs.c",
                "pd_ext/pd_process_evt_vcs.c",
                "pd_ext/pd_process_evt_dbg.c",
                "pd_ext/pd_process_evt_tcp.c",
                "pd_ext/pd_process_evt_com.c",
                "pd_ext/pd_policy_engine_src.c",
                "pd_ext/pd_policy_engine_snk.c",
                "pd_ext/pd_policy_engine_ufp.c",
                "pd_ext/pd_policy_engine_vcs.c",
                "pd_ext/pd_policy_engine_dfp.c",
                "pd_ext/pd_policy_engine_dr.c",
                "pd_ext/pd_policy_engine_drs.c",
                "pd_ext/pd_policy_engine_prs.c",
                "pd_ext/pd_policy_engine_dbg.c",
                "pd_ext/pd_policy_engine_com.c",
                "pd_ext/pd_dpm_alt_mode_dc.c",
            ],
        },
        "CONFIG_DUAL_ROLE_USB_INTF": {
            True: [
                "pd_ext/tcpci_dual_role.c",
            ],
        },
    }

    ddk_includes = [
        "pd_ext",
        "pd_ext/inc",
    ]

    if version_compare(kernel_version, "6.12"):
        define_oplus_ddk_module(
            name = "{}_tcpc_class".format(target),
            out = "tcpc_class.ko",
            srcs = ddk_srcs,
            conditional_srcs = ddk_conditional_srcs,
            includes = ddk_includes,
            ko_deps = [
                "{}_pd_dbg_info".format(target),
                "{}_rt-regmap".format(target),
            ],
            local_defines = [
                "CONFIG_RT_REGMAP",
                "OPLUS_FEATURE_CHG_BASIC",
            ],
            conditional_defines = {},
            hdrs = [],
            kconfig = kconfig,
            defconfig = defconfig,
            config = ddk_config,
        )
    else:
        define_oplus_ddk_module(
            name = "{}_tcpc_class".format(target),
            out = "tcpc_class.ko",
            srcs = ddk_srcs,
            conditional_srcs = ddk_conditional_srcs,
            includes = ddk_includes,
            ko_deps = [
                "{}_pd_dbg_info".format(target),
                "{}_rt-regmap".format(target),
            ],
            local_defines = [
                "CONFIG_RT_REGMAP",
                "OPLUS_FEATURE_CHG_BASIC",
            ],
            conditional_defines = {},
            hdrs = [],
            kconfig = kconfig,
            defconfig = defconfig,
        )

    module_list.extend(filter_deps_map(target, {
        "CONFIG_TCPC_CLASS": "{}_tcpc_class".format(target),
    }))

    ddk_headers(
        name = "pd_ext_headers",
        hdrs  = native.glob([
            "pd_ext/inc/*.h",
        ]),
        includes = [
            "pd_ext/inc",
        ],
    )

    return module_list

def define_tcpci_late_sync_module():
    module_list = []

    target = oplus_modules_get_target_variant()
    kernel_version = oplus_ddk_get_kernel_version()

    if bazel_support_platform == "qcom":
        kconfig = None
        defconfig = None
        if version_compare(kernel_version, "6.12"):
            ddk_config = "//soc-repo:{}_config".format(target)
    else:
        kconfig = ":kconfig.oplus_chg.generated"
        defconfig = ":oplus_chg_{}_defconfig".format(target)
        if version_compare(kernel_version, "6.12"):
            ddk_config = None

    ddk_srcs = [
        "pd_ext/tcpci_late_sync.c",
    ]

    ddk_includes = [
        "pd_ext",
        "pd_ext/inc",
    ]

    ddk_hdrs = native.glob([
        "pd_ext/*.h",
        "pd_ext/inc/*.h",
    ])

    if version_compare(kernel_version, "6.12"):
        define_oplus_ddk_module(
            name = "{}_tcpci_late_sync".format(target),
            out = "tcpci_late_sync.ko",
            srcs = ddk_srcs,
            includes = ddk_includes,
            ko_deps = [
                "{}_tcpc_class".format(target),
            ],
            local_defines = [
                "CONFIG_RT_REGMAP",
                "OPLUS_FEATURE_CHG_BASIC",
            ],
            conditional_defines = {},
            hdrs = ddk_hdrs,
            kconfig = kconfig,
            defconfig = defconfig,
            config = ddk_config,
        )
    else:
        define_oplus_ddk_module(
            name = "{}_tcpci_late_sync".format(target),
            out = "tcpci_late_sync.ko",
            srcs = ddk_srcs,
            includes = ddk_includes,
            ko_deps = [
                "{}_tcpc_class".format(target),
            ],
            local_defines = [
                "CONFIG_RT_REGMAP",
                "OPLUS_FEATURE_CHG_BASIC",
            ],
            conditional_defines = {},
            hdrs = ddk_hdrs,
            kconfig = kconfig,
            defconfig = defconfig,
        )

    module_list.extend(filter_deps_map(target, {
        "CONFIG_TCPC_CLASS": "{}_tcpci_late_sync".format(target),
    }))

    return module_list

def define_pd_dbg_info_module():
    module_list = []

    target = oplus_modules_get_target_variant()
    kernel_version = oplus_ddk_get_kernel_version()

    if bazel_support_platform == "qcom":
        kconfig = None
        defconfig = None
        if version_compare(kernel_version, "6.12"):
            ddk_config = "//soc-repo:{}_config".format(target)
    else:
        kconfig = ":kconfig.oplus_chg.generated"
        defconfig = ":oplus_chg_{}_defconfig".format(target)
        if version_compare(kernel_version, "6.12"):
            ddk_config = None

    ddk_srcs = [
        "pd_ext/pd_dbg_info.c",
    ]

    ddk_includes = [
        "pd_ext",
        "pd_ext/inc",
    ]

    ddk_hdrs = native.glob([
        "pd_ext/*.h",
        "pd_ext/inc/*.h",
    ])

    if version_compare(kernel_version, "6.12"):
        define_oplus_ddk_module(
            name = "{}_pd_dbg_info".format(target),
            out = "pd_dbg_info.ko",
            srcs = ddk_srcs,
            includes = ddk_includes,
            ko_deps = [],
            local_defines = [
                "CONFIG_RT_REGMAP",
                "OPLUS_FEATURE_CHG_BASIC",
            ],
            conditional_defines = {},
            hdrs = ddk_hdrs,
            kconfig = kconfig,
            defconfig = defconfig,
            config = ddk_config,
        )
    else:
        define_oplus_ddk_module(
            name = "{}_pd_dbg_info".format(target),
            out = "pd_dbg_info.ko",
            srcs = ddk_srcs,
            includes = ddk_includes,
            ko_deps = [],
            local_defines = [
                "CONFIG_RT_REGMAP",
                "OPLUS_FEATURE_CHG_BASIC",
            ],
            conditional_defines = {},
            hdrs = ddk_hdrs,
            kconfig = kconfig,
            defconfig = defconfig,
        )

    module_list.extend(filter_deps_map(target, {
        "CONFIG_TCPC_CLASS": "{}_pd_dbg_info".format(target),
    }))

    return module_list

def define_rt_regmap_module():
    module_list = []

    target = oplus_modules_get_target_variant()
    kernel_version = oplus_ddk_get_kernel_version()

    if bazel_support_platform == "qcom":
        kconfig = None
        defconfig = None
        if version_compare(kernel_version, "6.12"):
            ddk_config = "//soc-repo:{}_config".format(target)
    else:
        kconfig = ":kconfig.oplus_chg.generated"
        defconfig = ":oplus_chg_{}_defconfig".format(target)
        if version_compare(kernel_version, "6.12"):
            ddk_config = None

    ddk_srcs = [
        "pd_ext/rt-regmap.c",
    ]

    ddk_includes = [
        "pd_ext",
        "pd_ext/inc",
    ]

    ddk_hdrs = native.glob([
        "pd_ext/*.h",
        "pd_ext/inc/*.h",
    ])

    if version_compare(kernel_version, "6.12"):
        define_oplus_ddk_module(
            name = "{}_rt-regmap".format(target),
            out = "rt-regmap.ko",
            srcs = ddk_srcs,
            includes = ddk_includes,
            ko_deps = [],
            local_defines = [
                "CONFIG_RT_REGMAP",
                "OPLUS_FEATURE_CHG_BASIC",
            ],
            conditional_defines = {},
            hdrs = ddk_hdrs,
            kconfig = kconfig,
            defconfig = defconfig,
            config = ddk_config,
        )
    else:
        define_oplus_ddk_module(
            name = "{}_rt-regmap".format(target),
            out = "rt-regmap.ko",
            srcs = ddk_srcs,
            includes = ddk_includes,
            ko_deps = [],
            local_defines = [
                "CONFIG_RT_REGMAP",
                "OPLUS_FEATURE_CHG_BASIC",
            ],
            conditional_defines = {},
            hdrs = ddk_hdrs,
            kconfig = kconfig,
            defconfig = defconfig,
        )

    module_list.extend(filter_deps_map(target, {
        "CONFIG_TCPC_CLASS": "{}_rt_regmap".format(target),
    }))

    return module_list

def define_tcpc_rt1711h_module():
    module_list = []

    target = oplus_modules_get_target_variant()
    kernel_version = oplus_ddk_get_kernel_version()

    if bazel_support_platform == "qcom":
        kconfig = None
        defconfig = None
        if version_compare(kernel_version, "6.12"):
            ddk_config = "//soc-repo:{}_config".format(target)
    else:
        kconfig = ":kconfig.oplus_chg.generated"
        defconfig = ":oplus_chg_{}_defconfig".format(target)
        if version_compare(kernel_version, "6.12"):
            ddk_config = None

    ddk_srcs = [
        "pd_ext/tcpc_rt1711h.c",
    ]

    ddk_includes = [
        "pd_ext",
        "pd_ext/inc",
    ]

    ddk_hdrs = native.glob([
        "pd_ext/*.h",
        "pd_ext/inc/*.h",
    ])

    if version_compare(kernel_version, "6.12"):
        define_oplus_ddk_module(
            name = "{}_tcpc_rt1711h".format(target),
            out = "tcpc_rt1711h.ko",
            srcs = ddk_srcs,
            includes = ddk_includes,
            ko_deps = [
                "{}_tcpc_class".format(target),
                "{}_rt-regmap".format(target),
                "{}_pd_dbg_info".format(target),
            ],
            local_defines = [
                "CONFIG_RT_REGMAP",
                "OPLUS_FEATURE_CHG_BASIC",
            ],
            conditional_defines = {},
            hdrs = ddk_hdrs,
            kconfig = kconfig,
            defconfig = defconfig,
            config = ddk_config,
        )
    else:
        define_oplus_ddk_module(
            name = "{}_tcpc_rt1711h".format(target),
            out = "tcpc_rt1711h.ko",
            srcs = ddk_srcs,
            includes = ddk_includes,
            ko_deps = [
                "{}_tcpc_class".format(target),
                "{}_rt-regmap".format(target),
                "{}_pd_dbg_info".format(target),
            ],
            local_defines = [
                "CONFIG_RT_REGMAP",
                "OPLUS_FEATURE_CHG_BASIC",
            ],
            conditional_defines = {},
            hdrs = ddk_hdrs,
            kconfig = kconfig,
            defconfig = defconfig,
        )

    module_list.extend(filter_deps_map(target, {
        "CONFIG_TCPC_RT1711H": "{}_tcpc_rt1711h".format(target),
    }))

    return module_list


