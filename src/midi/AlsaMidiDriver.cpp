#include "midi/AlsaMidiDriver.hpp"
#include "net/ClockSynchronizer.hpp"
#include <iostream>

namespace MidiWire {

AlsaMidiDriver::AlsaMidiDriver() = default;

AlsaMidiDriver::~AlsaMidiDriver() {
    closeInputPort();
    std::lock_guard<std::mutex> lock(mutex_);
    if (seqHandle_) {
        if (virtualOutPortId_ >= 0) {
            snd_seq_delete_simple_port(seqHandle_, virtualOutPortId_);
        }
        snd_seq_close(seqHandle_);
        seqHandle_ = nullptr;
    }
}

bool AlsaMidiDriver::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    int err = snd_seq_open(&seqHandle_, "default", SND_SEQ_OPEN_DUPLEX, 0);
    if (err < 0) {
        std::cerr << "[AlsaMidiDriver] Error opening ALSA sequencer: " << snd_strerror(err) << std::endl;
        return false;
    }
    snd_seq_set_client_name(seqHandle_, "MidiWire");
    return true;
}

std::vector<std::string> AlsaMidiDriver::getAvailableInputs() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> deviceNames;
    discoveredPorts_.clear();

    if (!seqHandle_) return deviceNames;

    snd_seq_client_info_t* cinfo;
    snd_seq_port_info_t* pinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);

    snd_seq_client_info_set_client(cinfo, -1);
    while (snd_seq_query_next_client(seqHandle_, cinfo) >= 0) {
        int client = snd_seq_client_info_get_client(cinfo);
        if (client == snd_seq_client_id(seqHandle_)) continue; // Skip self

        snd_seq_port_info_set_client(pinfo, client);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(seqHandle_, pinfo) >= 0) {
            unsigned int caps = snd_seq_port_info_get_capability(pinfo);
            if ((caps & (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) == 
                (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) {
                
                std::string clientName = snd_seq_client_info_get_name(cinfo);
                std::string portName = snd_seq_port_info_get_name(pinfo);
                std::string fullName = clientName + ":" + portName;

                int port = snd_seq_port_info_get_port(pinfo);
                discoveredPorts_.push_back({client, port, fullName});
                deviceNames.push_back(fullName);
            }
        }
    }
    return deviceNames;
}

bool AlsaMidiDriver::openInputPort(uint32_t index, MidiInputCallback callback) {
    closeInputPort();
    std::lock_guard<std::mutex> lock(mutex_);

    if (index >= discoveredPorts_.size() || !seqHandle_) return false;

    inputCallback_ = callback;

    inPortId_ = snd_seq_create_simple_port(seqHandle_, "MidiWire Input",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);

    if (inPortId_ < 0) return false;

    const auto& target = discoveredPorts_[index];
    snd_seq_connect_from(seqHandle_, inPortId_, target.client, target.port);

    running_ = true;
    readThread_ = std::thread(&AlsaMidiDriver::readLoop, this);
    return true;
}

void AlsaMidiDriver::closeInputPort() {
    running_ = false;
    if (readThread_.joinable()) {
        readThread_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (seqHandle_ && inPortId_ >= 0) {
        snd_seq_delete_simple_port(seqHandle_, inPortId_);
        inPortId_ = -1;
    }
}

bool AlsaMidiDriver::createVirtualOutputPort(const std::string& portName) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!seqHandle_) return false;

    virtualOutPortId_ = snd_seq_create_simple_port(seqHandle_, portName.c_str(),
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);

    return virtualOutPortId_ >= 0;
}

void AlsaMidiDriver::sendToVirtualPort(const MidiPacket& packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!seqHandle_ || virtualOutPortId_ < 0) return;

    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, virtualOutPortId_);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);

    uint8_t type = packet.status & 0xF0;
    switch (type) {
        case 0x90:
            snd_seq_ev_set_noteon(&ev, packet.channel, packet.data1, packet.data2);
            break;
        case 0x80:
            snd_seq_ev_set_noteoff(&ev, packet.channel, packet.data1, packet.data2);
            break;
        case 0xB0:
            snd_seq_ev_set_controller(&ev, packet.channel, packet.data1, packet.data2);
            break;
        case 0xE0:
            snd_seq_ev_set_pitchbend(&ev, packet.channel, (packet.data2 << 7) | packet.data1);
            break;
        default:
            return;
    }

    snd_seq_event_output_direct(seqHandle_, &ev);
}

void AlsaMidiDriver::readLoop() {
    while (running_) {
        snd_seq_event_t* ev = nullptr;
        if (snd_seq_event_input(seqHandle_, &ev) >= 0 && ev) {
            MidiPacket packet{};
            packet.timestampNs = ClockSynchronizer::getCurrentMonotonicNs();

            switch (ev->type) {
                case SND_SEQ_EVENT_NOTEON:
                    packet.status = 0x90;
                    packet.channel = ev->data.note.channel;
                    packet.data1 = ev->data.note.note;
                    packet.data2 = ev->data.note.velocity;
                    break;
                case SND_SEQ_EVENT_NOTEOFF:
                    packet.status = 0x80;
                    packet.channel = ev->data.note.channel;
                    packet.data1 = ev->data.note.note;
                    packet.data2 = ev->data.note.velocity;
                    break;
                case SND_SEQ_EVENT_CONTROLLER:
                    packet.status = 0xB0;
                    packet.channel = ev->data.control.channel;
                    packet.data1 = ev->data.control.param;
                    packet.data2 = ev->data.control.value;
                    break;
                default:
                    snd_seq_free_event(ev);
                    continue;
            }

            MidiInputCallback cb;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                cb = inputCallback_;
            }
            if (cb) cb(packet);
            snd_seq_free_event(ev);
        }
    }
}

} // namespace MidiWire