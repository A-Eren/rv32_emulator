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

/// Whether a memory size is one a machine can actually have.
std::string check_memory_size(const std::string& what, u32 size) {
    if (size % 4 != 0) {
        return what + " must be a multiple of 4 bytes -- both spaces are exported a word per line";
    }
    if (size < kMinMemorySize || size > kMaxMemorySize) {
        return what + " must be between " + std::to_string(kMinMemorySize) + " and " +
               std::to_string(kMaxMemorySize) + " bytes";
    }
    return {};
}

/// "0xffff0040" -- how a person names an address, and how the memory map
/// documents one.
std::string hex_address(Addr addr) {
    char text[16];
    std::snprintf(text, sizeof text, "0x%08x", addr);
    return text;
}

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
        {"uart", "character output", false, false,
         [](u32, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<UartDevice>();
         }},
        {"leds", "32-bit output register", false, false,
         [](u32, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<LedDevice>();
         }},
        {"switches", "32-bit input register", false, false,
         [](u32, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<SwitchDevice>();
         }},
        {"button", "momentary input, can raise IRQ", false, false,
         [](u32, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<ButtonDevice>();
         }},
        {"value", "numeric input register", false, false,
         [](u32, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<ValueDevice>();
         }},
        {"display", "7-segment digits", false, false,
         [](u32, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<DisplayDevice>();
         }},
        {"mtime", "cycle counter / mtime", false, false,
         [](u32, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<CycleDevice>();
         }},
        {"mtimecmp", "timer compare", false, false,
         [](u32, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<TimerDevice>();
         }},
        {"irq", "software/external IRQ requests", false, false,
         [](u32, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<IrqDevice>();
         }},
        {"ram", "memory block, words settable by hand", true, false,
         [](u32 size, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<RamDevice>(size == 0 ? 256u : size, false);
         }},
        {"rom", "read-only memory block", true, false,
         [](u32 size, const std::string&) -> std::unique_ptr<Device> {
             return std::make_unique<RamDevice>(size == 0 ? 256u : size, true);
         }},
        {"custom", "your own registers, settable by hand, can raise IRQ", true, true,
         [](u32 size, const std::string& name) -> std::unique_ptr<Device> {
             // `size` is words here rather than bytes: a custom peripheral is
             // counted in registers, which is how its author thinks of it.
             return std::make_unique<CustomDevice>(size == 0 ? 4u : size,
                                                   name.empty() ? "custom" : name);
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

std::unique_ptr<Device> make_device(std::string_view type, u32 size, const std::string& name) {
    const DeviceType* found = find_device_type(type);
    return found == nullptr ? nullptr : found->create(size, name);
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
    bool in_machine = false;

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
            in_machine = false;
            continue;
        }
        if (line == "[machine]") {
            in_machine = true;
            in_device = false;
            continue;
        }
        if (!line.empty() && line[0] == '[') {
            fail("unknown table '" + line + "'; only [machine] and [[device]] are understood");
            return config;
        }
        if (!in_device && !in_machine) {
            fail("'" + line + "' appears before any [machine] or [[device]]");
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

        const auto number = [&](u32& target) {
            if (!parse_number(value, target)) {
                fail("'" + value + "' is not a number");
                return false;
            }
            return true;
        };

        if (in_machine) {
            if (key != "imem" && key != "dmem") {
                fail("unknown key '" + key + "' in [machine]; expected imem or dmem");
                return config;
            }
            u32 size = 0;
            if (!number(size)) return config;
            const std::string checked = check_memory_size(key, size);
            if (!checked.empty()) {
                fail(checked);
                return config;
            }
            (key == "imem" ? config.imem_size : config.dmem_size) = size;
            continue;
        }

        DeviceSpec& spec = config.devices.back();

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
        } else if (key == "address") {
            u32 addr = 0;
            if (!number(addr)) return config;
            if (addr < kMmioBase || addr - kMmioBase >= kMmioSize) {
                fail("address " + hex_address(addr) + " is outside the peripheral window (" +
                     hex_address(kMmioBase) + ".." + hex_address(kMmioBase + kMmioSize - 1) +
                     ")");
                return config;
            }
            // Rounded down to the slot it falls in rather than rejected: a
            // person naming an address inside a device means that device.
            spec.slot = slot_of_address(addr);
        } else if (key == "name") {
            spec.name = value;
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
    out << "# rv32 machine configuration.\n"
           "# Each [[device]] sits at an address in the window at 0xffff0000; see\n"
           "# docs/memory-map.md for the register layouts.\n";

    if (config.imem_size != 0 || config.dmem_size != 0) {
        out << "\n[machine]\n";
        if (config.imem_size != 0) out << "imem = " << config.imem_size << "\n";
        if (config.dmem_size != 0) out << "dmem = " << config.dmem_size << "\n";
    }

    for (const DeviceSpec& spec : config.devices) {
        out << "\n[[device]]\n";
        out << "type = \"" << spec.type << "\"\n";
        // Written as an address rather than a slot number: an address is what
        // the program uses and what the panel shows, so a file that says one
        // can be read against the code without a conversion in between.
        out << "address = " << hex_address(slot_address(spec.slot)) << "\n";
        if (!spec.name.empty()) out << "name = \"" << spec.name << "\"\n";
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

DeviceConfig describe(const Hart& hart) {
    DeviceConfig config = describe(hart.bus());
    config.imem_size = hart.imem().size();
    config.dmem_size = hart.bus().dmem().size();
    return config;
}

std::string apply_machine_config(Hart& hart, const DeviceConfig& config,
                                 const std::string& base_directory,
                                 std::vector<std::string>* warnings) {
    if (!config.ok()) return config.error;
    if (config.imem_size != 0 || config.dmem_size != 0) {
        hart.resize_memories(config.imem_size != 0 ? config.imem_size : hart.imem().size(),
                             config.dmem_size != 0 ? config.dmem_size : hart.bus().dmem().size());
    }
    return apply_device_config(hart.bus(), config, base_directory, warnings);
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
        if (const auto* custom = dynamic_cast<const CustomDevice*>(device)) {
            spec.size = custom->word_count();
            spec.name = custom->name();
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

std::size_t first_free_slot(const Bus& bus, std::size_t span) {
    const std::size_t width = std::max<std::size_t>(1, span);
    for (std::size_t slot = 0; slot + width <= kMmioSlots; ++slot) {
        bool free_here = true;
        for (std::size_t index = slot; index < slot + width; ++index) {
            if (bus.device_at_slot(index) != nullptr) free_here = false;
        }
        if (free_here) return slot;
    }
    return 0;  // a full window: the caller gets an overlap and a warning
}

std::string apply_device_config(Bus& bus, const DeviceConfig& config,
                                const std::string& base_directory,
                                std::vector<std::string>* warnings) {
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
            device = make_device(spec.type, spec.size, spec.name);
        }
        if (device == nullptr) return "unknown device type '" + spec.type + "'";

        // An overlap is worth saying out loud but not worth refusing over: the
        // machine still builds, with the later device winning the addresses
        // they share.
        if (warnings != nullptr) {
            const std::size_t span = std::max<std::size_t>(1, device->slot_count());
            for (const Device* other : bus.devices_overlapping(spec.slot, span)) {
                warnings->push_back("'" + spec.type + "' at " +
                                    hex_address(slot_address(spec.slot)) + " overlaps '" +
                                    std::string(other->type_name()) + "' at " +
                                    hex_address(other->base_address()) +
                                    "; the later one wins the addresses they share");
            }
        }

        auto* ram = dynamic_cast<RamDevice*>(device.get());
        Device* attached = bus.attach(spec.slot, std::move(device));
        if (attached == nullptr) {
            return "'" + spec.type + "' at slot " + std::to_string(spec.slot) +
                   " runs off the end of the peripheral window";
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
