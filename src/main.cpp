#include <iostream>
#include <csignal>
#include <string>
#include <vector>
#include "app.hpp"
#include "ipc_server.hpp"

static di::App* g_app = nullptr;

static void sig_handler(int sig) {
    if (g_app) {
        g_app->stop();
    }
}

static void print_version() {
    std::cout << "Dynamic Island for Hyprland v1.1\n"
              << "Author: MI-Syafaudin\n"
              << "GitHub: https://github.com/MI-Syafaudin/Dynamic-Island\n";
}

static void print_help(const char* prog) {
    std::cout << "Dynamic Island for Hyprland (Wayland Native)\n"
              << "Author: MI-Syafaudin (https://github.com/MI-Syafaudin/Dynamic-Island)\n\n"
              << "Usage:\n"
              << "  " << prog << "                     Start Dynamic Island daemon\n"
              << "  " << prog << " toggle              Toggle Island expand / collapse\n"
              << "  " << prog << " collapse            Force collapse to idle\n"
              << "  " << prog << " expand <mode>       Expand to mode (audio|brightness|media|system|clock|network|quick)\n"
              << "  " << prog << " volume <val>        Adjust volume (+5%, -5%, toggle)\n"
              << "  " << prog << " brightness <val>    Adjust brightness (+5%, -5%)\n"
              << "  " << prog << " media <action>      Control playback (play-pause|next|previous)\n"
              << "  " << prog << " screenshot <mode>   Take screenshot (full|area)\n"
              << "  " << prog << " notify <app> <msg>  Show notification banner on island\n"
              << "  " << prog << " reload              Reload config.json\n"
              << "  " << prog << " quit                Stop running daemon\n"
              << "  " << prog << " version             Show version and author info\n"
              << "  " << prog << " --help              Show this help message\n";
}

int main(int argc, char** argv) {
    // If command-line arguments are provided, act as CLI client
    if (argc > 1) {
        std::string first_arg = argv[1];
        if (first_arg == "--help" || first_arg == "-h") {
            print_help(argv[0]);
            return 0;
        }
        if (first_arg == "--version" || first_arg == "-v" || first_arg == "version") {
            print_version();
            return 0;
        }

        std::string cmd_line;
        for (int i = 1; i < argc; ++i) {
            if (i > 1) cmd_line += " ";
            cmd_line += argv[i];
        }

        std::string response;
        if (di::IpcServer::send_command(cmd_line, response)) {
            std::cout << response;
            return 0;
        } else {
            if (first_arg == "toggle" || first_arg == "expand" || first_arg == "volume") {
                std::cerr << "Dynamic Island is not running. Starting daemon...\n";
            } else {
                std::cerr << "Dynamic Island is not running.\n";
                return 1;
            }
        }
    }

    // Check if another instance is already running
    std::string ping_resp;
    if (di::IpcServer::send_command("ping", ping_resp)) {
        std::cout << "Dynamic Island is already running.\n"
                  << "Run 'dynamic-island toggle' to expand or 'dynamic-island --help' for commands.\n";
        return 0;
    }

    di::App app;
    g_app = &app;

    std::signal(SIGINT, sig_handler);
    std::signal(SIGTERM, sig_handler);
    std::signal(SIGHUP, SIG_IGN);

    std::cout << "[Dynamic Island] Initializing native Wayland Layer Shell (by MI-Syafaudin)...\n";
    if (!app.init()) {
        std::cerr << "[Dynamic Island] Failed to initialize.\n";
        return 1;
    }

    std::cout << "[Dynamic Island] Running smoothly at 60 Hz.\n";
    app.run();

    std::cout << "[Dynamic Island] Clean shutdown.\n";
    return 0;
}
