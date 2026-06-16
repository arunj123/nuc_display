/**
 * Integration Test for nuc_display
 * 
 * Runs the real nuc_display binary with a generated test config and scripted
 * key injection via Linux uinput. Each scenario forks the app, runs a timed
 * key sequence, then sends SIGINT for graceful shutdown.
 *
 * Requires: root (for uinput + DRM access), real display hardware.
 * Usage: sudo ./test_integration [--scenario N]
 */

#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <vector>
#include <chrono>
#include <thread>
#include <csignal>
#include <cstdlib>
#include <functional>
#include <filesystem>
#include <nlohmann/json.hpp>

#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <linux/uinput.h>
#include <linux/input-event-codes.h>

// ============================================================================
// UInput Virtual Keyboard
// ============================================================================

class UinputInjector {
public:
    UinputInjector() {
        fd_ = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        if (fd_ < 0) {
            std::cerr << "[Injector] Failed to open /dev/uinput: " << strerror(errno)
                      << " (are you root?)\n";
            return;
        }

        // Enable key events
        ioctl(fd_, UI_SET_EVBIT, EV_KEY);
        // Register all standard keys we might use
        for (int k = 0; k < KEY_MAX; ++k) {
            ioctl(fd_, UI_SET_KEYBIT, k);
        }

        struct uinput_setup usetup{};
        std::strncpy(usetup.name, "NucDisplay-TestInjector", UINPUT_MAX_NAME_SIZE - 1);
        usetup.id.bustype = BUS_USB;
        usetup.id.vendor  = 0x1234;
        usetup.id.product = 0x5678;
        usetup.id.version = 1;

        ioctl(fd_, UI_DEV_SETUP, &usetup);
        ioctl(fd_, UI_DEV_CREATE);

        // Give the kernel time to register the device
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        valid_ = true;
        std::cout << "[Injector] Virtual keyboard created.\n";
    }

    ~UinputInjector() {
        if (fd_ >= 0) {
            ioctl(fd_, UI_DEV_DESTROY);
            close(fd_);
        }
    }

    bool is_valid() const { return valid_; }

    void press_key(uint16_t code) {
        emit(EV_KEY, code, 1);  // DOWN
        emit(EV_SYN, SYN_REPORT, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        emit(EV_KEY, code, 0);  // UP
        emit(EV_SYN, SYN_REPORT, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void rapid_press(uint16_t code, int count, int delay_ms = 30) {
        for (int i = 0; i < count; ++i) {
            emit(EV_KEY, code, 1);
            emit(EV_SYN, SYN_REPORT, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            emit(EV_KEY, code, 0);
            emit(EV_SYN, SYN_REPORT, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }

private:
    void emit(uint16_t type, uint16_t code, int32_t val) {
        struct input_event ie{};
        ie.type  = type;
        ie.code  = code;
        ie.value = val;
        gettimeofday(&ie.time, nullptr);
        if (write(fd_, &ie, sizeof(ie)) < 0) {
            std::cerr << "[Injector] Write error: " << strerror(errno) << "\n";
        }
    }

    int fd_ = -1;
    bool valid_ = false;
};

// ============================================================================
// Test Config Generator
// ============================================================================

static const char* TEST_CONFIG_PATH = "/tmp/nuc_integ_config.json";

static std::string get_project_root() {
    // Try to find the project root relative to the binary
    auto exe_path = std::filesystem::read_symlink("/proc/self/exe");
    auto dir = exe_path.parent_path(); // build_hw/tests or build/tests
    // Go up to project root (tests/../..)
    for (int i = 0; i < 3; ++i) {
        if (std::filesystem::exists(dir / "config.json") && 
            std::filesystem::exists(dir / "assets")) {
            return dir.string();
        }
        dir = dir.parent_path();
    }
    return std::filesystem::current_path().string();
}

static bool generate_test_config() {
    std::string root = get_project_root();
    
    // Find test videos
    std::string video1 = root + "/tests/sample_with_audio.mp4";
    std::string video2 = root + "/tests/sample_no_audio.mp4";
    
    if (!std::filesystem::exists(video1)) {
        std::cerr << "[Config] Missing test video: " << video1 << "\n";
        return false;
    }

    std::ofstream f(TEST_CONFIG_PATH);
    if (!f.is_open()) {
        std::cerr << "[Config] Cannot write to " << TEST_CONFIG_PATH << "\n";
        return false;
    }

    f << R"({
    "location": {
        "name": "Integration Test",
        "lat": 49.4248,
        "lon": 11.0897
    },
    "global_keys": {
        "hide_videos": "v"
    },
    "stock_keys": {
        "next_stock": "dot",
        "prev_stock": "comma",
        "next_chart": "equal",
        "prev_chart": "minus"
    },
    "stocks": [
        { "symbol": "NVD.F", "name": "NVIDIA", "currency_symbol": "€" },
        { "symbol": "AMZ.F", "name": "Amazon", "currency_symbol": "€" }
    ],
    "videos": [
        {
            "enabled": true,
            "audio_enabled": false,
            "playlists": [")" << video1 << R"(", ")" << video2 << R"("],
            "x": 0.42, "y": 0.02, "w": 0.56, "h": 0.75,
            "start_trigger": "s",
            "keys": {
                "next": "n",
                "prev": "p",
                "skip_forward": "right",
                "skip_backward": "left"
            }
        }
    ],
    "layout": [
        { "type": "weather" },
        { "type": "stocks" },
        { "type": "news" },
        { "type": "video", "video_index": 0 }
    ],
    "http_server": {
        "enabled": false
    },
    "power_save": {
        "enabled": false
    }
})";
    f.close();
    std::cout << "[Config] Generated test config at " << TEST_CONFIG_PATH << "\n";
    return true;
}

// ============================================================================
// Scenario Runner
// ============================================================================

struct ScenarioResult {
    std::string name;
    bool passed;
    int exit_code;
    double duration_sec;
};

/**
 * Fork the nuc_display binary, run a key injection scenario, then SIGINT.
 * @param binary_path  Path to nuc_display binary
 * @param injector     Reference to UinputInjector
 * @param scenario_fn  Function that runs the key sequence (called in parent)
 * @param run_seconds  Total time to let the app run before SIGINT
 * @return ScenarioResult with pass/fail + exit code
 */
using ScenarioFn = void(*)(UinputInjector&);

static ScenarioResult run_scenario(
    const std::string& name,
    const std::string& binary_path,
    UinputInjector& injector,
    ScenarioFn scenario_fn,
    int run_seconds = 10
) {
    std::cout << "\n========================================\n";
    std::cout << "  SCENARIO: " << name << "\n";
    std::cout << "========================================\n";

    auto start = std::chrono::steady_clock::now();

    pid_t pid = fork();
    if (pid < 0) {
        return {name, false, -1, 0.0};
    }

    if (pid == 0) {
        // Child: exec the nuc_display binary with the temporary test config path
        // This avoids overwriting the real config.json in the current directory
        execl(binary_path.c_str(), binary_path.c_str(), "--config", TEST_CONFIG_PATH, nullptr);
        
        // If exec fails:
        std::cerr << "[Child] Failed to exec " << binary_path << ": " << strerror(errno) << "\n";
        _exit(127);
    }

    // Parent: wait for app to start, then inject keys
    // Valgrind requires a longer startup window (at least 5s on this hardware)
    std::this_thread::sleep_for(std::chrono::seconds(5)); 

    // Run the scenario key injection
    scenario_fn(injector);

    // Wait for the remaining time
    auto elapsed = std::chrono::steady_clock::now() - start;
    int remaining_ms = run_seconds * 1000 - 
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    if (remaining_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(remaining_ms));
    }

    // Send SIGINT for graceful shutdown
    std::cout << "[Runner] Sending SIGINT to PID " << pid << "\n";
    kill(pid, SIGINT);

    // Wait for child with timeout
    int status = 0;
    int wait_result = 0;
    for (int i = 0; i < 600; ++i) { // 60s timeout (Valgrind is EXTREMELY slow on teardown)
        wait_result = waitpid(pid, &status, WNOHANG);
        if (wait_result > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (wait_result <= 0) {
        // Force kill if not exited
        std::cerr << "[Runner] Force-killing unresponsive process\n";
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    }

    auto end = std::chrono::steady_clock::now();
    double duration = std::chrono::duration<double>(end - start).count();

    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : (128 + WTERMSIG(status));
    bool passed = (exit_code == 0);

    std::cout << "[Runner] " << name << ": exit_code=" << exit_code
              << " duration=" << duration << "s"
              << " → " << (passed ? "PASS ✅" : "FAIL ❌") << "\n";

    return {name, passed, exit_code, duration};
}

// ============================================================================
// Test Scenarios
// ============================================================================

// Scenario 1: Cold start — just let the app run and fetch data
static void scenario_cold_start(UinputInjector& /*inj*/) {
    std::cout << "[Scenario] Cold start: waiting for init + data fetch...\n";
    // No keys — just let it run for the full duration
}

// Scenario 2: Stock navigation
static void scenario_stock_navigation(UinputInjector& inj) {
    std::cout << "[Scenario] Stock navigation: cycling stocks and charts...\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // next_stock (KEY_DOT=52) × 5
    for (int i = 0; i < 5; ++i) {
        inj.press_key(KEY_DOT);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    // prev_stock (KEY_COMMA=51) × 2
    for (int i = 0; i < 2; ++i) {
        inj.press_key(KEY_COMMA);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    // next_chart (KEY_EQUAL=13) × 3
    for (int i = 0; i < 3; ++i) {
        inj.press_key(KEY_EQUAL);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    // prev_chart (KEY_MINUS=12) × 1
    inj.press_key(KEY_MINUS);
}

// Scenario 3: Video trigger + navigation
static void scenario_video_lifecycle(UinputInjector& inj) {
    std::cout << "[Scenario] Video lifecycle: trigger, play, navigate...\n";
    
    // Trigger video start (KEY_S=31)
    inj.press_key(KEY_S);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // Next video (KEY_N=49)
    inj.press_key(KEY_N);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Skip forward (KEY_RIGHT=106)
    inj.press_key(KEY_RIGHT);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Skip backward (KEY_LEFT=105)
    inj.press_key(KEY_LEFT);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Previous video (KEY_P=25)
    inj.press_key(KEY_P);
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

// Scenario 4: Hide/show toggle
static void scenario_hide_show(UinputInjector& inj) {
    std::cout << "[Scenario] Hide/show: rapid toggles...\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Start video first
    inj.press_key(KEY_S);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Toggle hide/show (KEY_V=47) × 3 rapid
    for (int i = 0; i < 3; ++i) {
        inj.press_key(KEY_V);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

// Scenario 5: Video Play/Stop Toggle
static void scenario_video_toggle(UinputInjector& inj) {
    std::cout << "[Scenario] Video Play/Stop toggle...\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 1. Play Video
    inj.press_key(KEY_S);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // 2. Stop Video (Unload completely)
    inj.press_key(KEY_S);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 3. Play Video Again (From beginning)
    inj.press_key(KEY_S);
    std::this_thread::sleep_for(std::chrono::seconds(3));
}

// Scenario 6: Stress — rapid random keys
static void scenario_rapid_keys(UinputInjector& inj) {
    std::cout << "[Scenario] Stress: 50 rapid key presses...\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Start video first
    inj.press_key(KEY_S);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    uint16_t keys[] = {KEY_DOT, KEY_COMMA, KEY_EQUAL, KEY_MINUS,
                       KEY_N, KEY_P, KEY_RIGHT, KEY_LEFT, KEY_V, KEY_S};
    int num_keys = sizeof(keys) / sizeof(keys[0]);
    
    for (int i = 0; i < 50; ++i) {
        // KEY_S in this random array will continuously toggle play/stop on the video pipeline!
        inj.press_key(keys[i % num_keys]);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
}

// Scenario 7: Clean shutdown during active rendering
static void scenario_clean_shutdown(UinputInjector& inj) {
    std::cout << "[Scenario] Clean shutdown: start everything, then SIGINT...\n";
    
    // Start video
    inj.press_key(KEY_S);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Navigate around
    inj.press_key(KEY_DOT);
    inj.press_key(KEY_EQUAL);
    inj.press_key(KEY_N);
    // SIGINT will be sent by the runner after the run time
}

// ============================================================================
// Main
// ============================================================================

static int parse_time_to_minutes(const std::string& time_str) {
    int h = 0, m = 0;
    if (std::sscanf(time_str.c_str(), "%d:%d", &h, &m) == 2) {
        return h * 60 + m;
    }
    return -1;
}

static bool is_in_power_save_range(int current_min, int start_min, int end_min) {
    if (start_min < end_min) {
        return current_min >= start_min && current_min < end_min;
    } else {
        return current_min >= start_min || current_min < end_min;
    }
}

static void check_and_wake_screen() {
    std::string root = get_project_root();
    std::string config_path = root + "/config.json";
    if (!std::filesystem::exists(config_path)) {
        std::cout << "[Test] config.json not found at " << config_path << ", skipping power save check.\n";
        return;
    }

    std::ifstream f(config_path);
    if (!f.is_open()) {
        std::cerr << "[Test] Failed to open config.json at " << config_path << "\n";
        return;
    }

    nlohmann::json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        std::cerr << "[Test] Failed to parse config.json: " << e.what() << "\n";
        return;
    }

    if (!j.contains("power_save") || !j["power_save"].is_object()) {
        return;
    }

    auto ps = j["power_save"];
    bool enabled = ps.value("enabled", false);
    if (!enabled) {
        return;
    }

    std::string start_time = ps.value("start_time", "23:00");
    std::string end_time = ps.value("end_time", "07:00");

    auto wall_now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(wall_now);
    struct tm *parts = std::localtime(&now_c);
    int current_min = parts->tm_hour * 60 + parts->tm_min;

    int start_min = parse_time_to_minutes(start_time);
    int end_min = parse_time_to_minutes(end_time);

    if (start_min >= 0 && end_min >= 0 && is_in_power_save_range(current_min, start_min, end_min)) {
        std::cout << "[Test] Screen is off due to scheduled off range (" << start_time << " - " << end_time << "). Waking it up!\n";
        
        // 1. Unblank via fb0/blank sysfs
        std::ofstream blank_file("/sys/class/graphics/fb0/blank");
        if (blank_file.is_open()) {
            blank_file << "0\n";
            blank_file.close();
            std::cout << "[Test] Successfully wrote 0 to /sys/class/graphics/fb0/blank\n";
        } else {
            std::cerr << "[Test] Failed to open /sys/class/graphics/fb0/blank (are we root?)\n";
        }

        // 2. Unblank via virtual console /dev/tty0
        int vt_fd = open("/dev/tty0", O_WRONLY);
        if (vt_fd >= 0) {
            if (write(vt_fd, "\033[9;0]", 6) == 6) {
                std::cout << "[Test] Successfully sent unblank escape sequence to /dev/tty0\n";
            }
            close(vt_fd);
        } else {
            std::cerr << "[Test] Failed to open /dev/tty0\n";
        }
    } else {
        std::cout << "[Test] Display is not in power save window or power save is disabled.\n";
    }
}

int main(int argc, char** argv) {
    std::cout << "=== NUC Display Integration Test ===\n";
    std::cout << "PID: " << getpid() << ", UID: " << getuid() << "\n";
    
    if (getuid() != 0) {
        std::cerr << "ERROR: Must run as root (need uinput + DRM access).\n";
        return 1;
    }

    // Check and wake screen if off due to scheduled off
    check_and_wake_screen();

    // Check if the service is active and stop it to release DRM master and wake up target display
    bool service_was_active = false;
    if (std::system("systemctl is-active --quiet nuc_display") == 0) {
        std::cout << "[Test] nuc_display service is active. Stopping it to release DRM master...\n";
        std::system("systemctl stop nuc_display");
        service_was_active = true;
    }

    // Find the nuc_display binary
    std::string root = get_project_root();
    std::string binary = "";
    
    // Check common build paths
    for (auto& candidate : {
        root + "/build_hw/nuc_display",
        root + "/build/nuc_display",
    }) {
        if (std::filesystem::exists(candidate)) {
            binary = candidate;
            break;
        }
    }
    
    if (binary.empty()) {
        std::cerr << "ERROR: Cannot find nuc_display binary.\n";
        return 1;
    }
    std::cout << "Binary: " << binary << "\n";
    std::cout << "Project root: " << root << "\n";
    
    // Parse optional --scenario N
    int only_scenario = -1;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--scenario" && i + 1 < argc) {
            only_scenario = std::atoi(argv[i + 1]);
        }
    }

    // Generate test config
    if (!generate_test_config()) {
        return 1;
    }

    // Create virtual keyboard
    UinputInjector injector;
    if (!injector.is_valid()) {
        std::cerr << "ERROR: Failed to create uinput device.\n";
        return 1;
    }

    // Change to project root so config.json paths are relative to it
    std::filesystem::current_path(root);

    // Define scenarios
    struct Scenario {
        const char* name;
        ScenarioFn fn;
        int duration_sec;
    };

    Scenario scenarios[] = {
        {"1. Cold Start",          scenario_cold_start,       8},
        {"2. Stock Navigation",    scenario_stock_navigation, 10},
        {"3. Video Lifecycle",     scenario_video_lifecycle,  12},
        {"4. Hide/Show Toggle",    scenario_hide_show,        10},
        {"5. Video Play/Stop",     scenario_video_toggle,     10},
        {"6. Rapid Key Stress",    scenario_rapid_keys,       8},
        {"7. Clean Shutdown",      scenario_clean_shutdown,   6},
    };

    // Run scenarios
    std::vector<ScenarioResult> results;
    size_t num_scenarios = sizeof(scenarios) / sizeof(scenarios[0]);
    for (size_t i = 0; i < num_scenarios; ++i) {
        if (only_scenario >= 0 && (int)i != only_scenario - 1) continue;
        
        auto result = run_scenario(
            scenarios[i].name, binary, injector,
            scenarios[i].fn, scenarios[i].duration_sec
        );
        results.push_back(result);
        
        // Brief pause between scenarios for cleanup
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Cleanup
    std::filesystem::remove(TEST_CONFIG_PATH);

    // Summary
    std::cout << "\n========================================\n";
    std::cout << "  INTEGRATION TEST RESULTS\n";
    std::cout << "========================================\n";
    
    int passed = 0, failed = 0;
    for (const auto& r : results) {
        std::cout << (r.passed ? "  ✅ " : "  ❌ ") << r.name
                  << " (exit=" << r.exit_code << ", " << r.duration_sec << "s)\n";
        if (r.passed) ++passed;
        else ++failed;
    }
    
    std::cout << "\nTotal: " << passed << " passed, " << failed << " failed\n";
    
    if (service_was_active) {
        std::cout << "[Test] Restarting nuc_display service...\n";
        std::system("systemctl start nuc_display");
    }
    
    return (failed > 0) ? 1 : 0;
}
