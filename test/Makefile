# Makefile — host-тесты чистых модулей (gcc, без Arduino).
# Цели:
#   make test    — собрать и запустить все тесты
#   make build   — только собрать
#   make clean   — удалить бинарники

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
INC       = -I. -I../firmware/common/audio -I../firmware/common/transport \
            -I../firmware/common/util -I../firmware/common/config

TESTS = audio_filter_test transport_packet_test util_test udp_audio_receiver_test
BINS  = $(addsuffix _bin,$(TESTS))
DEPS  = $(addsuffix .d,$(TESTS))

.PHONY: all test build clean

all: build

build: $(BINS)

# Автозависимости от заголовков (MMD) + флаги из INC.
$(BINS): %_bin: %.cpp
	$(CXX) $(CXXFLAGS) $(INC) -MMD -MP $< -o $@ -lm

test: $(BINS)
	@set -e; for t in $(TESTS); do \
		echo ">>> $$t"; ./$$t"_bin"; \
	done
	@echo ">>> config_gen_test"; python3 config_gen_test.py

clean:
	rm -f $(BINS) $(DEPS)

-include $(DEPS)
