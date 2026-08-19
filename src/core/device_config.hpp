// Describing a machine's peripherals in a file.
//
// The catalogue maps a type name to a constructor, so both the configuration
// file and the "add device" list in the interface work from one list rather
// than each hard-coding what exists.
//
// The file format is a deliberate subset of TOML -- arrays of tables with
// scalar keys -- parsed here in about a hundred lines rather than taken as a
// dependency. What is supported is documented in docs/memory-map.md; anything
// else is reported with a line number rather than ignored.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/bus.hpp"
#include "core/device.hpp"

namespace rv::core {

/// One entry in the catalogue of device types that can be created by name.
struct DeviceType {
    std::string_view name;
    std::string_view description;
    /// True if `size` is meaningful, which is what the interface uses to decide
    /// whether to ask for one.
    bool takes_size = false;
    std::unique_ptr<Device> (*create)(u32 size);
};

/// Every device type that can be named in a file or added from the interface.
const std::vector<DeviceType>& device_catalogue();
const DeviceType* find_device_type(std::string_view name);
std::unique_ptr<Device> make_device(std::string_view type, u32 size = 0);

/// One device as a file describes it.
struct DeviceSpec {
    std::string type;
    std::size_t slot = 0;
    /// Bytes, for ram and rom.
    u32 size = 0;
    /// Initial value for an input device, or the contents of a display.
    u32 value = 0;
    bool has_value = false;
    /// A .mem image to preload a ram or rom block from, relative to the
    /// configuration file.
    std::string load;
    bool read_only = false;
    /// Digits, for a display.
    int digits = 4;
};

struct DeviceConfig {
    std::vector<DeviceSpec> devices;
    std::string error;   // empty when the file parsed
    bool ok() const { return error.empty(); }
};

/// The directory part of a path, which is what `load = "..."` entries are
/// resolved against -- a configuration should refer to its images relative to
/// itself, so a directory can be moved or shared whole.
std::string directory_of(const std::string& path);

/// Parse a configuration. `source_name` appears in error messages.
DeviceConfig parse_device_config(const std::string& text, const std::string& source_name);
DeviceConfig read_device_config(const std::string& path);

/// Render a configuration back out, so the interface can save what a person
/// assembled by hand.
std::string write_device_config(const DeviceConfig& config);

/// Describe the devices currently on a bus, for saving.
DeviceConfig describe(const Bus& bus);

/// Replace everything on the bus with what the configuration says. Returns an
/// error message, or an empty string. `base_directory` is where `load = "..."`
/// paths are resolved from.
std::string apply_device_config(Bus& bus, const DeviceConfig& config,
                                const std::string& base_directory = {});

}  // namespace rv::core
