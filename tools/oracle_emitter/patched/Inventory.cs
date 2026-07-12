// PATCHED LOCAL COPY of HEC.FDA.Model/structures/Inventory.cs @ f63682a86a30dc306a105689714a92bfd95956c5
// Numeric-only, mirroring the patched/Structure.cs precedent: the in-memory ctor and the numeric
// methods this Task 6 fixture exercises are kept VERBATIM (GetDamageCategories,
// GetInventoryTrimmedToImpactArea, GetInventoryAndWaterTrimmedToDamageCategory,
// GenerateRandomNumbers, SampleOccupancyTypes, GetGroundElevations, Validate). Dropped:
//  - Both shapefile/terrain constructors -- need StructureFactory/RASHelper/Projection/
//    Geospatial.GDALAssist/RasMapperLib, none reachable from this subset-compiled project (same
//    rationale as patched/Structure.cs's dropped ctors).
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
//
// Phase 4 Task 2 RE-ADDS ComputeDamages/AggregateResults -> List<ConsequenceResult> (severed above
// pending HEC.FDA.Model.metrics.ConsequenceResult, which Phase 4 Task 1 ported), VERBATIM except
// `Utility.Parallel.SmartFor(nStruc, ...)` -> a plain serial `for` over structures -- the emitter
// runs single-threaded, no reachable `Utility.Parallel`. The backing
// _invertedWSEL/_strucParallelCollection/_contentParallelCollection/_otherParallelCollection/
// _vehicleParallelCollection/_occTypeIndices fields and their null/size-mismatch reallocation
// guards are kept, matching the real C# exactly (see the corresponding C++ inventory.hpp comment
// for why the C++ port uses plain per-call locals instead -- a deliberate, documented deviation
// there, not here: the emitter mirrors the real C# 1:1 so it stays a faithful oracle).
using System.Collections.Generic;
using HEC.FDA.Model.metrics;
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

        // ported from: Inventory.cs's private scratch fields backing ComputeDamages/AggregateResults.
        // NOT SAFE TO CALL ComputeDamages IN PARALLEL (see ComputeDamages below) -- transcribed
        // verbatim, unlike the C++ port, which uses per-call locals instead (documented deviation,
        // see inventory.hpp).
        private float[,] _invertedWSEL; // [struc, pf]
        private double[,] _strucParallelCollection;
        private double[,] _contentParallelCollection;
        private double[,] _otherParallelCollection;
        private double[,] _vehicleParallelCollection;
        private int[] _occTypeIndices;

        // ported from: Inventory.cs public List<ConsequenceResult> ComputeDamages(List<float[]> wses,
        // int analysisYear, string damageCategory, List<DeterministicOccupancyType>
        // deterministicOccupancyType). VERBATIM except `Utility.Parallel.SmartFor(nStruc, ...)` ->
        // a serial `for` over structures (see file header) -- everything else, INCLUDING the
        // other/vehicle store swap 5 lines from the bottom, is transcribed exactly.
        public List<ConsequenceResult> ComputeDamages(List<float[]> wses, int analysisYear, string damageCategory, List<DeterministicOccupancyType> deterministicOccupancyType)
        {
            List<ConsequenceResult> aggregateConsequenceResults = new();
            //assume each structure has a corresponding index to the depth

            int nPf = wses.Count;
            int nStruc = wses[0].Length;
            // NOT SAFE TO CALL THIS METHOD IN PARALLEL
            if (_invertedWSEL == null || _invertedWSEL.GetLength(0) != nStruc || _invertedWSEL.GetLength(1) != nPf)
            {
                _invertedWSEL = new float[nStruc, nPf];
            }

            for (int i = 0; i < nPf; i++)
            {
                var pf = wses[i];
                for (int j = 0; j < nStruc; j++)
                {
                    _invertedWSEL[j, i] = pf[j];
                }
            }

            if (_strucParallelCollection == null || _strucParallelCollection.GetLength(0) != nPf || _strucParallelCollection.GetLength(1) != nStruc)
            {
                _strucParallelCollection = new double[nPf, nStruc];
            }
            if (_contentParallelCollection == null || _contentParallelCollection.GetLength(0) != nPf || _contentParallelCollection.GetLength(1) != nStruc)
            {
                _contentParallelCollection = new double[nPf, nStruc];
            }
            if (_otherParallelCollection == null || _otherParallelCollection.GetLength(0) != nPf || _otherParallelCollection.GetLength(1) != nStruc)
            {
                _otherParallelCollection = new double[nPf, nStruc];
            }
            if (_vehicleParallelCollection == null || _vehicleParallelCollection.GetLength(0) != nPf || _vehicleParallelCollection.GetLength(1) != nStruc)
            {
                _vehicleParallelCollection = new double[nPf, nStruc];
            }

            if (_occTypeIndices == null || _occTypeIndices.Length != nStruc)
            {
                _occTypeIndices = new int[nStruc];
                for (int i = 0; i < nStruc; i++)
                {
                    var struc = Structures[i];
                    int occc = struc.FindOccTypeIndex(deterministicOccupancyType);
                    _occTypeIndices[i] = occc;
                }
            }

            // DEVIATION from C#: Utility.Parallel.SmartFor(nStruc, (start, end) => {...}, 256)
            // replaced with a plain serial for-loop over the same range -- no reachable
            // Utility.Parallel in this subset-compiled emitter project. Body unchanged.
            for (int i = 0; i < nStruc; i++)
            {
                DeterministicOccupancyType dt = null;
                var dtIdx = _occTypeIndices[i];
                dt = deterministicOccupancyType[dtIdx];
                for (int j = 0; j < nPf; j++)
                {
                    float wse = _invertedWSEL[i, j];
                    if (wse != -9999)
                    {
                        var (structDamage, contDamage, vehicleDamage, otherDamage) = Structures[i].ComputeDamage(wse, dt, PriceIndex, analysisYear);
                        _strucParallelCollection[j, i] = (structDamage);
                        _contentParallelCollection[j, i] = (contDamage);
                        _otherParallelCollection[j, i] = (vehicleDamage);
                        _vehicleParallelCollection[j, i] = (otherDamage);
                    }
                }
            }
            return AggregateResults(wses, damageCategory, aggregateConsequenceResults, _strucParallelCollection, _contentParallelCollection, _otherParallelCollection, _vehicleParallelCollection);
        }

        // ported from: Inventory.cs private List<ConsequenceResult> AggregateResults(...). VERBATIM.
        private List<ConsequenceResult> AggregateResults(List<float[]> wses, string damageCategory, List<ConsequenceResult> aggregateConsequenceResults, double[,] structureParallelCollection,
            double[,] contentParallelCollection, double[,] otherParallelCollection, double[,] vehicleParallelCollection)
        {
            for (int j = 0; j < wses.Count; j++)
            {
                ConsequenceResult aggregateConsequenceResult = new(damageCategory);
                for (int i = 0; i < Structures.Count; i++)
                {
                    aggregateConsequenceResult.IncrementConsequence(structureParallelCollection[j, i], contentParallelCollection[j, i], otherParallelCollection[j, i], vehicleParallelCollection[j, i]);
                }
                aggregateConsequenceResults.Add(aggregateConsequenceResult);
            }
            return aggregateConsequenceResults;
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
