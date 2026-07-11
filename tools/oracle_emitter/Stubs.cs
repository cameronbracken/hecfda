// Empty namespace stub so the dead `using Amazon.Runtime;` in the upstream
// UncertainPairedData.cs resolves without referencing the AWS SDK (no Amazon type is used).
namespace Amazon.Runtime { }

// Empty namespace stub (Task P2T4a) so the dead `using HEC.FDA.Model.extensions;` in the upstream
// GraphicalFrequencyUncertaintyCalculators.cs resolves without pulling in
// ContinuousDistributionExtensions.cs's own transitive deps (IProvideRandomNumbers,
// Statistics.Histograms.DynamicHistogram) -- no type from that namespace is actually used by
// GraphicalFrequencyUncertaintyCalculators.cs.
namespace HEC.FDA.Model.extensions { }
