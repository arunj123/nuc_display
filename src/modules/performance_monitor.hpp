#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <fstream>
#include <iostream>

namespace nuc_display::modules {

struct PerformanceStats {
    double cpu_usage;        // Percentage (0.0 to 100.0)
    double ram_usage_mb;     // RSS in MB
    double gpu_freq_mhz;     // Actual frequency
    double temperature_c;    // Temperature in Celsius
    double uptime_sec;       // Process uptime
};

class PerformanceMonitor {
public:
    PerformanceMonitor();
    ~PerformanceMonitor() = default;

    // Updates all stats from system paths
    void update();

    // logs stats to console or file
    void log() const;

    const PerformanceStats& stats() const { return current_stats_; }

private:
    PerformanceStats current_stats_;
    std::chrono::steady_clock::time_point start_time_;
    
    // Previous CPU counters for delta calculation
    unsigned long long last_total_user_, last_total_user_low_, last_total_sys_, last_total_idle_;

    void update_cpu();
    void update_ram();
    void update_gpu();
    void update_temp();
};

} // namespace nuc_display::modules
