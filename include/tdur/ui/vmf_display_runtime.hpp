#pragma once

#include "tdur/ui/vmf_program.hpp"
#include "tdur/ui/vmf_stack.hpp"
#include "tdur/ui/vmf_transform.hpp"
#include "tdur/ui/vmf_variables.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace tdur::ui {

struct VmfInstanceHandle {
    std::uint32_t context{};
    std::uint32_t slot{};
    std::uint32_t generation{};
    friend bool operator==(const VmfInstanceHandle&,
                           const VmfInstanceHandle&) = default;
};
struct VmfNodeHandle {
    std::uint32_t context{};
    std::uint32_t slot{};
    std::uint32_t generation{};
    friend bool operator==(const VmfNodeHandle&,
                           const VmfNodeHandle&) = default;
};

[[nodiscard]] VmfSpriteIdentity
vmf_sprite_identity(VmfInstanceHandle handle) noexcept;
[[nodiscard]] VmfInstanceHandle
vmf_instance_handle(VmfSpriteIdentity identity) noexcept;

enum class VmfInstanceKind { timeline, button };

enum class VmfTimelineAction {
    next_frame,
    previous_frame,
    play,
    stop,
    goto_frame,
};

struct VmfDriverFrames {
    std::int32_t current = 1;
    std::int32_t requested = 1;
    bool request_pending{};
    friend bool operator==(const VmfDriverFrames&,
                           const VmfDriverFrames&) = default;
};

struct VmfInstanceSnapshot {
    VmfInstanceHandle handle;
    VmfInstanceKind kind{};
    std::uint32_t character_id{}; // Own resource, independent of its node.
    std::optional<VmfInstanceHandle> parent;
    std::optional<VmfNodeHandle> attached_node;
    std::int32_t depth = -1;
    std::int32_t birth_frame = 1;
    std::optional<std::uint32_t> scope_id; // Timeline only; root 0 is valid.
    bool cached{};
    VmfDriverFrames frames;     // Timeline only, initially {1,1,false}.
    std::uint32_t play_state{}; // Timeline: 2 if one frame, otherwise 1.
    std::uint32_t flags{}; // Instance flags, not placement/transform flags.
    std::uint32_t record_balance{}; // Wrapper +CC, NOT the live node count.
    std::size_t active_nodes{};
    std::size_t cached_children{};
    VmfTransform local;
    VmfTransform composed;
};

struct VmfNodeSnapshot {
    VmfNodeHandle handle;
    VmfInstanceHandle owner;
    std::uint32_t character_id{};
    std::uint16_t depth{};
    std::uint16_t clip_depth{};
    std::uint32_t ratio{};
    std::uint32_t placement_flags{};
    std::array<char, 32> name{};
    std::optional<VmfInstanceHandle> child;
    VmfTransform local;
    VmfTransform composed;
};

struct VmfDisplayRuntimeLimits {
    // Root consumes one timeline slot. Cached objects consume their slots.
    // timelines >= 1; buttons/nodes may be zero. Instance total and node count
    // must each be <= UINT32_MAX-1. Insufficient storage bytes, including zero,
    // is resource_limit; invalid counts/depth bounds are invalid_limits.
    std::size_t max_timelines = 4096;
    std::size_t max_buttons = 4096;
    std::size_t max_nodes = 16384;
    std::size_t max_parent_depth = 64; // Root depth 1; maximum supported 256.
    std::size_t max_storage_bytes = 64U * 1024U * 1024U;
};

enum class VmfDisplayRuntimeErrorCode {
    invalid_limits,
    allocation_failure,
    resource_limit,
    invalid_handle,
    wrong_kind,
    invalid_driver_state,
    invalid_record,
    invalid_native_state,
    needs_services,
    closed,
    variables,
};
struct VmfDisplayRuntimeError {
    VmfDisplayRuntimeErrorCode code{};
    std::string_view detail; // Static diagnostic; errors do not allocate.
    std::optional<VmfVariableError> variable_error{};
};

struct VmfTargetLookupLimits {
    // Per lookup, not accumulated across an execute_vmf_actions invocation.
    // Count bytes before the first NUL, excluding it; ignore bytes after NUL.
    // This quota covers that entire prefix, including a colon suffix which
    // native path resolution may ignore. A view end is a logical terminator.
    std::size_t max_path_bytes = 64U * 1024U;
    // Charge each active node examined and each parent step. '..' attempting
    // to pass root costs one visit; a leading '/' already at root costs zero.
    std::size_t max_visits = 1'000'000;
};
struct VmfTargetResolution {
    bool found{};
    // A native miss clears the target; a successful lookup has a live target.
    std::optional<VmfInstanceHandle> target;
};

struct VmfTextBindingLimits {
    // Modern scratch extent, including NUL, not an inferred native capacity.
    // Two buffers are preallocated; an error publishes neither prefix nor
    // caches. Ordinary successful bindings copy only their actual text.
    std::size_t max_scratch_bytes = 64U * 1024U;
    VmfTargetLookupLimits targets;
    // Input to native44B030's selector initialization; zero is a modern host
    // default, not a claim about every original context's byte+2C.
    std::uint8_t context_byte_2c{};
};
struct VmfTextResourceSnapshot {
    std::uint32_t character_id{};
    std::uint8_t selector_2f{};
    bool binding_executed{};
    std::optional<std::size_t> font_record_index;
    std::optional<std::size_t> companion_record_index;
};

enum class VmfPlaceDisposition { created_node, reused_node, ignored_depth };
struct VmfPlaceReport {
    VmfPlaceDisposition disposition{};
    bool native_al{}; // Not a success boolean; ignored_depth also returns 1.
    std::optional<VmfNodeHandle> node;
};

struct VmfDisplaySnapshot {
    VmfProgramPtr program;
    // Includes cached instances, ordered by slot.
    std::vector<VmfInstanceSnapshot> instances;
    // Active nodes grouped by owner slot, then ascending native depth.
    std::vector<VmfNodeSnapshot> nodes;
};

// Bounded live display lists and instance lifetimes, NOT a frame scheduler.
// Driver setters publish state only: no implicit tick, Goto, script, input or
// render. Operations use their owner's stored state. The caller serializes
// access; no callbacks/reentrancy are supported in this initial integration.
//
// Type37 variable/focus services remain needs_services before mutation.
// Variable cleanup(flag200) requires the explicit owned dictionary backend.
// Its ordered erases commit before topology changes, without callbacks.
// Pool/counter exhaustion is an added safe refusal;
// cached objects are never evicted. Nodes retain native pool field residues.
// Handles distinguish contexts and generations; identity/scope counters never
// wrap. The native record_balance deliberately uses modulo32 INC/DEC instead.
// Snapshots own copies, never borrow pool elements. Native pointer alias and
// exception/partial-write behavior are not exposed as a modern C++ contract.
class VmfDisplayRuntime {
  public:
    [[nodiscard]] static std::expected<std::unique_ptr<VmfDisplayRuntime>,
                                       VmfDisplayRuntimeError>
    create(VmfProgramPtr program, VmfDisplayRuntimeLimits limits = {}) noexcept;
    // Owns one common dictionary for this display's timeline scopes. Its
    // separate variable budget is explicit; the fixed cleanup plan counts
    // against display storage. Text37/focus services remain unavailable.
    [[nodiscard]] static std::expected<std::unique_ptr<VmfDisplayRuntime>,
                                       VmfDisplayRuntimeError>
    create_with_variables(VmfProgramPtr program,
                          VmfVariableLimits variable_limits = {},
                          VmfDisplayRuntimeLimits limits = {}) noexcept;
    // Owns native PC44BDC0 variable initialization and per-context font caches.
    // Requires the program's explicit text decoding. PC fields requiring the
    // unbound input/focus service still stop before mutation. No glyph drawing
    // or SDL callback is simulated; donor Xbox input semantics remain separate.
    [[nodiscard]] static std::expected<std::unique_ptr<VmfDisplayRuntime>,
                                       VmfDisplayRuntimeError>
    create_with_text_bindings(VmfProgramPtr program,
                              VmfTextBindingLimits text_limits = {},
                              VmfVariableLimits variable_limits = {},
                              VmfDisplayRuntimeLimits limits = {}) noexcept;
    ~VmfDisplayRuntime();
    VmfDisplayRuntime(const VmfDisplayRuntime&) = delete;
    VmfDisplayRuntime& operator=(const VmfDisplayRuntime&) = delete;
    VmfDisplayRuntime(VmfDisplayRuntime&&) = delete;
    VmfDisplayRuntime& operator=(VmfDisplayRuntime&&) = delete;

    [[nodiscard]] VmfInstanceHandle root() const noexcept;
    // Owning immutable program, including after close().
    [[nodiscard]] VmfProgramPtr program() const noexcept;
    // Borrowed until display destruction, including after close. Access is
    // serialized. Writes go through the methods below to arm cleanup flags.
    [[nodiscard]] const VmfVariables *variable_dictionary() const noexcept;
    [[nodiscard]] std::expected<VmfTextResourceSnapshot, VmfDisplayRuntimeError>
    inspect_text_resource(std::uint32_t character_id) const noexcept;
    // Copies the active scratch through NUL. A small output is refused without
    // writing. The modern initial scratch is empty; native startup bytes have
    // not been established. Requires a live context and the binding backend.
    [[nodiscard]] std::expected<std::size_t, VmfDisplayRuntimeError>
    copy_text_scratch(std::span<char> destination) const noexcept;
    // Resolved-target portion of449240/449290/4492E0. nullopt name is native
    // NULL: no insertion, but flag200 is still armed. Empty name is a real
    // key. A full table likewise performs no assignment and still arms200.
    // Modern variable errors are nested and stop before the flag write.
    [[nodiscard]] std::expected<void, VmfDisplayRuntimeError>
    set_variable_number_bits(VmfInstanceHandle target,
                             std::optional<std::string_view> name,
                             std::uint32_t bits) noexcept;
    [[nodiscard]] std::expected<void, VmfDisplayRuntimeError>
    set_variable_text(VmfInstanceHandle target,
                      std::optional<std::string_view> name,
                      std::string_view text) noexcept;
    [[nodiscard]] std::expected<void, VmfDisplayRuntimeError>
    set_variable_linked(VmfInstanceHandle target,
                        std::optional<std::string_view> name,
                        const VmfTextReference &text) noexcept;
    // Native 444110/4442C0/444310/444360/4443A0, diagnostic logging disabled.
    // Publishes requests/play state only: no frame execution, rewind or queue
    // reset. frame_index is zero-based and used only by goto_frame. Previous
    // at current=1 preserves an existing request. An invalid enum is refused
    // as invalid_driver_state before mutation. No implicit root/NULL target.
    [[nodiscard]] std::expected<void, VmfDisplayRuntimeError>
    apply_timeline_action(VmfInstanceHandle target, VmfTimelineAction action,
                          std::uint16_t frame_index = 0) noexcept;
    // 443FE0/447FE0: search whole stored names in active depth order, without
    // pre-splitting on delimiters. XOR/DF byte matching is NOT CRT casefold.
    // Empty and colon-prefixed paths succeed on origin. A leading slash walks
    // parent links; '..' consumes exactly two bytes, with no slash requirement.
    // Native misses yield {false,nullopt}; invalid handles and quotas are
    // errors. A comparison requiring bytes beyond the bounded C-string (e.g.
    // stored space matches input NUL under DF) is invalid_native_state, never
    // invented zero padding. No instance/display state changes on any result.
    [[nodiscard]] std::expected<VmfTargetResolution, VmfDisplayRuntimeError>
    resolve_target(VmfInstanceHandle origin, std::string_view path,
                   VmfTargetLookupLimits limits = {}) const noexcept;
    // 448990: first label in this timeline's OWN resource, frame number base1.
    // Native _strnicmp(...,63) uses the calling CRT/thread's current LC_CTYPE;
    // this does not select or establish the original game's locale. No tick,
    // script, queue, temporary cursor or frame mutation is exposed.
    [[nodiscard]] std::expected<std::optional<std::uint32_t>,
                                VmfDisplayRuntimeError>
    find_label(VmfInstanceHandle target, std::string_view label) const noexcept;
    // 444490: a match updates requested/pending, then Stop; absence preserves
    // requested/pending and still Stop. Returns whether a label matched.
    [[nodiscard]] std::expected<bool, VmfDisplayRuntimeError>
    apply_goto_label(VmfInstanceHandle target, std::string_view label) noexcept;
    // Host441560: same label lookup/request clamping, then play1 if the
    // supplied byte is nonzero, stop2 otherwise, even when no label matches.
    // Returns the original base1 match (before clamping), not its request.
    [[nodiscard]] std::expected<std::optional<std::uint32_t>,
                                VmfDisplayRuntimeError>
    apply_host_goto_label(VmfInstanceHandle target, std::string_view label,
                          std::uint8_t play) noexcept;
    // current: 1..frame_count+1 (End boundary); requested: 1..frame_count.
    // request_pending is independent, not normalized by this driver setter.
    [[nodiscard]] std::expected<void, VmfDisplayRuntimeError>
    set_driver_frames(VmfInstanceHandle owner, VmfDriverFrames frames) noexcept;
    // Publishes a timeline's local transform/override flags and propagates
    // composed transforms. Bit0x200 requires the owned variable backend and,
    // once armed, is retained across transform/override publications.
    [[nodiscard]] std::expected<void, VmfDisplayRuntimeError>
    set_driver_properties(VmfInstanceHandle owner, VmfTransform local,
                          std::uint32_t flags) noexcept;
    // Resolved-target portion of4464A0 with native diagnostic logging disabled.
    // index is the unsigned bit pattern AFTER ADDSS0.5/CVTTSS2SI by the caller.
    // Text is borrowed only for this call, after all operand pops/aliases.
    // Native ignored indices succeed without mutation. Flag-only indices
    // 7/16/17 inspect the first byte=='1', retain all other bits and do not
    // propagate. Numeric transforms require a parent, after conversion.
    [[nodiscard]] std::expected<void, VmfDisplayRuntimeError>
    set_property(VmfInstanceHandle target, std::uint32_t index,
                 std::string_view text) noexcept;
    // Includes wrapper44AF80's modulo32 +CC increment if native AL is true.
    // The source placement/removal must belong to the owner's own sprite.
    [[nodiscard]] std::expected<VmfPlaceReport, VmfDisplayRuntimeError>
    place(VmfInstanceHandle owner, std::size_t placement_index) noexcept;
    // Includes wrapper44AFC0's +CC decrement even if no active node is found.
    [[nodiscard]] std::expected<bool, VmfDisplayRuntimeError>
    remove(VmfInstanceHandle owner, std::size_t removal_index) noexcept;
    // 447200: clears active nodes and prunes cache against requested frame.
    // Does NOT reset current frame/record cursor; that belongs to the driver.
    [[nodiscard]] std::expected<void, VmfDisplayRuntimeError>
    rewind(VmfInstanceHandle owner) noexcept;
    // Destroys the whole context using root cleanup, then invalidates root.
    // Idempotent. No renderer/callback/script runs in close/destruction.
    [[nodiscard]] std::expected<void, VmfDisplayRuntimeError> close() noexcept;

    [[nodiscard]] std::expected<VmfInstanceSnapshot, VmfDisplayRuntimeError>
    inspect_instance(VmfInstanceHandle handle) const noexcept;
    [[nodiscard]] std::expected<VmfNodeSnapshot, VmfDisplayRuntimeError>
    inspect_node(VmfNodeHandle handle) const noexcept;
    // Budget: sizeof(result) + sizeof(snapshot element) per element. Existing
    // shared program storage and allocator/capacity overhead are excluded.
    [[nodiscard]] std::expected<VmfDisplaySnapshot, VmfDisplayRuntimeError>
    snapshot(std::size_t max_output_bytes = 16U * 1024U * 1024U) const noexcept;

  private:
    friend class VmfFramePlayer;
    [[nodiscard]] std::expected<std::optional<VmfNodeHandle>,
                                VmfDisplayRuntimeError>
    playback_head(VmfInstanceHandle owner) const noexcept;
    [[nodiscard]] std::expected<std::optional<VmfNodeHandle>,
                                VmfDisplayRuntimeError>
    playback_next(VmfNodeHandle node) const noexcept;
    [[nodiscard]] std::expected<void, VmfDisplayRuntimeError>
    playback_reset_balance(VmfInstanceHandle owner) noexcept;
    struct Storage;
    explicit VmfDisplayRuntime(std::unique_ptr<Storage> storage) noexcept;
    std::unique_ptr<Storage> storage_;
};

} // namespace tdur::ui
