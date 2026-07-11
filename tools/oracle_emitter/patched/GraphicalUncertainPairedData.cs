// PATCHED LOCAL COPY of HEC.FDA.Model/paireddata/GraphicalUncertainPairedData.cs @ f63682a86a30dc306a105689714a92bfd95956c5
// Ctors/methods retained verbatim. WriteToXML/ReadFromXML are replaced with trivial stubs (the
// oracle gate never serializes) -- same pattern already used by patched/CurveMetaData.cs and
// patched/GraphicalDistribution.cs.
using System.Collections.Generic;
using HEC.FDA.Model.extensions;
using System.Xml.Linq;
using System;
using HEC.FDA.Model.interfaces;
using HEC.MVVMFramework.Model.Messaging;

namespace HEC.FDA.Model.paireddata
{
    public class GraphicalUncertainPairedData : ValidationErrorLogger, IPairedDataProducer, ICanBeNull, IMetaData
    {
        #region Fields
        private double[] _RandomNumbers;

        #endregion

        #region Properties
        public GraphicalDistribution GraphicalDistributionWithLessSimple { get; }
        public CurveMetaData CurveMetaData { get; private set; }
        /// <summary>
        /// Exceedance probabilities are the required and the input, combined.
        /// </summary>
        public double[] CombinedExceedanceProbabilities { get; private set; }
        public bool IsNull
        {
            get
            {
                return CurveMetaData.IsNull;
            }
        }
        #endregion

        #region Constructors
        public GraphicalUncertainPairedData()
        {
            CurveMetaData = new();
            GraphicalDistributionWithLessSimple = new();
            CombinedExceedanceProbabilities = new double[] { 0 };
        }
        public GraphicalUncertainPairedData(double[] exceedanceProbabilities, double[] flowOrStageValues, int equivalentRecordLength, CurveMetaData curveMetaData, bool usingStagesNotFlows)
        {
            GraphicalDistributionWithLessSimple = new GraphicalDistribution(exceedanceProbabilities, flowOrStageValues, equivalentRecordLength, usingStagesNotFlows);
            CombinedExceedanceProbabilities = GraphicalDistributionWithLessSimple.ExceedanceProbabilities;
            CurveMetaData = curveMetaData;
        }
        private GraphicalUncertainPairedData(double[] combinedExceedanceProbabilities, GraphicalDistribution graphicalDistributionWithLessSimple, CurveMetaData curveMetaData)
        {
            GraphicalDistributionWithLessSimple = graphicalDistributionWithLessSimple;
            CombinedExceedanceProbabilities = combinedExceedanceProbabilities;
            CurveMetaData = curveMetaData;
        }
        #endregion

        #region Methods
        public void GenerateRandomNumbers(int seed, int size)
        {
            Random random = new Random(seed);
            double[] randos = new double[size];
            for (int i = 0; i < size; i++)
            {
                randos[i] = random.NextDouble();
            }
            _RandomNumbers = randos;
        }
        private static double[] ExceedanceToNonExceedance(double[] exceedanceProbabilities)
        {
            double[] nonExceedanceProbabilities = new double[exceedanceProbabilities.Length];
            for (int i = 0; i < nonExceedanceProbabilities.Length; i++)
            {
                nonExceedanceProbabilities[i] = 1 - exceedanceProbabilities[i];
            }
            return nonExceedanceProbabilities;
        }
        /// <summary>
        /// Returns the relationship in Non-Exceedence Probabilities
        /// </summary>
        public PairedData SamplePairedData(double probability)
        {
            int numCoords = GraphicalDistributionWithLessSimple.StageOrLogFlowDistributions.Length;

            double[] y = new double[numCoords];
            for (int i = 0; i < numCoords; i++)
            {
                y[i] = GraphicalDistributionWithLessSimple.StageOrLogFlowDistributions[i].InverseCDF(probability);
            }

            PairedData pairedData = new(ExceedanceToNonExceedance(GraphicalDistributionWithLessSimple.ExceedanceProbabilities), y, CurveMetaData);
            if (probability < 0.5)
            {
                pairedData.ForceStrictMonotonicityBottomUp();
            }
            else
            {
                pairedData.ForceStrictMonotonicityTopDown();
            }
            return pairedData;
        }

        /// <summary>
        /// Returns the relationship in Non-Exceedence Probabilities
        /// </summary>
        public PairedData SamplePairedData(long iterationNumber, bool computeIsDeterministic = false)
        {
            double probability;
            double[] y = new double[GraphicalDistributionWithLessSimple.StageOrLogFlowDistributions.Length];
            if (computeIsDeterministic)
            {
                probability = 0.5;
            }
            else
            {
                if (_RandomNumbers.Length == 0)
                {
                    throw new Exception("Random numbers have not been created for UPD sampling");
                }
                if (iterationNumber < 0 || iterationNumber >= _RandomNumbers.Length)
                {
                    throw new Exception("Iteration number cannot be less than 0 or greater than the size of the random number array");

                }
                probability = _RandomNumbers[iterationNumber];
            }
            return SamplePairedData(probability);
        }

        public bool Equals(GraphicalUncertainPairedData incomingGraphicalUncertainPairedData)
        {
            bool nullMatches = CurveMetaData.IsNull.Equals(incomingGraphicalUncertainPairedData.CurveMetaData.IsNull);
            if (nullMatches && IsNull)
            {
                return true;
            }
            bool erlIsTheSame = GraphicalDistributionWithLessSimple.EquivalentRecordLength.Equals(incomingGraphicalUncertainPairedData.GraphicalDistributionWithLessSimple.EquivalentRecordLength);
            if (!erlIsTheSame)
            {
                return false;
            }
            for (int i = 0; i < GraphicalDistributionWithLessSimple.StageOrLogFlowDistributions.Length; i++)
            {
                bool distributionIsTheSame = GraphicalDistributionWithLessSimple.StageOrLogFlowDistributions[i].Equals(incomingGraphicalUncertainPairedData.GraphicalDistributionWithLessSimple.StageOrLogFlowDistributions[i]);
                if (!distributionIsTheSame)
                {
                    return false;
                }
            }
            for (int i = 0; i < CombinedExceedanceProbabilities.Length; i++)
            {
                bool probabilityIsTheSame = CombinedExceedanceProbabilities[i].Equals(incomingGraphicalUncertainPairedData.CombinedExceedanceProbabilities[i]);
                if (!probabilityIsTheSame)
                {
                    return false;
                }
            }
            return true;
        }

        #endregion
        #region XML Methods (PATCHED: trivial stubs -- see file header)
        public XElement WriteToXML() => new XElement("Graphical_Uncertain_Paired_Data");
        public static GraphicalUncertainPairedData ReadFromXML(XElement xElement) => new GraphicalUncertainPairedData();
        #endregion
    }
}
