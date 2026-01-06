.PHONY: all build run backtest clean test

# Default target: build and run ticker
all: build run

# Build the project
build:
	@mkdir -p build
	@cd build && cmake .. && cmake --build .

# Run the ticker with multi-strategy trading
run:
	@./build/ticker --strategies

# Run backtesting
backtest: build
	@./build/backtest

# Clean build artifacts
clean:
	rm -rf build

# Run tests (placeholder for now)
test: build
	@cd build && ctest --output-on-failure
