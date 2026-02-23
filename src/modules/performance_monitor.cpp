#include "modules/performance_monitor.hpp"
#include <unistd.h>
#include <sstream>
#include <iomanip>

namespace nuc_display::modules {

PerformanceMonitor::PerformanceMonitor() {
    this->start_time_ = std::chrono::steady_clock::now();
    this->current_stats_ = {0, 0, 0, 0, 0, 0};
    
    // Initialize CPU counters
    FILE* file = fopen("/proc/stat", "r");
    if (file) {
        fscanf(file, "cpu %llu %llu %llu %llu", &last_total_user_, &last_total_user_low_, &last_total_sys_, &last_total_idle_);
        fclose(file);
    }

    // Read max GPU frequency once
    std::ifstream max_freq_file("/sys/class/drm/card1/gt_max_freq_mhz");
    if (max_freq_file) {
        max_freq_file >> this->current_stats_.gpu_max_freq_mhz;
    } else {
        std::ifstream max_freq_file0("/sys/class/drm/card0/gt_max_freq_mhz");
        if (max_freq_file0) max_freq_file0 >> this->current_stats_.gpu_max_freq_mhz;
    }
}

void PerformanceMonitor::update() {
    this->update_cpu();
    this->update_ram();
    this->update_gpu();
    this->update_temp();
    
    auto now = std::chrono::steady_clock::now();
    this->current_stats_.uptime_sec = std::chrono::duration<double>(now - start_time_).count();
}

void PerformanceMonitor::update_cpu() {
    unsigned long long user, user_low, sys, idle;
    FILE* file = fopen("/proc/stat", "r");
    if (!file) return;
    
    if (fscanf(file, "cpu %llu %llu %llu %llu", &user, &user_low, &sys, &idle) != 4) {
        fclose(file);
        return;
    }
    fclose(file);

    unsigned long long total = (user - last_total_user_) + (user_low - last_total_user_low_) + (sys - last_total_sys_);
    unsigned long long period = total + (idle - last_total_idle_);

    if (period > 0) {
        this->current_stats_.cpu_usage = (total * 100.0) / period;
    }

    last_total_user_ = user;
    last_total_user_low_ = user_low;
    last_total_sys_ = sys;
    last_total_idle_ = idle;
}

void PerformanceMonitor::update_ram() {
    // Read resident set size (RSS) from /proc/self/statm
    // The second value is the RSS in pages
    std::ifstream statm("/proc/self/statm");
    unsigned long size, rss;
    if (statm >> size >> rss) {
        long page_size_kb = sysconf(_SC_PAGESIZE) / 1024;
        this->current_stats_.ram_usage_mb = (rss * page_size_kb) / 1024.0;
    }
}

void PerformanceMonitor::update_gpu() {
    // Intel GPU frequency in MHz
    std::ifstream freq_file("/sys/class/drm/card1/gt_act_freq_mhz");
    if (freq_file) {
        freq_file >> this->current_stats_.gpu_freq_mhz;
    } else {
        // Try card0 if card1 fails
        std::ifstream freq_file0("/sys/class/drm/card0/gt_act_freq_mhz");
        if (freq_file0) freq_file0 >> this->current_stats_.gpu_freq_mhz;
    }
}

void PerformanceMonitor::update_temp() {
    // Temperature in millidegrees C
    std::ifstream temp_file("/sys/class/hwmon/hwmon2/temp1_input");
    if (temp_file) {
        double temp_milli;
        temp_file >> temp_milli;
        this->current_stats_.temperature_c = temp_milli / 1000.0;
    }
}

void PerformanceMonitor::log() const {
    std::cout << "[Perf] "
              << std::fixed << std::setprecision(1)
              << "CPU: " << current_stats_.cpu_usage << "% | "
              << "RAM: " << current_stats_.ram_usage_mb << " MB | "
              << "GPU: " << (int)current_stats_.gpu_freq_mhz << "/" << (int)current_stats_.gpu_max_freq_mhz << " MHz | "
              << "Temp: " << current_stats_.temperature_c << "°C | "
              << "Uptime: " << (int)current_stats_.uptime_sec << "s"
              << std::endl;
}

} // namespace nuc_display::modules
