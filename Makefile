.PHONY: all build run clean test

# Default target: build and run
all: build run

# Build the project
build:
	@mkdir -p build
	@cd build && cmake .. && cmake --build .

# Run the trading system
run: build
	@build/lft

# Run tests
test: build
	@cd build && ctest --output-on-failure

# Clean build artifacts
clean:
	rm -rf build
