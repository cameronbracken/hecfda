// PATCHED LOCAL COPY of HEC.FDA.Model/structures/Structure.cs @ f63682a86a30dc306a105689714a92bfd95956c5
// Numeric-only: both PointM/Geospatial.Vectors.Point constructors (and the `PointM Point`
// property) are dropped in favor of ONE numeric constructor with no point parameter -- avoids the
// RasMapperLib/Geospatial.Vectors dependency, which (like the H5Assist.Chunking-dependent
// Serialization class CurveMetaData.cs/GraphicalUncertainPairedData.cs were patched to avoid) is
// otherwise unreachable from this subset-compiled project. The `ConsequenceResult ComputeDamage(
// float, List<DeterministicOccupancyType>, double, int)` overload (needs HEC.FDA.Model.metrics) and
// `ProduceDetailsHeader`/`ProduceDetails`/`CalculateDepthZeroDamage`/`FindHighestDepthZeroPercentDamage`
// (CSV output, needs Point.X/Y + f_inverse) are dropped too -- neither is exercised by the numeric
// depth-damage compute this gate validates. Everything else -- AddRules, the tuple
// ComputeDamage(float, DeterministicOccupancyType, double, int), FindOccType/FindOccTypeIndex,
// ResetIndexTracking, and the four mutable LastWSPStageDamageSegmentTopIndex* fields -- is kept
// VERBATIM.
using HEC.MVVMFramework.Base.Implementations;
using HEC.MVVMFramework.Base.Enumerations;
using System.Collections.Generic;
using System;
using Math = System.Math;

namespace HEC.FDA.Model.structures
{
    public class Structure : Validation
    {
        #region Properties
        public string Fid { get; }
        public double FirstFloorElevation { get; }
        public double GroundElevation { get; }
        public double InventoriedStructureValue { get; }
        public double InventoriedContentValue { get; set; }
        public double InventoriedVehicleValue { get; set; }
        public double InventoriedOtherValue { get; set; }
        public string DamageCatagory { get; }
        public string OccTypeName { get; }
        public int ImpactAreaID { get; }
        public string Cbfips { get; set; }
        internal double BeginningDamageDepth { get; }
        internal double FoundationHeight { get; }
        internal int YearInService { get; }
        internal int NumberOfStructures { get; }
        internal string Notes { get; }
        internal string Description { get; }
        private int LastWSPStageDamageSegmentTopIndexStructure = 1;
        private int LastWSPStageDamageSegmentTopIndexContent = 1;
        private int LastWSPStageDamageSegmentTopIndexVehicle = 1;
        private int LastWSPStageDamageSegmentTopIndexOther = 1;
        #endregion

        #region Constructors
        /// <summary>
        /// PATCHED: numeric-only constructor (the spatial point parameter is severed; see file header).
        /// </summary>
        public Structure(string fid, double firstFloorElevation, double val_struct, string st_damcat, string occtype, int impactAreaID, double val_cont = 0, double val_vehic = 0, double val_other = 0,
            string cbfips = "unassigned", double beginDamage = utilities.IntegerGlobalConstants.DEFAULT_MISSING_VALUE, double groundElevation = utilities.IntegerGlobalConstants.DEFAULT_MISSING_VALUE,
            double foundationHeight = utilities.IntegerGlobalConstants.DEFAULT_MISSING_VALUE, int year = utilities.IntegerGlobalConstants.DEFAULT_MISSING_VALUE, int numStructures = 1, string notes = "", string description = "")
        {
            Fid = fid;
            InventoriedStructureValue = val_struct;
            InventoriedContentValue = val_cont;
            InventoriedVehicleValue = val_vehic;
            InventoriedOtherValue = val_other;
            DamageCatagory = st_damcat;
            OccTypeName = occtype;
            ImpactAreaID = impactAreaID;
            Cbfips = cbfips;
            FirstFloorElevation = firstFloorElevation;
            GroundElevation = groundElevation;
            FoundationHeight = foundationHeight;
            YearInService = year;
            NumberOfStructures = numStructures;
            BeginningDamageDepth = beginDamage;
            AddRules();
            Notes = notes;
            Description = description;
        }
        #endregion
        #region Methods
        private void AddRules()
        {
            AddSinglePropertyRule(nameof(FirstFloorElevation), new Rule(() => FirstFloorElevation > -300, $"First floor elevation must be greater than -300, but is {FirstFloorElevation} for Structure {Fid}", ErrorLevel.Fatal));
            AddSinglePropertyRule(nameof(InventoriedStructureValue), new Rule(() => InventoriedStructureValue >= 0, $"The inventoried structure value must be greater than or equal to 0, but is {InventoriedStructureValue} for Structure {Fid}", ErrorLevel.Fatal));
            AddSinglePropertyRule(nameof(InventoriedContentValue), new Rule(() => InventoriedContentValue >= 0, $"The inventoried content value must be greater than or equal to 0, but is {InventoriedContentValue} for Structure {Fid}", ErrorLevel.Fatal));
            AddSinglePropertyRule(nameof(InventoriedOtherValue), new Rule(() => InventoriedOtherValue >= 0, $"The inventoried other value must be greater than or equal to 0, but is {InventoriedOtherValue} for Structure {Fid}", ErrorLevel.Fatal));
            AddSinglePropertyRule(nameof(InventoriedVehicleValue), new Rule(() => InventoriedVehicleValue >= 0, $"The inventoried vehicle value must be greater than or equal to 0, but is {InventoriedVehicleValue} for Structure {Fid}", ErrorLevel.Fatal));
            AddSinglePropertyRule(nameof(DamageCatagory), new Rule(() => DamageCatagory != null && DamageCatagory != "", $"Damage category should not be null but appears null for Structure {Fid}", ErrorLevel.Fatal));
            AddSinglePropertyRule(nameof(OccTypeName), new Rule(() => OccTypeName != null && OccTypeName != "", $"The occupancy type should not be null but appears null for Structure {Fid}", ErrorLevel.Fatal));
        }

        /// <summary>
        /// Begins the final (innermost) calculation of the Scenario Stage Damage Compute.
        /// Scenario SD
        /// Impact Area SD
        /// Damage Catagory
        /// Compute Chunk
        /// Iteration
        /// Structure
        /// W.S.Profile <--
        /// </summary>
        public (double, double, double, double) ComputeDamage(float waterSurfaceElevation, DeterministicOccupancyType deterministicOccupancyType, double priceIndex = 1, int analysisYear = 9999)
        {
            //TODO: We need a way to make sure that the sampled first floor elevation is reasonable
            //that is hard when we throw away the foundation height
            //det depth begin damage equal to foundation height
            double sampledFFE;
            if (deterministicOccupancyType.IsFirstFloorElevationLogNormal)
            {
                sampledFFE = FirstFloorElevation * (deterministicOccupancyType.FirstFloorElevationOffset);
            }
            else
            {
                sampledFFE = FirstFloorElevation + deterministicOccupancyType.FirstFloorElevationOffset;
            }
            double depthabovefoundHeight = waterSurfaceElevation - sampledFFE;
            double sampledStructureValue;
            double structDamage = 0;
            double contDamage = 0;
            double vehicleDamage = 0;
            double otherDamage = 0;

            //house should have been constructed before or equal to the analysis year to be damaged
            if (YearInService <= analysisYear)
            {
                //Beginning damage depth is relative to the first floor elevation and so a beginning damage depth of -1 means that damage begins 1 foot below the first floor elevation
                //if not defined by the user, the beginning damage depth is equal to the negative of foundation height
                if (BeginningDamageDepth <= depthabovefoundHeight)
                {
                    //Structure
                    double structDamagepercent = deterministicOccupancyType.StructPercentDamagePairedData.f(depthabovefoundHeight, ref LastWSPStageDamageSegmentTopIndexStructure);
                    if (structDamagepercent > 100)
                    {
                        structDamagepercent = 100;
                    }
                    if (structDamagepercent < 0)
                    {
                        structDamagepercent = 0;
                    }
                    if (deterministicOccupancyType.IsStructureValueLogNormal)
                    {
                        sampledStructureValue = Math.Pow((deterministicOccupancyType.StructureValueOffset), Math.Log(InventoriedStructureValue)) * (InventoriedStructureValue);
                        structDamage = (structDamagepercent / 100) * priceIndex * NumberOfStructures * sampledStructureValue;
                    }
                    else
                    {
                        sampledStructureValue = InventoriedStructureValue * (deterministicOccupancyType.StructureValueOffset);
                        structDamage = (structDamagepercent / 100) * priceIndex * NumberOfStructures * sampledStructureValue;
                    }

                    //Content
                    if (deterministicOccupancyType.ComputeContentDamage)
                    {
                        double contentDamagePercent = deterministicOccupancyType.ContentPercentDamagePairedData.f(depthabovefoundHeight, ref LastWSPStageDamageSegmentTopIndexContent);
                        if (contentDamagePercent > 100)
                        {
                            contentDamagePercent = 100;
                        }
                        if (contentDamagePercent < 0)
                        {
                            contentDamagePercent = 0;
                        }
                        if (deterministicOccupancyType.UseCSVR)
                        {
                            contDamage = (contentDamagePercent / 100) * priceIndex * NumberOfStructures * (deterministicOccupancyType.ContentToStructureValueRatio / 100) * sampledStructureValue;
                        }
                        else
                        {
                            if (deterministicOccupancyType.IsContentValueLogNormal)
                            {
                                double sampledContentValue = Math.Pow(deterministicOccupancyType.ContentValueOffset, Math.Log(InventoriedContentValue)) * (InventoriedContentValue);
                                contDamage = (contentDamagePercent / 100) * priceIndex * NumberOfStructures * (sampledContentValue);
                            }
                            else
                            {
                                double sampledContentValue = (InventoriedContentValue * (deterministicOccupancyType.ContentValueOffset));
                                contDamage = (contentDamagePercent / 100) * priceIndex * NumberOfStructures * sampledContentValue;

                            }
                        }
                    }

                    //Vehicle
                    if (deterministicOccupancyType.ComputeVehicleDamage)
                    {
                        double vehicleDamagePercent = deterministicOccupancyType.VehiclePercentDamagePairedData.f(depthabovefoundHeight, ref LastWSPStageDamageSegmentTopIndexVehicle);
                        if (vehicleDamagePercent > 100)
                        {
                            vehicleDamagePercent = 100;
                        }
                        if (vehicleDamagePercent < 0)
                        {
                            vehicleDamagePercent = 0;
                        }
                        if (deterministicOccupancyType.IsVehicleValueLogNormal)
                        {
                            double sampledVehicleValue = Math.Pow(deterministicOccupancyType.VehicleValueOffset, Math.Log(InventoriedVehicleValue)) * (InventoriedVehicleValue);
                            vehicleDamage = (vehicleDamagePercent / 100) * priceIndex * NumberOfStructures * sampledVehicleValue;
                        }
                        else
                        {
                            double sampledVehicleValue = InventoriedVehicleValue * (deterministicOccupancyType.VehicleValueOffset);
                            vehicleDamage = (vehicleDamagePercent / 100) * priceIndex * NumberOfStructures * sampledVehicleValue;
                        }
                    }

                    //Other
                    if (deterministicOccupancyType.ComputeOtherDamage)
                    {
                        double otherDamagePercent = deterministicOccupancyType.OtherPercentDamagePairedData.f(depthabovefoundHeight, ref LastWSPStageDamageSegmentTopIndexOther);
                        if (otherDamagePercent > 100)
                        {
                            otherDamagePercent = 100;
                        }
                        if (otherDamagePercent < 0)
                        {
                            otherDamagePercent = 0;
                        }
                        if (deterministicOccupancyType.UseOSVR)
                        {
                            otherDamage = (otherDamagePercent / 100) * priceIndex * NumberOfStructures * (sampledStructureValue) * (deterministicOccupancyType.OtherToStructureValueRatio / 100);
                        }
                        else
                        {
                            if (deterministicOccupancyType.IsOtherValueLogNormal)
                            {
                                double sampledOtherValue = Math.Pow(deterministicOccupancyType.OtherValueOffset, Math.Log(InventoriedOtherValue)) * (InventoriedOtherValue);
                                otherDamage = (otherDamagePercent / 100) * priceIndex * NumberOfStructures * sampledOtherValue;
                            }
                            else
                            {
                                double sampledOtherValue = (InventoriedOtherValue) * (deterministicOccupancyType.OtherValueOffset);
                                otherDamage = (otherDamagePercent / 100) * priceIndex * NumberOfStructures * sampledOtherValue;
                            }
                        }
                    }
                }
            }
            return (structDamage, contDamage, vehicleDamage, otherDamage);
        }

        public DeterministicOccupancyType FindOccType(List<DeterministicOccupancyType> deterministicOccupancyTypeList)
        {
            int index = FindOccTypeIndex(deterministicOccupancyTypeList);
            return deterministicOccupancyTypeList[index];
        }
        public int FindOccTypeIndex(List<DeterministicOccupancyType> deterministicOccupancyTypeList)
        {
            //see if we can match an occupancy type from the provided list to the structure occ type name
            for (int i = 0; i < deterministicOccupancyTypeList.Count; i++)
            {
                if (deterministicOccupancyTypeList[i].OccupancyTypeName == OccTypeName)
                {
                    return i;
                }
            }
            // couldn't find occupancy type
            throw new Exception($"Failed to find OccupancyType Named: {OccTypeName} referenced by structure ID: {Fid}");
        }

        internal void ResetIndexTracking()
        {
            LastWSPStageDamageSegmentTopIndexStructure = 1;
            LastWSPStageDamageSegmentTopIndexContent = 1;
            LastWSPStageDamageSegmentTopIndexOther = 1;
            LastWSPStageDamageSegmentTopIndexVehicle = 1;
        }
        #endregion
    }
}
