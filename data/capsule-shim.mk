# allow stub file generation to be quiet or verbose per the value of V
V         ?= 0
GENSTUB_V1 =
GENSTUB_V0 = @echo "  GENSTUB " $@;
GENSTUB    = $(GENSTUB_V$(V))

CAPSULE_TREE ?= /host
CAPSULE_RUNTIME_TREE ?= $(CAPSULE_TREE)
CAPSULE_SEARCH_TREE ?= $(CAPSULE_TREE)

# regenerate if any dependencies get updated:
shim/%.c: $(srcdir)/shim/%.excluded $(srcdir)/shim/%.shared $(srcdir)/shim/%.symbols
	$(GENSTUB)V=$V \
		$(CAPSULE_MKSTUBLIB_TOOL) \
		--no-update-symbols \
		--symbols-from=$(srcdir)/shim/$*.symbols \
		$(AM_CAPSULE_MKSTUBLIB_FLAGS) \
		$(CAPSULE_MKSTUBLIB_FLAGS) \
		$* \
		$(srcdir)/shim/$*.excluded \
		$(srcdir)/shim/$*.shared \
		$@ \
		$(subst .,:,$(CAPSULE_VERSION)) \
		$(CAPSULE_RUNTIME_TREE)

# error out when it's time to regenerate the exportable symbols list
# (we do not do this automatically because it's part of our ABI)
$(srcdir)/shim/%.symbols: $(srcdir)/shim/%.shared
	@if cmp -s $< $@.updated-for; then \
		touch $@; \
	else \
		echo "*** ERROR: List of shared libraries has changed" >&2; \
		echo "*** Run: make maintainer-update-capsule-symbols [CAPSULE_SEARCH_TREE=/some-sysroot]" >&2; \
		exit 1; \
	fi

maintainer-update-capsule-symbols: always
	@true
.PHONY: maintainer-update-capsule-symbols

maintainer-update-capsule-symbols: maintainer-update-capsule-symbols/$(CAPSULE_SONAME)

maintainer-update-capsule-symbols/%: always
	$(AM_V_GEN)set -e; \
	shared=$(srcdir)/shim/$*.shared; \
	out=$(srcdir)/shim/$*.symbols; \
	rm -f $$out.updated-for; \
	for dso in $* $$(cat "$$shared"); \
	do \
		$(CAPSULE_SYMBOLS_TOOL) $$dso $(CAPSULE_SEARCH_TREE); \
	done > "$$out.tmp"; \
	LC_ALL=C sort -u "$$out.tmp" > "$$out.tmp2"; \
	rm -f "$$out.tmp"; \
	mv "$$out.tmp2" "$$out"; \
	cp $(srcdir)/shim/$*.shared $$out.updated-for

.PHONY: always
