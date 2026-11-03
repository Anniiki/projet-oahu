#pragma once

#include "tdur/ui/vmf_fscommand_router.hpp"
#include "tdur/ui/vmf_menu_movie.hpp"

#include <array>
#include <span>

namespace tdur::ui {

enum class VmfFsGlobalBranch {
    unknown,
    avno,
    avye,
    cams,
    clms,
    frca,
    frok,
    gein,
    geou,
    mcin,
    mcou,
    neav,
    tfoc,
    tuin,
    tuou,
    wend
};
// Native99B7AB..99B7C7: four physical bytes, each MOVSX before SHL/OR.
// This low-level reader refuses incomplete physical windows. A complete
// C-string command shorter than four bytes is handled separately as AL0.
std::expected<std::uint32_t, VmfFsCommandError>
classify_vmf_command4(std::string_view command) noexcept;
VmfFsGlobalBranch identify_vmf_global_branch(std::uint32_t code) noexcept;

struct VmfFsStateOwner {
    std::optional<std::uint32_t> state_c, field_38;
};
using VmfFsCollectionItem = VmfMenuMovieState;
struct VmfFsFrokChild {
    std::optional<std::uint8_t> flags_8;
};
struct VmfFsFrokRoot {
    std::optional<VmfFsFrokChild*> child_90;
};
struct VmfFsModeOwner {
    std::optional<std::uint32_t> dword_2c;
    std::optional<std::uint8_t> byte_30, byte_31;
};
struct VmfFsTutorialLink {
    std::optional<std::uint8_t> byte_4;
};
struct VmfFsTutorialEnclosing {
    std::optional<VmfFsTutorialLink*> link_ffa6c;
};
struct VmfFsTutorialOwner {
    std::optional<std::uint32_t> kind_44;
    // Explicit logical relation for (owner - 0x10), never x86 pointer math.
    // Unknown waits; required known NULL refuses before unsafe native access.
    std::optional<VmfFsTutorialEnclosing*> enclosing;
};
struct VmfFsFocusGlobals {
    std::optional<std::uint32_t> count_14160fc, state_1416100, index_1416104,
        state_1416108;
};
struct VmfFsGlobalState {
    // Same native [this+4] for delegates and the state mutations, not two
    // independently bindable aliases. Known NULL may be passed to a helper;
    // direct state dereference requires a non-NULL owner.
    std::optional<VmfFsStateOwner*> owner_4;
    std::optional<std::uint8_t> saved_8;
};
struct VmfFsGlobalGlobals {
    std::optional<VmfFsFrokRoot*> root_11403d0;
    std::optional<VmfFsModeOwner*> owner_114c04c;
    std::optional<void*> owner_114c048, owner_10e75f0;
    std::optional<VmfFsTutorialOwner*> owner_10e72f0;
    // Fixed writable cells: an unknown previous value does not prevent a
    // native store. Unknown values are required only before actual reads.
    std::optional<std::uint8_t> tutorial_flag_141194d;
    VmfFsFocusGlobals focus;
};
class VmfFsGlobalCall {
  public:
    virtual ~VmfFsGlobalCall() = default;
    // Complete named helper, not availability. Known NULL argument is passed
    // intact; this caller does not dereference it. The callee owns its policy.
    virtual std::expected<void, VmfFsCommandError>
    invoke(void* argument) noexcept = 0;
};
class VmfFsGlobalCollection {
  public:
    virtual ~VmfFsGlobalCollection() = default;
    // Complete99A9A0/99A780 on SAME fixed logical114B0C0, selector1.
    // count is the signed interpretation of99A9A0's32-bit matching count
    // Capture it once; never use lookup's NULL as a loop sentinel.
    // item is a fresh, side-effect-free lookup by FILTERED rank: nonzero
    // byte2E0 and groupAD4 == selector, in storage order. It returns the last
    // match for an absent rank, NULL only when no row matches (99A780).
    // Only after lookup completion may a known NULL item be skipped.
    virtual std::expected<std::int32_t, VmfFsCommandError>
    count(std::uint32_t selector) noexcept = 0;
    virtual std::expected<VmfFsCollectionItem*, VmfFsCommandError>
    item(std::uint32_t selector, std::int32_t index) noexcept = 0;
};
class VmfFsGlobalFocusQuery {
  public:
    virtual ~VmfFsGlobalFocusQuery() = default;
    // Complete4409C0(userdata, fixed F07C88 data, local32). No layout or text
    // interpretation of local32 is inferred; fill what441200 consumes before
    // returning a nonzero AL. No backing is retained beyond the invocation.
    virtual std::expected<std::uint8_t, VmfFsCommandError>
    query(const void* userdata, const void* constant_f07c88,
          std::span<std::byte, 32> local) noexcept = 0;
};
class VmfFsGlobalFocusApply {
  public:
    virtual ~VmfFsGlobalFocusApply() = default;
    // Complete441200; consume/copy necessary data, never keep a stack buffer.
    virtual std::expected<void, VmfFsCommandError>
    apply(const void* userdata,
          std::span<const std::byte, 32> local) noexcept = 0;
};
class VmfFsGlobalTutorial {
  public:
    virtual ~VmfFsGlobalTutorial() = default;
    // Complete7FBD80 on fixed logical1214D30, not a readiness acknowledgement.
    virtual std::expected<void, VmfFsCommandError>
    invoke(bool entering) noexcept = 0;
};
struct VmfFsGlobalInputs {
    VmfFsGlobalState& state;
    VmfFsGlobalGlobals& globals;
    std::optional<VmfFsGlobalCall*> call_99fc80, call_9a0110, call_99fcc0,
        call_99feb0, call_866040, call_866080;
    std::optional<VmfFsGlobalCollection*> collection_114b0c0;
    std::optional<const void*> constant_f07c88;
    std::optional<VmfFsGlobalFocusQuery*> focus_query_4409c0;
    std::optional<VmfFsGlobalFocusApply*> focus_apply_441200;
    std::optional<VmfFsGlobalTutorial*> tutorial_1214d30;
};
enum class VmfFsGlobalPhase {
    classify,
    owner4,
    state_compare,
    state_clear,
    state_set,
    leaf_call,
    frok_root,
    frok_child,
    frok_flags,
    collection,
    count,
    loop_lookup,
    loop_flags,
    post_owner,
    mode_owner,
    mcin_dword,
    mcin_byte,
    mcou_byte,
    focus_key,
    focus_query,
    focus_count,
    focus_zero100,
    focus_zero108,
    focus_index,
    focus_apply,
    tuin_owner_first,
    tuin_read,
    tuin_save,
    tuin_owner_second,
    tuin_zero,
    tuou_owner_first,
    tutorial_flag,
    tutorial_kind,
    tutorial_enclosing,
    tutorial_link,
    tutorial_write,
    tutorial_call,
    tuin_owner_after,
    tuou_saved_read,
    tuou_owner_after,
    tuou_restore,
    wend_argument,
    finished
};
struct VmfFsGlobalSnapshot {
    VmfFsGlobalPhase phase{};
    VmfFsGlobalBranch branch{};
    std::size_t steps{}, calls{}, writes{};
    std::int32_t loop_count{}, loop_index{};
    std::optional<std::uint8_t> result_al;
    std::string_view waiting_for;
    bool faulted{};
};
// Whole99B7A0 control/data effects, disk-primary/secondary796B/2ranges;
// gap3B excluded. The review corroborates static bytes, not runtime execution.
// See docs/evidence/vmf_fscommand_global.md. Helpers remain mandatory complete
// service bodies. The old445CB0 capture does not corroborate this body.
// Inputs/state/globals/pointees are stable serialized borrowed objects and
// bindings stay live. Preserve callee affinity. No raw x86 address is called.
// The command view end is a C-string boundary, as in the445CB0 envelope.
// This is a complete bounded command, not a partial transport fragment.
// Only consumed reads need known values; overwritten destinations need no
// invented old value. Unknown waits; required known NULL faults; valid native
// NULL branches/arguments remain distinct. Errors/cancel keep prefix effects,
// do not finish or roll back, and a caller must not resume them as completed.
class VmfFsGlobalContext {
  public:
    VmfFsGlobalContext(std::string_view command, const void* userdata) noexcept;
    VmfFsGlobalContext(const VmfFsGlobalContext&) = delete;
    VmfFsGlobalContext& operator=(const VmfFsGlobalContext&) = delete;
    VmfFsGlobalContext(VmfFsGlobalContext&&) = delete;
    VmfFsGlobalContext& operator=(VmfFsGlobalContext&&) = delete;
    std::expected<VmfHostCallReport, VmfFsCommandError>
    run(const VmfFsGlobalInputs& inputs, std::size_t budget = 4096) noexcept;
    std::expected<std::uint8_t, VmfFsCommandError> result() const noexcept;
    std::expected<void, VmfFsCommandError> cancel() noexcept;
    VmfFsGlobalSnapshot snapshot() const noexcept;
    std::optional<VmfFsCommandError> last_error() const noexcept;

  private:
    friend class VmfFsGlobalHandler;
    void select(VmfFsGlobalBranch branch) noexcept;
    void finish(std::uint8_t result) noexcept;
    void after_tutorial_selection() noexcept;
    void next_item() noexcept;
    std::string_view command_;
    const void* userdata_{};
    VmfFsStateOwner* owner4_{};
    VmfFsModeOwner* mode_{};
    VmfFsFrokRoot* frok_root_{};
    VmfFsFrokChild* frok_child_{};
    VmfFsCollectionItem* item_{};
    VmfFsGlobalCollection* collection_{};
    VmfFsTutorialOwner* tutorial_owner_{};
    VmfFsTutorialEnclosing* enclosing_{};
    VmfFsTutorialLink* link_{};
    void* argument_{};
    const void* focus_key_{};
    std::uint32_t focus_count_{};
    std::uint8_t saved_byte_{};
    bool was_two_{};
    std::array<std::byte, 32> local_{};
    VmfFsGlobalSnapshot state_;
    std::optional<VmfFsCommandError> error_;
    bool running_{};
};
// Modern owned1..32 slots, not native player capacity. Inputs are BORROWED,
// not copied: native globals can change inside a helper and be reloaded later.
// begin classifies ONCE. Unknown codes finish AL0 without any slot/services;
// recognized calls keep that choice even if command bytes later change.
// A complete command shorter than four bytes also finishes AL0: its NUL
// boundary cannot produce any of the fifteen keys, whatever lies beyond it.
// Slot remains occupied during run/release, pool closes before teardown.
// Discard released pointers; no ABA guarantee. Never destroy during a callback.
// Adapter storage allocates at construction only; helper allocation policies
// remain their responsibility. Serialize the whole pool and all dependencies.
class VmfFsGlobalHandler final : public VmfFsHandler {
  public:
    explicit VmfFsGlobalHandler(const VmfFsGlobalInputs& inputs,
                                std::size_t capacity = 8);
    ~VmfFsGlobalHandler() noexcept override;
    VmfFsGlobalHandler(const VmfFsGlobalHandler&) = delete;
    VmfFsGlobalHandler& operator=(const VmfFsGlobalHandler&) = delete;
    VmfFsGlobalHandler(VmfFsGlobalHandler&&) = delete;
    VmfFsGlobalHandler& operator=(VmfFsGlobalHandler&&) = delete;
    std::expected<VmfFsHandlerStart, VmfFsCommandError>
    begin(std::string_view command, const void* userdata) noexcept override;
    std::size_t capacity() const noexcept;
    std::size_t leased() const noexcept;

  private:
    struct Slot;
    const VmfFsGlobalInputs& inputs_;
    std::unique_ptr<Slot[]> slots_;
    std::size_t capacity_{};
    bool closing_{};
};
} // namespace tdur::ui
