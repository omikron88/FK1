#include "fk1/machine.hpp"

#include "fk1/embedded_rom.hpp"
#include "fk1/floppy.hpp"
#include "fk1/interrupt_controller3214.hpp"
#include "fk1/keyboard.hpp"
#include "fk1/pit8253.hpp"
#include "fk1/ppi8255.hpp"
#include "fk1/uart8251.hpp"

#include <Z80.h>

#include <algorithm>
#include <array>
#include <deque>
#include <limits>
#include <utility>
#include <vector>

namespace fk1 {

static_assert(Keyboard::repeat_delay_ticks == Machine::master_clock_hz / 2U);
static_assert(Keyboard::repeat_interval_ticks == Machine::master_clock_hz / 4U);

namespace {

struct DiskAuxControl {
    void reset() noexcept
    {
        read_enabled = false;
        write_enabled = false;
        data_mark_selected = false;
        format_enabled = false;
    }

    void write(const std::uint8_t value) noexcept
    {
        read_enabled = (value & 0x01U) != 0;
        write_enabled = (value & 0x02U) != 0;
        data_mark_selected = (value & 0x04U) != 0;
        format_enabled = (value & 0x10U) != 0;
    }

    bool read_enabled{false};
    bool write_enabled{false};
    bool data_mark_selected{false};
    bool format_enabled{false};
};

[[nodiscard]] bool programs_counter_one(
    const std::uint8_t register_index,
    const std::uint8_t value) noexcept
{
    if((register_index & 0x03U) != 3U) {
        return false;
    }
    const auto selected_counter = static_cast<std::uint8_t>((value >> 6U) & 0x03U);
    const auto access = static_cast<std::uint8_t>((value >> 4U) & 0x03U);
    return selected_counter == 1U && access != 0U;
}

[[nodiscard]] bool is_id_address_mark(const TimedFmByte& byte) noexcept
{
    return byte.data == 0xFEU && byte.clocks == 0xC7U;
}

[[nodiscard]] bool is_data_address_mark(const TimedFmByte& byte) noexcept
{
    return (byte.data == 0xFBU || byte.data == 0xF8U) && byte.clocks == 0xC7U;
}

[[nodiscard]] std::uint8_t written_clock_mask(
    const std::uint8_t machine_control_port_a) noexcept
{
    switch(machine_control_port_a & 0x03U) {
    case 0x01U:
        return 0xD7U;
    case 0x02U:
        return 0xEFU;
    case 0x03U:
        return 0xC7U;
    default:
        return 0xFFU;
    }
}

constexpr std::array<std::uint8_t, 4> quadrature_states{
    0x00U,
    0x01U,
    0x03U,
    0x02U,
};

[[nodiscard]] std::uint8_t quadrature_phase(
    const std::uint8_t signals,
    const unsigned int shift) noexcept
{
    const auto state = static_cast<std::uint8_t>((signals >> shift) & 0x03U);
    switch(state) {
    case 0x01U:
        return 1;
    case 0x03U:
        return 2;
    case 0x02U:
        return 3;
    default:
        return 0;
    }
}

[[nodiscard]] std::int64_t queue_mouse_delta(
    const std::int64_t pending,
    const std::int32_t delta) noexcept
{
    constexpr auto limit = std::int64_t{1'000'000};
    return std::clamp(pending + static_cast<std::int64_t>(delta), -limit, limit);
}

} // namespace

struct Machine::PeripheralState {
    void reset() noexcept
    {
        printer_keyboard.reset();
        printer_keyboard.set_port_c_input(printer_online ? 0x20U : 0x00U);
        timer.reset();
        disk_data.reset();
        uart.reset();
        machine_control.reset();
        interrupts.reset();
        keyboard.reset();
        disk_aux.reset();
        floppy.reset_controller();

        // Pull-ups and fixed inputs documented for the FK-1 board.
        disk_data.set_port_c_input(0x20);
        machine_control.set_port_a_input(0x40);
        machine_control.set_port_c_input(floppy.control_port_c_inputs());
        uart.set_cts_active(
            !serial_connection_managed || (serial_connected && serial_clear_to_send));
        uart.set_dsr_active(serial_connection_managed && serial_connected);

        timer.set_gate(0, true);
        timer.set_gate(1, true);
        timer.set_gate(2, true);
        timer_clock_accumulator = 0;
        disk_timing_enabled = false;
        disk_sync_latch = false;
        disk_data_interrupt_level = false;
        suppress_disk_byte_counter_once = false;
        ordinary_write_active = false;
        format_write_active = false;
        last_disk_byte_tick = 0;
        pending_disk_writes.clear();
        disk_timeout_latch = false;
        video_interrupt_latch = false;
        mouse_interrupt_latch = false;
    }

    Ppi8255 printer_keyboard;
    Pit8253 timer;
    Ppi8255 disk_data;
    Uart8251 uart;
    Ppi8255 machine_control;
    InterruptController3214 interrupts;
    Keyboard keyboard;
    std::deque<std::uint8_t> printer_bytes;
    bool printer_online{false};
    bool serial_connection_managed{false};
    bool serial_connected{false};
    bool serial_clear_to_send{true};
    FloppySubsystem floppy;
    DiskAuxControl disk_aux;
    std::uint64_t timer_clock_accumulator{0};
    // E53 maps AX, BX, AY, BY, T1 and T2 to PD0..PD5. The two button
    // inputs are pulled high and become active-low at the processor port.
    std::uint8_t mouse_signals_raw{0x30U};
    std::uint8_t mouse_x_phase{0};
    std::uint8_t mouse_y_phase{0};
    std::int64_t mouse_pending_x{0};
    std::int64_t mouse_pending_y{0};
    std::uint64_t mouse_edge_tick_accumulator{Machine::mouse_edge_interval_ticks};
    bool mouse_y_next{false};
    bool disk_timing_enabled{false};
    bool disk_sync_latch{false};
    bool disk_data_interrupt_level{false};
    bool suppress_disk_byte_counter_once{false};
    bool ordinary_write_active{false};
    bool format_write_active{false};
    std::uint32_t last_disk_byte_tick{0};
    std::vector<TimedFmByte> pending_disk_writes;
    bool disk_timeout_latch{false};
    bool video_interrupt_latch{false};
    bool mouse_interrupt_latch{false};
};

struct Machine::CpuState {
    explicit CpuState(Machine& machine) noexcept
    {
        cpu.context = &machine;
        cpu.fetch_opcode = read_memory;
        cpu.fetch = read_memory;
        cpu.read = read_memory;
        cpu.write = write_memory;
        cpu.in = read_io;
        cpu.out = write_io;
        cpu.halt = nullptr;
        cpu.nop = read_memory;
        cpu.nmia = nullptr;
        cpu.inta = interrupt_acknowledge;
        cpu.int_fetch = nullptr;
        cpu.ld_i_a = nullptr;
        cpu.ld_r_a = nullptr;
        cpu.reti = nullptr;
        cpu.retn = nullptr;
        cpu.hook = nullptr;
        cpu.illegal = nullptr;
        cpu.options = Z80_MODEL_ZILOG_NMOS;
        z80_power(&cpu, Z_TRUE);
    }

    static zuint8 read_memory(void* context, const zuint16 address) noexcept
    {
        return static_cast<Machine*>(context)->read_memory(address);
    }

    static void write_memory(void* context, const zuint16 address, const zuint8 value) noexcept
    {
        static_cast<Machine*>(context)->write_memory(address, value);
    }

    static zuint8 read_io(void* context, const zuint16 port) noexcept
    {
        auto& machine = *static_cast<Machine*>(context);
        machine.synchronize_cpu_io(z80_in_cycle(&machine.cpu_->cpu));
        return machine.read_io(port);
    }

    static void write_io(void* context, const zuint16 port, const zuint8 value) noexcept
    {
        auto& machine = *static_cast<Machine*>(context);
        machine.synchronize_cpu_io(z80_out_cycle(&machine.cpu_->cpu));
        machine.write_io(port, value);
    }

    static zuint8 interrupt_acknowledge(void* context, const zuint16) noexcept
    {
        return static_cast<Machine*>(context)->interrupt_vector();
    }

    Z80 cpu{};
    std::uint64_t synchronized_instruction_cycles{0};
};

Machine::Machine()
{
    static_cast<void>(set_rom_image(embedded_boot_rom()));
    peripherals_ = std::make_unique<PeripheralState>();
    cpu_ = std::make_unique<CpuState>(*this);
    reset();
}

Machine::~Machine() = default;

void Machine::reset() noexcept
{
    video_ram_.fill(0);
    main_ram_.fill(0);
    memory_map_ = MemoryMap::rom_video;
    cycle_count_ = 0;
    master_tick_count_ = 0;
    frame_tick_accumulator_ = 0;
    frame_count_ = 0;
    cpu_cycle_overrun_ = 0;
    vertical_scroll_ = 0;
    peripherals_->reset();
    z80_instant_reset(&cpu_->cpu);
    refresh_interrupt_sources();
    render_video();
}

void Machine::run_cycles(const std::uint64_t cycles) noexcept
{
    if(cycles <= cpu_cycle_overrun_) {
        cpu_cycle_overrun_ -= cycles;
        return;
    }

    auto remaining = cycles - cpu_cycle_overrun_;
    cpu_cycle_overrun_ = 0;

    while(remaining != 0) {
        cpu_->synchronized_instruction_cycles = 0;
        const auto executed = static_cast<std::uint64_t>(z80_run(&cpu_->cpu, 1));

        if(executed == 0) {
            return;
        }

        if(cpu_->synchronized_instruction_cycles < executed) {
            advance_time(executed - cpu_->synchronized_instruction_cycles);
        }
        if(executed >= remaining) {
            cpu_cycle_overrun_ = executed - remaining;
            return;
        }

        remaining -= executed;
    }
}

void Machine::advance_time(const std::uint64_t cpu_cycles) noexcept
{
    cycle_count_ += cpu_cycles;
    const auto master_ticks = cpu_cycles * master_ticks_per_cpu_cycle;
    master_tick_count_ += master_ticks;
    peripherals_->floppy.advance(
        master_ticks,
        this,
        [](void* const context, const TimedFmByte& byte) noexcept {
            static_cast<Machine*>(context)->process_floppy_byte(byte);
        });
    refresh_floppy_inputs();

    const auto keyboard_repeats = peripherals_->keyboard.advance(master_ticks);
    for(std::uint64_t repeat = 0; repeat < keyboard_repeats; ++repeat) {
        deliver_keyboard_code(peripherals_->keyboard.repeat_code());
    }

    advance_mouse(master_ticks);

    peripherals_->timer_clock_accumulator += master_ticks;
    const auto timer_clocks = peripherals_->timer_clock_accumulator / 12U;
    peripherals_->timer_clock_accumulator %= 12U;
    if(timer_clocks != 0) {
        const auto edges = peripherals_->timer.clock(1, timer_clocks);
        peripherals_->uart.clock_transmit(edges.rising_edges);
        peripherals_->uart.clock_receive(edges.rising_edges);
        // E14/8 arms TOUT only while RDM waits for the data mark belonging
        // to a previously accepted ID field. An address-mark scan may cross
        // the long Index gap without raising I6.
        if(peripherals_->disk_timing_enabled
           && peripherals_->disk_aux.read_enabled
           && peripherals_->disk_aux.data_mark_selected
           && edges.rising_edges != 0) {
            peripherals_->disk_timeout_latch = true;
        }
        if(peripherals_->disk_timing_enabled
           && peripherals_->disk_aux.write_enabled
           && !peripherals_->ordinary_write_active
           && edges.rising_edges != 0) {
            // OUT1 sets the write-enable latch after the programmed delay
            // from the accepted ID field. Counter 2 resets it after the CRC.
            peripherals_->ordinary_write_active = true;
            static_cast<void>(
                transfer_floppy_write_byte(peripherals_->last_disk_byte_tick));
        }
    }
    flush_pending_disk_writes();

    frame_tick_accumulator_ += master_ticks;

    const auto completed_frames = frame_tick_accumulator_ / master_ticks_per_frame;
    if(completed_frames != 0) {
        frame_tick_accumulator_ %= master_ticks_per_frame;
        frame_count_ += completed_frames;

        // CLK0 is connected before the PA2 gate that controls I4.
        static_cast<void>(peripherals_->timer.clock(0, completed_frames));
        const auto video_interrupt_enabled = peripherals_->machine_control.port_a_output_enabled()
            && (peripherals_->machine_control.output_a() & 0x04U) != 0;
        if(video_interrupt_enabled) {
            peripherals_->video_interrupt_latch = true;
        }
        render_video();
    }

    refresh_interrupt_sources();
}

void Machine::synchronize_cpu_io(const std::uint8_t relative_cycle) noexcept
{
    const auto target = static_cast<std::uint64_t>(relative_cycle);
    if(target > cpu_->synchronized_instruction_cycles) {
        advance_time(target - cpu_->synchronized_instruction_cycles);
        cpu_->synchronized_instruction_cycles = target;
    }
}

std::uint64_t Machine::cycle_count() const noexcept
{
    return cycle_count_;
}

std::uint64_t Machine::master_tick_count() const noexcept
{
    return master_tick_count_;
}

std::uint64_t Machine::frame_count() const noexcept
{
    return frame_count_;
}

std::uint16_t Machine::program_counter() const noexcept
{
    return Z80_PC(cpu_->cpu);
}

bool Machine::cpu_halted() const noexcept
{
    return cpu_->cpu.halt_line != 0;
}

bool Machine::interrupt_pending() const noexcept
{
    return peripherals_->interrupts.pending();
}

std::uint8_t Machine::interrupt_vector() const noexcept
{
    return peripherals_->interrupts.vector();
}

std::uint8_t Machine::interrupt_mask() const noexcept
{
    return peripherals_->interrupts.mask();
}

void Machine::keyboard_key_down(
    const std::uint32_t key_id,
    const std::uint8_t code) noexcept
{
    const auto emitted = peripherals_->keyboard.key_down(key_id, code);
    if(emitted) {
        deliver_keyboard_code(*emitted);
    }
}

void Machine::keyboard_key_up(const std::uint32_t key_id) noexcept
{
    peripherals_->keyboard.key_up(key_id);
}

bool Machine::keyboard_key_held() const noexcept
{
    return peripherals_->keyboard.key_held();
}

void Machine::set_printer_online(const bool online) noexcept
{
    peripherals_->printer_online = online;
    refresh_printer_inputs();
}

bool Machine::printer_online() const noexcept
{
    return peripherals_->printer_online;
}

std::optional<std::uint8_t> Machine::take_printer_byte() noexcept
{
    if(peripherals_->printer_bytes.empty()) {
        return std::nullopt;
    }
    const auto byte = peripherals_->printer_bytes.front();
    peripherals_->printer_bytes.pop_front();
    return byte;
}

void Machine::set_serial_connected(const bool connected) noexcept
{
    peripherals_->serial_connection_managed = true;
    peripherals_->serial_connected = connected;
    peripherals_->serial_clear_to_send = true;
    peripherals_->uart.set_cts_active(connected);
    peripherals_->uart.set_dsr_active(connected);
    if(!connected) {
        peripherals_->uart.clear_queued_receive_bytes();
        while(peripherals_->uart.take_transmitted_byte()) {
        }
    }
    refresh_interrupt_sources();
}

void Machine::set_serial_clear_to_send(const bool clear) noexcept
{
    peripherals_->serial_clear_to_send = clear;
    peripherals_->uart.set_cts_active(peripherals_->serial_connected && clear);
    refresh_interrupt_sources();
}

bool Machine::serial_connected() const noexcept
{
    return peripherals_->serial_connected;
}

bool Machine::queue_serial_received_byte(const std::uint8_t value) noexcept
{
    if(!peripherals_->serial_connected) {
        return false;
    }
    return peripherals_->uart.queue_received_byte(value);
}

std::optional<std::uint8_t> Machine::take_serial_transmitted_byte() noexcept
{
    return peripherals_->uart.take_transmitted_byte();
}

bool Machine::insert_floppy(
    const std::size_t drive,
    FloppyDiskImage image,
    const FloppyAccess access)
{
    const auto inserted = peripherals_->floppy.insert(drive, std::move(image), access);
    refresh_floppy_inputs();
    return inserted;
}

std::optional<FloppyDiskImage> Machine::eject_floppy(const std::size_t drive) noexcept
{
    auto image = peripherals_->floppy.eject(drive);
    refresh_floppy_inputs();
    return image;
}

bool Machine::set_floppy_index_pulse_width(const std::uint32_t master_ticks) noexcept
{
    const auto accepted = peripherals_->floppy.set_index_pulse_width(master_ticks);
    refresh_floppy_inputs();
    return accepted;
}

const FloppySubsystem& Machine::floppy() const noexcept
{
    return peripherals_->floppy;
}

void Machine::set_mouse_signals(const std::uint8_t signals) noexcept
{
    peripherals_->mouse_x_phase = quadrature_phase(signals, 0U);
    peripherals_->mouse_y_phase = quadrature_phase(signals, 2U);
    peripherals_->mouse_pending_x = 0;
    peripherals_->mouse_pending_y = 0;
    peripherals_->mouse_edge_tick_accumulator = mouse_edge_interval_ticks;
    update_mouse_signals(signals);
    refresh_interrupt_sources();
}

void Machine::mouse_motion(const std::int32_t delta_x, const std::int32_t delta_y) noexcept
{
    peripherals_->mouse_pending_x = queue_mouse_delta(peripherals_->mouse_pending_x, delta_x);
    peripherals_->mouse_pending_y = queue_mouse_delta(peripherals_->mouse_pending_y, delta_y);
    advance_mouse(0);
    refresh_interrupt_sources();
}

void Machine::set_mouse_button(const MouseButton button, const bool pressed) noexcept
{
    const auto mask = button == MouseButton::button_1 ? std::uint8_t{0x10U}
                                                       : std::uint8_t{0x20U};
    auto signals = peripherals_->mouse_signals_raw;
    if(pressed) {
        signals = static_cast<std::uint8_t>(signals & static_cast<std::uint8_t>(~mask));
    } else {
        signals = static_cast<std::uint8_t>(signals | mask);
    }
    update_mouse_signals(signals);
    refresh_interrupt_sources();
}

void Machine::update_mouse_signals(const std::uint8_t signals) noexcept
{
    // Only AX, BX, AY, BY, T1 and T2 feed the edge detector on sheet 5.
    const auto changed = static_cast<std::uint8_t>(
        (signals ^ peripherals_->mouse_signals_raw) & 0x3FU);
    peripherals_->mouse_signals_raw = signals;
    const auto mouse_interrupt_enabled = peripherals_->machine_control.port_a_output_enabled()
        && (peripherals_->machine_control.output_a() & 0x08U) != 0;
    if(changed != 0 && mouse_interrupt_enabled) {
        peripherals_->mouse_interrupt_latch = true;
    }
}

void Machine::advance_mouse(const std::uint64_t master_ticks) noexcept
{
    auto& state = *peripherals_;
    const auto capacity = std::numeric_limits<std::uint64_t>::max()
        - state.mouse_edge_tick_accumulator;
    state.mouse_edge_tick_accumulator += std::min(master_ticks, capacity);

    while(state.mouse_edge_tick_accumulator >= mouse_edge_interval_ticks
          && (state.mouse_pending_x != 0 || state.mouse_pending_y != 0)) {
        state.mouse_edge_tick_accumulator -= mouse_edge_interval_ticks;

        const auto both_axes = state.mouse_pending_x != 0 && state.mouse_pending_y != 0;
        const auto use_y = state.mouse_pending_x == 0
            || (both_axes && state.mouse_y_next);
        if(both_axes) {
            state.mouse_y_next = !state.mouse_y_next;
        }

        auto signals = state.mouse_signals_raw;
        if(use_y) {
            const auto positive = state.mouse_pending_y > 0;
            state.mouse_pending_y += positive ? -1 : 1;
            state.mouse_y_phase = static_cast<std::uint8_t>(
                (state.mouse_y_phase + (positive ? 1U : 3U)) & 0x03U);
            signals = static_cast<std::uint8_t>(
                (signals & 0xF3U) | (quadrature_states[state.mouse_y_phase] << 2U));
        } else {
            const auto positive = state.mouse_pending_x > 0;
            state.mouse_pending_x += positive ? -1 : 1;
            // The original X encoder is mechanically mirrored relative to
            // host screen coordinates: motion right has BX leading AX.
            state.mouse_x_phase = static_cast<std::uint8_t>(
                (state.mouse_x_phase + (positive ? 3U : 1U)) & 0x03U);
            signals = static_cast<std::uint8_t>(
                (signals & 0xFCU) | quadrature_states[state.mouse_x_phase]);
        }
        update_mouse_signals(signals);
    }

    if(state.mouse_pending_x == 0 && state.mouse_pending_y == 0) {
        state.mouse_edge_tick_accumulator = std::min(
            state.mouse_edge_tick_accumulator,
            mouse_edge_interval_ticks);
    }
}

std::uint8_t Machine::mouse_signals() const noexcept
{
    return peripherals_->mouse_signals_raw;
}

bool Machine::mouse_interrupt_latched() const noexcept
{
    return peripherals_->mouse_interrupt_latch;
}

bool Machine::video_interrupt_latched() const noexcept
{
    return peripherals_->video_interrupt_latch;
}

bool Machine::set_rom_image(const std::span<const std::uint8_t> image) noexcept
{
    if(!is_supported_rom_size(image.size())) {
        return false;
    }

    rom_.fill(0xFF);
    std::copy(image.begin(), image.end(), rom_.begin());
    rom_size_ = image.size();
    return true;
}

std::span<const std::uint8_t> Machine::rom_image() const noexcept
{
    return std::span<const std::uint8_t>{rom_.data(), rom_size_};
}

std::uint8_t Machine::read_rom(const std::uint16_t address) const noexcept
{
    return rom_[static_cast<std::size_t>(address & 0x3FFFU) & (rom_size_ - 1U)];
}

std::uint8_t Machine::read_memory(const std::uint16_t address) const noexcept
{
    if(memory_map_ == MemoryMap::ram) {
        return main_ram_[address];
    }

    if(address < 0x4000U) {
        return read_rom(address);
    }
    if(address < 0x8000U) {
        return video_ram_[address - 0x4000U];
    }
    return main_ram_[address];
}

void Machine::write_memory(const std::uint16_t address, const std::uint8_t value) noexcept
{
    if(memory_map_ == MemoryMap::ram) {
        main_ram_[address] = value;
        return;
    }

    if(address >= 0x4000U && address < 0x8000U) {
        video_ram_[address - 0x4000U] = value;
    } else if(address >= 0x8000U) {
        main_ram_[address] = value;
    }
}

Machine::MemoryMap Machine::memory_map() const noexcept
{
    return memory_map_;
}

std::uint8_t Machine::read_io(const std::uint16_t port) noexcept
{
    std::uint8_t value = 0xFF;
    switch(port & 0x70U) {
    case 0x00U:
        value = peripherals_->printer_keyboard.read(static_cast<std::uint8_t>(port));
        break;
    case 0x10U:
        value = peripherals_->timer.read(static_cast<std::uint8_t>(port));
        break;
    case 0x20U:
        value = peripherals_->disk_data.read(static_cast<std::uint8_t>(port));
        break;
    case 0x30U:
        memory_map_ = MemoryMap::ram;
        break;
    case 0x40U:
        value = peripherals_->uart.read(static_cast<std::uint8_t>(port));
        break;
    case 0x50U:
        memory_map_ = MemoryMap::rom_video;
        break;
    case 0x60U:
        refresh_floppy_inputs();
        value = peripherals_->machine_control.read(static_cast<std::uint8_t>(port));
        break;
    case 0x70U:
        value = peripherals_->mouse_signals_raw;
        peripherals_->mouse_interrupt_latch = false;
        break;
    }
    refresh_interrupt_sources();
    return value;
}

void Machine::write_io(const std::uint16_t port, const std::uint8_t value) noexcept
{
    switch(port & 0x70U) {
    case 0x00U:
        {
        const auto register_index = static_cast<std::uint8_t>(port);
        peripherals_->printer_keyboard.write(register_index, value);
        if((register_index & 0x03U) == 0U
           && peripherals_->printer_online
           && peripherals_->printer_keyboard.group_a_mode() == Ppi8255::Mode::mode1
           && peripherals_->printer_keyboard.port_a_output_enabled()) {
            peripherals_->printer_bytes.push_back(
                peripherals_->printer_keyboard.output_a());
            peripherals_->printer_keyboard.pulse_ack_a();
        }
        }
        break;
    case 0x10U:
        if(programs_counter_one(static_cast<std::uint8_t>(port), value)) {
            // Loading a new timeout interval explicitly acknowledges any
            // previous I6 request before the next mark can retrigger it.
            peripherals_->disk_timeout_latch = false;
        }
        peripherals_->timer.write(static_cast<std::uint8_t>(port), value);
        break;
    case 0x20U:
        peripherals_->disk_data.write(static_cast<std::uint8_t>(port), value);
        synchronize_disk_selection();
        break;
    case 0x30U:
        peripherals_->interrupts.set_mask(value);
        break;
    case 0x40U:
        peripherals_->uart.write(static_cast<std::uint8_t>(port), value);
        break;
    case 0x50U: {
        peripherals_->disk_aux.write(value);
        if(!peripherals_->disk_aux.write_enabled) {
            peripherals_->ordinary_write_active = false;
        }
        if(!peripherals_->disk_aux.format_enabled) {
            peripherals_->format_write_active = false;
        }
        if(!peripherals_->disk_aux.read_enabled
           && !peripherals_->disk_aux.format_enabled) {
            // The boot ROM's I6 handler acknowledges timeout by clearing the
            // auxiliary register (OUT 50h,0), without reprogramming counter 1.
            peripherals_->disk_timeout_latch = false;
        }
        if(!peripherals_->disk_timing_enabled) {
            peripherals_->disk_aux.reset();
        }
        break;
    }
    case 0x60U:
        peripherals_->machine_control.write(static_cast<std::uint8_t>(port), value);
        synchronize_control_outputs();
        break;
    case 0x70U:
        peripherals_->video_interrupt_latch = false;
        break;
    }
    refresh_interrupt_sources();
}

void Machine::synchronize_control_outputs() noexcept
{
    const auto& control = peripherals_->machine_control;
    if(control.port_b_output_enabled()) {
        set_vertical_scroll(control.output_b());
    }

    peripherals_->floppy.set_control_port_c(
        control.output_c(),
        control.port_c_lower_output_enabled());
    refresh_floppy_inputs();

    peripherals_->disk_timing_enabled = control.port_a_output_enabled()
        && (control.output_a() & 0x40U) == 0;
    if(!peripherals_->disk_timing_enabled) {
        peripherals_->disk_sync_latch = false;
        peripherals_->ordinary_write_active = false;
        peripherals_->format_write_active = false;
        peripherals_->disk_timeout_latch = false;
        peripherals_->disk_aux.reset();
    }
    // The disk synchronization latch is reset until the FM subsystem detects
    // the selected mark. This preserves the documented short mode-set states.
    peripherals_->timer.set_gate(
        1,
        !peripherals_->disk_timing_enabled || peripherals_->disk_sync_latch);
}

void Machine::synchronize_disk_selection() noexcept
{
    const auto& disk_data = peripherals_->disk_data;
    auto effective_port_c = std::uint8_t{0x20U};
    if(disk_data.port_c_upper_output_enabled()) {
        effective_port_c = disk_data.output_c();
    }
    peripherals_->floppy.select_from_disk_ppi(effective_port_c);
    refresh_floppy_inputs();
}

void Machine::process_floppy_byte(const TimedFmByte& byte) noexcept
{
    peripherals_->last_disk_byte_tick = byte.tick;
    if(!peripherals_->disk_timing_enabled) {
        return;
    }

    auto transferred_byte = byte;

    if(peripherals_->disk_aux.format_enabled && byte.tick == 0) {
        if(peripherals_->format_write_active) {
            // The second Index ends the exactly one-revolution format cycle.
            peripherals_->ordinary_write_active = false;
            peripherals_->format_write_active = false;
            peripherals_->disk_aux.reset();
            peripherals_->disk_sync_latch = false;
            // Software observes the Index transition directly. I6 is the
            // separate hardware timeout source, not a format-complete signal.
            peripherals_->timer.set_gate(1, false);
            refresh_interrupt_sources();
            return;
        }
        peripherals_->format_write_active = true;
    }

    if(peripherals_->format_write_active || peripherals_->ordinary_write_active) {
        transferred_byte = transfer_floppy_write_byte(byte.tick);
    }

    if(!peripherals_->disk_aux.read_enabled) {
        return;
    }

    if(!peripherals_->disk_sync_latch) {
        const auto selected_mark = peripherals_->disk_aux.data_mark_selected
            ? is_data_address_mark(transferred_byte)
            : is_id_address_mark(transferred_byte);
        if(selected_mark) {
            peripherals_->disk_sync_latch = true;
            peripherals_->timer.set_gate(1, true);
            // The detector presents the synchronization mark to the 8255, but
            // byte-counting begins with the following field byte.
            peripherals_->suppress_disk_byte_counter_once = true;
        } else {
            return;
        }
    }

    // The detector synchronizes on the selected mark and the deserializer also
    // presents that mark itself, followed by the field bytes, to the 8255.
    peripherals_->disk_data.pulse_strobe_b(transferred_byte.data);
    refresh_interrupt_sources();
}

TimedFmByte Machine::transfer_floppy_write_byte(const std::uint32_t tick) noexcept
{
    const auto data = peripherals_->disk_data.output_a();
    const auto byte = TimedFmByte{
        tick,
        data,
        written_clock_mask(peripherals_->machine_control.output_a()),
    };
    peripherals_->pending_disk_writes.push_back(byte);

    // The serializer has consumed PA and requests the following byte. This
    // continues even when write protect or an unloaded head inhibits media.
    peripherals_->disk_data.pulse_ack_a();
    refresh_interrupt_sources();
    return byte;
}

void Machine::flush_pending_disk_writes() noexcept
{
    for(const auto& byte : peripherals_->pending_disk_writes) {
        static_cast<void>(peripherals_->floppy.write_selected_byte(byte));
    }
    peripherals_->pending_disk_writes.clear();
}

void Machine::refresh_floppy_inputs() noexcept
{
    peripherals_->machine_control.set_port_c_input(
        peripherals_->floppy.control_port_c_inputs());
}

void Machine::refresh_printer_inputs() noexcept
{
    // PC5 is Centronics ONLINE/SELECT; PAPER END on PC4 remains inactive.
    peripherals_->printer_keyboard.set_port_c_input(
        peripherals_->printer_online ? 0x20U : 0x00U);
}

void Machine::refresh_interrupt_sources() noexcept
{
    const auto disk_data_interrupt =
        peripherals_->disk_data.intr_a() || peripherals_->disk_data.intr_b();
    if(!peripherals_->disk_data_interrupt_level && disk_data_interrupt) {
        if(peripherals_->suppress_disk_byte_counter_once) {
            peripherals_->suppress_disk_byte_counter_once = false;
        } else {
            static_cast<void>(peripherals_->timer.clock(2));
        }
    }
    peripherals_->disk_data_interrupt_level = disk_data_interrupt;

    // OUT2 is wired as a level to both I5 and the asynchronous reset of the
    // auxiliary disk register.
    const auto end_of_data = peripherals_->timer.output(2);
    if(end_of_data) {
        peripherals_->disk_aux.reset();
        peripherals_->disk_sync_latch = false;
        peripherals_->ordinary_write_active = false;
        peripherals_->format_write_active = false;
        if(peripherals_->disk_timing_enabled) {
            peripherals_->timer.set_gate(1, false);
        }
    }

    auto& interrupts = peripherals_->interrupts;
    interrupts.set_input(7, disk_data_interrupt);
    interrupts.set_input(6, peripherals_->disk_timeout_latch);
    interrupts.set_input(5, end_of_data);
    interrupts.set_input(4, peripherals_->video_interrupt_latch);
    interrupts.set_input(3, peripherals_->mouse_interrupt_latch);
    interrupts.set_input(2, peripherals_->uart.interrupt_requested());
    interrupts.set_input(1, peripherals_->printer_keyboard.intr_b());
    interrupts.set_input(0, peripherals_->printer_keyboard.intr_a());

    z80_int(&cpu_->cpu, interrupts.pending() ? Z_TRUE : Z_FALSE);
}

void Machine::deliver_keyboard_code(const std::uint8_t code) noexcept
{
    // The keyboard data bus is active-low. DIAG.MAC reads the complemented
    // byte from PB and applies CPL before displaying the logical key code.
    peripherals_->printer_keyboard.pulse_strobe_b(static_cast<std::uint8_t>(~code));
    refresh_interrupt_sources();
}

void Machine::write_video_ram(const std::uint16_t address, const std::uint8_t value) noexcept
{
    video_ram_[address & (video_ram_size - 1)] = value;
}

std::uint8_t Machine::read_video_ram(const std::uint16_t address) const noexcept
{
    return video_ram_[address & (video_ram_size - 1)];
}

void Machine::set_vertical_scroll(const std::uint8_t value) noexcept
{
    vertical_scroll_ = value;
}

std::uint8_t Machine::vertical_scroll() const noexcept
{
    return vertical_scroll_;
}

std::span<const Machine::Pixel> Machine::frame_buffer() const noexcept
{
    return frame_buffer_;
}

void Machine::render_video() noexcept
{
    constexpr std::size_t pixels_per_byte = 8;
    constexpr std::size_t byte_columns = video_width / pixels_per_byte;

    for(std::size_t byte_column = 0; byte_column < byte_columns; ++byte_column) {
        for(std::size_t screen_y = 0; screen_y < video_height; ++screen_y) {
            const auto source_y = static_cast<std::uint8_t>(screen_y + vertical_scroll_);
            const auto address = (byte_column << 8U) | source_y;
            const auto video_byte = video_ram_[address];
            const auto pixel_offset = (screen_y * video_width) + (byte_column * pixels_per_byte);

            for(std::size_t bit = 0; bit < pixels_per_byte; ++bit) {
                const auto mask = static_cast<std::uint8_t>(0x80U >> bit);
                frame_buffer_[pixel_offset + bit] = (video_byte & mask) != 0 ? white : black;
            }
        }
    }
}

} // namespace fk1
