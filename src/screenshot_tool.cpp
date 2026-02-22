#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <csignal>
#include <unistd.h>
#include <dirent.h>

int find_pid_by_name(const std::string& name) {
    DIR* dir = opendir("/proc");
    if (!dir) return -1;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_DIR) continue;

        std::string pid_str = entry->d_name;
        if (pid_str.find_first_not_of("0123456789") != std::string::npos) continue;

        std::string cmdline_path = "/proc/" + pid_str + "/cmdline";
        std::ifstream cmdline_file(cmdline_path);
        if (!cmdline_file.is_open()) continue;

        std::string cmdline;
        std::getline(cmdline_file, cmdline, '\0');
        
        // Check for exact name or path match
        if (cmdline.find(name) != std::string::npos) {
            closedir(dir);
            return std::stoi(pid_str);
        }
    }

    closedir(dir);
    return -1;
}

int main(int argc, char* argv[]) {
    std::string process_name = "nuc_display";
    if (argc > 1) {
        process_name = argv[1];
    }

    std::cout << "Targeting process: " << process_name << std::endl;
    int pid = find_pid_by_name(process_name);

    if (pid <= 0) {
        std::cerr << "Could not find PID for process: " << process_name << std::endl;
        return 1;
    }

    std::cout << "Found PID: " << pid << ". Sending SIGUSR1 (Screenshot Trigger)..." << std::endl;

    if (kill(pid, SIGUSR1) == 0) {
        std::cout << "Signal sent successfully. Checking the log of nuc_display for confirmation." << std::endl;
        std::cout << "The screenshot will be saved as 'manual_screenshot.png' in the application directory." << std::endl;
    } else {
        std::perror("kill failed");
        return 1;
    }

    return 0;
}
