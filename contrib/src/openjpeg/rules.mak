# jpeg

OPENJPEG_VERSION := 2.3.0
OPENJPEG_URL := https://github.com/uclouvain/openjpeg/archive/v$(OPENJPEG_VERSION).tar.gz

$(TARBALLS)/openjpeg-v$(OPENJPEG_VERSION).tar.gz:
	$(call download_pkg,v$(OPENJPEG_URL),openjpeg)

.sum-openjpeg: openjpeg-v$(OPENJPEG_VERSION).tar.gz

openjpeg: openjpeg-v$(OPENJPEG_VERSION).tar.gz .sum-openjpeg
	$(UNPACK)
	mv openjpeg-$(OPENJPEG_VERSION) openjpeg-v$(OPENJPEG_VERSION)
ifdef HAVE_VISUALSTUDIO
#	$(APPLY) $(SRC)/openjpeg/msvc.patch
endif
#	$(APPLY) $(SRC)/openjpeg/restrict.patch
	$(call pkg_static,"./src/lib/openjp2/libopenjp2.pc.cmake.in")
	$(MOVE)

.openjpeg: openjpeg
	cd $< && $(HOSTVARS) $(CMAKE) \
		-DBUILD_SHARED_LIBS=OFF -DBUILD_PKGCONFIG_FILES=ON \
		.
	cd $< && $(MAKE) install
	touch $@
