# host-тест: компилирует все src/ для x86_64 (хост), линкует с host_test.cpp
# и прогоняет проверку ELF/PLT/сигнатур на файле игры.
#
#   make test GAME_SO=/путь/libblackrussia-client.so
#
ZIG     ?= $(shell command -v zig 2>/dev/null || echo /tmp/zig15/lib/python3.11/site-packages/ziglang/zig)
GAME_SO ?= ../reference/libblackrussia-client.so

TOP  := ..
BUILD:= build
TGT  := x86_64-linux-gnu
CFL  := -target $(TGT) -O1 -fno-stack-protector -D_GNU_SOURCE -I$(TOP)/src -I$(TOP)/include -I$(TOP)/third_party/imgui

SRC  := $(wildcard $(TOP)/src/*.c $(TOP)/src/*.cpp $(TOP)/third_party/imgui/*.cpp)

.PHONY: test clean

test:
	@mkdir -p $(BUILD)
	@for f in $(SRC); do \
	  b=$$(basename $$f); n=$${b%.cpp}; n=$${n%.c}; o=$(BUILD)/$$n.o; \
	  case "$$f" in \
	    *.cpp) $(ZIG) c++ $(CFL) -std=c++17 -fno-exceptions -fno-rtti -c $$f -o $$o ;; \
	    *.c)   $(ZIG) cc  $(CFL) -c $$f -o $$o ;; \
	  esac || exit 1; \
	done
	@$(ZIG) c++ $(CFL) -std=c++17 -fno-exceptions -fno-rtti -c host_test.cpp -o $(BUILD)/host_test.o
	@$(ZIG) c++ -target $(TGT) -o $(BUILD)/br_host_test $(BUILD)/*.o
	@echo "== запуск host-теста =="
	$(BUILD)/br_host_test $(abspath $(GAME_SO))

clean:
	rm -rf $(BUILD)
