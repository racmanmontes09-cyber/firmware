#!/bin/bash
# LEAF Firmware Performance Benchmark Runner
# This script builds and analyzes the firmware's performance characteristics

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/.pio/build/4d_systems_esp32s3_gen4_r8n16"

echo "========================================"
echo "  LEAF FIRMWARE PERFORMANCE BENCHMARK"
echo "========================================"
echo ""

# Build the firmware
echo "Building firmware..."
BUILD_START=$(date +%s%N)
BUILD_OUTPUT=$(pio run -d "$SCRIPT_DIR" 2>&1)
BUILD_END=$(date +%s%N)
BUILD_TIME_MS=$(( (BUILD_END - BUILD_START) / 1000000 ))

# Check if build succeeded
if echo "$BUILD_OUTPUT" | grep -q "SUCCESS"; then
    echo "Build completed successfully in ${BUILD_TIME_MS}ms"
else
    echo "Build failed after ${BUILD_TIME_MS}ms"
    echo "$BUILD_OUTPUT" | tail -20
    exit 1
fi
echo ""

# Extract RAM and Flash usage from build output
RAM_LINE=$(echo "$BUILD_OUTPUT" | grep "RAM:" || true)
FLASH_LINE=$(echo "$BUILD_OUTPUT" | grep "Flash:" || true)

echo "=== Binary Size Analysis ==="
echo "RAM Usage:  $RAM_LINE"
echo "Flash Usage: $FLASH_LINE"
echo ""

# Analyze object files for size breakdown
echo "=== Application Source Files ==="
echo "File                          Size (bytes)"
echo "-------------------------------------------"

# Get sizes of application source files
if [[ -d "$BUILD_DIR/src" ]]; then
    for src_file in "$BUILD_DIR"/src/*.o; do
        if [[ -f "$src_file" ]]; then
            filename=$(basename "$src_file" .o)
            size=$(stat -c%s "$src_file" 2>/dev/null || echo "0")
            printf "%-30s %10s\n" "$filename.cpp.o" "$size"
        fi
    done
else
    echo "No source object files found"
fi

echo ""
echo "=== Library Dependencies ==="
echo "Library                      Size (bytes)"
echo "-------------------------------------------"

# Get sizes of library files
if [[ -d "$BUILD_DIR" ]]; then
    for lib_dir in "$BUILD_DIR"/lib*/; do
        if [[ -d "$lib_dir" ]]; then
            lib_name=$(basename "$lib_dir")
            total_size=0
            for obj_file in "$lib_dir"/*.o; do
                if [[ -f "$obj_file" ]]; then
                    file_size=$(stat -c%s "$obj_file" 2>/dev/null || echo "0")
                    total_size=$((total_size + file_size))
                fi
            done
            # Also check for .a files
            for archive_file in "$lib_dir"/*.a; do
                if [[ -f "$archive_file" ]]; then
                    file_size=$(stat -c%s "$archive_file" 2>/dev/null || echo "0")
                    total_size=$((total_size + file_size))
                fi
            done
            if [[ $total_size -gt 0 ]]; then
                printf "%-30s %10s\n" "$lib_name" "$total_size"
            fi
        fi
    done
fi

echo ""
echo "=== Framework Files ==="
echo "File                          Size (bytes)"
echo "-------------------------------------------"

# Get sizes of framework files
if [[ -d "$BUILD_DIR/FrameworkArduino" ]]; then
    for src_file in "$BUILD_DIR"/FrameworkArduino/*.o; do
        if [[ -f "$src_file" ]]; then
            filename=$(basename "$src_file" .o)
            size=$(stat -c%s "$src_file" 2>/dev/null || echo "0")
            printf "%-30s %10s\n" "$filename" "$size"
        fi
    done
fi

echo ""
echo "=== Performance Characteristics ==="
echo ""
echo "Estimated Loop Frequency:"
echo "  - Safety control interval: 1000ms (1 Hz)"
echo "  - Sensor sampling interval: 500ms (2 Hz)"
echo "  - MQTT status heartbeat: 4000ms (0.25 Hz)"
echo "  - Telemetry flush interval: 4000ms (0.25 Hz)"
echo ""
echo "Sensor Read Latency (estimated from ADC configuration):"
echo "  - pH sensor: 5 samples × 1ms delay = ~5ms"
echo "  - EC sensor: 5 samples × 1ms delay = ~5ms"
echo "  - Water level: 5 samples × 1ms delay = ~5ms"
echo "  - SHT31 (I2C): ~10-50ms (typical)"
echo "  - DS18B20 (1-Wire): ~75-94ms (9-bit resolution)"
echo ""
echo "Watchdog Timeout: 10 seconds"
echo "  - Loop must complete within 10s to avoid reset"
echo ""
echo "========================================"
echo "  BENCHMARK COMPLETE"
echo "========================================"
echo ""
echo "Firmware binary: $BUILD_DIR/firmware.bin"
echo "Build time: ${BUILD_TIME_MS}ms"
echo ""
echo "To view runtime performance metrics on the device:"
echo "1. Upload the firmware to your ESP32"
echo "2. Open serial monitor at 115200 baud"
echo "3. Send 'diag perf' command to view timing statistics"
echo "4. Wait for 100 loop iterations to see timing averages"
echo ""
echo "Key performance metrics tracked:"
echo "  - sensors_read: Time to read all sensors"
echo "  - actuator_update: Time to update actuator control logic"
echo "  - mqtt_publish: Time to publish telemetry via MQTT"
echo "  - loop_total: Total main loop execution time"
echo ""
