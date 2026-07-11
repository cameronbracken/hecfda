// PATCHED LOCAL COPY of HEC.FDA.Model/structures/Inventory.cs @ f63682a86a30dc306a105689714a92bfd95956c5
// Numeric-only, mirroring the patched/Structure.cs precedent: the in-memory ctor and the numeric
// methods this Task 6 fixture exercises are kept VERBATIM (GetDamageCategories,
// GetInventoryTrimmedToImpactArea, GetInventoryAndWaterTrimmedToDamageCategory,
// GenerateRandomNumbers, SampleOccupancyTypes, GetGroundElevations, Validate). Dropped:
//  - Both shapefile/terrain constructors -- need StructureFactory/RASHelper/Projection/
//    Geospatial.GDALAssist/RasMapperLib, none reachable from this subset-compiled project (same
//    rationale as patched/Structure.cs's dropped ctors).
//  - ComputeDamages/AggregateResults -> List<ConsequenceResult> and the backing
//    _invertedWSEL/_strucParallelCollection/_contentParallelCollection/_otherParallelCollection/
//    _vehicleParallelCollection/_occTypeIndices fields -- needs HEC.FDA.Model.metrics
//    (ConsequenceResult, Phase 5) and Utility.Parallel.SmartFor.
//  - GetPointMs() -- needs PointMs/Structure.Point (spatial, and Structure's own Point property
//    is already dropped in patched/Structure.cs).
//  - StructureDetails/ProduceDetails-based output -- needs Structure.ProduceDetailsHeader
//    (already dropped in patched/Structure.cs).
// GetErrorsFromProperties (both overloads), ResetStructureWaterIndexTracking, and
// MessageReport/ReportMessage are kept VERBATIM (unlike patched/Structure.cs, which drops its
// CSV/messaging analogues) because `Inventory : PropertyValidationHelper,
// IDontImplementValidationButMyPropertiesDo` -- via IReportMessage -- requires them to be
// implemented for the class to compile at all, and every type they touch (Validation.
// GetErrorMessages, MessageHub, MessageReportedEventHandler/MessageEventArgs) is already reachable
// transitively through the existing HEC.FDA.Statistics project reference (same chain
// OccupancyType.cs -- itself unpatched -- already relies on).
using System.Collections.Generic;
using HEC.MVVMFramework.Base.Enumerations;
using HEC.MVVMFramework.Base.Events;
using HEC.MVVMFramework.Base.Implementations;
using HEC.MVVMFramework.Model.Messaging;
using Statistics;

namespace HEC.FDA.Model.structures
{
    public class Inventory : PropertyValidationHelper, IDontImplementValidationButMyPropertiesDo
    {
        #region Properties
        public List<Structure> Structures { get; } = new List<Structure>();
        public Dictionary<string, OccupancyType> OccTypes { get; set; }
        public double PriceIndex { get; set; }
        #endregion

        #region Constructors
        /// <summary>
        /// PATCHED: only the in-memory ctor is kept (the two shapefile/terrain ctors are severed;
        /// see file header).
        /// </summary>
        public Inventory(Dictionary<string, OccupancyType> occTypes, List<Structure> structures, double priceIndex = 1)
        {
            OccTypes = occTypes;
            Structures = structures;
            PriceIndex = priceIndex;
        }
        #endregion

        #region Methods
        public float[] GetGroundElevations()
        {
            float[] result = new float[Structures.Count];
            for (int i = 0; i < Structures.Count; i++)
            {
                result[i] = (float)Structures[i].GroundElevation;
            }
            return result;
        }
        internal List<string> GetDamageCategories()
        {
            List<string> uniqueDamageCategories = new();
            foreach (Structure structure in Structures)
            {
                if (!uniqueDamageCategories.Contains(structure.DamageCatagory))
                {
                    uniqueDamageCategories.Add(structure.DamageCatagory);
                }
            }
            return uniqueDamageCategories;
        }

        public Inventory GetInventoryTrimmedToImpactArea(int impactAreaFID)
        {
            List<Structure> filteredStructureList = new();

            foreach (Structure structure in Structures)
            {
                if (structure.ImpactAreaID == impactAreaFID)
                {
                    filteredStructureList.Add(structure);
                }
            }
            return new Inventory(OccTypes, filteredStructureList, PriceIndex);
        }

        public (Inventory, List<float[]>) GetInventoryAndWaterTrimmedToDamageCategory(string damageCategory, List<float[]> wsesAtEachStructureByProfile)
        {
            List<Structure> filteredStructureList = new();
            List<List<float>> listedWSEsFiltered = new();
            for (int j = 0; j < wsesAtEachStructureByProfile.Count; j++)
            {
                List<float> listOfStages = new();
                listedWSEsFiltered.Add(listOfStages);
            }
            for (int i = 0; i < Structures.Count; i++)
            {
                if (Structures[i].DamageCatagory == damageCategory)
                {
                    filteredStructureList.Add(Structures[i]);
                    for (int j = 0; j < wsesAtEachStructureByProfile.Count; j++)
                    {
                        listedWSEsFiltered[j].Add(wsesAtEachStructureByProfile[j][i]);
                    }
                }
            }
            List<float[]> arrayedWSEsFiltered = new();
            foreach (List<float> wses in listedWSEsFiltered)
            {
                arrayedWSEsFiltered.Add(wses.ToArray());
            }
            return (new Inventory(OccTypes, filteredStructureList, PriceIndex), arrayedWSEsFiltered);
        }

        public void GenerateRandomNumbers(ConvergenceCriteria convergenceCriteria)
        {
            int quantityOfRandomNumbers = System.Convert.ToInt32(convergenceCriteria.MaxIterations * 2);
            foreach (OccupancyType occupancyType in OccTypes.Values)
            {
                occupancyType.GenerateRandomNumbers(quantityOfRandomNumbers);
            }
        }

        public List<DeterministicOccupancyType> SampleOccupancyTypes(long iteration, bool computeIsDeterministic)
        {
            List<DeterministicOccupancyType> deterministicOccupancyTypes = new();
            foreach (OccupancyType occupancyType in OccTypes.Values)
            {
                DeterministicOccupancyType deterministicOccupancyType = occupancyType.Sample(iteration, computeIsDeterministic);
                deterministicOccupancyTypes.Add(deterministicOccupancyType);
            }

            return deterministicOccupancyTypes;
        }
        #endregion

        public string GetErrorsFromProperties()
        {
            string errors = "";
            foreach (OccupancyType occupancyType in OccTypes.Values)
            {
                errors += occupancyType.GetErrorsFromProperties();
            }
            foreach (Structure structure in Structures)
            {
                errors += structure.GetErrorMessages(ErrorLevel.Unassigned, "Structure" + structure.Fid);
            }
            return errors;
        }
        public string GetErrorsFromProperties(int impactAreaID)
        {
            string errors = "";
            foreach (OccupancyType occupancyType in OccTypes.Values)
            {
                errors += occupancyType.GetErrorsFromProperties();
            }
            foreach (Structure structure in Structures)
            {
                errors += structure.GetErrorMessages(ErrorLevel.Unassigned, "Structure" + structure.Fid);
            }
            if (Structures.Count == 0)
            {
                errors += $"There are no structures found in the inventory that lie within impact area {impactAreaID}" + System.Environment.NewLine;
            }
            return errors;
        }
        internal void ResetStructureWaterIndexTracking()
        {
            foreach (Structure structure in Structures)
            {
                structure.ResetIndexTracking();
            }
        }

        public event MessageReportedEventHandler MessageReport;

        public void ReportMessage(object sender, MessageEventArgs e)
        {
            MessageHub.Register(this);
            MessageReport?.Invoke(sender, e);
            MessageHub.Unregister(this);
        }

        public void Validate()
        {
            HasErrors = false;
            ErrorLevel = ErrorLevel.Unassigned;
            foreach (OccupancyType occupancyType in OccTypes.Values)
            {
                occupancyType.Validate();
                if (occupancyType.HasErrors)
                {
                    if (ErrorLevel < occupancyType.ErrorLevel)
                    {
                        ErrorLevel = occupancyType.ErrorLevel;
                    }
                }
            }
            foreach (Structure structure in Structures)
            {
                structure.Validate();
                if (structure.HasErrors)
                {
                    if (structure.ErrorLevel > ErrorLevel) { ErrorLevel = structure.ErrorLevel; }
                    HasErrors = true;
                }
            }
            if (Structures.Count == 0)
            {
                HasErrors = true;
                ErrorLevel = ErrorLevel.Minor;
            }
        }
    }
}
