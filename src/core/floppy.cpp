#include "fk1/floppy.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace fk1 {

bool FloppyTrack::set_bytes(const std::span<const TimedFmByte> bytes)
{
    if(!std::is_sorted(
           bytes.begin(),
           bytes.end(),
           [](const TimedFmByte& left, const TimedFmByte& right) {
               return left.tick < right.tick;
           })) {
        return false;
    }

    for(std::size_t index = 0; index < bytes.size(); ++index) {
        if(bytes[index].tick >= revolution_ticks) {
            return false;
        }
        if(index != 0
           && bytes[index].tick - bytes[index - 1U].tick < byte_ticks) {
            return false;
        }
    }

    if(bytes.size() > 1U) {
        const auto wrap_distance = static_cast<std::uint64_t>(bytes.front().tick)
            + revolution_ticks - bytes.back().tick;
        if(wrap_distance < byte_ticks) {
            return false;
        }
    }

    bytes_.assign(bytes.begin(), bytes.end());
    return true;
}

bool FloppyTrack::write_byte(const TimedFmByte& byte)
{
    if(byte.tick >= revolution_ticks) {
        return false;
    }

    const auto position = std::lower_bound(
        bytes_.begin(),
        bytes_.end(),
        byte.tick,
        [](const TimedFmByte& candidate, const std::uint32_t tick) {
            return candidate.tick < tick;
        });
    if(position != bytes_.end() && position->tick == byte.tick) {
        *position = byte;
        return true;
    }
    if(bytes_.empty()) {
        bytes_.push_back(byte);
        return true;
    }

    const auto previous = position == bytes_.begin() ? std::prev(bytes_.end())
                                                      : std::prev(position);
    const auto next = position == bytes_.end() ? bytes_.begin() : position;
    const auto distance_from_previous = static_cast<std::uint64_t>(byte.tick)
        + (previous->tick > byte.tick ? revolution_ticks : 0U) - previous->tick;
    const auto distance_to_next = static_cast<std::uint64_t>(next->tick)
        + (next->tick < byte.tick ? revolution_ticks : 0U) - byte.tick;
    if(distance_from_previous < byte_ticks || distance_to_next < byte_ticks) {
        return false;
    }

    bytes_.insert(position, byte);
    return true;
}

void FloppyTrack::clear() noexcept
{
    bytes_.clear();
}

std::span<const TimedFmByte> FloppyTrack::bytes() const noexcept
{
    return bytes_;
}

bool FloppyDiskImage::set_track(
    const std::size_t track_index,
    const std::span<const TimedFmByte> bytes)
{
    return track_index < tracks_.size() && tracks_[track_index].set_bytes(bytes);
}

const FloppyTrack* FloppyDiskImage::track(const std::size_t track_index) const noexcept
{
    return track_index < tracks_.size() ? &tracks_[track_index] : nullptr;
}

FloppyTrack* FloppyDiskImage::track(const std::size_t track_index) noexcept
{
    return track_index < tracks_.size() ? &tracks_[track_index] : nullptr;
}

void FloppyDrive::insert(FloppyDiskImage image, const FloppyAccess access)
{
    image_ = std::move(image);
    access_ = access;
}

std::optional<FloppyDiskImage> FloppyDrive::eject() noexcept
{
    auto image = std::move(image_);
    image_.reset();
    access_ = FloppyAccess::read_only;
    return image;
}

void FloppyDrive::visit_bytes(
    const std::uint64_t master_ticks,
    void* const context,
    const FloppyByteCallback callback) const noexcept
{
    if(!image_ || master_ticks == 0 || callback == nullptr) {
        return;
    }

    const auto* current_track = image_->track(head_track_);
    if(current_track == nullptr || current_track->bytes().empty()) {
        return;
    }
    const auto bytes = current_track->bytes();
    const auto visit_interval = [&](const std::uint32_t begin, const std::uint32_t end) {
        auto byte = std::lower_bound(
            bytes.begin(),
            bytes.end(),
            begin,
            [](const TimedFmByte& candidate, const std::uint32_t tick) {
                return candidate.tick < tick;
            });
        while(byte != bytes.end() && byte->tick < end) {
            callback(context, *byte);
            ++byte;
        }
    };

    auto remaining = master_ticks;
    auto phase = rotation_phase_;
    while(remaining != 0) {
        const auto interval = std::min<std::uint64_t>(
            remaining,
            static_cast<std::uint64_t>(revolution_ticks - phase));
        visit_interval(phase, static_cast<std::uint32_t>(phase + interval));
        remaining -= interval;
        phase = 0;
    }
}

void FloppyDrive::advance(const std::uint64_t master_ticks) noexcept
{
    if(!image_ || master_ticks == 0) {
        return;
    }

    const auto whole_revolutions = master_ticks / revolution_ticks;
    const auto remaining_ticks = master_ticks % revolution_ticks;
    const auto phase_sum = static_cast<std::uint64_t>(rotation_phase_) + remaining_ticks;
    completed_revolutions_ += whole_revolutions + (phase_sum / revolution_ticks);
    rotation_phase_ = static_cast<std::uint32_t>(phase_sum % revolution_ticks);
}

bool FloppyDrive::write_byte(const TimedFmByte& byte)
{
    if(write_protected()) {
        return false;
    }
    auto* current_track = image_->track(head_track_);
    return current_track != nullptr && current_track->write_byte(byte);
}

void FloppyDrive::step_toward_track_zero() noexcept
{
    if(head_track_ != 0) {
        --head_track_;
    }
}

void FloppyDrive::step_toward_higher_track() noexcept
{
    if(head_track_ < last_track) {
        ++head_track_;
    }
}

bool FloppyDrive::media_present() const noexcept
{
    return image_.has_value();
}

bool FloppyDrive::write_protected() const noexcept
{
    // With no medium, report the safe state and reject writes just like a
    // read-only image. Rotation and Index remain inactive independently.
    return !image_ || access_ == FloppyAccess::read_only;
}

bool FloppyDrive::track_zero() const noexcept
{
    return head_track_ == 0;
}

bool FloppyDrive::index_active(const std::uint32_t pulse_width_ticks) const noexcept
{
    return image_.has_value() && pulse_width_ticks != 0
        && rotation_phase_ < pulse_width_ticks;
}

std::uint8_t FloppyDrive::head_track() const noexcept
{
    return head_track_;
}

std::uint32_t FloppyDrive::rotation_phase() const noexcept
{
    return rotation_phase_;
}

std::uint64_t FloppyDrive::completed_revolutions() const noexcept
{
    return completed_revolutions_;
}

const FloppyDiskImage* FloppyDrive::image() const noexcept
{
    return image_ ? &*image_ : nullptr;
}

FloppyDiskImage* FloppyDrive::image() noexcept
{
    return image_ ? &*image_ : nullptr;
}

void FloppySubsystem::reset_controller() noexcept
{
    // A system reset affects the shared cable electronics, not spindle phase,
    // inserted media, or the physical position of either head.
    selected_drive_ = drive_b;
    step_asserted_ = false;
    direction_to_higher_track_ = false;
    track_greater_than_43_active_ = false;
    head_loaded_ = false;
}

void FloppySubsystem::advance(
    const std::uint64_t master_ticks,
    void* const context,
    const FloppyByteCallback callback) noexcept
{
    selected_drive().visit_bytes(master_ticks, context, callback);
    for(auto& current_drive : drives_) {
        current_drive.advance(master_ticks);
    }
}

bool FloppySubsystem::insert(
    const std::size_t drive_index,
    FloppyDiskImage image,
    const FloppyAccess access)
{
    if(drive_index >= drives_.size()) {
        return false;
    }
    drives_[drive_index].insert(std::move(image), access);
    return true;
}

std::optional<FloppyDiskImage> FloppySubsystem::eject(const std::size_t drive_index) noexcept
{
    return drive_index < drives_.size() ? drives_[drive_index].eject() : std::nullopt;
}

bool FloppySubsystem::write_selected_byte(const TimedFmByte& byte)
{
    // With the head unloaded the controller and its ACK handshake still run,
    // but no magnetic transitions reach the medium (as used by DIAG.MAC SQ08).
    return head_loaded_ && selected_drive().write_byte(byte);
}

bool FloppySubsystem::set_index_pulse_width(const std::uint32_t master_ticks) noexcept
{
    if(master_ticks >= FloppyDrive::revolution_ticks) {
        return false;
    }
    index_pulse_width_ticks_ = master_ticks;
    return true;
}

void FloppySubsystem::select_from_disk_ppi(const std::uint8_t effective_port_c) noexcept
{
    selected_drive_ = (effective_port_c & 0x20U) != 0 ? drive_b : drive_a;
}

void FloppySubsystem::set_control_port_c(
    const std::uint8_t output,
    const bool lower_output_enabled) noexcept
{
    if(!lower_output_enabled) {
        step_asserted_ = false;
        direction_to_higher_track_ = false;
        track_greater_than_43_active_ = false;
        head_loaded_ = false;
        return;
    }

    const auto step_asserted = (output & 0x01U) != 0;
    direction_to_higher_track_ = (output & 0x02U) != 0;
    track_greater_than_43_active_ = (output & 0x04U) != 0;
    head_loaded_ = (output & 0x08U) != 0;

    // DIAG.MAC uses PC1=0 for RESTORE and PC1=1 when seeking outward.
    // It emits a step as PC0 1 -> 0, i.e. the trailing edge of active /STEP.
    if(step_asserted_ && !step_asserted) {
        if(direction_to_higher_track_) {
            selected_drive().step_toward_higher_track();
        } else {
            selected_drive().step_toward_track_zero();
        }
    }
    step_asserted_ = step_asserted;
}

std::uint8_t FloppySubsystem::control_port_c_inputs() const noexcept
{
    std::uint8_t status = 0x40U;
    const auto& current_drive = selected_drive();
    if(current_drive.track_zero()) {
        status = static_cast<std::uint8_t>(status | 0x10U);
    }
    if(current_drive.write_protected()) {
        status = static_cast<std::uint8_t>(status | 0x20U);
    }
    if(current_drive.index_active(index_pulse_width_ticks_)) {
        status = static_cast<std::uint8_t>(status | 0x80U);
    }
    return status;
}

std::size_t FloppySubsystem::selected_drive_index() const noexcept
{
    return selected_drive_;
}

std::uint32_t FloppySubsystem::index_pulse_width() const noexcept
{
    return index_pulse_width_ticks_;
}

bool FloppySubsystem::track_greater_than_43_active() const noexcept
{
    return track_greater_than_43_active_;
}

bool FloppySubsystem::head_loaded() const noexcept
{
    return head_loaded_;
}

const FloppyDrive& FloppySubsystem::drive(const std::size_t drive_index) const noexcept
{
    return drives_[drive_index < drives_.size() ? drive_index : drive_b];
}

FloppyDrive& FloppySubsystem::selected_drive() noexcept
{
    return drives_[selected_drive_];
}

const FloppyDrive& FloppySubsystem::selected_drive() const noexcept
{
    return drives_[selected_drive_];
}

} // namespace fk1
