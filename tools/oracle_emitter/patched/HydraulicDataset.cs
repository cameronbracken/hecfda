// PATCHED LOCAL COPY of HEC.FDA.Model/hydraulics/HydraulicDataset.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
// Numeric-only, smallest-surface patch (Phase 4 Task 5): the real HydraulicDataset.cs pulls in
// Geospatial.GDALAssist, HEC.FDA.Model.Spatial, hydraulics.enums/Interfaces, HEC.FDA.Model.
// paireddata/structures, RasMapperLib, and System.Xml.Linq for its disk-reading constructor,
// XML persistence, and GetGraphicalStageFrequency/GetHydraulicDatasetInFloatsWithProbabilities
// methods (GetWSE, PointMs, Inventory, IHydraulicProfile), none reachable from this
// subset-compiled project (same rationale as patched/Structure.cs's dropped ctors). This patch
// keeps ONLY the two pure-numeric CorrectDryStructureWSEs overloads (VERBATIM body, unchanged)
// plus a thin CorrectAllProfiles wrapper that reproduces the per-profile driving loop from
// GetHydraulicDatasetInFloatsWithProbabilities (also pure numeric, no GetWSE/disk access):
//   for (int i = 0; i < waterData.Count - 1; i++)
//       waterData[i] = CorrectDryStructureWSEs(waterData[i], groundElevs, waterData[i + 1]);
//   waterData[waterData.Count - 1] = CorrectDryStructureWSEs(waterData[waterData.Count - 1], groundElevs);
// so the oracle emitter can drive the correction over a full profile list the same way the real
// method does. Everything else -- fields, both ctors, LoadFromXML, GetGraphicalStageFrequency,
// the GetWSE-driven half of GetHydraulicDatasetInFloatsWithProbabilities, ToXML, and
// AllStagesIncreaseWithDecreasingProbability -- is dropped.
using System.Collections.Generic;

namespace HEC.FDA.Model.hydraulics
{
    public class HydraulicDataset
    {
        // ported from: HydraulicDataset.cs CorrectDryStructureWSEs -- VERBATIM.
        public static float[] CorrectDryStructureWSEs(float[] wsesToCorrect, float[] groundElevs, float[] nextProfileWses = null)
        {
            float offsetForDryStructures = 9;
            float offsetForBarelyDryStructures = 2;
            if (nextProfileWses == null)
            {
                for (int i = 0; i < wsesToCorrect.Length; i++)
                {
                    bool dryInCurrentProfile = wsesToCorrect[i] < groundElevs[i];
                    //The case where the largest profile has dry structures
                    if (dryInCurrentProfile)
                    {
                        wsesToCorrect[i] = (groundElevs[i] - offsetForDryStructures);
                    }
                }
            }
            else
            {
                for (int i = 0; i < wsesToCorrect.Length; i++)
                {
                    bool dryInNextProfile = nextProfileWses[i] < groundElevs[i];
                    bool dryInCurrentProfile = wsesToCorrect[i] < groundElevs[i];
                    if (dryInCurrentProfile)
                    {
                        //The case where the next largest profile is also dry
                        if (dryInNextProfile)
                        {
                            wsesToCorrect[i] = (groundElevs[i] - offsetForDryStructures);
                        }
                        //The case where the next largest profile is not dry
                        else
                        {
                            wsesToCorrect[i] = (groundElevs[i] - offsetForBarelyDryStructures);
                        }
                    }
                }
            }
            return wsesToCorrect;
        }

        // ported from: HydraulicDataset.cs GetHydraulicDatasetInFloatsWithProbabilities's
        // post-read correction loop (the two lines after the GetWSE foreach) -- pure numeric, no
        // disk access. Exposed as a static wrapper here since the oracle emitter has no
        // Inventory/GetWSE-backed waterData to build a real HydraulicDataset instance from.
        public static List<float[]> CorrectAllProfiles(List<float[]> waterData, float[] groundElevs)
        {
            for (int i = 0; i < waterData.Count - 1; i++)
            {
                waterData[i] = CorrectDryStructureWSEs(waterData[i], groundElevs, waterData[i + 1]);
            }
            waterData[waterData.Count - 1] = CorrectDryStructureWSEs(waterData[waterData.Count - 1], groundElevs);
            return waterData;
        }
    }
}
