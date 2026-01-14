# Makefile for JC4880P443C Examples
# Example projects for GUITION JC4880P443C (ESP32-P4 + ESP32-C6) smart display

.PHONY: all build upload flash clean format check monitor size erase list help

# Examples directory
EXAMPLES_DIR := examples

# List of all examples
EXAMPLES := $(notdir $(wildcard $(EXAMPLES_DIR)/*))

# Default target
all: help

## Build:

build: ## Build an example (EXAMPLE=name required)
ifndef EXAMPLE
	$(error EXAMPLE is required. Usage: make build EXAMPLE=01_display_basic)
endif
	cd $(EXAMPLES_DIR)/$(EXAMPLE) && pio run

build-all: ## Build all examples
	@for ex in $(EXAMPLES); do \
		echo "Building $$ex..."; \
		cd $(EXAMPLES_DIR)/$$ex && pio run && cd ../..; \
	done

## Upload:

upload: ## Build and flash an example (EXAMPLE=name required)
ifndef EXAMPLE
	$(error EXAMPLE is required. Usage: make upload EXAMPLE=01_display_basic)
endif
	cd $(EXAMPLES_DIR)/$(EXAMPLE) && pio run --target upload

flash: upload ## Alias for upload

## Clean:

clean: ## Clean build artifacts for an example (EXAMPLE=name required)
ifndef EXAMPLE
	$(error EXAMPLE is required. Usage: make clean EXAMPLE=01_display_basic)
endif
	cd $(EXAMPLES_DIR)/$(EXAMPLE) && pio run --target clean

clean-all: ## Clean all examples build artifacts
	@for ex in $(EXAMPLES); do \
		echo "Cleaning $$ex..."; \
		cd $(EXAMPLES_DIR)/$$ex && pio run --target clean && cd ../..; \
	done

## Code Quality:

format: ## Format code with clang-format
	@echo "Formatting examples..."
	@find $(EXAMPLES_DIR) -path "*/.pio" -prune -o \( -name "*.cpp" -o -name "*.h" \) -print | xargs clang-format -i
	@echo "Done."

check: ## Run static analysis on an example (EXAMPLE=name required)
ifndef EXAMPLE
	$(error EXAMPLE is required. Usage: make check EXAMPLE=01_display_basic)
endif
	cd $(EXAMPLES_DIR)/$(EXAMPLE) && pio check

## Monitor:

monitor: ## Open serial monitor (EXAMPLE=name required)
ifndef EXAMPLE
	$(error EXAMPLE is required. Usage: make monitor EXAMPLE=01_display_basic)
endif
	cd $(EXAMPLES_DIR)/$(EXAMPLE) && pio device monitor

## Size:

size: ## Show firmware size (EXAMPLE=name required)
ifndef EXAMPLE
	$(error EXAMPLE is required. Usage: make size EXAMPLE=01_display_basic)
endif
	cd $(EXAMPLES_DIR)/$(EXAMPLE) && pio run --target size

## Erase:

erase: ## Erase device flash (EXAMPLE=name required)
ifndef EXAMPLE
	$(error EXAMPLE is required. Usage: make erase EXAMPLE=01_display_basic)
endif
	cd $(EXAMPLES_DIR)/$(EXAMPLE) && pio run --target erase

## Info:

list: ## List all available examples
	@echo "Available examples:"
	@echo ""
	@for ex in $(EXAMPLES); do \
		echo "  $$ex"; \
	done
	@echo ""
	@echo "Usage: make build EXAMPLE=01_display_basic"

## Development:

init: ## Initialize development environment
	@echo "Installing PlatformIO..."
	pip install platformio
	@echo "Development environment ready."

docs: ## Open documentation
	@if command -v xdg-open > /dev/null; then \
		xdg-open docs/architecture.md; \
	elif command -v open > /dev/null; then \
		open docs/architecture.md; \
	else \
		echo "Open docs/architecture.md manually"; \
	fi

## Help:

help: ## Show this help
	@echo "JC4880P443C Examples - Build System"
	@echo ""
	@echo "Usage: make [target] EXAMPLE=<example_name>"
	@echo ""
	@echo "Examples:"
	@for ex in $(EXAMPLES); do \
		echo "  $$ex"; \
	done
	@echo ""
	@awk 'BEGIN {FS = ":.*##"; section=""} \
		/^##/ { section=substr($$0, 4); next } \
		/^[a-zA-Z_-]+:.*##/ { \
			if (section != "") { printf "\n\033[1m%s\033[0m\n", section; section="" } \
			printf "  \033[36m%-15s\033[0m %s\n", $$1, $$2 \
		}' $(MAKEFILE_LIST)
	@echo ""
	@echo "Example:"
	@echo "  make build EXAMPLE=01_display_basic"
	@echo "  make upload EXAMPLE=04_wifi_scan"
	@echo "  make monitor EXAMPLE=01_display_basic"
