# Rust's linker adapter consumes the same SDK contract as C applications.
include $(PYXIS_RUST_SDK)/share/pyxis.mk
.PHONY: print-rust-link
print-rust-link:
	@printf '%s\n' '$(CC)' '$(PYXIS_LDFLAGS)' '$(PYXIS_START)' '$(PYXIS_LDLIBS)'
