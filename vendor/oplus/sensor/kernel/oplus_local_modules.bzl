load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module", "oplus_ddk_get_target", "oplus_ddk_get_variant")

def define_oplus_local_modules():
    target = oplus_ddk_get_target()
    variant  = oplus_ddk_get_variant()
    kernel_build_variant = "{}_{}".format(target, variant)
    bazel_support_target = oplus_ddk_get_target()

    # Create headers for oplus_trace_sensor_err (always available)
    ddk_headers(
        name = "oplus_trace_sensor_err_headers",
        hdrs = native.glob([
            "oplus_sensor_err/*.h",
        ]),
        includes = ["oplus_sensor_err"],
    )

    # Define oplus_trace_sensor_err module (common for both QCOM and MTK)
    define_oplus_ddk_module(
        name = "oplus_trace_sensor_err",
        srcs = native.glob([
            "**/*.h",
            "oplus_sensor_err/oplus_trace_sensor_err.c",
        ]),
        includes = ["oplus_sensor_err", "."],
        local_defines = [
            "CONFIG_OPLUS_FEATURE_TRACE_SENSOR_ERR=1",
        ],
        conditional_defines = {
            "qcom": ["CFG_OPLUS_ARCH_IS_QCOM"],
            "mtk": ["CFG_OPLUS_ARCH_IS_MTK"],
        },
    )

