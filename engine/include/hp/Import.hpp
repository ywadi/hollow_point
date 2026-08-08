// What an import *produces* (T0169, D39): the engine-native assets a
// container source implies, written as visible project files the author owns.
//
// `Assets.hpp` answers identity and loading; this header answers production.
// Importing `models/car.glb` yields, beside it, one `.hpmat` per material and
// one image file per embedded texture, under `models/car.glb.assets/`, each
// with an ordinary `.hpmeta` sidecar — so a scene references an imported
// material exactly like an authored one, and a game author changes its
// roughness without ever touching the `.glb`.
//
// Identity is D39's registry: the source's own metafile records
// `{kind, key, guid}` per generated sub-asset, keyed by *name* and never by
// array index, minted once and never renumbered. Re-import is extract-once —
// files that exist are never overwritten, so the author's edits win
// structurally rather than by merge.
//
// Everything here is **deviceless** — VFS in, VFS out, no GPU anywhere — so
// the whole production path is testable in the fast suite. `importAsset`
// calls it after a successful mesh load; an editor or a test may call it
// directly.

#pragma once

#include <hp/Api.hpp>
#include <hp/Guid.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace hp {

/// One artefact of a production pass.
struct ImportedSubAsset {
    /// The registry namespace: `material` or `image` today.
    std::string kind;

    /// The stable key within the kind — the sanitised sub-asset name, with
    /// D39's duplicate (`_<n>`) and unnamed (`<kind>_<index>`) rules applied.
    std::string key;

    /// The registry identity, shared with the generated file's own sidecar.
    Guid guid;

    /// Virtual path of the generated file.
    std::string path;

    /// Whether this pass wrote the file. False means it already existed and
    /// was left alone — extract-once, the author owns it (D39).
    bool created = false;
};

/// Everything one production pass did.
struct ImportProducts {
    /// False when the source could not be read or parsed at all. An empty
    /// `assets` with `ok == true` is a model that has nothing to produce.
    bool ok = false;

    /// The sub-assets, produced or reconnected, in registry order.
    std::vector<ImportedSubAsset> assets;
};

/// Produces the engine assets a glTF source implies. See the header comment
/// for the contract; D39 for the decisions it spends.
///
/// Best-effort throughout: a material that cannot be mapped is logged and
/// skipped, never silently dropped (T0169.8), and a read-only project
/// directory produces log lines rather than failures — the same stance
/// `importAsset` takes for the identity metafile.
///
/// @param modelPath virtual path to a `.gltf`/`.glb` in the mount tree.
/// @returns what was produced. `ok == false` only when the source itself
///          could not be read as a glTF container.
[[nodiscard]] HP_API ImportProducts produceEngineAssets(std::string_view modelPath);

} // namespace hp
