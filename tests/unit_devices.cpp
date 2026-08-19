// Configurable peripherals.
//
// The design claim under test: because every device implements a
// side-effect-free peek/poke pair, the debugger's existing one-word memory
// delta already undoes a write to a device, and the memory view can edit a
// device register like any other address. If that holds, adding a peripheral
// costs nothing anywhere else.
#include "core/device_config.hpp"
#include "dbg/history.hpp"
#include "machine_fixture.hpp"

using namespace fixture;
using namespace rv::core;
using rv::isa::InstrId;

namespace {

/// `sw value, offset(base)` where base is the MMIO window.
std::vector<rv::Word> store_program(rv::u32 offset, rv::u32 value) {
    return {
        U(InstrId::LUI, 1, static_cast<rv::i32>(0xffff'0000u)),
        U(InstrId::LUI, 2, static_cast<rv::i32>(value & 0xffff'f000u)),
        I(InstrId::ADDI, 2, 2, static_cast<rv::i32>(rv::sign_extend(value & 0xfffu, 12))),
        S(InstrId::SW, 1, 2, static_cast<rv::i32>(offset)),
        EBREAK(),
    };
}

}  // namespace

// ---- the catalogue --------------------------------------------------------

RV_TEST(devices, the_catalogue_can_build_every_type_it_lists) {
    RV_CHECK(!device_catalogue().empty());
    for (const DeviceType& type : device_catalogue()) {
        std::unique_ptr<Device> device = make_device(type.name, 64);
        RV_CHECK(device != nullptr);
        if (device == nullptr) continue;
        // A device must name itself the way the catalogue does, or a saved
        // configuration would not round-trip.
        const bool ram_alias = type.name == "ram" || type.name == "rom";
        RV_CHECK(ram_alias || device->type_name() == type.name);
        RV_CHECK(!device->description().empty());
    }
}

RV_TEST(devices, an_unknown_type_is_refused_rather_than_guessed) {
    RV_CHECK(make_device("frobnicator") == nullptr);
    RV_CHECK(find_device_type("frobnicator") == nullptr);
}

// ---- attaching and detaching ----------------------------------------------

RV_TEST(devices, the_default_machine_has_the_documented_layout) {
    Machine m;
    const Bus& bus = m.hart().bus();
    RV_CHECK(bus.uart() != nullptr);
    RV_CHECK(bus.leds() != nullptr);
    RV_CHECK(bus.timer() != nullptr);
    RV_CHECK_EQ(bus.leds()->base_address(), 0xffff'0010u);
    RV_CHECK_EQ(bus.timer()->base_address(), 0xffff'0040u);
}

RV_TEST(devices, a_slot_holds_one_device_at_a_time) {
    Machine m;
    Bus& bus = m.hart().bus();
    // Slot 1 already has the LEDs.
    RV_CHECK(bus.attach(1, std::make_unique<LedDevice>()) == nullptr);

    RV_CHECK(bus.detach(1));
    RV_CHECK(bus.leds() == nullptr);
    RV_CHECK(bus.attach(1, std::make_unique<ValueDevice>()) != nullptr);
}

RV_TEST(devices, a_memory_block_takes_as_many_slots_as_it_needs) {
    Machine m;
    Bus& bus = m.hart().bus();
    bus.detach_all();

    // 64 bytes at 16 bytes per slot is four slots.
    Device* ram = bus.attach(0, std::make_unique<RamDevice>(64));
    RV_CHECK(ram != nullptr);
    RV_CHECK_EQ(ram->slot_count(), std::size_t{4});
    RV_CHECK(bus.device_at_slot(3) == ram);
    RV_CHECK(bus.device_at_slot(4) == nullptr);

    // And the slots it spans are genuinely taken.
    RV_CHECK(bus.attach(2, std::make_unique<LedDevice>()) == nullptr);
    RV_CHECK(bus.attach(4, std::make_unique<LedDevice>()) != nullptr);
}

RV_TEST(devices, an_address_with_nothing_attached_faults) {
    // Reading as zero would let a program touch a peripheral that is not there
    // and never find out.
    Machine m;
    m.hart().bus().detach_all();
    m.load({
        U(InstrId::LUI, 1, static_cast<rv::i32>(0xffff'0000u)),
        I(InstrId::LW, 2, 1, 0),
        EBREAK(),
    });
    m.step();
    const StepOutcome outcome = m.step();
    RV_CHECK(outcome.trapped);
    RV_CHECK_EQ(static_cast<int>(outcome.cause), static_cast<int>(TrapCause::LoadAccessFault));
}

// ---- memory blocks ---------------------------------------------------------

RV_TEST(devices, a_ram_block_stores_and_loads_like_memory) {
    Machine m;
    Bus& bus = m.hart().bus();
    bus.attach(8, std::make_unique<RamDevice>(64));

    m.load(store_program(0x80, 0x1234'5000));  // slot 8 is at +0x80
    m.run();
    RV_CHECK_HEX(bus.peek_word(0xffff'0080u), 0x1234'5000u);
}

RV_TEST(devices, a_rom_ignores_stores_but_can_still_be_edited_by_hand) {
    Machine m;
    Bus& bus = m.hart().bus();
    auto* rom = static_cast<RamDevice*>(bus.attach(8, std::make_unique<RamDevice>(64, true)));
    RV_CHECK(rom != nullptr);

    m.load(store_program(0x80, 0x1234'5000));
    m.run();
    RV_CHECK_HEX(bus.peek_word(0xffff'0080u), 0u);  // the program's write did nothing

    // But the debugger's path is not the program's path: being unable to edit a
    // ROM from a memory view would be an obstacle, not a safeguard.
    bus.poke_word(0xffff'0080u, 0xdead'beefu);
    RV_CHECK_HEX(bus.peek_word(0xffff'0080u), 0xdead'beefu);
}

RV_TEST(devices, a_preloaded_block_survives_a_reset) {
    // The point of "give me some ready-made memory": a lookup table has to
    // still be there after the machine restarts.
    Machine m;
    auto* rom = static_cast<RamDevice*>(
        m.hart().bus().attach(8, std::make_unique<RamDevice>(32, true)));
    rom->load_words({0x1111'1111u, 0x2222'2222u});

    RV_CHECK_HEX(m.hart().bus().peek_word(0xffff'0080u), 0x1111'1111u);
    m.hart().reset(0);
    RV_CHECK_HEX(m.hart().bus().peek_word(0xffff'0080u), 0x1111'1111u);
    RV_CHECK_HEX(m.hart().bus().peek_word(0xffff'0084u), 0x2222'2222u);
}

RV_TEST(devices, a_program_can_read_a_preloaded_table) {
    Machine m;
    auto* rom = static_cast<RamDevice*>(
        m.hart().bus().attach(8, std::make_unique<RamDevice>(32, true)));
    rom->load_words({0x0000'00aau, 0x0000'00bbu});

    m.load({
        U(InstrId::LUI, 1, static_cast<rv::i32>(0xffff'0000u)),
        I(InstrId::LW, 2, 1, 0x80),
        I(InstrId::LW, 3, 1, 0x84),
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(2), 0xaau);
    RV_CHECK_HEX(m.reg(3), 0xbbu);
}

// ---- input devices ---------------------------------------------------------

RV_TEST(devices, a_value_device_hands_the_program_what_the_user_typed) {
    Machine m;
    auto* value = static_cast<ValueDevice*>(
        m.hart().bus().attach(8, std::make_unique<ValueDevice>()));
    value->set_input(0, 1234);

    m.load({
        U(InstrId::LUI, 1, static_cast<rv::i32>(0xffff'0000u)),
        I(InstrId::LW, 2, 1, 0x80),
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(2), 1234u);
}

RV_TEST(devices, a_button_raises_the_external_interrupt_line) {
    Machine m;
    auto* button = static_cast<ButtonDevice*>(
        m.hart().bus().attach(8, std::make_unique<ButtonDevice>()));

    RV_CHECK_HEX(m.hart().bus().pending_interrupts(0), 0u);
    button->set_input(0, 1);  // press
    RV_CHECK_HEX(m.hart().bus().pending_interrupts(0), kIrqExternal);

    // And it can be told not to, for a button that is only polled.
    button->set_input(1, 0);
    RV_CHECK_HEX(m.hart().bus().pending_interrupts(0), 0u);
}

RV_TEST(devices, a_device_describes_its_own_controls) {
    // This is what lets the interface render a device it has never heard of.
    SwitchDevice switches;
    const std::vector<InputControl> controls = switches.inputs();
    RV_CHECK_EQ(controls.size(), std::size_t{1});
    RV_CHECK_EQ(static_cast<int>(controls[0].kind), static_cast<int>(InputKind::Toggle));
    RV_CHECK_EQ(controls[0].bit_count, 32);

    ButtonDevice button;
    RV_CHECK_EQ(button.inputs().size(), std::size_t{2});
    RV_CHECK_EQ(static_cast<int>(button.inputs()[0].kind), static_cast<int>(InputKind::Button));

    LedDevice leds;
    RV_CHECK(leds.inputs().empty());  // output only
}

RV_TEST(devices, a_display_renders_its_digits) {
    DisplayDevice display(4);
    RV_CHECK_STR(display.rendered(), "----");  // blank at reset
    display.poke(0, 0x01020304);
    RV_CHECK_STR(display.rendered(), "1234");
}

// ---- the debugger's view ---------------------------------------------------

RV_TEST(devices, the_memory_view_can_read_and_edit_a_device_register) {
    // peek/poke is what makes a peripheral appear in the memory view rather
    // than as a hole, and what makes it editable there.
    Machine m;
    Bus& bus = m.hart().bus();

    bus.poke_word(0xffff'0010u, 0x0000'00ffu);  // the LED register
    RV_CHECK_HEX(bus.leds()->value(), 0xffu);
    RV_CHECK_HEX(bus.peek_word(0xffff'0010u), 0xffu);
}

RV_TEST(devices, peeking_a_device_has_no_side_effects) {
    // A memory view redraws constantly. If peeking consumed anything, merely
    // looking at the machine would change it.
    Machine m;
    const Bus& bus = m.hart().bus();
    const std::string before = bus.uart_output();
    for (int i = 0; i < 100; ++i) {
        (void)bus.peek_word(0xffff'0000u);
        (void)bus.peek_word(0xffff'0004u);
    }
    RV_CHECK_STR(bus.uart_output(), before);
}

RV_TEST(devices, reverse_stepping_undoes_a_write_to_a_device) {
    // The claim the whole design rests on: no per-device history is needed,
    // because the ordinary one-word delta plus peek/poke covers it.
    Machine m;
    m.load(store_program(0x10, 0x0000'0000u));  // the LED register
    m.hart().bus().leds()->set_raw(0xaaaa'aaaau);

    rv::dbg::StepHistory history(64);
    const auto step_recording = [&] {
        const StepOutcome outcome = m.hart().step();
        history.record(outcome, m.hart());
    };

    step_recording();
    step_recording();
    step_recording();
    RV_CHECK_HEX(m.hart().bus().leds()->value(), 0xaaaa'aaaau);

    step_recording();  // the store
    RV_CHECK_HEX(m.hart().bus().leds()->value(), 0u);

    history.undo(m.hart());
    RV_CHECK_HEX(m.hart().bus().leds()->value(), 0xaaaa'aaaau);
}

RV_TEST(devices, reverse_stepping_undoes_a_write_to_a_ram_block) {
    Machine m;
    m.hart().bus().attach(8, std::make_unique<RamDevice>(64));
    m.load(store_program(0x80, 0x1234'5000u));
    m.hart().bus().poke_word(0xffff'0080u, 0x9999'9999u);

    rv::dbg::StepHistory history(64);
    for (int i = 0; i < 3; ++i) history.record(m.hart().step(), m.hart());
    history.record(m.hart().step(), m.hart());  // the store
    RV_CHECK_HEX(m.hart().bus().peek_word(0xffff'0080u), 0x1234'5000u);

    history.undo(m.hart());
    RV_CHECK_HEX(m.hart().bus().peek_word(0xffff'0080u), 0x9999'9999u);
}

// ---- configuration ---------------------------------------------------------

RV_TEST(devices, a_configuration_file_describes_a_machine) {
    const DeviceConfig config = parse_device_config(
        "# a comment\n"
        "[[device]]\n"
        "type = \"uart\"\n"
        "slot = 0\n"
        "\n"
        "[[device]]\n"
        "type = \"switches\"\n"
        "slot = 2\n"
        "value = 0b1010\n"
        "\n"
        "[[device]]\n"
        "type = \"rom\"\n"
        "slot = 8\n"
        "size = 64\n",
        "test.toml");

    RV_CHECK(config.ok());
    RV_CHECK_EQ(config.devices.size(), std::size_t{3});
    RV_CHECK_STR(config.devices[1].type, "switches");
    RV_CHECK_HEX(config.devices[1].value, 0b1010u);
    RV_CHECK_EQ(config.devices[2].size, 64u);
}

RV_TEST(devices, applying_a_configuration_replaces_what_was_there) {
    // A machine described in a file gets exactly what the file says, so a
    // configuration is reproducible rather than additive.
    Machine m;
    RV_CHECK(m.hart().bus().uart() != nullptr);

    const DeviceConfig config = parse_device_config(
        "[[device]]\n"
        "type = \"leds\"\n"
        "slot = 0\n",
        "test.toml");
    RV_CHECK_STR(apply_device_config(m.hart().bus(), config), "");

    RV_CHECK(m.hart().bus().uart() == nullptr);
    RV_CHECK(m.hart().bus().leds() != nullptr);
    RV_CHECK_EQ(m.hart().bus().leds()->base_address(), 0xffff'0000u);
}

RV_TEST(devices, a_configuration_round_trips_through_text) {
    Machine m;
    m.hart().bus().switches()->set_value(0x5555);
    const DeviceConfig described = describe(m.hart().bus());
    const DeviceConfig reparsed = parse_device_config(write_device_config(described), "out.toml");

    RV_CHECK(reparsed.ok());
    RV_CHECK_EQ(reparsed.devices.size(), described.devices.size());
    for (std::size_t index = 0; index < reparsed.devices.size(); ++index) {
        RV_CHECK_STR(reparsed.devices[index].type, described.devices[index].type);
        RV_CHECK_EQ(reparsed.devices[index].slot, described.devices[index].slot);
    }
}

RV_TEST(devices, configuration_errors_name_the_line_and_the_problem) {
    const auto fails_with = [](const char* text, const char* needle) {
        const DeviceConfig config = parse_device_config(text, "t.toml");
        return !config.ok() && config.error.find(needle) != std::string::npos;
    };

    RV_CHECK(fails_with("[[device]]\ntype = \"frobnicator\"\n", "unknown device type"));
    // And it says what the known types are, rather than leaving you guessing.
    RV_CHECK(fails_with("[[device]]\ntype = \"frobnicator\"\n", "uart"));
    RV_CHECK(fails_with("[[device]]\ntype = \"uart\"\nslot = 99\n", "out of range"));
    RV_CHECK(fails_with("[[device]]\ntype = \"uart\"\nwibble = 1\n", "unknown key"));
    RV_CHECK(fails_with("[[device]]\ntype = uart\n", "must be quoted"));
    RV_CHECK(fails_with("type = \"uart\"\n", "before any [[device]]"));
    RV_CHECK(fails_with("[[device]]\nslot = 0\n", "has no type"));
    // The line number is part of the message.
    RV_CHECK(fails_with("[[device]]\ntype = \"uart\"\nwibble = 1\n", "t.toml:3"));
}

RV_TEST(devices, a_conflicting_slot_is_reported_rather_than_silently_dropped) {
    Machine m;
    const DeviceConfig config = parse_device_config(
        "[[device]]\n"
        "type = \"ram\"\n"
        "slot = 0\n"
        "size = 64\n"
        "\n"
        "[[device]]\n"
        "type = \"leds\"\n"
        "slot = 2\n",  // inside the RAM block's four slots
        "test.toml");
    RV_CHECK(config.ok());

    const std::string error = apply_device_config(m.hart().bus(), config);
    RV_CHECK_NE(error.find("slot is occupied"), std::string::npos);
}
