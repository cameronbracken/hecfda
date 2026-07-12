// ported from: upstream/HEC-FDA/HEC.FDA.Model/hydraulics/HydraulicDataset.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_STAGE_DAMAGE_HYDRAULIC_PROFILES_HPP
#define HECFDA_MODEL_STAGE_DAMAGE_HYDRAULIC_PROFILES_HPP
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>
namespace hecfda {
namespace model {
namespace stage_damage {

// ported from: HydraulicDataset.cs's `public static float[] CorrectDryStructureWSEs(float[]
// wsesToCorrect, float[] groundElevs, float[] nextProfileWses = null)`. The two C# overloads
// (nextProfileWses null vs. non-null) are collapsed here into one loop over the whole profile
// list, mirroring the pure-numeric driving loop in GetHydraulicDatasetInFloatsWithProbabilities:
//   for (int i = 0; i < waterData.Count - 1; i++)
//       waterData[i] = CorrectDryStructureWSEs(waterData[i], groundElevs, waterData[i + 1]);
//   waterData[waterData.Count - 1] = CorrectDryStructureWSEs(waterData[waterData.Count - 1], groundElevs);
// (GetWSE/disk reads that build waterData in the first place are severed; this function only
// reproduces the correction that runs AFTER those reads.) A structure is "dry" in a profile when
// its WSE is strictly less than the structure's ground elevation. Profile i (i < Count-1) is
// corrected against profile i+1's UNCORRECTED values (matching the C# sequential assignment,
// where waterData[i+1] hasn't been reassigned yet when waterData[i] is corrected): dry in both i
// and i+1 -> `groundElev - offsetForDryStructures` (9); dry in i but wet in i+1 -> `groundElev -
// offsetForBarelyDryStructures` (2). The last profile has no "next" profile, so it uses the
// nextProfileWses==null branch: dry -> `groundElev - offsetForDryStructures` (9); wet -> left
// unchanged. Mutates `wses` in place, matching the C# in-place array mutation (CorrectDryStructureWSEs
// returns the same array reference it was given).
inline void correct_dry_structure_wses(std::vector<std::vector<float>>& wses,
                                        const std::vector<float>& ground_elevations) {
    constexpr float kOffsetForDryStructures = 9.0f;
    constexpr float kOffsetForBarelyDryStructures = 2.0f;
    if (wses.empty()) {
        return;
    }
    for (std::size_t p = 0; p + 1 < wses.size(); ++p) {
        std::vector<float>& current = wses[p];
        const std::vector<float>& next = wses[p + 1];
        for (std::size_t i = 0; i < current.size(); ++i) {
            bool dry_in_current_profile = current[i] < ground_elevations[i];
            if (dry_in_current_profile) {
                bool dry_in_next_profile = next[i] < ground_elevations[i];
                if (dry_in_next_profile) {
                    current[i] = ground_elevations[i] - kOffsetForDryStructures;
                } else {
                    current[i] = ground_elevations[i] - kOffsetForBarelyDryStructures;
                }
            }
        }
    }
    std::vector<float>& last = wses.back();
    for (std::size_t i = 0; i < last.size(); ++i) {
        bool dry_in_current_profile = last[i] < ground_elevations[i];
        if (dry_in_current_profile) {
            last[i] = ground_elevations[i] - kOffsetForDryStructures;
        }
    }
}

// ported from: HydraulicDataset.cs -- REPLACES HydraulicDataset as the port's passed-in-arrays
// input boundary. Upstream's `HydraulicDataset(List<IHydraulicProfile> profiles, ...)` ctor loads
// profiles from disk-backed IHydraulicProfile objects and orders them via `profiles.Sort();
// profiles.Reverse();`, producing DESCENDING exceedance-probability order: index 0 = most
// frequent/lowest-stage profile, last index = rarest/highest-stage profile. All disk I/O (GetWSE,
// PointMs, RAS, GetGraphicalStageFrequency, XML persistence) is severed per CLAUDE.md's
// hydraulics-as-arrays scope; the port's caller supplies already-extracted per-profile
// probabilities and per-structure WSEs, in that same descending-exceedance order.
//
// Ordering-enforcement decision: the ctor REQUIRES (throws std::invalid_argument if violated)
// that `probabilities` is already non-increasing, rather than silently sorting the arrays itself.
// Re-deriving descending order in the port would require permuting `wses_by_profile` in lockstep
// with `probabilities`, which is fine on its own, but there is no way for the port to recover
// which "profile identity" (e.g. a hydraulic-profile name/path) a given probability+WSE-row pair
// belongs to once they're bare arrays -- unlike upstream, which sorts IHydraulicProfile objects
// that carry that identity. Requiring the caller to pre-sort keeps the contract explicit and
// avoids a silent reordering that could desync `wses_by_profile` from whatever external metadata
// (e.g. profile names) the caller is tracking alongside these arrays.
class HydraulicProfiles {
   public:
    HydraulicProfiles(std::vector<double> probabilities, std::vector<std::vector<float>> wses_by_profile)
        : probabilities_(std::move(probabilities)), wses_by_profile_(std::move(wses_by_profile)) {
        if (probabilities_.size() != wses_by_profile_.size()) {
            throw std::invalid_argument(
                "HydraulicProfiles: probabilities and wses_by_profile must have the same length");
        }
        for (std::size_t i = 1; i < probabilities_.size(); ++i) {
            if (probabilities_[i] > probabilities_[i - 1]) {
                throw std::invalid_argument(
                    "HydraulicProfiles: probabilities must be in descending exceedance order "
                    "(index 0 = most frequent/lowest-stage profile)");
            }
        }
    }

    std::vector<double> profile_probabilities() const { return probabilities_; }

    // ported from: the corrected result of GetHydraulicDatasetInFloatsWithProbabilities's
    // waterData after its CorrectDryStructureWSEs loop. Applies correct_dry_structure_wses to a
    // COPY of the stored WSEs and returns that copy -- the member `wses_by_profile_` is never
    // mutated, unlike upstream's in-place array mutation.
    std::vector<std::vector<float>> get_corrected_wses(const std::vector<float>& ground_elevations) const {
        std::vector<std::vector<float>> corrected = wses_by_profile_;
        correct_dry_structure_wses(corrected, ground_elevations);
        return corrected;
    }

   private:
    std::vector<double> probabilities_;
    std::vector<std::vector<float>> wses_by_profile_;
};

}  // namespace stage_damage
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_STAGE_DAMAGE_HYDRAULIC_PROFILES_HPP
