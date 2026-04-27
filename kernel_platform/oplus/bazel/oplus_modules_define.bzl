load("//build/kernel/kleaf:kernel.bzl", "ddk_module")
load("//soc-repo:target_variants.bzl", "get_all_la_variants")
load(":oplus_modules_variant.bzl",
    "bazel_support_target",
    "bazel_support_variant",
    "LINUX_KERNEL_VERSION"
)
load(":oplus_modules_variant.bzl", "OPLUS_FEATURES")

bazel_support_platform = "qcom"
bazel_wifionly = None

def oplus_ddk_get_target():
    return bazel_support_target[0]

def oplus_ddk_get_variant():
    return bazel_support_variant[0]

def oplus_ddk_get_kernel_version():
    return LINUX_KERNEL_VERSION

"""
Convert environment variables prefixed with OPLUS_FEATURE_ into a dictionary
"""
def oplus_ddk_get_oplus_features():
    oplus_feature_list = {}
    for o in OPLUS_FEATURES.strip().split(' '):
        lst = o.split('=')
        if len(lst) != 2:
            # print('Error: environment variable [%s]' % o)
            continue
        oplus_feature_list[lst[0]] = lst[1]

    return oplus_feature_list


"""
Convert environment variables prefixed with OPLUS_FEATURE_ into a dictionary
"""
def get_oplus_features_as_list():
    oplus_features = []
    for o in OPLUS_FEATURES.strip().split(' '):
        lst = o.split('=')
        if len(lst) != 2:
            # print('Error: environment variable [%s]' % o)
            continue
        oplus_features.append(o)

    return oplus_features


def define_oplus_ddk_module(
    name,
    srcs = None,
    header_deps = [],
    ko_deps = [],
    hdrs = None,
    includes = None,
    conditional_srcs = None,
    conditional_defines = None,
    linux_includes = None,
    out = None,
    local_defines = None,
    kconfig = None,
    defconfig = None,
    copts = None,
    conditional_build = None,
    **kwargs):

    # Set default srcs if not provided
    # Default to glob all .c and .h files, excluding hidden files
    if srcs == None:
        srcs = native.glob(
            [
                "**/*.c",
                "**/*.h",
            ],
            exclude = [
                ".*",
                ".*/**",
            ],
        )

    # Set default output name if not provided
    if out == None:
        out = name + ".ko"

    # Initialize local_defines if needed
    if local_defines == None:
        local_defines = []

    # Merge platform-specific defines from conditional_defines into local_defines
    if conditional_defines:
        platform_defines = conditional_defines.get(bazel_support_platform)
        if platform_defines:
            local_defines = local_defines + platform_defines

    deps_all_headers = select({
        "//build/kernel/kleaf:socrepo_true": ["//soc-repo:all_headers"],
        "//build/kernel/kleaf:socrepo_false": ["//msm-kernel:all_headers"],
    })
    all_deps = deps_all_headers + header_deps + ko_deps

    for target in bazel_support_target:
        for variant in bazel_support_variant:
            kernel_build_variant = "{}_{}".format(target, variant)

            # Select the appropriate kernel build based on whether we're using soc-repo or msm-kernel
            kernel_build = select({
                "//build/kernel/kleaf:socrepo_true": "//soc-repo:{}_base_kernel".format(kernel_build_variant),
                "//build/kernel/kleaf:socrepo_false": "//msm-kernel:{}".format(kernel_build_variant),
            })

            ddk_module(
                name = name,
                srcs = srcs,
                out = out,
                local_defines = local_defines,
                copts = copts,
                includes = includes,
                conditional_srcs = conditional_srcs,
                linux_includes = linux_includes,
                hdrs = hdrs,
                deps = all_deps,
                kernel_build = kernel_build,
                kconfig = kconfig,
                defconfig = defconfig,
                visibility = ["//visibility:public"],
                **kwargs
            )


