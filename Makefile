# SPDX-License-Identifier: LGPL-2.1-or-later
CC = cc
CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -g
WARNINGS = -std=c11 -Wall -Wextra -Wpedantic -Werror
BUILD ?= build

SOURCES = src/mode.c src/protocol.c src/decode.c src/readonly.c src/linux.c src/diagnostic.c src/supervisor.c src/json.c
TESTS = $(BUILD)/test-mode $(BUILD)/test-protocol $(BUILD)/test-decode $(BUILD)/test-readonly $(BUILD)/test-linux $(BUILD)/test-diagnostic $(BUILD)/test-json

.PHONY: all test clean
all: $(BUILD)/cros-fingerprint-admin $(BUILD)/test-admin-driver $(BUILD)/test-events $(BUILD)/cros-fingerprint-prepare $(BUILD)/test-prepare $(BUILD)/test-provision $(BUILD)/test-store $(BUILD)/test-registry $(BUILD)/test-binding $(BUILD)/cros-fingerprint-seed-context-test $(BUILD)/test-smoke $(BUILD)/test-restore $(BUILD)/test-context $(BUILD)/test-state $(BUILD)/test-seed $(TESTS) $(BUILD)/cros-fingerprint-diagnostic

$(BUILD)/test-%: $(SOURCES) tests/test_%.c include/cros_fp.h include/cros_fp_linux.h include/cros_fp_diagnostic.h include/cros_fp_json.h Makefile
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(SOURCES) tests/test_$*.c $(LDFLAGS) -o $@

$(BUILD)/cros-fingerprint-diagnostic: $(SOURCES) src/main.c include/*.h Makefile
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(SOURCES) src/main.c $(LDFLAGS) -o $@

$(BUILD)/test-seed: src/seed.c tests/test_seed.c include/cros_fp_seed.h include/cros_fp_provision.h Makefile
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) src/seed.c tests/test_seed.c $(LDFLAGS) -lcrypto -o $@

$(BUILD)/test-state: src/state.c tests/test_state.c include/cros_fp_state.h include/cros_fp_seed.h Makefile
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) src/state.c tests/test_state.c $(LDFLAGS) -o $@

$(BUILD)/test-context: src/context.c src/state.c tests/test_context.c include/cros_fp_context.h include/cros_fp_state.h include/cros_fp_seed.h include/cros_fp.h Makefile
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) src/context.c src/state.c tests/test_context.c $(LDFLAGS) -o $@

$(BUILD)/test-restore: src/restore.c src/seed.c src/state.c src/readonly.c src/decode.c tests/test_restore.c include/*.h Makefile
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) src/restore.c src/seed.c src/state.c src/readonly.c src/decode.c tests/test_restore.c $(LDFLAGS) -lcrypto -o $@

SMOKE_SOURCES = $(SOURCES) src/seed.c src/state.c src/restore.c src/context.c src/smoke.c

$(BUILD)/test-smoke: $(SMOKE_SOURCES) tests/test_smoke.c include/*.h Makefile
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(SMOKE_SOURCES) tests/test_smoke.c $(LDFLAGS) -lcrypto -o $@

$(BUILD)/cros-fingerprint-seed-context-test: $(SMOKE_SOURCES) src/experiment_main.c include/*.h Makefile
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(SMOKE_SOURCES) src/experiment_main.c $(LDFLAGS) -lcrypto -o $@

test: all
	python3 tests/test_admin_cli.py $(BUILD)
	./$(BUILD)/test-events
	./$(BUILD)/test-prepare
	./$(BUILD)/test-provision
	./$(BUILD)/test-store
	./$(BUILD)/test-registry
	./$(BUILD)/test-binding
	./$(BUILD)/test-smoke
	./$(BUILD)/test-restore
	./$(BUILD)/test-context
	./$(BUILD)/test-state
	./$(BUILD)/test-seed
	./$(BUILD)/test-mode
	./$(BUILD)/test-protocol
	./$(BUILD)/test-decode
	./$(BUILD)/test-readonly
	./$(BUILD)/test-linux
	./$(BUILD)/test-diagnostic
	python3 tests/test_cli.py $(BUILD)
	python3 tests/test_experiment_cli.py $(BUILD)
	python3 tests/test_prepare_cli.py $(BUILD)

clean:
	rm -rf build build-sanitize

$(BUILD)/test-binding: src/binding.c tests/test_binding.c include/cros_fp_binding.h include/cros_fp.h Makefile
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) src/binding.c tests/test_binding.c $(LDFLAGS) -o $@

$(BUILD)/test-registry: src/registry.c src/seed.c src/binding.c tests/test_registry.c include/cros_fp_registry.h include/cros_fp_binding.h include/cros_fp_seed.h include/cros_fp_provision.h Makefile
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) src/registry.c src/seed.c src/binding.c tests/test_registry.c $(LDFLAGS) -lcrypto -o $@

$(BUILD)/test-provision: src/seed.c tests/test_provision.c include/cros_fp_seed.h include/cros_fp_provision.h Makefile
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) src/seed.c tests/test_provision.c $(LDFLAGS) -lcrypto -o $@

$(BUILD)/test-store: src/store.c src/seed.c src/registry.c src/state.c src/context.c tests/test_store.c include/*.h Makefile
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) src/store.c src/seed.c src/registry.c src/state.c src/context.c tests/test_store.c $(LDFLAGS) -lcrypto -o $@

PREPARE_SOURCES = $(SMOKE_SOURCES) src/registry.c src/store.c src/prepare.c
$(BUILD)/test-prepare: $(PREPARE_SOURCES) tests/test_prepare.c include/*.h Makefile
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(PREPARE_SOURCES) tests/test_prepare.c $(LDFLAGS) -lcrypto -o $@

$(BUILD)/test-linux: $(SOURCES) tests/test_linux.c include/*.h Makefile
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(SOURCES) tests/test_linux.c $(LDFLAGS) -Wl,--wrap=free -o $@

$(BUILD)/cros-fingerprint-prepare: $(PREPARE_SOURCES) src/prepare_main.c include/*.h Makefile
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(PREPARE_SOURCES) src/prepare_main.c $(LDFLAGS) -lcrypto -o $@

$(BUILD)/test-events: src/events.c tests/test_events.c include/cros_fp_events.h include/cros_fp.h Makefile
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) src/events.c tests/test_events.c $(LDFLAGS) -o $@

ADMIN_SOURCES = src/admin.c src/store.c src/seed.c src/registry.c src/state.c src/supervisor.c
$(BUILD)/cros-fingerprint-admin: $(ADMIN_SOURCES) src/admin_main.c include/*.h Makefile
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(ADMIN_SOURCES) src/admin_main.c $(LDFLAGS) -lcrypto -o $@

$(BUILD)/test-admin-driver: $(ADMIN_SOURCES) tests/admin_driver.c include/*.h Makefile
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(ADMIN_SOURCES) tests/admin_driver.c $(LDFLAGS) -lcrypto -o $@
