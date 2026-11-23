################################################################################
#
# sedutil PBA additions
#
# Buildroot's own sedutil package is used, with its source replaced by this
# repository (see SEDUTIL_OVERRIDE_SRCDIR in the local.mk that build.sh
# writes). This hook stamps the real version into the binaries, because an
# overridden package is only known to Buildroot as version "custom".
#
################################################################################

SEDUTIL_PBA_VERSION ?= custom

define SEDUTIL_PBA_SET_VERSION
	echo '#define GIT_VERSION "$(SEDUTIL_PBA_VERSION)"' > $(@D)/linux/Version.h
endef
SEDUTIL_POST_CONFIGURE_HOOKS += SEDUTIL_PBA_SET_VERSION
