load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load("//soc-repo:target_variants.bzl", "get_all_la_variants")
load(":oplus_modules_variant.bzl",
    "bazel_support_target",
    "bazel_support_variant"
)

load(":oplus_modules_define.bzl", "oplus_ddk_get_oplus_features")

def ddk_copy_to_dist_dir(
        name = None,
        module_list = [],
        conditional_builds = None):

    return
