CANOE_BASE_DTB += ../qcom/canoe.dtb ../qcom/canoe-v2.dtb ../qcom/canoe-tp.dtb ../qcom/canoe-tp-v2.dtb ../qcom/alor.dtb \
	../qcom/alor-interposer.dtb ../qcom/alor-interposer-v2.dtb ../qcom/alorp.dtb
CANOE_APQ_BASE_DTB += ../qcom/canoep.dtb ../qcom/canoep-v2.dtb ../qcom/canoep-tp.dtb ../qcom/canoep-tp-v2.dtb

CANOE_BOARDS += \
		infiniti-24831-canoe-overlay.dtbo \
		infiniti-24863-canoe-overlay.dtbo \
		lafa-24624-canoe-overlay.dtbo \
		swangoose-25001-canoe-overlay.dtbo \
		swangoose-25001-canoe-overlay-evb.dtbo \
		swangoose-25001-canoe-overlay-pret0.dtbo \
		swangoose-25001-canoe-overlay-evt.dtbo \
		swangoose-25201-canoe-overlay.dtbo \
		swangoose-25201-canoe-overlay-evt.dtbo \
		fairlady-25821-canoe-overlay.dtbo \
		fairlady-25871-canoe-overlay.dtbo \
		macan-24855-alor-overlay.dtbo \
		macan-24877-alor-overlay-IN.dtbo \
		macan-24877-alor-overlay-EU-US.dtbo \
		macan-24855-alor-overlay-dvt2.dtbo \
		u9-25602-alor-overlay.dtbo \
		u9-25673-alor-overlay.dtbo \
		u9p-25709-alor-overlay.dtbo \
		dengta-25021-canoe-overlay.dtbo \
		dengta-25211-canoe-overlay.dtbo \
		iceland-25926-canoe-overlay.dtbo \
		iceland-25927-canoe-overlay.dtbo \
		iceland-25978-canoe-overlay-EU.dtbo \
		iceland-25978-canoe-overlay-IN.dtbo \
		ivy-25928-alor-overlay.dtbo \
		ivy-25929-alor-overlay.dtbo \
		ivy-25980-alor-overlay-EU.dtbo \
		ivy-25980-alor-overlay-IN.dtbo \

dtb-y += $(CANOE_BASE_DTB) $(CANOE_APQ_BASE_DTB) $(CANOE_BOARDS)

always-y        := $(dtb-y)
subdir-y        := $(dts-dirs)
clean-files     := *.dtb *.dtbo
