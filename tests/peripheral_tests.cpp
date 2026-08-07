#include "fk1/interrupt_controller3214.hpp"
#include "fk1/disk_image_8sd.hpp"
#include "fk1/floppy.hpp"
#include "fk1/keyboard.hpp"
#include "fk1/pit8253.hpp"
#include "fk1/ppi8255.hpp"
#include "fk1/uart8251.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <utility>
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

void test_interrupt_controller_priority_and_mask()
{
    fk1::InterruptController3214 controller;
    controller.set_input(0, true);
    controller.set_input(4, true);
    controller.set_input(7, true);

    expect(!controller.pending(), "the 3214 reset mask must block every interrupt input");
    expect(controller.vector() == fk1::InterruptController3214::no_vector,
           "a masked 3214 must not drive an IM2 vector");

    controller.reset();
    controller.set_mask(8);
    for(std::size_t input = 0; input < fk1::InterruptController3214::input_count; ++input) {
        controller.set_input(input, true);
        const auto expected = static_cast<std::uint8_t>(
            (fk1::InterruptController3214::input_count - 1U - input) * 2U);
        expect(controller.active_input() == input && controller.vector() == expected,
               "each 3214 input must generate its specified even IM2 vector");
        controller.set_input(input, false);
    }

    controller.set_input(0, true);
    controller.set_input(4, true);
    controller.set_input(7, true);
    controller.set_mask(1);
    expect(controller.pending() && controller.active_input() == 7 && controller.vector() == 0x00,
           "mask 1 must enable only highest-priority I7 with vector 00h");

    controller.set_input(7, false);
    expect(!controller.pending(), "mask 1 must continue to block I4 and I0");

    controller.set_mask(8);
    expect(controller.active_input() == 4 && controller.vector() == 0x06,
           "I4 must win over I0 and supply vector 06h");
    controller.set_input(4, false);
    expect(controller.active_input() == 0 && controller.vector() == 0x0E,
           "I0 must supply the final even vector 0Eh");

    controller.set_mask(0xFF);
    expect(controller.mask() == 8,
           "undocumented mask values above eight must saturate without indexing outside I7-I0");
    controller.reset();
    expect(controller.mask() == 0 && !controller.pending(),
           "reset must clear both the mask and all sampled interrupt inputs");
}

void test_timed_fm_track_and_drive_rotation()
{
    constexpr std::array<fk1::TimedFmByte, 4> track_bytes{{
        {0, 0x00, 0xFF},
        {384, 0xFC, 0xF7},
        {768, 0x01, 0xFF},
        {1'999'616, 0xFF, 0xFF},
    }};
    fk1::FloppyDiskImage image;
    expect(image.set_track(0, track_bytes),
           "a complete FM track must retain timed data and clock-mask bytes");
    expect(image.track(0) != nullptr && image.track(0)->bytes().size() == track_bytes.size()
               && image.track(0)->bytes()[1] == track_bytes[1],
           "FM timing, data and missing-clock marks must survive insertion into an image");

    constexpr std::array<fk1::TimedFmByte, 2> overlapping{{
        {100, 0x11, 0xFF},
        {200, 0x22, 0xFF},
    }};
    expect(!image.set_track(0, overlapping),
           "overlapping 32-us FM bytes must be rejected instead of corrupting track timing");
    expect(image.track(0)->bytes().size() == track_bytes.size(),
           "rejecting an invalid track must preserve the previous complete track");
    expect(!image.set_track(fk1::FloppyDiskImage::track_count, track_bytes),
           "the image must reject tracks outside the physical 0-76 range");
    expect(image.track(0)->write_byte({384, 0xA5, 0xC7})
               && image.track(0)->bytes()[1] == fk1::TimedFmByte{384, 0xA5, 0xC7},
           "a low-level write must replace the complete data/clock cell at its exact tick");
    expect(!image.track(0)->write_byte({500, 0x11, 0xFF})
               && !image.track(0)->write_byte(
                   {fk1::FloppyTrack::revolution_ticks, 0x11, 0xFF}),
           "a low-level write must reject overlapping or out-of-revolution cells");

    fk1::FloppyDrive drive;
    drive.advance(fk1::FloppyDrive::revolution_ticks * 3ULL);
    expect(!drive.media_present() && drive.rotation_phase() == 0
               && drive.completed_revolutions() == 0,
           "an empty drive must not rotate or generate revolutions");
    drive.insert(std::move(image), fk1::FloppyAccess::read_only);
    expect(drive.media_present() && drive.write_protected() && drive.track_zero(),
           "a read-only image must rotate with write protect active at initial Track 00");

    drive.advance(fk1::FloppyDrive::revolution_ticks - 1U);
    expect(drive.rotation_phase() == fk1::FloppyDrive::revolution_ticks - 1U
               && drive.completed_revolutions() == 0,
           "rotation must retain the exact master-tick position before Index");
    drive.advance(1);
    expect(drive.rotation_phase() == 0 && drive.completed_revolutions() == 1,
           "exactly 2,000,000 master ticks must complete one 360-RPM revolution without drift");
    drive.advance(fk1::FloppyDrive::revolution_ticks * 5ULL + 123U);
    expect(drive.rotation_phase() == 123 && drive.completed_revolutions() == 6,
           "large time advances must preserve revolution count and remainder exactly");

    drive.step_toward_higher_track();
    expect(drive.head_track() == 1 && !drive.track_zero(),
           "a physical outward step must leave Track 00");
    drive.step_toward_track_zero();
    drive.step_toward_track_zero();
    expect(drive.head_track() == 0,
           "restore steps must clamp at Track 00 instead of wrapping");

    fk1::FloppySubsystem subsystem;
    fk1::FloppyDiskImage image_a;
    fk1::FloppyDiskImage image_b;
    expect(subsystem.insert(0, std::move(image_a), fk1::FloppyAccess::read_write),
           "the subsystem must accept media in drive A");
    subsystem.advance(1'000);
    expect(subsystem.insert(1, std::move(image_b), fk1::FloppyAccess::read_write),
           "the subsystem must accept media in drive B independently");
    subsystem.advance(500);
    expect(subsystem.drive(0).rotation_phase() == 1'500
               && subsystem.drive(1).rotation_phase() == 500,
           "the two spindle phases must remain independent when media is inserted at different times");
    subsystem.select_from_disk_ppi(0x20);
    const auto drive_a_phase = subsystem.drive(0).rotation_phase();
    const auto drive_b_phase = subsystem.drive(1).rotation_phase();
    subsystem.select_from_disk_ppi(0x00);
    expect(subsystem.drive(0).rotation_phase() == drive_a_phase
               && subsystem.drive(1).rotation_phase() == drive_b_phase,
           "switching PC5 between drives must never reset either spindle phase");
}

void test_8sd_ibm_3740_import()
{
    constexpr std::array<std::uint8_t, 9> crc_check{{
        '1', '2', '3', '4', '5', '6', '7', '8', '9',
    }};
    expect(fk1::Ibm3740SectorImage::crc16(crc_check) == 0x29B1,
           "the IBM 3740 importer must use CRC-16/CCITT with the standard FFFF seed");

    std::vector<std::uint8_t> raw(fk1::Ibm3740SectorImage::image_size);
    for(std::size_t index = 0; index < raw.size(); ++index) {
        raw[index] = static_cast<std::uint8_t>(index);
    }
    expect(!fk1::Ibm3740SectorImage::import(
               std::span<const std::uint8_t>{raw}.first(raw.size() - 1U)),
           "a truncated .8sd sector image must be rejected");
    expect(!fk1::Ibm3740SectorImage::import(raw, 0),
           "an invalid sector skew that repeats a physical slot must be rejected");

    const auto disk = fk1::Ibm3740SectorImage::import(raw);
    expect(disk.has_value(), "a 77x26x128-byte .8sd image must import successfully");
    if(!disk) {
        return;
    }

    const auto round_trip = fk1::Ibm3740SectorImage::export_image(*disk);
    expect(round_trip.has_value() && *round_trip == raw,
           "analysing an unchanged low-level IBM 3740 disk must reproduce the .8sd image");

    const auto* track_zero = disk->track(0);
    const auto* track_last = disk->track(76);
    expect(track_zero != nullptr
               && track_zero->bytes().size() == fk1::Ibm3740SectorImage::fm_bytes_per_track,
           "each imported track must contain the complete 5,208-byte IBM 3740 layout");
    expect(track_last != nullptr
               && track_last->bytes().size() == fk1::Ibm3740SectorImage::fm_bytes_per_track,
           "all 77 physical tracks must be generated");
    if(track_zero == nullptr || track_zero->bytes().size() < 234U || track_last == nullptr) {
        return;
    }

    const auto bytes = track_zero->bytes();
    expect(bytes[46] == fk1::TimedFmByte{46U * 384U, 0xFC, 0xD7},
           "the index address mark must retain its IBM FM missing-clock pattern");
    expect(bytes[79] == fk1::TimedFmByte{79U * 384U, 0xFE, 0xC7}
               && bytes[80].data == 0 && bytes[81].data == 0
               && bytes[82].data == 1 && bytes[83].data == 0,
           "the first ID field must identify track 0, head 0, sector 1, 128-byte size");
    expect(bytes[103] == fk1::TimedFmByte{103U * 384U, 0xFB, 0xC7},
           "the data address mark must retain its IBM FM missing-clock pattern");
    expect(bytes[104].data == raw[0] && bytes[231].data == raw[127],
           "the first 128-byte sector must be copied byte-for-byte after its data mark");
    const auto physical_slot_for_sector_two = 6U;
    const auto sector_two_start = 73U + physical_slot_for_sector_two * 188U;
    expect(bytes[sector_two_start + 9U].data == 2
               && bytes[sector_two_start + 31U].data == raw[128],
           "the default six-sector skew must place logical sector 2 in physical slot 7");
    expect(bytes.back().tick == 1'999'488U,
           "the standard track must end 128 master ticks before the exact revolution boundary");

    const auto last_track_bytes = track_last->bytes();
    const auto last_track_first_id = 79U;
    expect(last_track_bytes[last_track_first_id + 1U].data == 76,
           "the final generated track ID must be physical track 76");
    const auto last_sector_data = 73U + 25U * 188U + 31U;
    const auto last_raw_offset = raw.size() - fk1::Ibm3740SectorImage::bytes_per_sector;
    expect(last_track_bytes[last_sector_data].data == raw[last_raw_offset]
               && last_track_bytes[last_sector_data + 127U].data == raw.back(),
           "track-major, sector-major .8sd ordering must be preserved through the final sector");

    auto modified_disk = fk1::Ibm3740SectorImage::import(raw);
    auto* modified_track = modified_disk ? modified_disk->track(0) : nullptr;
    expect(modified_track != nullptr
               && modified_track->write_byte(
                   {104U * fk1::FloppyTrack::byte_ticks, 0xA5, 0xFF}),
           "the first sector data cell must be writable in the low-level image");
    const auto modified_raw = modified_disk
        ? fk1::Ibm3740SectorImage::export_image(*modified_disk)
        : std::nullopt;
    expect(modified_raw.has_value() && (*modified_raw)[0] == 0xA5
               && std::equal(raw.begin() + 1, raw.end(), modified_raw->begin() + 1),
           "track analysis must transfer a changed data field back into exactly one .8sd byte");
}

void test_floppy_byte_stream_wrap()
{
    constexpr std::array<fk1::TimedFmByte, 3> bytes{{
        {0, 0x11, 0xFF},
        {384, 0x22, 0xFF},
        {fk1::FloppyTrack::revolution_ticks - 384U, 0x33, 0xFF},
    }};
    fk1::FloppyDiskImage image;
    expect(image.set_track(0, bytes), "the byte-stream test track must be valid");
    fk1::FloppyDrive drive;
    drive.insert(std::move(image), fk1::FloppyAccess::read_only);

    std::vector<std::uint8_t> observed;
    const auto collect = [](void* context, const fk1::TimedFmByte& byte) noexcept {
        static_cast<std::vector<std::uint8_t>*>(context)->push_back(byte.data);
    };
    drive.visit_bytes(384, &observed, collect);
    drive.advance(384);
    drive.visit_bytes(fk1::FloppyTrack::revolution_ticks - 384U, &observed, collect);
    drive.advance(fk1::FloppyTrack::revolution_ticks - 384U);
    drive.visit_bytes(1, &observed, collect);

    expect(observed == std::vector<std::uint8_t>({0x11, 0x22, 0x33, 0x11}),
           "timed FM bytes must be emitted once in [phase, phase+ticks), including across Index");
}

void test_keyboard_codes_and_repeat_timing()
{
    using Keyboard = fk1::Keyboard;
    using SpecialKey = Keyboard::SpecialKey;

    expect(Keyboard::ascii_code('A') == 0x41 && Keyboard::ascii_code('z') == 0x7A,
           "ordinary keyboard characters must retain their ASCII codes");
    expect(Keyboard::ascii_code(0x1B) == 0x1B
               && Keyboard::ascii_code(0x08) == 0x7F
               && Keyboard::ascii_code(0x09) == 0x09
               && Keyboard::ascii_code(0x0D) == 0x0D,
           "Escape, Backspace, Tab and Enter must use the FK-1 terminal codes");

    for(std::uint32_t character = '@'; character <= '_'; ++character) {
        const auto code = Keyboard::ascii_code(character, true);
        expect(code.has_value() && *code == static_cast<std::uint8_t>(character & 0x1FU),
               "Ctrl+@ through Ctrl+_ must generate terminal control codes 00h through 1Fh");
    }
    expect(Keyboard::ascii_code('a', true) == 0x01
               && Keyboard::ascii_code('z', true) == 0x1A,
           "lowercase host keycodes with Ctrl must map to control codes 01h through 1Ah");
    expect(!Keyboard::ascii_code(0x100U).has_value(),
           "non-ASCII host characters must not be guessed as FK-1 keyboard codes");

    expect(Keyboard::special_code(SpecialKey::roll) == 0x80
               && Keyboard::special_code(SpecialKey::roll, true) == 0x81
               && Keyboard::special_code(SpecialKey::copy) == 0x82
               && Keyboard::special_code(SpecialKey::copy, true) == 0x83
               && Keyboard::special_code(SpecialKey::break_key) == 0x84
               && Keyboard::special_code(SpecialKey::break_key, true) == 0x85,
           "ROL, COPY and BREAK must expose their unshifted and shifted code pairs");
    expect(Keyboard::special_code(SpecialKey::up) == 0xC1
               && Keyboard::special_code(SpecialKey::down) == 0xC2
               && Keyboard::special_code(SpecialKey::right) == 0xC3
               && Keyboard::special_code(SpecialKey::left) == 0xC4
               && Keyboard::special_code(SpecialKey::home) == 0x8D,
           "cursor and Home keys must use the documented FK-1 codes");
    expect(Keyboard::special_code(SpecialKey::user_1) == 0xD0
               && Keyboard::special_code(SpecialKey::user_2) == 0xD1
               && Keyboard::special_code(SpecialKey::user_3) == 0xD2,
           "the three user keys must map to D0h through D2h");

    Keyboard keyboard;
    expect(keyboard.key_down(7, 0x41) == 0x41,
           "a new physical key press must emit its code immediately");
    expect(!keyboard.key_down(7, 0x41).has_value(),
           "a duplicate host key-down event must not act as FK-1 autorepeat");
    expect(keyboard.advance(Keyboard::repeat_delay_ticks - 1U) == 0,
           "autorepeat must wait for the complete half-second delay");
    expect(keyboard.advance(1) == 1,
           "the first repeated character must appear after exactly half a second");
    expect(keyboard.advance(Keyboard::repeat_interval_ticks * 2U) == 2,
           "held keys must repeat at four characters per second");
    keyboard.key_up(7);
    expect(!keyboard.key_held()
               && keyboard.advance(Keyboard::repeat_interval_ticks * 2U) == 0,
           "releasing a key must not emit a byte and must stop autorepeat");
}

void test_ppi_mode_zero_and_bsr()
{
    fk1::Ppi8255 ppi;
    ppi.set_port_a_input(0xA5);
    expect(ppi.read(0) == 0xA5, "8255 reset must leave port A as an input");

    ppi.write(3, 0x88);
    ppi.write(0, 0x12);
    ppi.write(1, 0x34);
    ppi.set_port_c_input(0xA0);
    ppi.write(2, 0x05);
    expect(ppi.read(0) == 0x12, "mode 0 output port A must read its output latch");
    expect(ppi.read(1) == 0x34, "mode 0 output port B must read its output latch");
    expect(ppi.read(2) == 0xA5, "port C must combine external upper and latched lower bits");

    ppi.write(3, 0x07); // BSR set PC3
    expect((ppi.read(2) & 0x08U) != 0, "8255 BSR must set an ordinary port C latch bit");
    ppi.write(3, 0x06); // BSR reset PC3
    expect((ppi.read(2) & 0x08U) == 0, "8255 BSR must reset an ordinary port C latch bit");
}

void test_ppi_mode_one_handshake()
{
    fk1::Ppi8255 ppi;
    ppi.write(3, 0xAE);
    ppi.write(3, 0x05); // BSR PC2: enable group B input interrupt
    ppi.pulse_strobe_b(0x7F);

    expect(ppi.intr_b(), "a strobed mode 1 input byte must raise INTR_B when INTE_B is set");
    expect((ppi.read(2) & 0x03U) == 0x03U, "mode 1 port C must expose IBF_B and INTR_B");
    expect(ppi.read(1) == 0x7F, "mode 1 port B must return the strobed input latch");
    expect(!ppi.intr_b(), "reading a mode 1 input port must clear its interrupt");

    ppi.write(3, 0x0D); // BSR PC6: enable group A output interrupt
    ppi.write(0, 0x55);
    expect((ppi.read(2) & 0x80U) == 0, "writing mode 1 port A must assert active-low OBF_A");
    ppi.pulse_ack_a();
    expect(ppi.intr_a(), "ACK_A must raise INTR_A after a mode 1 output transfer");
    expect((ppi.read(2) & 0x88U) == 0x88U, "port C must expose inactive OBF_A and active INTR_A");
}

void test_pit_modes_and_latch()
{
    fk1::Pit8253 pit;
    pit.set_gate(0, true);
    pit.write(3, 0x34); // counter 0, LSB/MSB, mode 2, binary
    pit.write(0, 3);
    pit.write(0, 0);

    expect(pit.output(0), "8253 mode 2 output must start high");
    static_cast<void>(pit.clock(0, 2));
    expect(pit.output(0), "mode 2 output must stay high before terminal count");
    const auto falling = pit.clock(0);
    expect(!pit.output(0) && falling.falling_edges == 1,
           "mode 2 terminal count must generate one low clock interval");
    const auto rising = pit.clock(0);
    expect(pit.output(0) && rising.rising_edges == 1,
           "mode 2 output must return high on the next input clock");

    pit.write(3, 0x00); // latch counter 0
    const auto low = pit.read(0);
    const auto high = pit.read(0);
    expect(static_cast<unsigned>(low | static_cast<unsigned>(high) << 8U) == 2U,
           "8253 latch must preserve a coherent LSB/MSB count snapshot");

    pit.write(3, 0x31); // counter 0, LSB/MSB, mode 0, BCD
    pit.write(0, 0x10);
    pit.write(0, 0x00);
    expect(!pit.output(0), "8253 mode 0 output must be low after loading");
    static_cast<void>(pit.clock(0, 10));
    expect(pit.output(0), "BCD count 0010 must reach terminal count after ten clocks");

    pit.write(3, 0xF0); // 8254 read-back encoding; ignored by 8253
    expect(pit.output(0), "an 8254 read-back command must not alter an 8253 channel");
}

void test_pit_triggered_and_strobe_modes()
{
    fk1::Pit8253 pit;

    pit.write(3, 0x72); // counter 1, LSB/MSB, mode 1
    pit.write(1, 3);
    pit.write(1, 0);
    expect(pit.output(1), "mode 1 must wait high for a gate trigger");
    pit.set_gate(1, true);
    expect(!pit.output(1), "a rising gate must trigger the mode 1 one-shot");
    static_cast<void>(pit.clock(1, 3));
    expect(pit.output(1), "mode 1 must return high at terminal count");

    pit.write(3, 0x76); // counter 1, LSB/MSB, mode 3
    pit.write(1, 4);
    pit.write(1, 0);
    static_cast<void>(pit.clock(1, 2));
    expect(!pit.output(1), "mode 3 must toggle low after half an even count");
    static_cast<void>(pit.clock(1, 2));
    expect(pit.output(1), "mode 3 must toggle high after the second half-period");

    pit.set_gate(2, true);
    pit.write(3, 0xB8); // counter 2, LSB/MSB, mode 4
    pit.write(2, 2);
    pit.write(2, 0);
    static_cast<void>(pit.clock(2, 2));
    expect(!pit.output(2), "mode 4 must emit a one-clock low software strobe");
    static_cast<void>(pit.clock(2));
    expect(pit.output(2), "mode 4 strobe must return high after one clock");

    pit.set_gate(2, false);
    pit.write(3, 0xBA); // counter 2, LSB/MSB, mode 5
    pit.write(2, 2);
    pit.write(2, 0);
    pit.set_gate(2, true);
    static_cast<void>(pit.clock(2, 2));
    expect(!pit.output(2), "mode 5 must emit a low strobe after a hardware trigger");
    static_cast<void>(pit.clock(2));
    expect(pit.output(2), "mode 5 hardware strobe must last one clock");
}

void test_uart_async_transmit_and_receive()
{
    fk1::Uart8251 uart;
    uart.write(1, 0x4E); // x16, 8 data bits, no parity, one stop bit
    uart.write(1, 0x05); // Tx enable, Rx enable
    expect(uart.tx_ready(), "8251 TXRDY must rise after enabling an empty transmitter");

    uart.write(0, 0xA5);
    expect(!uart.tx_ready(), "writing the 8251 transmit buffer must clear TXRDY");
    uart.clock_transmit(16);
    expect(uart.tx_ready(), "moving a byte into the shift register must restore TXRDY");
    uart.clock_transmit(16 * 10);
    const auto transmitted = uart.take_transmitted_byte();
    expect(transmitted.has_value() && *transmitted == 0xA5,
           "8251 asynchronous framing must transmit the programmed byte");

    uart.receive_byte(0x42);
    expect(uart.rx_ready(), "a received 8251 byte must set RXRDY");
    uart.receive_byte(0x43);
    expect((uart.status() & fk1::Uart8251::status_overrun_error) != 0,
           "receiving over a full 8251 buffer must set overrun error");
    expect(uart.read(0) == 0x42 && !uart.rx_ready(),
           "reading 8251 data must return the buffered byte and clear RXRDY");
    uart.write(1, 0x15); // preserve enables and reset error flags
    expect((uart.status() & fk1::Uart8251::status_overrun_error) == 0,
           "the 8251 error-reset command bit must clear error flags");
}

void test_uart_async_serial_input()
{
    fk1::Uart8251 uart;
    uart.write(1, 0x4E);
    uart.write(1, 0x04); // receiver enable

    constexpr std::uint8_t value = 0x96;
    uart.set_rxd(false);
    uart.clock_receive(1);
    uart.clock_receive(8); // middle of the start bit
    for(std::uint8_t bit = 0; bit < 8; ++bit) {
        uart.set_rxd((value & static_cast<std::uint8_t>(1U << bit)) != 0);
        uart.clock_receive(16);
    }
    uart.set_rxd(true);
    uart.clock_receive(16);

    expect(uart.rx_ready() && uart.read(0) == value,
           "8251 x16 receiver must sample an asynchronous serial frame LSB first");
}

void test_uart_host_receive_queue()
{
    fk1::Uart8251 uart;
    uart.write(1, 0x4E);
    uart.write(1, 0x04); // receiver enable

    expect(uart.queue_received_byte(0xA5),
           "the 8251 host receive queue must accept a byte");
    expect(uart.queued_receive_bytes() == 1,
           "a queued 8251 host byte must remain pending until clocked");
    uart.clock_receive(16 * 5);
    expect(!uart.rx_ready(),
           "a host byte must not bypass the programmed 8251 receive timing");
    uart.clock_receive(16 * 5);
    expect(uart.rx_ready() && uart.read(0) == 0xA5,
           "the host receive queue must serialize an asynchronous frame");
    expect(uart.queued_receive_bytes() == 0,
           "a completely clocked host byte must leave the receive queue");

    for(std::size_t index = 0; index < fk1::Uart8251::host_receive_queue_capacity; ++index) {
        expect(uart.queue_received_byte(static_cast<std::uint8_t>(index)),
               "the bounded 8251 host receive queue must accept bytes up to its capacity");
    }
    expect(!uart.queue_received_byte(0x00),
           "the 8251 host receive queue must apply backpressure at its capacity");
    uart.clear_queued_receive_bytes();
    expect(uart.queued_receive_bytes() == 0,
           "clearing the 8251 host receive queue must discard pending input");

    expect(uart.queue_received_byte(0x00),
           "the cleared 8251 host receive queue must accept another byte");
    uart.clock_receive(1); // place RxD at the low start bit
    uart.write(1, 0x40); // software reset
    uart.write(1, 0x4E);
    uart.write(1, 0x04);
    uart.clock_receive(16 * 12);
    expect(!uart.rx_ready(),
           "an 8251 software reset must restore idle RxD after discarding a host frame");

    fk1::Uart8251 disabled_receiver;
    disabled_receiver.write(1, 0x4E);
    disabled_receiver.write(1, 0x00);
    expect(disabled_receiver.queue_received_byte(0x5A),
           "the host serial line must accept data while the 8251 receiver is disabled");
    disabled_receiver.clock_receive(16 * 10);
    disabled_receiver.write(1, 0x04);
    disabled_receiver.clock_receive(16 * 12);
    expect(!disabled_receiver.rx_ready(),
           "serial data passing while the 8251 receiver is disabled must be discarded");
}

void test_uart_synchronous_initialization()
{
    fk1::Uart8251 uart;
    // Put the device into command phase, then exercise the software reset used
    // by DIAG.MAC before sending the real initialization sequence.
    uart.write(1, 0x00);
    uart.write(1, 0x00);
    uart.write(1, 0x00);
    uart.write(1, 0x40);
    uart.write(1, 0x3C); // two SYN characters, even parity, 8 data bits
    uart.write(1, 0x55);
    uart.write(1, 0x55);
    uart.write(1, 0x33); // Tx enable, DTR, error reset, RTS

    expect(uart.mode_word() == 0x3C && uart.command_word() == 0x33,
           "8251 must consume both synchronous SYN characters before its command word");
    expect(uart.interrupt_requested(), "FK-1 UART interrupt source must include synchronous TXRDY");
    uart.write(0, 0x69);
    uart.clock_transmit(10);
    const auto transmitted = uart.take_transmitted_byte();
    expect(transmitted.has_value() && *transmitted == 0x69,
           "8251 synchronous mode must transmit data without start or stop bits");
}

} // namespace

int main()
{
    test_interrupt_controller_priority_and_mask();
    test_timed_fm_track_and_drive_rotation();
    test_8sd_ibm_3740_import();
    test_floppy_byte_stream_wrap();
    test_keyboard_codes_and_repeat_timing();
    test_ppi_mode_zero_and_bsr();
    test_ppi_mode_one_handshake();
    test_pit_modes_and_latch();
    test_pit_triggered_and_strobe_modes();
    test_uart_async_transmit_and_receive();
    test_uart_async_serial_input();
    test_uart_host_receive_queue();
    test_uart_synchronous_initialization();

    if(failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All FK-1 peripheral tests passed\n";
    return EXIT_SUCCESS;
}
