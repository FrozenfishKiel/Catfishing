# Integrated Stage B Report

## Scope

- Added the project ASC, grouped AbilitySet grants/removal, AbilityInputConfig, native Fishing input/ability/state/outcome/cue/data tags, five player-input Fishing abilities, and the outcome base.
- Routed Enhanced Input edges through the controller into the current pawn ASC and processed them once from `PostProcessInput`.
- Added the Command Component input edge contract. Primary press/release use independent request IDs, one hold-cycle correlation ID, and monotonic input sequence. Commands whose C/D payload or authoritative context does not yet exist return `DependencyUnavailable` and never report a fake commit.
- Converted legacy Start/Assist/Scoop/Chum controller RPC implementations into compatibility forwards to the same Fishing Command Component.
- Added the native instant SetByCaller stamina GE, ASC initialization/reset/pending-compensation API, new-session readiness, per-step Session drain, HookedFight idempotent initialization, and terminal/EndPlay recovery.
- Replaced `ACatCharacter`'s base ASC with `UCatAbilitySystemComponent`; the authority grants the configured default AbilitySet once per Character and removes it only during final Character teardown.

## RED

The complete Stage B test file was added before production types. The first Editor build failed at `CatAbilityInputLayerTests.cpp` because `AbilitySystem/CatAbilitySet.h` did not exist. This was the expected feature-missing RED. The same parallel build also showed unrelated in-progress Environment failures; no Environment file was changed by Stage B.

## Coverage

- AbilitySet grouped grant, configured level/input tag, grouped removal, and handle invalidation.
- Press, release, held state, per-frame pressed/released clearing, and held persistence until release.
- Repeated possess/unpossess input cleanup using the real Character and Controller.
- Local `OnGiveAbility` reconstruction of the InputTag route from dynamic spec tags, plus strict five-action InputConfig readiness. Full authority/owning-client replication remains part of the final A–G network gate.
- Primary independent RequestId, shared hold-cycle correlation, monotonic sequence, next-cycle correlation renewal, and orphan-release cleanup after transient reset.
- SetByCaller FightStamina initialization/drain/reset plus pending recovery when ActorInfo returns.
- Remote-authority prediction lifecycle policy, paused edge preservation, same-frame tap activation, malformed AbilitySet rejection, pending-recovery activation blocking, baseline-current initialization, and pre-hook terminal recovery isolation.

## Static Review

- Production code has no remaining direct `SetNumericAttributeBase` write to `FightStamina`.
- Default AbilitySet removal occurs in Character `EndPlay`, not `UnPossessed`.
- Controller Move/Look/Jump/Sprint/MappingContext bodies were preserved; Stage B changes are narrow insertions around them.
- No Config, Content, Docs, Environment, or `CatGameplayTypesTests.cpp` file belongs to the Stage B whitelist.

## Verification

- RED Editor build: expected failure on missing Stage B header captured.
- Review RED: the first focused run exposed unregistered dynamic ASC fixtures; after fixture correction, a second run exposed the real cleared-ActorInfo readiness defect and an order-dependent stale warning expectation. The focused integration run then exposed its missing test AttributeSet registration. All three were corrected without widening production visibility.
- Final `CatfishingEditor Win64 Development` build: succeeded after all review fixes and test fixture corrections.
- Final `Catfishing Win64 Development` build: succeeded after all review fixes.
- `Catfishing.Unit.AbilitySystem`: 12/12 completed, 0 failed/not-run/in-process (one existing `GameplayCueNotifyPaths` configuration warning; Config intentionally untouched).
- `Catfishing.Integration.AbilitySystem`: 2/2 completed, 0 failed/not-run/in-process (same configuration warning).
- `Catfishing.Integration.Fishing.AbilityInput.PrimaryEdgesUseIndependentRequestsAndSharedCorrelation`: 1/1 succeeded, 0 warnings/failures.
- Shared post-Water framework regression gate: 3/3 succeeded with no warnings/failures; Water owner commit `9ec37bd` also reported final Editor/Game GREEN before the Stage B-only fixture follow-ups.

## Concerns

- Ability assets and InputConfig assets remain intentionally uncreated and unconfigured; runtime readiness remains fail-closed until the user wires them.
- Stage B establishes authoritative command edges, but Rod/session target payload resolution assigned to C/D remains an explicit `DependencyUnavailable` terminal result.
- The asset-free lifecycle test distinguishes authority mirror, listen-server-local, and owning-client behavior but is not a multi-process networking fixture; end-to-end replicated-spec and exactly-once command coverage is deferred to the final A–G network gate.
- UE 5.8 emits an existing warning because `GameplayCueNotifyPaths` is absent; the Stage B hard boundary forbids changing Config.
