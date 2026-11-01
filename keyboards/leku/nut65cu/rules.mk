# Shared NUT65 code (both models): keyboards/leku/nut65_shared/
VPATH += keyboards/leku/nut65_shared

# Run-time debounce time for Fn+D (see hs_debounce.c).
DEBOUNCE_TYPE = custom
SRC += hs_debounce.c
SRC += cu_features.c
SRC += cu_store.c cu_log.c cu_keys.c cu_profile.c cu_host.c cu_mode.c cu_overlay.c cu_egg.c cu_barfx.c
