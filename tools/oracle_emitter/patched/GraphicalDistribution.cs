// PATCHED LOCAL COPY of HEC.FDA.Model/extensions/GraphicalDistribution.cs @ f63682a86a30dc306a105689714a92bfd95956c5
// Ctor + AddRules/IsArrayValid retained verbatim. WriteToXML/ReadFromXML are replaced with trivial
// stubs (the oracle gate never serializes) so this file doesn't need
// HEC.FDA.Model.utilities.Serialization (which pulls in the H5Assist.Chunking NuGet package, not
// available to this project) -- same pattern already used by patched/CurveMetaData.cs.
using System;
using System.Collections.Generic;
using System.Linq;
using Statistics.Distributions;
using Statistics;
using HEC.MVVMFramework.Base.Enumerations;
using HEC.MVVMFramework.Base.Implementations;
using System.Xml.Linq;
using HEC.FDA.Model.utilities;
using HEC.MVVMFramework.Base.Events;
using HEC.MVVMFramework.Model.Messaging;
using HEC.FDA.Model.paireddata;

namespace HEC.FDA.Model.extensions
{
    public class GraphicalDistribution : ValidationErrorLogger
    {
        #region Properties
        public double[] StageOrLoggedFlowValues { get; internal set; }
        public bool UsingStagesNotFlows { get; }
        public int EquivalentRecordLength { get; }
        public double[] ExceedanceProbabilities { get; internal set; }
        public ContinuousDistribution[] StageOrLogFlowDistributions { get; internal set; }

        #endregion

        #region Constructor
        public GraphicalDistribution()
        {
            EquivalentRecordLength = 10;
            UsingStagesNotFlows = true;
            ExceedanceProbabilities = new double[] { 0 };
            StageOrLogFlowDistributions = new Normal[] { new Normal(0, 1) };
            StageOrLoggedFlowValues = new double[] { 0 };

        }
        /// <summary>
        /// Graphical Distribution implements Beth Faber's Less Simple Method for calculating uncertainty about the distribution
        /// See the HEC-FDA Technical Reference for more information on the Less Simple Method
        /// This constructor assumes that exceedance probabilities and flow or stage values have a strictly monotonic relationships.
        /// </summary>
        /// <param name="userInputExceedanceProbabilities"></param> User-provided exceedance probabilities. There should be at least 8.
        /// <param name="stageOrUnloggedFlowValues"></param> User-provided flow or stage values. A value should correspond to a probability.
        /// <param name="equivalentRecordLength"></param> The equivalent record length in years.

        public GraphicalDistribution(double[] userInputExceedanceProbabilities, double[] stageOrUnloggedFlowValues, int equivalentRecordLength, bool usingStagesNotFlows = true)
        {
            EquivalentRecordLength = equivalentRecordLength;
            UsingStagesNotFlows = usingStagesNotFlows;
            (double[] exceedenceProbs, ContinuousDistribution[] stageOrLogFlowDists) = GraphicalFrequencyUncertaintyCalculators.LessSimpleMethod(userInputExceedanceProbabilities, stageOrUnloggedFlowValues, UsingStagesNotFlows, EquivalentRecordLength);

            ExceedanceProbabilities = exceedenceProbs;
            StageOrLoggedFlowValues = stageOrLogFlowDists.Select((x) => x.InverseCDF(.5)).ToArray();
            AddRules(userInputExceedanceProbabilities);
            Validate();
            if (ErrorLevel >= ErrorLevel.Major)
            {
                string message = $"There are major or worse errors associated with a graphical frequency function, confidence intervals cannot be computed." + Environment.NewLine;
                ErrorMessage errorMessage = new(message, ErrorLevel.Major);
                ReportMessage(this, new MessageEventArgs(errorMessage));
            }
            else
            {
                //then we compute uncertainty
                StageOrLogFlowDistributions = stageOrLogFlowDists;
            }
        }
        private GraphicalDistribution(double[] stageOrLoggedFlowValues, bool usingStagesNotFlows, int equivalentRecordLength, double[] exceedanceProbabilities, ContinuousDistribution[] stageOrLogFlowDistributions)
        {
            StageOrLoggedFlowValues = stageOrLoggedFlowValues;
            UsingStagesNotFlows = usingStagesNotFlows;
            EquivalentRecordLength = equivalentRecordLength;
            ExceedanceProbabilities = exceedanceProbabilities;
            StageOrLogFlowDistributions = stageOrLogFlowDistributions;
        }

        #endregion

        #region Methods
        private void AddRules(double[] exceedanceProbabilities)
        {
            AddSinglePropertyRule(nameof(EquivalentRecordLength), new Rule(() => EquivalentRecordLength > 0, "Equivalent record length must be greater than 0."));
            AddSinglePropertyRule(nameof(exceedanceProbabilities), new Rule(() => IsArrayValid(exceedanceProbabilities, (a, b) => (a >= b)), "Exceedance Probabilities must be strictly monotonically decreasing"));
            AddSinglePropertyRule(nameof(StageOrLoggedFlowValues), new Rule(() => IsArrayValid(StageOrLoggedFlowValues, (a, b) => (a <= b)), "Y must be strictly monotonically decreasing"));
        }
        private static bool IsArrayValid(double[] arrayOfData, Func<double, double, bool> comparison)
        {
            if (arrayOfData == null) return false;
            for (int i = 0; i < arrayOfData.Length - 1; i++)
            {
                if (comparison(arrayOfData[i], arrayOfData[i + 1]))
                {
                    return false;
                }
            }
            return true;
        }
        #endregion

        #region XML Methods (PATCHED: trivial stubs -- see file header)
        public static GraphicalDistribution ReadFromXML(XElement xElement) => new GraphicalDistribution();
        public XElement WriteToXML() => new XElement(GetType().Name);
        #endregion
    }
}
