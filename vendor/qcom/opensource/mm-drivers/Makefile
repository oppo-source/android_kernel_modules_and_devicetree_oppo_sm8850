# SPDX-License-Identifier: GPL-2.0-only

CUR_MKFILE = $(abspath $(lastword $(MAKEFILE_LIST)))
MM_ROOT = $(dir $(CUR_MKFILE))
KBUILD_OPTIONS := MSM_HFI_CORE_ROOT=$(MM_ROOT)
KBUILD_OPTIONS += MSM_HW_FENCE_ROOT=$(MM_ROOT)
KBUILD_OPTIONS += MSM_EXT_DISPLAY_ROOT=$(MM_ROOT)
KBUILD_OPTIONS += SYNC_FENCE_ROOT=$(MM_ROOT)

obj-m += hw_fence/
obj-m += msm_ext_display/
obj-m += sync_fence/


all:
	$(MAKE) -C $(KERNEL_SRC) M=$(shell pwd) modules $(KBUILD_OPTIONS)

modules_install:
	$(MAKE) INSTALL_MOD_STRIP=1 -C $(KERNEL_SRC) M=$(shell pwd) modules_install


clean:
	rm -f *.o *.ko *.mod.c *.mod.o *~ .*.cmd Module.symvers
	rm -rf .tmp_versions
