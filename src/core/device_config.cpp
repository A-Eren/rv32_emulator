#include "core/device_config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "core/devices.hpp"
#include "core/memfile.hpp"

namespace rv::core {
namespace {

std::string trim(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

/// Strip a `#` comment, respecting quotes so a path containing one survives.
std::string strip_comment(std::string_view line) {
    bool in_string = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        if (line[index] == '"') in_string = !in_string;
        if (line[index] == '#' && !in_string) return std::string(line.substr(0, index));
    }
    return std::string(line);
}

struct Value {
    std::string text;
    bool quoted = false;
};

/// Parse an integer in decimal, 0x hex or 0b binary, with `_` separators.
bool parse_number(std::string_view text, u32& out) {
    std::string cleaned;
    for (const char character : text) {
        if (character != '_') cleaned.push_back(character);
    }
    if (cleaned.empty()) return false;

    int base = 10;
    std::size_t start = 0;
    if (cleaned.size() > 2 && cleaned[0] == '0') {
        if (cleaned[1] == 'x' || cleaned[1] == 'X') {
            base = 16;
            start = 2;
        } else if (cleaned[1] == 'b' || cleaned[1] == 'B') {
            base = 2;
            start = 2;
        }
    }
    char* end = nullptr;
    const unsigned long long value = std::strtoull(cleaned.c_str() + start, &end, base);
    if (end == cleaned.c_str() + start || *end != '\0' || value > 0xffff'ffffull) return false;
    out = static_cast<u32>(value);
    return true;
}

std::string join_path(const std::string& directory, const std::string& file) {
    if (directory.empty() || file.empty() || file[0] == '/') return file;
    return directory.back() == '/' ? directory + file : directory + "/" + file;
}

}  // namespace

// ---------------------------------------------------------------------------
// Catalogue
// ---------------------------------------------------------------------------

const std::vector<DeviceType>& device_catalogue() {
    static const std::vector<DeviceType> types = {
        {"uart", "character output", false,
         [](u32) -> std::unique_ptr<Device> { return std::make_unique<UartDevice>(); }},
        {"leds", "32-bit output register", false,
         [](u32) -> std::unique_ptr<Device> { return std::make_unique<LedDevice>(); }},
        {"switches", "32-bit input register", false,
         [](u32) -> std::unique_ptr<Device> { return std::make_unique<SwitchDevice>(); }},
        {"button", "momentary input, can raise IRQ", false,
         [](u32) -> std::unique_ptr<Device> { return std::make_unique<ButtonDevice>(); }},
        {"value", "numeric input register", false,
         [](u32) -> std::unique_ptr<Device> { return std::make_unique<ValueDevice>(); }},
        {"display", "7-segment digits", false,
         [](u32) -> std::unique_ptr<Device> { return std::make_unique<DisplayDevice>(); }},
        {"mtime", "cycle counter / mtime", false,
         [](u32) -> std::unique_ptr<Device> { return std::make_unique<CycleDevice>(); }},
        {"mtimecmp", "timer compare", false,
         [](u32) -> std::unique_ptr<Device> { return std::make_unique<TimerDevice>(); }},
        {"irq", "software/external IRQ requests", false,
         [](u32) -> std::unique_ptr<Device> { return std::make_unique<IrqDevice>(); }},
        {"ram", "memory block", true,
         [](u32 size) -> std::unique_ptr<Device> {
             return std::make_unique<RamDevice>(size == 0 ? 256u : size, false);
         }},
        {"rom", "read-only memory block", true,
         [](u32 size) -> std::unique_ptr<Device> {
             return std::make_unique<RamDevice>(size == 0 ? 256u : size, true);
         }},
    };
    return types;
}

const DeviceType* find_device_type(std::string_view name) {
    for (const DeviceType& type : device_catalogue()) {
        if (type.name == name) return &type;
    }
    return nullptr;
}

std::unique_ptr<Device> make_device(std::string_view type, u32 size) {
    const DeviceType* found = find_device_type(type);
    return found == nullptr ? nullptr : found->create(size);
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

DeviceConfig parse_device_config(const std::string& text, const std::string& source_name) {
    DeviceConfig config;
    std::istringstream stream(text);
    std::string raw;
    int line_number = 0;
    bool in_device = false;

    const auto fail = [&](const std::string& message) {
        config.error = source_name + ":" + std::to_string(line_number) + ": " + message;
    };

    while (std::getline(stream, raw)) {
        ++line_number;
        const std::string line = trim(strip_comment(raw));
        if (line.empty()) continue;

        if (line == "[[device]]") {
            config.devices.emplace_back();
            in_device = true;
            continue;
        }
        if (!line.empty() && line[0] == '[') {
            fail("unknown table '" + line + "'; only [[device]] is understood");
            return config;
        }
        if (!in_device) {
            fail("'" + line + "' appears before any [[device]]");
            return config;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            fail("expected 'key = value', found '" + line + "'");
            return config;
        }
        const std::string key = trim(line.substr(0, equals));
        std::string value = trim(line.substr(equals + 1));

        bool quoted = false;
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
            quoted = true;
        }

        DeviceSpec& spec = config.devices.back();
        const auto number = [&](u32& target) {
            if (!parse_number(value, target)) {
                fail("'" + value + "' is not a number");
                return false;
            }
            return true;
        };

        if (key == "type") {
            if (!quoted) {
                fail("type must be quoted, as type = \"" + value + "\"");
                return config;
            }
            if (find_device_type(value) == nullptr) {
                std::string known;
                for (const DeviceType& type : device_catalogue()) {
                    if (!known.empty()) known += ", ";
                    known += type.name;
                }
                fail("unknown device type '" + value + "'; known types are " + known);
                return config;
            }
            spec.type = value;
        } else if (key == "slot") {
            u32 slot = 0;
            if (!number(slot)) return config;
            if (slot >= kMmioSlots) {
                fail("slot " + std::to_string(slot) + " is out of range (0.." +
                     std::to_string(kMmioSlots - 1) + ")");
                return config;
            }
            spec.slot = slot;
        } else if (key == "size") {
            if (!number(spec.size)) return config;
        } else if (key == "value") {
            if (!number(spec.value)) return config;
            spec.has_value = true;
        } else if (key == "load") {
            spec.load = value;
        } else if (key == "readonly" || key == "read_only") {
            spec.read_only = value == "true" || value == "1";
        } else if (key == "digits") {
            u32 digits = 4;
            if (!number(digits)) return config;
            spec.digits = static_cast<int>(digits);
        } else {
            fail("unknown key '" + key +
                 "'; understood keys are type, slot, size, value, load, readonly, digits");
            return config;
        }
    }

    for (std::size_t index = 0; index < config.devices.size(); ++index) {
        if (config.devices[index].type.empty()) {
            config.error = source_name + ": device " + std::to_string(index + 1) + " has no type";
            return config;
        }
    }
    return config;
}

std::string directory_of(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

DeviceConfig read_device_config(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        DeviceConfig config;
        config.error = "cannot open '" + path + "'";
        return config;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return parse_device_config(contents.str(), path);
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

std::string write_device_config(const DeviceConfig& config) {
    std::ostringstream out;
    out << "# rv32 device configuration.\n"
           "# Each [[device]] takes a slot in the window at 0xffff0000; see\n"
           "# docs/memory-map.md for the register layouts.\n";

    for (const DeviceSpec& spec : config.devices) {
        out << "\n[[device]]\n";
        out << "type = \"" << spec.type << "\"\n";
        out << "slot = " << spec.slot << "\n";
        if (spec.size != 0) out << "size = " << spec.size << "\n";
        if (spec.read_only) out << "readonly = true\n";
        if (!spec.load.empty()) out << "load = \"" << spec.load << "\"\n";
        if (spec.has_value) {
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "0x%08x", spec.value);
            out << "value = " << buffer << "\n";
        }
    }
    return out.str();
}

DeviceConfig describe(const Bus& bus) {
    DeviceConfig config;
    for (Device* device : bus.devices()) {
        DeviceSpec spec;
        spec.type = std::string(device->type_name());
        spec.slot = device->slot();

        if (const auto* ram = dynamic_cast<const RamDevice*>(device)) {
            spec.size = ram->size();
            spec.read_only = ram->read_only();
        }
        // Inputs are the part of a device a person set, so they are what is
        // worth writing back out.
        const std::vector<InputControl> inputs = device->inputs();
        if (!inputs.empty()) {
            spec.value = inputs.front().value;
            spec.has_value = true;
        }
        config.devices.push_back(std::move(spec));
    }
    std::sort(config.devices.begin(), config.devices.end(),
              [](const DeviceSpec& a, const DeviceSpec& b) { return a.slot < b.slot; });
    return config;
}

std::string apply_device_config(Bus& bus, const DeviceConfig& config,
                                const std::string& base_directory) {
    if (!config.ok()) return config.error;

    // Replace rather than add: a machine described in a file gets exactly what
    // the file says, so a configuration is reproducible.
    bus.detach_all();

    for (const DeviceSpec& spec : config.devices) {
        std::unique_ptr<Device> device;
        if (spec.type == "display") {
            device = std::make_unique<DisplayDevice>(spec.digits);
        } else if (spec.type == "ram" || spec.type == "rom") {
            device = std::make_unique<RamDevice>(spec.size == 0 ? 256u : spec.size,
                                                 spec.type == "rom" || spec.read_only);
        } else {
            device = make_device(spec.type);
        }
        if (device == nullptr) return "unknown device type '" + spec.type + "'";

        auto* ram = dynamic_cast<RamDevice*>(device.get());
        Device* attached = bus.attach(spec.slot, std::move(device));
        if (attached == nullptr) {
            return "cannot attach '" + spec.type + "' at slot " + std::to_string(spec.slot) +
                   ": the slot is occupied or the device does not fit";
        }

        if (ram != nullptr && !spec.load.empty()) {
            std::vector<Word> words;
            const MemFileResult result =
                read_mem_file(join_path(base_directory, spec.load), words);
            if (!result.ok) return result.error;
            ram->load_words(words);
        }
        if (spec.has_value && !attached->inputs().empty()) {
            attached->set_input(0, spec.value);
        } else if (spec.has_value) {
            // A value on an output device is its initial contents.
            attached->poke(0, spec.value);
        }
    }
    return {};
}

}  // namespace rv::core
