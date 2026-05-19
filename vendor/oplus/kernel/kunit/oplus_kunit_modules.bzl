load("@rules_pkg//pkg:install.bzl", "pkg_install")
load("@rules_pkg//pkg:mappings.bzl", "pkg_filegroup", "pkg_files")
load("@rules_pkg//pkg:pkg.bzl", "pkg_zip")

_KUNIT_DIR = "kunit_tests/testcases/oplus_kunit"

_DEFAULT_KUNIT_MODULES = [
    "//vendor/oplus/kernel/boot:oplus_kunit_example_test_module",
]

def define_oplus_kunit_ddk_modules(**kwargs):
    name = kwargs.get("name", "oplus_kunit")
    arch = kwargs.get("arch", "arm64")
    module_list = kwargs.get("module_list", _DEFAULT_KUNIT_MODULES)

    # 创建配置文件
    pkg_files(
        name = name + "_config",
        srcs = [
            "config_arm64.xml",
        ],
        renames = {
            "config_arm64.xml": _KUNIT_DIR + "/kunit.config",
        },
        visibility = ["//visibility:private"],
    )

    pkg_files(
        name = name + "_kunit_framework_modules",
        srcs = [
            "//common:kunit_modules_{}".format(arch),
        ],
        prefix = _KUNIT_DIR,
        visibility = ["//visibility:private"],
    )

    module_ko_targets = []
    for i, module_target in enumerate(module_list):
        module_name = module_target.split(":")[-1]

        native.genrule(
            name = "{}_extract_ko_{}".format(name, i),
            srcs = [module_target],
            outs = ["{}.ko".format(module_name)],
            cmd = """
            for file in $(locations {module_target}); do
                if [[ "$$file" == *".ko" ]]; then
                    cp "$$file" "$@"
                    break
                fi
            done
            """.format(module_target = module_target),
            visibility = ["//visibility:private"],
        )

        pkg_files(
            name = "{}_module_{}".format(name, i),
            srcs = [":{}_extract_ko_{}".format(name, i)],
            prefix = _KUNIT_DIR,
            visibility = ["//visibility:private"],
        )
        module_ko_targets.append(":{}_module_{}".format(name, i))

    pkg_filegroup(
        name = name + "_pkg_files",
        srcs = [
            ":" + name + "_config",
            ":" + name + "_kunit_framework_modules",
        ] + module_ko_targets,
        visibility = ["//visibility:private"],
    )

    pkg_install(
        name = name + "_install",
        srcs = [
            ":" + name + "_pkg_files",
        ],
        visibility = ["//visibility:private"],
    )

    pkg_zip(
        name = name + "_zip_arm64",
        srcs = [
            ":" + name + "_pkg_files",
        ],
        out = "oplus_kunit/kunit_tests.zip",
        visibility = ["//visibility:public"],
    )
