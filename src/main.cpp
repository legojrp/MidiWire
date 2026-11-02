#include "app/AppController.hpp"
#include "midi/MacMidiDriver.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <chrono>

#include "app/AppController.hpp"

#if defined(__APPLE__)
    #include "midi/MacMidiDriver.hpp"
    using NativeDriver = MidiWire::MacMidiDriver;
#elif defined(__linux__)
    #include "midi/AlsaMidiDriver.hpp"
    using NativeDriver = MidiWire::AlsaMidiDriver;
#elif defined(_WIN32)
    #include "midi/WinMidiDriver.hpp"
    using NativeDriver = MidiWire::WinMidiDriver;
#endif

void printBanner() {
    std::cout << "===================================================\n";
    std::cout << "             MIDIWIRE CLI CONTROLLER               \n";
    std::cout << "      Low-Latency P2P Remote MIDI Collaboration     \n";
    std::cout << "===================================================\n\n";
}

int main() {
    printBanner();

    // 1. Initialize Controller with Platform Driver
    auto driver = std::make_unique<NativeDriver>();
    MidiWire::AppController app(std::move(driver));

    if (!app.initialize()) {
        std::cerr << "[!] Failed to initialize AppController subsystems.\n";
        return 1;
    }

    // 2. Hardware MIDI Input Selection
    auto devices = app.getAvailableMidiDevices();
    std::cout << "[Step 1] Select Hardware MIDI Input Device:\n";
    if (devices.empty()) {
        std::cout << "  (No physical input devices found. Using Virtual Receiver mode only)\n";
    } else {
        for (size_t i = 0; i < devices.size(); ++i) {
            std::cout << "  [" << i << "] " << devices[i] << "\n";
        }
        std::cout << "  Enter device index (or -1 to skip): ";
        int choice = -1;
        std::cin >> choice;
        if (choice >= 0 && choice < static_cast<int>(devices.size())) {
            app.selectMidiInputDevice(static_cast<uint32_t>(choice));
            std::cout << "  -> Connected to input: " << devices[choice] << "\n";
        }
    }
    std::cout << "\n";

    // 3. DAW Virtual Output Port Creation
    std::cout << "[Step 2] Setting up Virtual Output Port for DAW...\n";
    app.setupVirtualMidiPort("MidiWire Remote In");
    std::cout << "  -> Created Virtual MIDI Port: 'MidiWire Remote In'\n\n";

    // 4. Signaling & Room Connection
    std::string signalUrl = "ws://localhost:8080";
    std::string roomId = "jam-room-1";

    std::cout << "[Step 3] Network Configuration:\n";
    std::cout << "  Enter Signaling Server URL [default: ws://localhost:8080]: ";
    std::cin.ignore();
    std::string inputUrl;
    std::getline(std::cin, inputUrl);
    if (!inputUrl.empty()) signalUrl = inputUrl;

    std::cout << "  Enter Room ID [default: jam-room-1]: ";
    std::string inputRoom;
    std::getline(std::cin, inputRoom);
    if (!inputRoom.empty()) roomId = inputRoom;

    std::cout << "\n[+] Connecting to " << signalUrl << " ...\n";
    app.connectToSignaling(signalUrl);
    
    std::cout << "[+] Joining Room: " << roomId << " ...\n";
    app.joinRoom(roomId);

    // 5. Start Playback Thread
    app.start();

    // 6. Interactive Session Control Loop
    std::cout << "\n===================================================\n";
    std::cout << "  SESSION ACTIVE - HOTKEYS:                        \n";
    std::cout << "    [p] Panic (Silence stuck notes across 16 ch)  \n";
    std::cout << "    [b] Adjust Jitter Buffer Depth                \n";
    std::cout << "    [q] Quit Session                              \n";
    std::cout << "===================================================\n\n";

    bool running = true;
    while (running) {
        std::cout << "MidiWire> ";
        char cmd;
        std::cin >> cmd;

        switch (cmd) {
            case 'p':
            case 'P':
                app.panic();
                std::cout << "  -> [PANIC] Sent All-Notes-Off to Virtual Output.\n";
                break;

            case 'b':
            case 'B': {
                std::cout << "  Enter new Jitter Buffer Depth (ms): ";
                uint32_t ms;
                if (std::cin >> ms) {
                    app.setJitterBufferDepthMs(ms);
                    std::cout << "  -> Jitter depth updated to " << ms << " ms.\n";
                }
                break;
            }

            case 'q':
            case 'Q':
                std::cout << "  Exiting session...\n";
                running = false;
                break;

            default:
                std::cout << "  Unknown command. Options: [p] Panic | [b] Buffer depth | [q] Quit\n";
                break;
        }
    }

    app.stop();
    std::cout << "[+] Session terminated cleanly.\n";
    return 0;
}