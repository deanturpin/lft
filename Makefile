.PHONY: all build run lft ticker backtest clean test

# Default target: build and run lft (calibrate + live)
all: build lft

# Build the project
build:
	@mkdir -p build
	@cd build && cmake .. && cmake --build .

# Run unified calibrate-and-execute
lft: build
	@./build/lft

# Run the ticker with multi-strategy trading (legacy)
ticker: build
	@./build/ticker --strategies

# Run standalone backtesting
backtest: build
	@./build/backtest

# Alias for lft
run: lft

# Clean build artifacts
clean:
	rm -rf build

# Run tests (placeholder for now)
test: build
	@cd build && ctest --output-on-failure
