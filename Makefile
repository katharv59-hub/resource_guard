CC     = gcc
WARN   = -Wall -Wextra -Wpedantic -Wshadow
CFLAGS = -std=c11 -g -O2 $(WARN) -Iengine
ENGINE = engine/banker.c engine/scenario.c
HDRS   = engine/banker.h

# Threading module (POSIX threads — Linux only)
THREAD = engine/threading.c
THDR   = engine/threading.h

# GUI module (GTK4 — Linux only)
GTK_CFLAGS ?= $(shell pkg-config --cflags gtk4 2>/dev/null)
GTK_LIBS   ?= $(shell pkg-config --libs gtk4 2>/dev/null)
GUI_SRCS    = gui/main.c gui/app.c gui/window.c gui/pages.c gui/style.c gui/smoke.c gui/model.c gui/safety_view.c gui/request_view.c
GUI_HDRS    = gui/app.h gui/window.h gui/pages.h gui/style.h gui/smoke.h gui/model.h gui/safety_view.h gui/request_view.h
GUI_BIN     = resourceguard_gui

all: banker_cli test_banker

banker_cli: cli/main.c $(ENGINE) $(HDRS)
	$(CC) $(CFLAGS) cli/main.c $(ENGINE) -o $@

test_banker: tests/test_banker.c $(ENGINE) $(HDRS)
	$(CC) $(CFLAGS) tests/test_banker.c $(ENGINE) -o $@

# Run from the project root: tests load tests/scenarios/*.txt
test: test_banker
	./test_banker

# Undefined behaviour (can run separately from AddressSanitizer)
ubsan:
	$(CC) $(CFLAGS) -O1 -fsanitize=undefined -fno-sanitize-recover=all tests/test_banker.c $(ENGINE) -o test_banker_ubsan
	./test_banker_ubsan

# Memory errors and undefined behaviour
sanitize:
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all tests/test_banker.c $(ENGINE) -o test_banker_asan
	./test_banker_asan

demo: banker_cli
	./banker_cli tests/scenarios/demo.txt

# --- Threading targets (Linux only, requires -pthread) ---

test_threading: tests/test_threading.c $(ENGINE) $(THREAD) $(HDRS) $(THDR)
	$(CC) $(CFLAGS) -pthread tests/test_threading.c $(ENGINE) $(THREAD) -o $@

test_threads: test_threading
	./test_threading

test_threads_ubsan:
	$(CC) $(CFLAGS) -O1 -pthread -fsanitize=undefined -fno-sanitize-recover=all \
		tests/test_threading.c $(ENGINE) $(THREAD) -o test_threading_ubsan
	./test_threading_ubsan

# ThreadSanitizer (separate from UBSan — TSan is incompatible with ASan)
tsan:
	$(CC) $(CFLAGS) -O1 -pthread -fsanitize=thread -fno-sanitize-recover=all \
		tests/test_threading.c $(ENGINE) $(THREAD) -o test_threading_tsan
	./test_threading_tsan

test_threads_sanitize:
	$(CC) $(CFLAGS) -pthread -fsanitize=address,undefined -fno-sanitize-recover=all \
		tests/test_threading.c $(ENGINE) $(THREAD) -o test_threading_asan
	./test_threading_asan

# --- Application Model / State Controller target (pure C11) ---
test_model: tests/test_model.c gui/model.c $(ENGINE) $(HDRS) gui/model.h
	$(CC) $(CFLAGS) -Igui tests/test_model.c gui/model.c $(ENGINE) -o $@
	./test_model

# --- GTK4 Desktop Application target (Linux only) ---
gui: $(GUI_BIN)

$(GUI_BIN): $(GUI_SRCS) $(GUI_HDRS) $(ENGINE) $(THREAD) $(HDRS) $(THDR)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -pthread $(GUI_SRCS) $(ENGINE) $(THREAD) $(GTK_LIBS) -o $@

test_gui: $(GUI_BIN)
	xvfb-run -s "-screen 0 1280x800x24" ./$(GUI_BIN) --smoke-test

clean:
	rm -f banker_cli test_banker test_banker_asan test_banker_ubsan \
	      test_threading test_threading_asan test_threading_ubsan test_threading_tsan \
	      test_model $(GUI_BIN)

.PHONY: all test ubsan sanitize demo clean gui test_gui test_model \
        test_threads test_threads_ubsan tsan test_threads_sanitize
