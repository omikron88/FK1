#include "fk1/machine.hpp"

#include "fk1/disk_image_8sd.hpp"
#include "fk1/embedded_rom.hpp"
#include "fk1/keyboard.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const char* message)
{
    if(!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

void install_halt_rom(fk1::Machine& machine)
{
    std::array<std::uint8_t, 2 * 1024> halt_rom{};
    halt_rom.fill(0x76);
    expect(machine.set_rom_image(halt_rom), "the HALT test ROM must be accepted");
    machine.reset();
}

void test_deterministic_frame_clock()
{
    fk1::Machine machine;
    install_halt_rom(machine);

    machine.run_cycles(fk1::Machine::cycles_per_frame - 4);
    expect(machine.frame_count() == 0, "a partial frame must not advance the video frame counter");

    machine.run_cycles(4);
    expect(machine.frame_count() == 1, "80,000 CPU cycles must produce one 50 Hz frame");
    expect(machine.cycle_count() == fk1::Machine::cycles_per_frame, "all CPU cycles must be accounted for");
    expect(machine.master_tick_count() == fk1::Machine::master_ticks_per_frame,
           "one video frame must contain 240,000 master ticks");

    machine.run_cycles(fk1::Machine::cycles_per_frame * 2);
    expect(machine.frame_count() == 3, "multiple completed frames must be retained");
}

void test_memory_maps()
{
    fk1::Machine machine;
    std::array<std::uint8_t, 2 * 1024> rom{};
    rom[0] = 0xA5;
    rom[rom.size() - 1] = 0x5A;
    expect(machine.set_rom_image(rom), "the memory-map test ROM must be accepted");
    machine.reset();

    expect(machine.memory_map() == fk1::Machine::MemoryMap::rom_video,
           "reset must select the ROM and video RAM map");
    expect(machine.read_memory(0x0000) == 0xA5, "the reset map must expose ROM at 0x0000");
    expect(machine.read_memory(0x3FFF) == 0x5A, "a 2 KiB ROM must mirror through 0x3fff");

    machine.write_memory(0x0000, 0x11);
    machine.write_memory(0x4000, 0x22);
    machine.write_memory(0x8000, 0x33);
    expect(machine.read_memory(0x0000) == 0xA5, "writes to ROM must be ignored");
    expect(machine.read_video_ram(0) == 0x22, "0x4000 must address physical video RAM in the reset map");
    expect(machine.read_memory(0x8000) == 0x33, "upper RAM must be writable in the reset map");

    expect(machine.read_io(0xA5B7) == 0xFF, "the RAM-map selector must leave the data bus pulled high");
    expect(machine.memory_map() == fk1::Machine::MemoryMap::ram,
           "A6-A4 equal to 0x30 must select the full RAM map");
    expect(machine.read_memory(0x0000) == 0x00, "the RAM map must expose physical RAM below 0x4000");
    expect(machine.read_memory(0x4000) == 0x00, "the RAM map must hide video RAM behind physical RAM");
    expect(machine.read_memory(0x8000) == 0x33, "upper RAM must be shared by both maps");

    machine.write_memory(0x0000, 0x44);
    machine.write_memory(0x4000, 0x55);
    expect(machine.read_io(0xFFDE) == 0xFF, "the ROM-map selector must leave the data bus pulled high");
    expect(machine.memory_map() == fk1::Machine::MemoryMap::rom_video,
           "A6-A4 equal to 0x50 must restore the ROM and video RAM map");
    expect(machine.read_memory(0x0000) == 0xA5, "restoring the reset map must reveal ROM again");
    expect(machine.read_memory(0x4000) == 0x22, "video RAM must remain independent from hidden RAM");

    static_cast<void>(machine.read_io(0x0030));
    expect(machine.read_memory(0x0000) == 0x44, "hidden low RAM must retain its contents");
    expect(machine.read_memory(0x4000) == 0x55, "hidden middle RAM must retain its contents");
}

void test_z80_executes_through_the_machine_bus()
{
    fk1::Machine machine;
    std::array<std::uint8_t, 2 * 1024> rom{};
    rom.fill(0x00);

    // ROM: IN A,(30h), then continue in underlying RAM. After RAM executes
    // IN A,(50h), opcode fetching returns to ROM for the HALT at address 9.
    rom[0] = 0xDB;
    rom[1] = 0x30;
    rom[9] = 0x76;
    expect(machine.set_rom_image(rom), "the CPU bus test ROM must be accepted");
    machine.reset();

    static_cast<void>(machine.read_io(0x0030));
    machine.write_memory(2, 0x3E); // LD A,5Ah
    machine.write_memory(3, 0x5A);
    machine.write_memory(4, 0x32); // LD (8000h),A
    machine.write_memory(5, 0x00);
    machine.write_memory(6, 0x80);
    machine.write_memory(7, 0xDB); // IN A,(50h)
    machine.write_memory(8, 0x50);
    static_cast<void>(machine.read_io(0x0050));

    // One additional internal HALT M1 cycle makes the deferred HALT signal
    // visible through the library's public state.
    machine.run_cycles(50);

    expect(machine.cpu_halted(), "the Z80 must execute the test program through HALT");
    expect(machine.program_counter() == 10, "the Z80 program counter must advance across both maps");
    expect(machine.memory_map() == fk1::Machine::MemoryMap::rom_video,
           "CPU I/O must restore the ROM and video RAM map");
    expect(machine.read_memory(0x8000) == 0x5A, "CPU memory writes must reach upper physical RAM");
    expect(machine.cycle_count() == 50, "the test program and first HALT M1 cycle must consume 50 Z80 cycles");
}

void test_peripheral_io_mirroring()
{
    fk1::Machine machine;

    machine.write_io(0x0063, 0x88);
    machine.write_io(0x12E5, 0x7B);
    expect(machine.vertical_scroll() == 0x7B,
           "mirrored group 0x60 port B writes must update video scrolling");
    expect(machine.read_io(0xFFE1) == 0x7B,
           "A7 and the upper I/O byte must not affect 8255 register decoding");

    machine.write_io(0x0041, 0x4E); // asynchronous, x16, 8 data bits, one stop bit
    machine.write_io(0x00C3, 0x05); // mirrored control: enable transmitter and receiver
    expect((machine.read_io(0x1241) & 0x01U) != 0,
           "mirrored 8251 status reads must expose TXRDY");
    machine.write_io(0x0030, 6);
    expect(machine.interrupt_vector() == 0x0A,
           "TXRDY from the 8251 must drive level-sensitive interrupt I2");
    machine.write_io(0x0040, 0x55);
    expect(!machine.interrupt_pending(),
           "filling the UART transmit buffer must remove I2 while RXRDY is inactive");
}

void test_video_and_mouse_interrupt_latches()
{
    fk1::Machine machine;
    install_halt_rom(machine);

    machine.set_mouse_signals(0xA0); // explicit low-level state for the latch-separation test
    machine.write_io(0x0063, 0x88); // PA/PB output, PC upper input
    machine.write_io(0x0060, 0x0C); // enable video I4 and mouse I3
    machine.write_io(0xA5B7, 5);    // mirrored group 30h: enable I7 through I3

    expect(machine.interrupt_mask() == 5 && !machine.interrupt_pending(),
           "a mirrored group 30h write must program the 3214 mask without creating a request");
    expect(machine.memory_map() == fk1::Machine::MemoryMap::rom_video,
           "writing the interrupt mask must not perform the group 30h read-side map switch");

    machine.run_cycles(fk1::Machine::cycles_per_frame);
    expect(machine.video_interrupt_latched() && machine.interrupt_vector() == 0x06,
           "an enabled 50 Hz V impulse must latch I4 and select vector 06h");

    machine.write_io(0x0060, 0x08); // disabling PA2 must not clear an existing I4 latch
    expect(machine.video_interrupt_latched(), "clearing PA2 must preserve an already latched video interrupt");
    expect(machine.read_io(0xFFFE) == 0xA0,
           "a group 70h read must return the current raw mouse signals");
    expect(machine.video_interrupt_latched(), "a group 70h read must not clear video I4");
    machine.write_io(0x807F, 0xA5);
    expect(!machine.video_interrupt_latched() && !machine.interrupt_pending(),
           "a mirrored group 70h write must clear I4 regardless of its data byte");

    machine.set_mouse_signals(0x5A);
    expect(machine.mouse_interrupt_latched() && machine.interrupt_vector() == 0x08,
           "a raw mouse-signal change with PA3 enabled must latch I3 and select vector 08h");
    machine.write_io(0x0070, 0);
    expect(machine.mouse_interrupt_latched(), "a group 70h write must not clear mouse I3");
    expect(machine.read_io(0x1277) == 0x5A,
           "a mirrored group 70h read must expose the unassigned raw mouse signal byte");
    expect(!machine.mouse_interrupt_latched() && !machine.interrupt_pending(),
           "reading group 70h must clear only the mouse interrupt latch");

    machine.write_io(0x0060, 0x00);
    machine.set_mouse_signals(0xA5);
    expect(!machine.mouse_interrupt_latched(),
           "mouse edges while PA3 is disabled must not create a deferred interrupt");
}

void test_mouse_quadrature_and_buttons()
{
    fk1::Machine machine;
    install_halt_rom(machine);
    machine.write_io(0x0063, 0x88); // PA/PB output, PC upper input
    machine.write_io(0x0060, 0x08); // enable mouse edge latch I3
    machine.write_io(0x0030, 5);    // expose I7 through I3

    expect(machine.mouse_signals() == 0x30,
           "T1 and T2 must idle high while both quadrature pairs start at 00");

    constexpr auto edge_cycles =
        fk1::Machine::mouse_edge_interval_ticks / fk1::Machine::master_ticks_per_cpu_cycle;
    const auto expect_mouse_edge = [&](const std::uint8_t expected, const char* message) {
        expect(machine.mouse_interrupt_latched() && machine.interrupt_vector() == 0x08,
               "every enabled quadrature or button edge must latch mouse interrupt I3");
        expect(machine.read_io(0x0070) == expected, message);
        expect(!machine.mouse_interrupt_latched(),
               "reading port 70h must acknowledge the current mouse edge");
    };

    machine.mouse_motion(4, 0);
    expect_mouse_edge(0x32, "positive X must start with BX leading AX");
    for(const auto state : std::array<std::uint8_t, 3>{0x33, 0x31, 0x30}) {
        machine.run_cycles(edge_cycles + 1U);
        expect_mouse_edge(state, "positive X must follow 00-10-11-01-00 Gray-code order");
    }

    machine.run_cycles(edge_cycles + 1U); // accumulate time for an immediate new edge
    machine.mouse_motion(-4, 0);
    expect_mouse_edge(0x31, "negative X must start with AX leading BX");
    for(const auto state : std::array<std::uint8_t, 3>{0x33, 0x32, 0x30}) {
        machine.run_cycles(edge_cycles + 1U);
        expect_mouse_edge(state, "negative X must reverse the quadrature Gray-code order");
    }

    machine.run_cycles(edge_cycles + 1U);
    machine.mouse_motion(0, 1);
    expect_mouse_edge(0x34, "positive Y must place AY and BY on PD2 and PD3");

    machine.set_mouse_signals(0x30);
    static_cast<void>(machine.read_io(0x0070));
    machine.set_mouse_button(fk1::MouseButton::button_1, true);
    expect_mouse_edge(0x20, "SDL left button T1 must be active-low on PD4");
    machine.set_mouse_button(fk1::MouseButton::button_1, false);
    expect_mouse_edge(0x30, "releasing T1 must restore its pull-up level");
    machine.set_mouse_button(fk1::MouseButton::button_2, true);
    expect_mouse_edge(0x10, "SDL right button T2 must be active-low on PD5");
    machine.set_mouse_button(fk1::MouseButton::button_2, false);
    expect_mouse_edge(0x30, "releasing T2 must restore its pull-up level");

    machine.write_io(0x0060, 0x00);
    machine.run_cycles(edge_cycles + 1U);
    machine.mouse_motion(1, 0);
    expect(machine.mouse_signals() == 0x32 && !machine.mouse_interrupt_latched(),
           "PA3=0 must leave physical quadrature running without creating deferred I3");
}

void test_z80_im2_interrupt_acknowledge()
{
    fk1::Machine machine;
    std::array<std::uint8_t, 2 * 1024> rom{};
    rom.fill(0x00);

    const std::array<std::uint8_t, 27> program{
        0xF3,                   // DI
        0x31, 0x00, 0xFF,       // LD SP,FF00h
        0x3E, 0x88,             // LD A,88h
        0xD3, 0x63,             // OUT (63h),A: configure control PPI
        0x3E, 0x04,             // LD A,04h
        0xD3, 0x60,             // OUT (60h),A: enable video interrupt
        0x3E, 0x04,             // LD A,04h
        0xD3, 0x30,             // OUT (30h),A: enable I7 through I4
        0x3E, 0x80,             // LD A,80h
        0xED, 0x47,             // LD I,A
        0xED, 0x5E,             // IM 2
        0xFB,                   // EI
        0x76,                   // HALT
        0xC3, 0x17, 0x00,       // JP 0017h
    };
    std::copy(program.begin(), program.end(), rom.begin());
    expect(machine.set_rom_image(rom), "the IM2 test ROM must be accepted");
    machine.reset();

    machine.write_memory(0x8006, 0x00);
    machine.write_memory(0x8007, 0x81);
    const std::array<std::uint8_t, 6> handler{
        0x3E, 0x99,             // LD A,99h
        0x32, 0x00, 0x82,       // LD (8200h),A
        0x76,                   // HALT without clearing I4
    };
    for(std::size_t offset = 0; offset < handler.size(); ++offset) {
        machine.write_memory(static_cast<std::uint16_t>(0x8100U + offset), handler[offset]);
    }

    machine.run_cycles(fk1::Machine::cycles_per_frame + 500);
    expect(machine.read_memory(0x8200) == 0x99,
           "the Z80 must read vector 06h during INTA and execute the IM2 handler at 8100h");
    expect(machine.video_interrupt_latched() && machine.interrupt_pending(),
           "the Z80 INTA cycle must not clear the level held by the video I4 latch");
    machine.write_io(0x0070, 0);
    expect(!machine.video_interrupt_latched() && !machine.interrupt_pending(),
           "only the explicit group 70h write must clear the acknowledged video request");
}

void test_keyboard_handshake_and_autorepeat()
{
    fk1::Machine machine;
    install_halt_rom(machine);

    machine.write_io(0x0003, 0xAE); // keyboard PB input, handshake mode 1
    machine.write_io(0x0003, 0x05); // BSR PC2: enable INTE_B
    machine.write_io(0x0030, 7);    // enable I7 through keyboard I1

    machine.keyboard_key_down(42, 0x41);
    expect(machine.keyboard_key_held() && machine.interrupt_vector() == 0x0C,
           "a key press must strobe the keyboard PPI and request I1");
    expect(machine.read_io(0x0001) == 0xBE && !machine.interrupt_pending(),
           "keyboard PB must receive active-low ASCII data and clear its handshake interrupt");

    machine.keyboard_key_down(42, 0x41);
    expect(!machine.interrupt_pending(),
           "duplicate key-down events from the host must not bypass emulated autorepeat");

    constexpr auto repeat_delay_cycles =
        fk1::Keyboard::repeat_delay_ticks / fk1::Machine::master_ticks_per_cpu_cycle;
    constexpr auto repeat_interval_cycles =
        fk1::Keyboard::repeat_interval_ticks / fk1::Machine::master_ticks_per_cpu_cycle;
    machine.run_cycles(repeat_delay_cycles);
    expect(machine.interrupt_vector() == 0x0C && machine.read_io(0x0001) == 0xBE,
           "a held key must repeat inverted data through the 8255 after half a second");
    machine.run_cycles(repeat_interval_cycles);
    expect(machine.interrupt_vector() == 0x0C && machine.read_io(0x0001) == 0xBE,
           "subsequent inverted repeats must arrive at four characters per second");

    machine.keyboard_key_up(42);
    machine.run_cycles(repeat_interval_cycles);
    expect(!machine.keyboard_key_held() && !machine.interrupt_pending(),
           "key release must stop repeat without sending a release code");

    machine.keyboard_key_down(43, 0x00);
    expect(machine.interrupt_vector() == 0x0C && machine.read_io(0x0001) == 0xFF,
           "logical Ctrl+@ code 00h must reach keyboard PB as active-low FFh");
    machine.keyboard_key_up(43);
}

void test_printer_file_sink_handshake_and_offline_status()
{
    fk1::Machine machine;
    install_halt_rom(machine);
    machine.write_io(0x0003, 0xAE); // PA printer output and PB keyboard input, both mode 1
    machine.write_io(0x0003, 0x0D); // BSR PC6: enable printer INTE_A
    machine.write_io(0x0030, 8);    // expose all interrupt inputs including printer I0

    expect(!machine.printer_online() && (machine.read_io(0x0002) & 0x20U) == 0,
           "a machine without a printer output file must report Centronics offline");
    machine.write_io(0x0000, 0x41);
    expect(!machine.take_printer_byte().has_value() && !machine.interrupt_pending(),
           "an offline printer must neither accept nor acknowledge a PA byte");

    machine.write_io(0x0003, 0xAE); // clear the unacknowledged offline transfer
    machine.write_io(0x0003, 0x0D);
    machine.set_printer_online(true);
    expect(machine.printer_online() && (machine.read_io(0x0002) & 0x30U) == 0x20U,
           "the file sink must assert ONLINE/SELECT on PC5 with PAPER END inactive on PC4");

    machine.write_io(0x0000, 0x00);
    const auto zero = machine.take_printer_byte();
    expect(zero.has_value() && *zero == 0x00 && !machine.take_printer_byte().has_value(),
           "an online printer must queue the complete raw eight-bit PA value exactly once");
    expect(machine.interrupt_pending() && machine.interrupt_vector() == 0x0E,
           "the virtual printer ACK must pass through 8255 INTR_A to priority input I0");
    expect((machine.read_io(0x0002) & 0xE8U) == 0xE8U,
           "an accepted byte must leave OBF_A and ACK_A inactive-high with INTR_A asserted");

    machine.write_io(0x0000, 0xFF);
    const auto all_bits = machine.take_printer_byte();
    expect(all_bits.has_value() && *all_bits == 0xFF,
           "printer redirection must remain binary and preserve all eight data bits");

    machine.reset();
    machine.write_io(0x0003, 0xAE);
    expect(machine.printer_online() && (machine.read_io(0x0002) & 0x30U) == 0x20U,
           "a system reset must not disconnect the selected host printer file");
    machine.set_printer_online(false);
    expect((machine.read_io(0x0002) & 0x20U) == 0,
           "disconnecting the file sink must return the Centronics interface offline");
}

void test_floppy_mechanics_and_ppi_signals()
{
    fk1::Machine machine;
    install_halt_rom(machine);

    expect(machine.floppy().selected_drive_index() == fk1::FloppySubsystem::drive_b,
           "the disk-data PPI pull-up must select drive B after reset");
    expect((machine.read_io(0x0062) & 0xF0U) == 0x70U,
           "an empty selected drive must expose Track 00, safe write protect, fixed PC6 and no Index");

    fk1::FloppyDiskImage drive_a_image;
    fk1::FloppyDiskImage drive_b_image;
    expect(machine.insert_floppy(
               fk1::FloppySubsystem::drive_a,
               std::move(drive_a_image),
               fk1::FloppyAccess::read_only),
           "drive A must accept a low-level FM image");
    expect(machine.insert_floppy(
               fk1::FloppySubsystem::drive_b,
               std::move(drive_b_image),
               fk1::FloppyAccess::read_write),
           "drive B must accept a writable low-level FM image");
    expect(machine.set_floppy_index_pulse_width(1'200),
           "Index pulse width must be configurable in master ticks");
    expect(!machine.set_floppy_index_pulse_width(fk1::FloppyDrive::revolution_ticks),
           "an Index pulse may not cover a complete revolution");
    expect((machine.read_io(0x0062) & 0xF0U) == 0xD0U,
           "writable drive B at phase zero must expose Track 00, fixed PC6 and active Index");

    machine.run_cycles(400);
    expect(machine.floppy().drive(fk1::FloppySubsystem::drive_a).rotation_phase() == 1'200
               && machine.floppy().drive(fk1::FloppySubsystem::drive_b).rotation_phase() == 1'200,
           "both inserted drives must rotate independently on the common 12-MHz timeline");
    expect((machine.read_io(0x0062) & 0x80U) == 0,
           "Index must become inactive at the configured pulse width");

    machine.write_io(0x0023, 0xA6); // mode set resets PC5 output to zero: select A
    expect(machine.floppy().selected_drive_index() == fk1::FloppySubsystem::drive_a
               && (machine.read_io(0x0062) & 0x20U) != 0,
           "PC5=0 on the data PPI must select read-only drive A without changing either phase");
    machine.write_io(0x0022, 0x20);
    expect(machine.floppy().selected_drive_index() == fk1::FloppySubsystem::drive_b
               && (machine.read_io(0x0062) & 0x20U) == 0,
           "PC5=1 must select writable drive B on the shared status cable");

    machine.write_io(0x0063, 0x88);
    machine.write_io(0x0062, 0x03); // assert STEP, PC1=1: outward
    machine.write_io(0x0062, 0x02); // trailing edge completes the step
    expect(machine.floppy().drive(fk1::FloppySubsystem::drive_b).head_track() == 1
               && (machine.read_io(0x0062) & 0x10U) == 0,
           "DIAG.MAC outward PC1 polarity and PC0 trailing edge must move only selected drive B");
    machine.write_io(0x0062, 0x01); // assert STEP, PC1=0: restore
    machine.write_io(0x0062, 0x00);
    expect(machine.floppy().drive(fk1::FloppySubsystem::drive_b).head_track() == 0
               && (machine.read_io(0x0062) & 0x10U) != 0,
           "DIAG.MAC restore polarity must return the selected head to Track 00");

    const auto phase_before_reset =
        machine.floppy().drive(fk1::FloppySubsystem::drive_b).rotation_phase();
    machine.reset();
    expect(machine.floppy().drive(fk1::FloppySubsystem::drive_b).media_present()
               && machine.floppy().drive(fk1::FloppySubsystem::drive_b).rotation_phase()
                   == phase_before_reset,
           "system reset must preserve inserted media and physical spindle phase");
}

void test_disk_timeout_is_armed_only_for_data_marks()
{
    const auto make_mark_image = [](const std::uint8_t data) {
        fk1::FloppyDiskImage image;
        const std::array<fk1::TimedFmByte, 1> mark{{
            {1'200U, data, 0xC7U},
        }};
        expect(image.set_track(0, mark), "the timeout probe mark must form a valid FM track");
        return image;
    };
    const auto configure_probe = [](fk1::Machine& machine, const std::uint8_t auxiliary) {
        machine.write_io(0x0023, 0xA6); // data PPI mode 1; PC5=0 selects drive A
        machine.write_io(0x0063, 0x88); // control PPI outputs, disk timing enabled
        machine.write_io(0x0060, 0x00);
        machine.write_io(0x0013, 0x72); // counter 1, LSB/MSB, mode 1
        machine.write_io(0x0011, 0x20); // deliberately short 32 us watchdog
        machine.write_io(0x0011, 0x00);
        machine.write_io(0x0030, 2);    // enable I7 and I6
        machine.write_io(0x0050, auxiliary);
    };

    fk1::Machine address_scan;
    install_halt_rom(address_scan);
    expect(address_scan.insert_floppy(
               fk1::FloppySubsystem::drive_a,
               make_mark_image(0xFE),
               fk1::FloppyAccess::read_only),
           "the address-mark timeout probe must mount in drive A");
    configure_probe(address_scan, 0x09); // RE, address mark selected
    address_scan.run_cycles(600);
    expect(!address_scan.interrupt_pending(),
           "E14/8 must not raise I6 while an address-mark scan crosses its timer interval");

    fk1::Machine data_scan;
    install_halt_rom(data_scan);
    expect(data_scan.insert_floppy(
               fk1::FloppySubsystem::drive_a,
               make_mark_image(0xFB),
               fk1::FloppyAccess::read_only),
           "the data-mark timeout probe must mount in drive A");
    configure_probe(data_scan, 0x0D); // RE + RDM, data mark selected
    data_scan.run_cycles(600);
    expect(data_scan.interrupt_pending() && data_scan.interrupt_vector() == 0x02,
           "E14/8 must raise I6 when RE+RDM remains active through the timer interval");
}

void test_track_format_runs_between_two_index_pulses_without_timeout()
{
    std::vector<std::uint8_t> raw(fk1::Ibm3740SectorImage::image_size, 0xE5);
    auto disk = fk1::Ibm3740SectorImage::import(raw);
    expect(disk.has_value(), "the format test needs a complete writable IBM 3740 disk");
    if(!disk) {
        return;
    }

    fk1::Machine machine;
    install_halt_rom(machine);
    expect(machine.insert_floppy(
               fk1::FloppySubsystem::drive_a,
               std::move(*disk),
               fk1::FloppyAccess::read_write),
           "the format test disk must mount read-write in drive A");
    machine.run_cycles(400);              // move away from the initial Index edge
    machine.write_io(0x0023, 0xA6);       // data PPI mode 1; PC5=0 selects A
    machine.write_io(0x0020, 0x55);       // held formatting byte
    machine.write_io(0x0063, 0x88);       // control PPI: PA/PB/PC lower outputs
    machine.write_io(0x0062, 0x08);       // load the head without stepping
    machine.write_io(0x0060, 0x00);       // enable disk timing
    machine.write_io(0x0030, 2);          // expose I6 to catch a false timeout
    machine.write_io(0x0050, 0x10);       // FOR

    constexpr auto two_revolutions_in_cycles =
        (fk1::FloppyDrive::revolution_ticks * 2ULL
         + fk1::Machine::master_ticks_per_cpu_cycle - 1U)
        / fk1::Machine::master_ticks_per_cpu_cycle;
    machine.run_cycles(two_revolutions_in_cycles + 1'000U);
    expect(!machine.interrupt_pending(),
           "normal FOR completion at the second Index must not raise timeout I6");

    const auto* formatted_track = machine.floppy().drive(0).image()->track(0);
    auto complete_revolution_written = formatted_track != nullptr
        && formatted_track->bytes().size() == fk1::Ibm3740SectorImage::fm_bytes_per_track;
    if(formatted_track != nullptr) {
        for(const auto& byte : formatted_track->bytes()) {
            complete_revolution_written = complete_revolution_written
                && byte.data == 0x55 && byte.clocks == 0xFF;
        }
    }
    expect(complete_revolution_written,
           "FOR must overwrite exactly one complete low-level revolution with serializer data");

    auto completed_bytes = std::vector<fk1::TimedFmByte>{};
    if(formatted_track != nullptr) {
        completed_bytes.assign(formatted_track->bytes().begin(), formatted_track->bytes().end());
    }
    machine.run_cycles(two_revolutions_in_cycles / 2U + 1'000U);
    const auto* track_after_another_revolution = machine.floppy().drive(0).image()->track(0);
    const auto unchanged = track_after_another_revolution != nullptr
        && track_after_another_revolution->bytes().size() == completed_bytes.size()
        && std::equal(track_after_another_revolution->bytes().begin(),
                      track_after_another_revolution->bytes().end(),
                      completed_bytes.begin());
    expect(unchanged,
           "FOR must remain inactive after the second Index terminates formatting");
}

void test_boot_rom_loads_boot_8sd()
{
#ifndef FK1_BOOT_DISK_IMAGE
    expect(false, "the boot smoke test requires FK1_BOOT_DISK_IMAGE");
#else
    std::ifstream stream{FK1_BOOT_DISK_IMAGE, std::ios::binary};
    expect(stream.good(), "disk/boot.8sd must be available to the boot smoke test");
    if(!stream) {
        return;
    }

    std::vector<std::uint8_t> raw(fk1::Ibm3740SectorImage::image_size);
    if(!stream.read(
           reinterpret_cast<char*>(raw.data()),
           static_cast<std::streamsize>(raw.size()))) {
        expect(false, "disk/boot.8sd must contain a complete sector-by-sector image");
        return;
    }
    const auto extra = stream.peek();
    expect(extra == std::char_traits<char>::eof(),
           "disk/boot.8sd must contain exactly 77x26x128 bytes");

    auto disk = fk1::Ibm3740SectorImage::import(raw);
    expect(disk.has_value(), "boot.8sd must expand into timed IBM 3740 FM tracks");
    if(!disk) {
        return;
    }

    fk1::Machine machine;
    expect(machine.set_floppy_index_pulse_width(20'400),
           "the SA800-compatible Index pulse must fit within one revolution");
    expect(machine.insert_floppy(
               fk1::FloppySubsystem::drive_a,
               std::move(*disk),
               fk1::FloppyAccess::read_write),
           "the boot image must mount read-write in drive A");

    machine.run_cycles(fk1::Machine::cpu_clock_hz * 3U);
    expect(machine.memory_map() == fk1::Machine::MemoryMap::ram
               && machine.program_counter() >= 0x8000U,
           "boot.rom must load boot.8sd, select RAM and transfer control to disk code");
    expect(machine.read_memory(0x8000U) == raw[0]
               && machine.read_memory(0x8001U) == raw[1]
               && machine.read_memory(0x8002U) == raw[2],
           "the first boot-sector instructions must arrive intact at RAM address 8000h");

    machine.run_cycles(fk1::Machine::cpu_clock_hz * 9U);
    constexpr std::array<std::uint16_t, 7> prompt_rows{{
        (1U << 2U) | (1U << 7U),
        (1U << 1U) | (1U << 3U) | (1U << 8U),
        (1U << 0U) | (1U << 4U) | (1U << 9U),
        (1U << 0U) | (1U << 1U) | (1U << 2U) | (1U << 3U) | (1U << 4U) | (1U << 10U),
        (1U << 0U) | (1U << 4U) | (1U << 9U),
        (1U << 0U) | (1U << 4U) | (1U << 8U),
        (1U << 0U) | (1U << 4U) | (1U << 7U),
    }};
    auto prompt_visible = true;
    const auto pixels = machine.frame_buffer();
    for(std::size_t row = 0; row < prompt_rows.size(); ++row) {
        for(std::size_t column = 0; column <= 10U; ++column) {
            const auto expected = (prompt_rows[row] & (1U << column)) != 0;
            const auto actual =
                pixels[(171U + row) * fk1::Machine::video_width + column] == fk1::Machine::white;
            prompt_visible = prompt_visible && actual == expected;
        }
    }
    expect(prompt_visible,
           "boot.8sd must load CPM2.SYS and reach the CP/M A> prompt without a track-2 read error");

    constexpr std::array<std::uint8_t, 17> save_command{{
        'S', 'A', 'V', 'E', ' ', '1', '0', '0', ' ',
        'B', 'I', 'G', '.', 'C', 'O', 'M', 0x0D,
    }};
    auto key_id = std::uint32_t{1};
    for(const auto code : save_command) {
        machine.keyboard_key_down(key_id, code);
        machine.keyboard_key_up(key_id++);
        machine.run_cycles(100'000);
    }
    machine.run_cycles(fk1::Machine::cpu_clock_hz * 30U);

    auto changed_disk = machine.eject_floppy(fk1::FloppySubsystem::drive_a);
    auto low_level_format_preserved = changed_disk.has_value();
    if(changed_disk) {
        for(std::size_t track_number = 0;
            track_number < fk1::FloppyDiskImage::track_count;
            ++track_number) {
            const auto* track = changed_disk->track(track_number);
            auto id_marks = std::size_t{0};
            auto data_marks = std::size_t{0};
            if(track != nullptr) {
                for(const auto& byte : track->bytes()) {
                    id_marks += byte.data == 0xFEU && byte.clocks == 0xC7U;
                    data_marks += (byte.data == 0xFBU || byte.data == 0xF8U)
                        && byte.clocks == 0xC7U;
                }
            }
            low_level_format_preserved = low_level_format_preserved
                && id_marks == fk1::Ibm3740SectorImage::sectors_per_track
                && data_marks == fk1::Ibm3740SectorImage::sectors_per_track;
        }
    }
    expect(low_level_format_preserved,
           "a long CP/M SAVE must preserve all low-level ID and data address marks");

    const auto changed_raw = changed_disk
        ? fk1::Ibm3740SectorImage::export_image(*changed_disk)
        : std::nullopt;
    expect(changed_raw.has_value() && *changed_raw != raw,
           "a long CP/M SAVE must change writable sectors and export them back to .8sd");

    auto saved_record_count = std::size_t{0};
    if(changed_raw) {
        constexpr std::array<std::uint8_t, 11> saved_name{{
            'B', 'I', 'G', ' ', ' ', ' ', ' ', ' ', 'C', 'O', 'M',
        }};
        constexpr auto directory_offset = 2U * 26U * 128U;
        constexpr auto directory_size = 2U * 1024U;
        for(std::size_t offset = directory_offset;
            offset + 32U <= directory_offset + directory_size;
            offset += 32U) {
            const auto matching_name = (*changed_raw)[offset] != 0xE5U
                && std::equal(
                    saved_name.begin(),
                    saved_name.end(),
                    changed_raw->begin() + static_cast<std::ptrdiff_t>(offset + 1U));
            if(matching_name) {
                saved_record_count += (*changed_raw)[offset + 15U];
            }
        }
    }
    expect(saved_record_count == 200U,
           "the exported CP/M directory must contain all 200 records of BIG.COM");
#endif
}

void test_embedded_and_alternative_roms()
{
    fk1::Machine machine;
    const auto embedded = fk1::embedded_boot_rom();

    expect(!embedded.empty(), "the embedded boot ROM must not be empty");
    expect(machine.rom_image().size() == embedded.size(), "the machine must use the embedded ROM by default");
    expect(std::equal(embedded.begin(), embedded.end(), machine.rom_image().begin()),
           "the machine ROM must match the image embedded at build time");

    std::array<std::uint8_t, 2 * 1024> diagnostic_rom{};
    diagnostic_rom[0] = 0xF3;
    diagnostic_rom[1] = 0x31;
    diagnostic_rom[diagnostic_rom.size() - 1] = 0x76;
    expect(machine.set_rom_image(diagnostic_rom), "a valid alternative ROM must be accepted");
    expect(std::equal(diagnostic_rom.begin(), diagnostic_rom.end(), machine.rom_image().begin()),
           "the alternative ROM must replace the embedded image");
    expect(machine.read_rom(0x0000) == 0xF3, "ROM reads must return the selected image");
    expect(machine.read_rom(0x0800) == 0xF3, "a 2 KiB ROM must mirror at address 0x0800");
    expect(machine.read_rom(0x3FFF) == 0x76, "a 2 KiB ROM must mirror through address 0x3FFF");

    const std::span<const std::uint8_t> empty_rom;
    expect(!machine.set_rom_image(empty_rom), "an empty ROM must be rejected");
    expect(machine.rom_image().size() == diagnostic_rom.size(),
           "rejecting an invalid ROM must preserve the current image");

    constexpr std::array<std::uint8_t, 4> invalid_rom{};
    expect(!machine.set_rom_image(invalid_rom), "a ROM with a non-hardware size must be rejected");
}

void test_video_addressing_and_bit_order()
{
    fk1::Machine machine;
    install_halt_rom(machine);
    machine.write_video_ram(0x0000, 0x81);
    machine.write_video_ram(0x0100, 0x80);
    machine.run_cycles(fk1::Machine::cycles_per_frame);

    const auto pixels = machine.frame_buffer();
    expect(pixels[0] == fk1::Machine::white, "the MSB must be the leftmost pixel");
    expect(pixels[1] == fk1::Machine::black, "an unset video bit must be black");
    expect(pixels[7] == fk1::Machine::white, "the LSB must be the rightmost pixel in a byte");
    expect(pixels[8] == fk1::Machine::white, "the high address byte must select the next byte column");
}

void test_vertical_scroll_wraps()
{
    fk1::Machine machine;
    install_halt_rom(machine);
    machine.write_video_ram(0x0001, 0x80);
    machine.set_vertical_scroll(1);
    machine.run_cycles(fk1::Machine::cycles_per_frame);

    const auto pixels = machine.frame_buffer();
    expect(pixels[0] == fk1::Machine::white, "vertical scroll 1 must display source row 1 at the top");

    machine.reset();
    machine.write_video_ram(0x0000, 0x80);
    machine.set_vertical_scroll(1);
    machine.run_cycles(fk1::Machine::cycles_per_frame);
    expect(machine.frame_buffer()[(fk1::Machine::video_height - 1) * fk1::Machine::video_width]
               == fk1::Machine::white,
           "vertical scrolling must wrap row 0 to the bottom");
}

} // namespace

int main()
{
    test_embedded_and_alternative_roms();
    test_deterministic_frame_clock();
    test_memory_maps();
    test_z80_executes_through_the_machine_bus();
    test_peripheral_io_mirroring();
    test_video_and_mouse_interrupt_latches();
    test_mouse_quadrature_and_buttons();
    test_z80_im2_interrupt_acknowledge();
    test_printer_file_sink_handshake_and_offline_status();
    test_keyboard_handshake_and_autorepeat();
    test_floppy_mechanics_and_ppi_signals();
    test_disk_timeout_is_armed_only_for_data_marks();
    test_track_format_runs_between_two_index_pulses_without_timeout();
    test_boot_rom_loads_boot_8sd();
    test_video_addressing_and_bit_order();
    test_vertical_scroll_wraps();

    if(failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All FK-1 core tests passed\n";
    return EXIT_SUCCESS;
}
