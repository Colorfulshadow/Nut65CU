MCU_LDSCRIPT = WB32FQ95_STAGE
USE_CHIBIOS_CONTRIB = yes
RGB_MATRIX_CUSTOM_USER = yes
RGBLIGHT_DRIVER = custom

include keyboards/leku/nut65_shared/rgb_record/rgb_record.mk
include keyboards/leku/nut65_shared/wls/wls.mk
include keyboards/linker/wireless/wireless.mk
