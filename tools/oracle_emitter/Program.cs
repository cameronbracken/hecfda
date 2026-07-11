// Oracle emitter: reconstructs every Phase-0 fixture case with the REAL HEC-FDA C# types
// (HEC.FDA.Statistics + a minimal subset-compiled HEC.FDA.Model paired-data closure) and emits the
// computed value per assertion as JSON. tools/verify_oracles.py compares these against the fixtures.
// Dev-only; needs the pinned upstream submodule. Run with DOTNET_ROLL_FORWARD=Major (net9 -> net10 runtime).
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.Json;
using Statistics;
using Statistics.Distributions;
using Statistics.Histograms;
using HEC.FDA.Model.paireddata;
using HEC.FDA.Model.compute;
using HEC.FDA.Model.utilities;
using HEC.FDA.Model.extensions;
using HEC.FDA.Model.structures;
using HEC.FDA.Model.metrics;

namespace oracle_emitter {
  class Program {
    static double D(JsonElement e) => e.GetDouble();
    static double[] DA(JsonElement e) => e.EnumerateArray().Select(x => x.GetDouble()).ToArray();

    static object EvalRng(string method, int seed, JsonElement args) {
      var r = new Random(seed);
      long n = (long)args[0].GetDouble();
      if (method == "next_random_sequence") {
        var v = new double[n]; for (long i=0;i<n;i++) v[i]=r.NextDouble(); return v;
      }
      if (method == "sum_random_sequence") {
        double s=0; for (long i=0;i<n;i++) s+=r.NextDouble(); return s;
      }
      throw new Exception("unknown rng method: " + method);
    }
    // Generic factory-based distribution dispatch (Task A4), mirroring IDistributionFactory's
    // Factory* param order per type: Normal(mean, stDev, sampleSize), Uniform(min, max, sampleSize).
    // Returns Statistics.ContinuousDistribution (not the IDistribution interface) so Validate()/
    // HasErrors/ErrorLevel (inherited via ValidationErrorLogger : Validation) are directly
    // reachable without a cast, alongside the IDistribution PDF/CDF/InverseCDF/Fit surface.
    static ContinuousDistribution DistFactory(string type, double[] p) {
      return type switch {
        "Normal" => new Normal(p[0], p[1], (int)p[2]),
        "Uniform" => new Uniform(p[0], p[1], (int)p[2]),
        "Triangular" => new Triangular(p[0], p[1], p[2], (int)p[3]),
        "Deterministic" => new Deterministic(p[0]),
        "LogNormal" => new LogNormal(p[0], p[1], (int)p[2]),
        "TruncatedNormal" => new TruncatedNormal(p[0], p[1], p[2], p[3], (long)p[4]),
        "TruncatedLogNormal" => new TruncatedLogNormal(p[0], p[1], p[2], p[3], (int)p[4]),
        "LogPearsonIII" => new LogPearson3(p[0], p[1], p[2], (int)p[3]),
        "TruncatedLogPearson3" => new TruncatedLogPearson3(p[0], p[1], p[2], p[3], p[4], (int)p[5]),
        _ => throw new Exception("unknown distribution type: " + type) };
    }
    static object EvalDistribution(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      string type = c.GetProperty("type").GetString();
      var dist = DistFactory(type, DA(c.GetProperty("params")));
      if (method == "pdf") return dist.PDF(D(argsEl[0]));
      if (method == "cdf") return dist.CDF(D(argsEl[0]));
      if (method == "inverse_cdf") return dist.InverseCDF(D(argsEl[0]));
      if (method == "has_errors" || method == "error_level") {
        dist.Validate();
        if (method == "has_errors") return dist.HasErrors ? 1.0 : 0.0;
        return (double)(byte)dist.ErrorLevel;
      }
      // Task D1: UncertainToDeterministicDistributionConverter, called against the REAL C#
      // distribution/converter -- this is what gates the port's converter, including the
      // LogPearsonIII Math.Pow(Mean, 10) bug, against real upstream behavior.
      if (method == "convert_to_deterministic") {
        return UncertainToDeterministicDistributionConverter.ConvertDistributionToDeterministic(dist).Value;
      }
      if (method.StartsWith("fit_")) {
        double[] data = DA(argsEl);
        IDistribution fitted = dist.Fit(data);
        string param = method.Substring(4);
        if (param == "sample_size") return (double)fitted.SampleSize;
        // TruncatedNormal : Normal in C#, so `fitted is Normal n` below would also match a fitted
        // TruncatedNormal -- but n.Mean/n.StandardDeviation would then resolve to the BASE Normal's
        // `new`-hidden (permanently inert, locked at 0/1) properties, not TruncatedNormal's real
        // ones. Checked first so its mean/standard_deviation/min/max return before the generic
        // Normal branch's subclass-shadowing footgun can fire.
        if (fitted is TruncatedNormal tn) {
          if (param == "mean") return tn.Mean;
          if (param == "standard_deviation") return tn.StandardDeviation;
          if (param == "min") return tn.Min;
          if (param == "max") return tn.Max;
        }
        if (fitted is Normal n) {
          if (param == "mean") return n.Mean;
          if (param == "standard_deviation") return n.StandardDeviation;
        }
        if (fitted is Uniform u) {
          if (param == "min") return u.Min;
          if (param == "max") return u.Max;
        }
        if (fitted is Triangular t) {
          if (param == "min") return t.Min;
          if (param == "max") return t.Max;
          if (param == "most_likely") return t.MostLikely;
        }
        if (fitted is Deterministic d) {
          if (param == "value") return d.Value;
        }
        if (fitted is LogNormal ln) {
          if (param == "mean") return ln.Mean;
          if (param == "standard_deviation") return ln.StandardDeviation;
        }
        throw new Exception("unknown fit param: " + param + " for type " + type);
      }
      throw new Exception("unknown distribution method: " + method);
    }
    // ShiftedGamma (Task B9+B10) is a public plain helper class, not an IDistribution, so it is
    // constructed directly here rather than through DistFactory. `construct.params` is
    // [alpha, beta, shift]. This transitively validates the internal Gamma class it wraps
    // (including Gamma's Newton/bisection InverseCDF root-finder) against the real C#.
    static object EvalShiftedGamma(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      double[] p = DA(c.GetProperty("params"));
      var dist = new ShiftedGamma(p[0], p[1], p[2]);
      if (method == "pdf") return dist.PDF(D(argsEl[0]));
      if (method == "cdf") return dist.CDF(D(argsEl[0]));
      if (method == "inverse_cdf") return dist.InverseCDF(D(argsEl[0]));
      throw new Exception("unknown shifted_gamma method: " + method);
    }
    // PearsonIII (Task B6) is a public plain helper class, not an IDistribution, so it is
    // constructed directly here rather than through DistFactory. `construct.params` is
    // [mean, sd, skew, n]. This transitively validates the internal Normal (no-skew branch) and
    // ShiftedGamma/Gamma (skewed branches) it delegates to against the real C#.
    static object EvalPearson3(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      double[] p = DA(c.GetProperty("params"));
      var dist = new PearsonIII(p[0], p[1], p[2], (long)p[3]);
      if (method == "pdf") return dist.PDF(D(argsEl[0]));
      if (method == "cdf") return dist.CDF(D(argsEl[0]));
      if (method == "inverse_cdf") return dist.InverseCDF(D(argsEl[0]));
      throw new Exception("unknown pearson3 method: " + method);
    }
    // Empirical (Task B11) takes TWO parallel arrays (cumulative probabilities + values), so it
    // doesn't fit DistFactory's scalar-`params` shape -- constructed directly here, like
    // ShiftedGamma/PearsonIII. `construct` is `{"probabilities": [...], "values": [...]}`.
    static object EvalEmpirical(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      var dist = new Empirical(DA(c.GetProperty("probabilities")), DA(c.GetProperty("values")));
      if (method == "pdf") return dist.PDF(D(argsEl[0]));
      if (method == "cdf") return dist.CDF(D(argsEl[0]));
      if (method == "inverse_cdf") return dist.InverseCDF(D(argsEl[0]));
      if (method == "mean") return dist.Mean;
      if (method == "median") return dist.Median;
      if (method == "min") return dist.Min;
      if (method == "max") return dist.Max;
      if (method == "standard_deviation") return dist.StandardDeviation;
      // Task D1: UncertainToDeterministicDistributionConverter's Empirical case, against the real
      // C# Empirical + converter (SampleMean defaults to 0.0 for a freshly-constructed instance).
      if (method == "convert_to_deterministic") {
        return UncertainToDeterministicDistributionConverter.ConvertDistributionToDeterministic(dist).Value;
      }
      throw new Exception("unknown empirical method: " + method);
    }
    // ConvergenceCriteria (Task C1) is a plain Validation-derived parameter holder, not an
    // IDistribution, so it is constructed directly here, like ShiftedGamma/PearsonIII/Empirical.
    // `construct.params` is [minIterations, maxIterations, zAlpha, tolerance].
    static object EvalConvergenceCriteria(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      double[] p = DA(c.GetProperty("params"));
      var cc = new ConvergenceCriteria((int)p[0], (int)p[1], p[2], p[3]);
      if (method == "min_iterations") return (double)cc.MinIterations;
      if (method == "max_iterations") return (double)cc.MaxIterations;
      if (method == "z_alpha") return cc.ZAlpha;
      if (method == "tolerance") return cc.Tolerance;
      if (method == "iteration_count") return (double)cc.IterationCount;
      if (method == "has_errors" || method == "error_level") {
        cc.Validate();
        if (method == "has_errors") return cc.HasErrors ? 1.0 : 0.0;
        return (double)(byte)cc.ErrorLevel;
      }
      throw new Exception("unknown convergence_criteria method: " + method);
    }
    // DynamicHistogram (Task C2) is the Monte Carlo result accumulator, constructed directly here
    // like ShiftedGamma/PearsonIII/Empirical/ConvergenceCriteria. `construct` is
    // {"bin_width": w, "data": [...], "added": [...]}: build new DynamicHistogram(binWidth,
    // new ConvergenceCriteria()), AddObservationsToHistogram(data), then one
    // AddObservationToHistogram(x) per x in `added` (the "AddedData" test variants). `mean` maps to
    // HistogramMean() (mean from the binned histogram); `sample_mean` to the raw running SampleMean;
    // `standard_deviation` to the raw running StandardDeviation; `histogram_standard_deviation` to
    // HistogramStandardDeviation().
    static object EvalHistogram(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      var hist = new DynamicHistogram(c.GetProperty("bin_width").GetDouble(), new ConvergenceCriteria());
      hist.AddObservationsToHistogram(DA(c.GetProperty("data")));
      if (c.TryGetProperty("added", out var added)) {
        foreach (var x in added.EnumerateArray()) hist.AddObservationToHistogram(x.GetDouble());
      }
      if (method == "min") return hist.Min;
      if (method == "max") return hist.Max;
      if (method == "sample_mean") return hist.SampleMean;
      if (method == "mean") return hist.HistogramMean();
      if (method == "histogram_standard_deviation") return hist.HistogramStandardDeviation();
      if (method == "standard_deviation") return hist.StandardDeviation;
      if (method == "pdf") return hist.PDF(D(argsEl[0]));
      if (method == "cdf") return hist.CDF(D(argsEl[0]));
      if (method == "inverse_cdf") return hist.InverseCDF(D(argsEl[0]));
      throw new Exception("unknown histogram method: " + method);
    }
    static PairedData MakePaired(JsonElement c) => new PairedData(DA(c.GetProperty("xs")), DA(c.GetProperty("ys")));
    // Extended (Task P2T2) beyond f/f_inverse/Integrate: compose/SumYsForGivenX/multiply (each
    // needs a second curve, from the case's "input" property) and the monotonicity-forcing +
    // SortToIncreasingXVals mutators (act on a fresh `pd`, then read Xvals/Yvals back) and the
    // f(x, ref index) overload (fresh `indexOfPreviousTopOfSegment = 0` per call, matching
    // test_fixtures.cpp's per-assertion fresh-construction convention).
    static object EvalPaired(JsonElement caseEl, string method, JsonElement args) {
      var pd = MakePaired(caseEl.GetProperty("construct"));
      if (method == "f") return pd.f(D(args[0]));
      if (method == "f_inverse") return pd.f_inverse(D(args[0]));
      if (method == "integrate") return pd.Integrate();
      if (method == "compose_xvals" || method == "compose_yvals") {
        PairedData r = pd.compose(MakePaired(caseEl.GetProperty("input")));
        return method == "compose_xvals" ? r.Xvals.ToArray() : r.Yvals.ToArray();
      }
      if (method == "sum_ys_for_given_x_xvals" || method == "sum_ys_for_given_x_yvals") {
        PairedData r = pd.SumYsForGivenX(MakePaired(caseEl.GetProperty("input")));
        return method == "sum_ys_for_given_x_xvals" ? r.Xvals.ToArray() : r.Yvals.ToArray();
      }
      if (method == "multiply_xvals" || method == "multiply_yvals") {
        PairedData r = (PairedData)pd.multiply(MakePaired(caseEl.GetProperty("input")));
        return method == "multiply_xvals" ? r.Xvals.ToArray() : r.Yvals.ToArray();
      }
      if (method == "force_weak_monotonicity_bottom_up_yvals") {
        pd.ForceWeakMonotonicityBottomUp();
        return pd.Yvals.ToArray();
      }
      if (method == "force_strict_monotonicity_top_down_yvals") {
        pd.ForceStrictMonotonicityTopDown();
        return pd.Yvals.ToArray();
      }
      if (method == "force_strict_monotonicity_bottom_up_yvals") {
        pd.ForceStrictMonotonicityBottomUp();
        return pd.Yvals.ToArray();
      }
      if (method == "sort_to_increasing_x_vals_xvals" || method == "sort_to_increasing_x_vals_yvals") {
        pd.SortToIncreasingXVals();
        return method == "sort_to_increasing_x_vals_xvals" ? pd.Xvals.ToArray() : pd.Yvals.ToArray();
      }
      if (method == "f_ref_index") {
        int index = 0;
        return pd.f(D(args[0]), ref index);
      }
      throw new Exception("unknown paired_data method: " + method);
    }
    static object EvalSpecial(string method, JsonElement args) {
      // Static SpecialFunctions surface; args is a flat array of scalars.
      return method switch {
        "log_gamma" => SpecialFunctions.logGamma(D(args[0])),
        "log_factorial" => SpecialFunctions.logFactorial((int)D(args[0])),
        "gamma" => SpecialFunctions.gamma(D(args[0])),
        "factorial" => SpecialFunctions.factorial((int)D(args[0])),
        "incomplete_gamma" => SpecialFunctions.incompleteGamma(D(args[0]), D(args[1])),
        "incomplete_gamma_range" => SpecialFunctions.incompleteGamma(D(args[0]), D(args[1]), D(args[2])),
        "reg_incomplete_gamma" => SpecialFunctions.regIncompleteGamma(D(args[0]), D(args[1])),
        "log_incomplete_gamma" => SpecialFunctions.logIncompleteGamma(D(args[0]), D(args[1])),
        "digamma" => SpecialFunctions.digamma(D(args[0])),
        "log_beta" => SpecialFunctions.logBeta(D(args[0]), D(args[1])),
        "beta" => SpecialFunctions.beta(D(args[0]), D(args[1])),
        "incomplete_beta" => SpecialFunctions.incompleteBeta(D(args[0]), D(args[1]), D(args[2])),
        "reg_incomplete_beta" => SpecialFunctions.regIncompleteBeta(D(args[0]), D(args[1]), D(args[2])),
        "trigamma" => SpecialFunctions.trigamma(D(args[0])),
        "single_par_gamma_pdf" => SpecialFunctions.singleParGammaPDF(D(args[0]), D(args[1])),
        "gamma_derivative" => SpecialFunctions.gammaDerivative(D(args[0]), D(args[1])),
        _ => throw new Exception("unknown special_functions method: " + method) };
    }
    static object EvalSampleStatistics(JsonElement c, string method) {
      var stats = new SampleStatistics(DA(c.GetProperty("data")));
      return method switch {
        "mean" => stats.Mean, "variance" => stats.Variance, "standard_deviation" => stats.StandardDeviation,
        "median" => stats.Median, "skewness" => stats.Skewness, "min" => stats.Min, "max" => stats.Max,
        "sample_size" => (double)stats.SampleSize,
        _ => throw new Exception("unknown sample_statistics method: " + method) };
    }
    // ys is now built from the generalized `{type, params}` distribution specs via the shared
    // DistFactory (same param order as the C++ IDistributionFactory), matching UncertainPairedData's
    // generalization from Normal to IDistribution. `metadata` is optional (xlabel/ylabel/name),
    // defaulting to CurveMetaData("x","y","oracle") -- irrelevant to the sampled yvals, kept only for
    // faithful construction. sample_and_integrate reads the case-level `seed`; the iteration overload
    // reads construct-level `seed`/`size` for GenerateRandomNumbers.
    static object EvalUpd(JsonElement caseEl, string method, JsonElement args) {
      var c = caseEl.GetProperty("construct");
      double[] xs = DA(c.GetProperty("xs"));
      var ys = c.GetProperty("ys").EnumerateArray()
        .Select(y => (IDistribution)DistFactory(y.GetProperty("type").GetString(), DA(y.GetProperty("params")))).ToArray();
      CurveMetaData md = c.TryGetProperty("metadata", out var m)
        ? new CurveMetaData(m.GetProperty("xlabel").GetString(), m.GetProperty("ylabel").GetString(), m.GetProperty("name").GetString())
        : new CurveMetaData("x","y","oracle");
      var upd = new UncertainPairedData(xs, ys, md);
      if (method == "sample_and_integrate") {
        int seed = caseEl.GetProperty("seed").GetInt32();
        double p = new RandomProvider(seed).NextRandom();
        return upd.SamplePairedDataRaw(p).Integrate();
      }
      if (c.TryGetProperty("seed", out var s) && c.TryGetProperty("size", out var sz)) {
        upd.GenerateRandomNumbers(s.GetInt32(), (long)sz.GetDouble());
      }
      if (method == "sample_paired_data") return upd.SamplePairedData(D(args[0])).Yvals.ToArray();
      if (method == "sample_paired_data_raw") return upd.SamplePairedDataRaw(D(args[0])).Yvals.ToArray();
      if (method == "sample_paired_data_raw_deterministic") return upd.SamplePairedDataRawDeterministic().Yvals.ToArray();
      if (method == "sample_paired_data_iteration") return upd.SamplePairedData((long)D(args[0]), D(args[1]) != 0.0).Yvals.ToArray();
      throw new Exception("unknown uncertain_paired_data method: " + method);
    }

    // InterpolateQuantiles (Task P2T4a) is a public static helper in HEC.FDA.Model.paireddata --
    // no null/RNG involved, so it's dispatched directly here like ShiftedGamma/PearsonIII/etc.
    // `construct` is {"input_exceedance_probabilities": [...], "input_data_for_interpolation":
    // [...]}; the single method `interpolate_on_x` takes the "required" exceedance probabilities
    // as its (array-valued) `args`.
    static object EvalInterpolateQuantiles(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      double[] inputExceedanceProbabilities = DA(c.GetProperty("input_exceedance_probabilities"));
      double[] inputDataForInterpolation = DA(c.GetProperty("input_data_for_interpolation"));
      if (method == "interpolate_on_x") {
        double[] required = DA(argsEl);
        return InterpolateQuantiles.InterpolateOnX(inputExceedanceProbabilities, required, inputDataForInterpolation);
      }
      throw new Exception("unknown interpolate_quantiles method: " + method);
    }
    // GraphicalFrequencyUncertaintyCalculators.LessSimpleMethod (Task P2T4a) is a public static
    // method in HEC.FDA.Model.utilities returning `(double[], ContinuousDistribution[])`.
    // `construct` is {"exceedance_probabilities": [...], "stages_or_flows": [...],
    // "using_stages_not_flows": bool, "equivalent_record_length": int?} (ERL defaults to 10,
    // matching the C# default parameter). Distribution mean/standard-deviation aren't exposed on
    // IDistribution/ContinuousDistribution, so they're read via an `is Normal`/`is LogNormal`
    // pattern match, mirroring EvalDistribution's fit_<param> demux above.
    static object EvalGraphicalCalculators(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      double[] exceedanceProbabilities = DA(c.GetProperty("exceedance_probabilities"));
      double[] stagesOrFlows = DA(c.GetProperty("stages_or_flows"));
      bool usingStagesNotFlows = c.GetProperty("using_stages_not_flows").GetBoolean();
      int erl = c.TryGetProperty("equivalent_record_length", out var erlEl) ? erlEl.GetInt32() : 10;
      (double[] filledProbs, ContinuousDistribution[] dists) = GraphicalFrequencyUncertaintyCalculators.LessSimpleMethod(
          exceedanceProbabilities, stagesOrFlows, usingStagesNotFlows, erl);
      if (method == "filled_exceedance_probabilities") return filledProbs;
      if (method == "distribution_means") {
        return dists.Select(d => d switch {
          Normal n => n.Mean,
          LogNormal ln => ln.Mean,
          _ => throw new Exception("unexpected distribution type: " + d.GetType().Name)
        }).ToArray();
      }
      if (method == "distribution_standard_deviations") {
        return dists.Select(d => d switch {
          Normal n => n.StandardDeviation,
          LogNormal ln => ln.StandardDeviation,
          _ => throw new Exception("unexpected distribution type: " + d.GetType().Name)
        }).ToArray();
      }
      if (method == "distribution_pdf_at") {
        int index = (int)D(argsEl[0]);
        double x = D(argsEl[1]);
        return dists[index].PDF(x);
      }
      throw new Exception("unknown graphical_calculators method: " + method);
    }

    // GraphicalUncertainPairedData (Task P2T4b) is the non-parametric graphical-uncertainty
    // frequency curve -- built from patched/GraphicalDistribution.cs +
    // patched/GraphicalUncertainPairedData.cs (see those files' headers for why they're patched
    // local copies rather than upstream Compile Includes: their WriteToXML/ReadFromXML are
    // stubbed out to avoid the H5Assist.Chunking-dependent Serialization class, which is
    // otherwise unreachable from this project). `construct` is {"exceedance_probabilities": [...],
    // "flow_or_stage_values": [...], "equivalent_record_length": int, "using_stages_not_flows":
    // bool, "seed": int?, "size": int?}; CurveMetaData is irrelevant to sampled yvals (same
    // rationale as EvalUpd) so it's always a fixed CurveMetaData("hello") (matching
    // GraphicalUncertaintyPairedDataTests.cs's SamplePairedDataShould fixture).
    static object EvalGupd(JsonElement caseEl, string method, JsonElement args) {
      var c = caseEl.GetProperty("construct");
      double[] exceedanceProbabilities = DA(c.GetProperty("exceedance_probabilities"));
      double[] flowOrStageValues = DA(c.GetProperty("flow_or_stage_values"));
      int erl = c.GetProperty("equivalent_record_length").GetInt32();
      bool usingStagesNotFlows = c.GetProperty("using_stages_not_flows").GetBoolean();
      var gupd = new GraphicalUncertainPairedData(exceedanceProbabilities, flowOrStageValues, erl, new CurveMetaData("hello"), usingStagesNotFlows);
      if (c.TryGetProperty("seed", out var s) && c.TryGetProperty("size", out var sz)) {
        gupd.GenerateRandomNumbers(s.GetInt32(), sz.GetInt32());
      }
      if (method == "sample_paired_data") return gupd.SamplePairedData(D(args[0])).Yvals.ToArray();
      if (method == "sample_paired_data_iteration") return gupd.SamplePairedData((long)D(args[0]), D(args[1]) != 0.0).Yvals.ToArray();
      if (method == "sample_paired_data_f") return gupd.SamplePairedData(D(args[0])).f(D(args[1]));
      if (method == "sample_paired_data_iteration_f") return gupd.SamplePairedData((long)D(args[0]), D(args[1]) != 0.0).f(D(args[2]));
      throw new Exception("unknown graphical_uncertain_paired_data method: " + method);
    }

    // ValueUncertainty (Phase 3 Task 1) is a plain Validation-derived (via ValidationErrorLogger)
    // per-structure uncertainty sampler, not an IDistribution, so it's constructed directly here
    // like ShiftedGamma/PearsonIII/ConvergenceCriteria. `construct` is {"dist": "<name>",
    // "std_or_min": ..., "max": ...}; `dist` parsed via Enum.Parse against IDistributionEnum
    // (same string set DistFactory's `type` switch uses). `sample` dispatches Sample(double);
    // `sample_iteration` dispatches Sample(long, bool) with args [iteration, computeIsDeterministic].
    static object EvalValueUncertainty(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      var distType = (IDistributionEnum)Enum.Parse(typeof(IDistributionEnum), c.GetProperty("dist").GetString());
      double stdOrMin = c.GetProperty("std_or_min").GetDouble();
      double max = c.GetProperty("max").GetDouble();
      var vu = new ValueUncertainty(distType, stdOrMin, max);
      if (method == "sample") return vu.Sample(D(argsEl[0]));
      if (method == "sample_iteration") return vu.Sample((long)D(argsEl[0]), D(argsEl[1]) != 0.0);
      throw new Exception("unknown value_uncertainty method: " + method);
    }

    // ValueRatioWithUncertainty (Phase 3 Task 2) is a plain Validation-derived per-structure
    // uncertainty sampler, same shape as EvalValueUncertainty above. `construct` is {"dist":
    // "<name>", "std_or_min": ..., "central": ..., "max": ...}; `dist` parsed via Enum.Parse
    // against IDistributionEnum. `sample` dispatches Sample(double); `sample_iteration`
    // dispatches Sample(long, bool) with args [iteration, computeIsDeterministic].
    static object EvalValueRatioWithUncertainty(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      var distType = (IDistributionEnum)Enum.Parse(typeof(IDistributionEnum), c.GetProperty("dist").GetString());
      double stdOrMin = c.GetProperty("std_or_min").GetDouble();
      double central = c.GetProperty("central").GetDouble();
      double max = c.GetProperty("max").GetDouble();
      var vru = new ValueRatioWithUncertainty(distType, stdOrMin, central, max);
      if (method == "sample") return vru.Sample(D(argsEl[0]));
      if (method == "sample_iteration") return vru.Sample((long)D(argsEl[0]), D(argsEl[1]) != 0.0);
      throw new Exception("unknown value_ratio_with_uncertainty method: " + method);
    }

    // FirstFloorElevationUncertainty (Phase 3 Task 3) is a plain Validation-derived per-structure
    // uncertainty sampler, same shape as EvalValueRatioWithUncertainty above. `construct` is
    // {"dist": "<name>", "std_or_min": ..., "max": ...} (no "central" field -- the center is
    // hardcoded to 0 inside the class). `sample` dispatches Sample(double); `sample_iteration`
    // dispatches Sample(long, bool) with args [iteration, computeIsDeterministic].
    static object EvalFirstFloorElevationUncertainty(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      var distType = (IDistributionEnum)Enum.Parse(typeof(IDistributionEnum), c.GetProperty("dist").GetString());
      double stdOrMin = c.GetProperty("std_or_min").GetDouble();
      double max = c.GetProperty("max").GetDouble();
      var ffeu = new FirstFloorElevationUncertainty(distType, stdOrMin, max);
      if (method == "sample") return ffeu.Sample(D(argsEl[0]));
      if (method == "sample_iteration") return ffeu.Sample((long)D(argsEl[0]), D(argsEl[1]) != 0.0);
      throw new Exception("unknown first_floor_elevation_uncertainty method: " + method);
    }

    // OccupancyType + DeterministicOccupancyType (Phase 3 Task 4) -- the integration class binding
    // the three leaf samplers to UncertainPairedData via OccupancyType.Builder(). `construct` is
    // {name, damage_category, depths, struct_damages:[{type,params}],
    // content_damages:[{type,params}], ffe:{dist,std_or_min,[max]},
    // structure_value:{dist,std_or_min,[max]}, csvr:{dist,std_or_min,central,[max]}} -- same shape
    // the C++ test_fixtures.cpp dispatch uses. `sample_iteration_*` methods dispatch
    // Sample(iteration, computeIsDeterministic) and read one field off the resulting
    // DeterministicOccupancyType; `generate_then_sample_iteration_struct_yvals` additionally reads
    // a "size" field off the assertion and calls GenerateRandomNumbers(size) first.
    static IDistribution[] DistArray(JsonElement ys) =>
      ys.EnumerateArray().Select(y => (IDistribution)DistFactory(y.GetProperty("type").GetString(), DA(y.GetProperty("params")))).ToArray();
    static OccupancyType MakeOccupancyType(JsonElement c) {
      double[] depths = DA(c.GetProperty("depths"));
      IDistribution[] structDamages = DistArray(c.GetProperty("struct_damages"));
      IDistribution[] contentDamages = DistArray(c.GetProperty("content_damages"));
      var md = new CurveMetaData("x", "y", "oracle");
      var structUpd = new UncertainPairedData(depths, structDamages, md);
      var contentUpd = new UncertainPairedData(depths, contentDamages, md);

      var ffeC = c.GetProperty("ffe");
      var ffeDist = (IDistributionEnum)Enum.Parse(typeof(IDistributionEnum), ffeC.GetProperty("dist").GetString());
      double ffeStdOrMin = ffeC.GetProperty("std_or_min").GetDouble();
      var ffe = ffeC.TryGetProperty("max", out var ffeMax)
        ? new FirstFloorElevationUncertainty(ffeDist, ffeStdOrMin, ffeMax.GetDouble())
        : new FirstFloorElevationUncertainty(ffeDist, ffeStdOrMin);

      var svC = c.GetProperty("structure_value");
      var svDist = (IDistributionEnum)Enum.Parse(typeof(IDistributionEnum), svC.GetProperty("dist").GetString());
      double svStdOrMin = svC.GetProperty("std_or_min").GetDouble();
      var sv = svC.TryGetProperty("max", out var svMax)
        ? new ValueUncertainty(svDist, svStdOrMin, svMax.GetDouble())
        : new ValueUncertainty(svDist, svStdOrMin);

      var csvrC = c.GetProperty("csvr");
      var csvrDist = (IDistributionEnum)Enum.Parse(typeof(IDistributionEnum), csvrC.GetProperty("dist").GetString());
      double csvrStdOrMin = csvrC.GetProperty("std_or_min").GetDouble();
      double csvrCentral = csvrC.GetProperty("central").GetDouble();
      var csvr = csvrC.TryGetProperty("max", out var csvrMax)
        ? new ValueRatioWithUncertainty(csvrDist, csvrStdOrMin, csvrCentral, csvrMax.GetDouble())
        : new ValueRatioWithUncertainty(csvrDist, csvrStdOrMin, csvrCentral);

      var builder = OccupancyType.Builder()
        .WithName(c.GetProperty("name").GetString())
        .WithDamageCategory(c.GetProperty("damage_category").GetString())
        .WithStructureDepthPercentDamage(structUpd)
        .WithContentDepthPercentDamage(contentUpd)
        .WithFirstFloorElevationUncertainty(ffe)
        .WithStructureValueUncertainty(sv)
        .WithContentToStructureValueRatio(csvr);

      // Optional: vehicle/other depth-percent-damage + value uncertainty (Phase 4 Task 2's
      // inventory_compute_damages fixture is the first construct to use these; occupancy_type.json
      // and inventory.json's existing cases omit them, matching OccupancyType's C# default of
      // ComputeVehicleDamage/ComputeOtherDamage == false until the corresponding With* is called).
      if (c.TryGetProperty("vehicle_damages", out var vehicleDamagesEl)) {
        IDistribution[] vehicleDamages = DistArray(vehicleDamagesEl);
        builder = builder.WithVehicleDepthPercentDamage(new UncertainPairedData(depths, vehicleDamages, md));
      }
      if (c.TryGetProperty("other_damages", out var otherDamagesEl)) {
        IDistribution[] otherDamages = DistArray(otherDamagesEl);
        builder = builder.WithOtherDepthPercentDamage(new UncertainPairedData(depths, otherDamages, md));
      }
      if (c.TryGetProperty("vehicle_value", out var vvC)) {
        var vvDist = (IDistributionEnum)Enum.Parse(typeof(IDistributionEnum), vvC.GetProperty("dist").GetString());
        double vvStdOrMin = vvC.GetProperty("std_or_min").GetDouble();
        var vv = vvC.TryGetProperty("max", out var vvMax)
          ? new ValueUncertainty(vvDist, vvStdOrMin, vvMax.GetDouble())
          : new ValueUncertainty(vvDist, vvStdOrMin);
        builder = builder.WithVehicleValueUncertainty(vv);
      }
      if (c.TryGetProperty("other_value", out var ovC)) {
        var ovDist = (IDistributionEnum)Enum.Parse(typeof(IDistributionEnum), ovC.GetProperty("dist").GetString());
        double ovStdOrMin = ovC.GetProperty("std_or_min").GetDouble();
        var ov = ovC.TryGetProperty("max", out var ovMax)
          ? new ValueUncertainty(ovDist, ovStdOrMin, ovMax.GetDouble())
          : new ValueUncertainty(ovDist, ovStdOrMin);
        builder = builder.WithOtherValueUncertainty(ov);
      }

      return builder.Build();
    }
    static object EvalOccupancyType(JsonElement caseEl, JsonElement assertionEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      var occ = MakeOccupancyType(c);
      if (method == "validate_error_level") {
        occ.Validate();
        return (double)(int)occ.ErrorLevel;
      }
      if (method == "validate_has_errors") {
        occ.Validate();
        return occ.HasErrors ? 1.0 : 0.0;
      }
      if (method == "generate_then_sample_iteration_struct_yvals") {
        long size = (long)assertionEl.GetProperty("size").GetDouble();
        occ.GenerateRandomNumbers(size);
        var sampled = occ.Sample((long)D(argsEl[0]), D(argsEl[1]) != 0.0);
        return sampled.StructPercentDamagePairedData.Yvals.ToArray();
      }
      var sampledDet = occ.Sample((long)D(argsEl[0]), D(argsEl[1]) != 0.0);
      if (method == "sample_iteration_struct_yvals") return sampledDet.StructPercentDamagePairedData.Yvals.ToArray();
      if (method == "sample_iteration_content_yvals") return sampledDet.ContentPercentDamagePairedData.Yvals.ToArray();
      if (method == "sample_iteration_structure_value_offset") return sampledDet.StructureValueOffset;
      if (method == "sample_iteration_ffe_offset") return sampledDet.FirstFloorElevationOffset;
      if (method == "sample_iteration_csvr") return sampledDet.ContentToStructureValueRatio;
      throw new Exception("unknown occupancy_type method: " + method);
    }

    // Structure (Phase 3 Task 5) -- the per-structure numeric depth-damage compute, built from
    // patched/Structure.cs (see that file's header for why it's a patched local copy). `construct`
    // is {occupancy_type: {name, damage_category, struct_depths, struct_damages:[{type,params}],
    // content_depths, content_damages:[{type,params}], ffe:{...}, structure_value:{...},
    // csvr:{...}}, sample:[iteration, computeIsDeterministic], structure:{fid,
    // first_floor_elevation, val_struct, st_damcat, occtype, impact_area_id, [val_cont],
    // [val_vehic], [val_other], [ground_elevation]}}. Unlike MakeOccupancyType (which shares one
    // "depths" array between struct/content), struct_depths/content_depths are separate here
    // because the SELA case's structure and content curves have different lengths -- see
    // fixtures/structures/structure.json's note. Each assertion builds the occupancy type +
    // structure fresh, samples once via Sample(iteration, computeIsDeterministic), and dispatches
    // compute_damage_{struct,content,vehicle,other} = the four tuple items of
    // Structure.ComputeDamage(wse, sampled, priceIndex=1, analysisYear=9999); `args` is [wse].
    static OccupancyType MakeStructureOccupancyType(JsonElement c) {
      IDistribution[] structDamages = DistArray(c.GetProperty("struct_damages"));
      IDistribution[] contentDamages = DistArray(c.GetProperty("content_damages"));
      var md = new CurveMetaData("x", "y", "oracle");
      var structUpd = new UncertainPairedData(DA(c.GetProperty("struct_depths")), structDamages, md);
      var contentUpd = new UncertainPairedData(DA(c.GetProperty("content_depths")), contentDamages, md);

      var ffeC = c.GetProperty("ffe");
      var ffeDist = (IDistributionEnum)Enum.Parse(typeof(IDistributionEnum), ffeC.GetProperty("dist").GetString());
      double ffeStdOrMin = ffeC.GetProperty("std_or_min").GetDouble();
      var ffe = ffeC.TryGetProperty("max", out var ffeMax)
        ? new FirstFloorElevationUncertainty(ffeDist, ffeStdOrMin, ffeMax.GetDouble())
        : new FirstFloorElevationUncertainty(ffeDist, ffeStdOrMin);

      var svC = c.GetProperty("structure_value");
      var svDist = (IDistributionEnum)Enum.Parse(typeof(IDistributionEnum), svC.GetProperty("dist").GetString());
      double svStdOrMin = svC.GetProperty("std_or_min").GetDouble();
      var sv = svC.TryGetProperty("max", out var svMax)
        ? new ValueUncertainty(svDist, svStdOrMin, svMax.GetDouble())
        : new ValueUncertainty(svDist, svStdOrMin);

      var csvrC = c.GetProperty("csvr");
      var csvrDist = (IDistributionEnum)Enum.Parse(typeof(IDistributionEnum), csvrC.GetProperty("dist").GetString());
      double csvrStdOrMin = csvrC.GetProperty("std_or_min").GetDouble();
      double csvrCentral = csvrC.GetProperty("central").GetDouble();
      var csvr = csvrC.TryGetProperty("max", out var csvrMax)
        ? new ValueRatioWithUncertainty(csvrDist, csvrStdOrMin, csvrCentral, csvrMax.GetDouble())
        : new ValueRatioWithUncertainty(csvrDist, csvrStdOrMin, csvrCentral);

      return OccupancyType.Builder()
        .WithName(c.GetProperty("name").GetString())
        .WithDamageCategory(c.GetProperty("damage_category").GetString())
        .WithStructureDepthPercentDamage(structUpd)
        .WithContentDepthPercentDamage(contentUpd)
        .WithFirstFloorElevationUncertainty(ffe)
        .WithStructureValueUncertainty(sv)
        .WithContentToStructureValueRatio(csvr)
        .Build();
    }
    static Structure MakeStructure(JsonElement c) {
      double valCont = c.TryGetProperty("val_cont", out var vc) ? vc.GetDouble() : 0;
      double valVehic = c.TryGetProperty("val_vehic", out var vv) ? vv.GetDouble() : 0;
      double valOther = c.TryGetProperty("val_other", out var vo) ? vo.GetDouble() : 0;
      double groundElevation = c.TryGetProperty("ground_elevation", out var ge) ? ge.GetDouble() : IntegerGlobalConstants.DEFAULT_MISSING_VALUE;
      return new Structure(
        c.GetProperty("fid").GetString(),
        c.GetProperty("first_floor_elevation").GetDouble(),
        c.GetProperty("val_struct").GetDouble(),
        c.GetProperty("st_damcat").GetString(),
        c.GetProperty("occtype").GetString(),
        c.GetProperty("impact_area_id").GetInt32(),
        val_cont: valCont,
        val_vehic: valVehic,
        val_other: valOther,
        groundElevation: groundElevation);
    }
    static object EvalStructure(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      var occ = MakeStructureOccupancyType(c.GetProperty("occupancy_type"));
      var sampleArgs = c.GetProperty("sample");
      var sampled = occ.Sample(sampleArgs[0].GetInt64(), sampleArgs[1].GetDouble() != 0.0);
      var structure = MakeStructure(c.GetProperty("structure"));
      float wse = (float)D(argsEl[0]);
      var (structDamage, contDamage, vehicleDamage, otherDamage) = structure.ComputeDamage(wse, sampled);
      if (method == "compute_damage_struct") return structDamage;
      if (method == "compute_damage_content") return contDamage;
      if (method == "compute_damage_vehicle") return vehicleDamage;
      if (method == "compute_damage_other") return otherDamage;
      throw new Exception("unknown structure method: " + method);
    }

    // Inventory (Phase 3 Task 6, the last core-code task of the phase) -- built from
    // patched/Inventory.cs (see that file's header for why it's a patched local copy). `construct`
    // is {occ_types: [<occupancy_type construct>, ...], structures: [<structure construct>, ...],
    // [price_index]}; reuses MakeOccupancyType/MakeStructure directly (same construct shapes as the
    // occupancy_type/structure dispatch targets above). See fixtures/structures/inventory.json's
    // note for the method list and why validate_* cases use an empty occ_types list.
    static Inventory MakeInventory(JsonElement c) {
      var occTypes = new Dictionary<string, OccupancyType>();
      foreach (var occCtor in c.GetProperty("occ_types").EnumerateArray()) {
        occTypes[occCtor.GetProperty("name").GetString()] = MakeOccupancyType(occCtor);
      }
      var structures = new List<Structure>();
      foreach (var structCtor in c.GetProperty("structures").EnumerateArray()) {
        structures.Add(MakeStructure(structCtor));
      }
      double priceIndex = c.TryGetProperty("price_index", out var pi) ? pi.GetDouble() : 1.0;
      return new Inventory(occTypes, structures, priceIndex);
    }
    static object EvalInventory(JsonElement caseEl, JsonElement assertionEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      var inv = MakeInventory(c);
      if (method == "damage_category_count") return (double)inv.GetDamageCategories().Count;
      if (method == "trim_to_impact_area_count") {
        var trimmed = inv.GetInventoryTrimmedToImpactArea(argsEl[0].GetInt32());
        return (double)trimmed.Structures.Count;
      }
      if (method == "generate_then_sample_struct_yvals") {
        var conv = assertionEl.GetProperty("convergence");
        var cc = new ConvergenceCriteria(conv.GetProperty("min_iterations").GetInt32(), conv.GetProperty("max_iterations").GetInt32());
        inv.GenerateRandomNumbers(cc);
        var sampled = inv.SampleOccupancyTypes((long)D(argsEl[0]), D(argsEl[1]) != 0.0);
        return sampled[0].StructPercentDamagePairedData.Yvals.ToArray();
      }
      if (method == "validate_error_level") {
        inv.Validate();
        return (double)(byte)inv.ErrorLevel;
      }
      if (method == "validate_has_errors") {
        inv.Validate();
        return inv.HasErrors ? 1.0 : 0.0;
      }
      throw new Exception("unknown inventory method: " + method);
    }

    // Inventory.ComputeDamages/AggregateResults (Phase 4 Task 2): re-added to patched/Inventory.cs
    // (Phase 3 severed them, pending ConsequenceResult -- Task 1). `construct` extends MakeInventory's
    // shape (occ_types/structures/[price_index]) with `wses` ([profile][structure] float matrix),
    // `analysis_year`, `damage_category`, and `sample` ([iteration, computeIsDeterministic] fed to
    // SampleOccupancyTypes). Every assertion for a case builds a fresh Inventory + samples once, then
    // calls ComputeDamages(wses, analysisYear, damageCategory, det) and returns one of the four
    // per-profile damage arrays (`compute_damages_struct/_content/_vehicle/_other`) -- the true
    // struct/content/vehicle/other totals per ConsequenceResult, in that semantic order (see
    // patched/Inventory.cs's ComputeDamages for the faithful store/AggregateResults-argument swap
    // this fixture is designed to lock).
    static object EvalInventoryComputeDamages(JsonElement caseEl, string method) {
      var c = caseEl.GetProperty("construct");
      var inv = MakeInventory(c);
      var sampleArgs = c.GetProperty("sample");
      var det = inv.SampleOccupancyTypes(sampleArgs[0].GetInt64(), sampleArgs[1].GetDouble() != 0.0);
      var wses = new List<float[]>();
      foreach (var pf in c.GetProperty("wses").EnumerateArray()) {
        wses.Add(pf.EnumerateArray().Select(x => (float)x.GetDouble()).ToArray());
      }
      int analysisYear = c.GetProperty("analysis_year").GetInt32();
      string damageCategory = c.GetProperty("damage_category").GetString();
      var results = inv.ComputeDamages(wses, analysisYear, damageCategory, det);
      if (method == "compute_damages_struct") return results.Select(r => r.StructureDamage).ToArray();
      if (method == "compute_damages_content") return results.Select(r => r.ContentDamage).ToArray();
      if (method == "compute_damages_vehicle") return results.Select(r => r.VehicleDamage).ToArray();
      if (method == "compute_damages_other") return results.Select(r => r.OtherDamage).ToArray();
      throw new Exception("unknown inventory_compute_damages method: " + method);
    }

    // ConsequenceResult (Phase 4 Task 1) is a plain per-structure damage accumulator, not an
    // IDistribution, constructed directly here like ValueUncertainty/Structure above. `construct`
    // is {"damage_category": "<name>"}; `increments` is a list of [structureDamage, contentDamage,
    // vehicleDamage, otherDamage] tuples applied in order via IncrementConsequence. `method`
    // dispatches one of the eight accessors; the four *Quantity accessors return int, boxed as
    // double via implicit cast for the shared double-comparison harness. `equals` builds a second
    // ConsequenceResult from the case's `compare_to` block (same {construct, increments} shape)
    // and returns 1.0/0.0 for cr.Equals(cr2). ConsequenceResult.cs is compiled directly into this
    // project (see the csproj `<Compile Include=...>`, not referenced via a built assembly), so
    // the `internal` Equals is directly accessible -- no reflection needed.
    static object EvalConsequenceResult(JsonElement caseEl, string method) {
      var c = caseEl.GetProperty("construct");
      var cr = new ConsequenceResult(c.GetProperty("damage_category").GetString());
      foreach (var inc in caseEl.GetProperty("increments").EnumerateArray()) {
        cr.IncrementConsequence(D(inc[0]), D(inc[1]), D(inc[2]), D(inc[3]));
      }
      if (method == "structure_damage") return cr.StructureDamage;
      if (method == "content_damage") return cr.ContentDamage;
      if (method == "vehicle_damage") return cr.VehicleDamage;
      if (method == "other_damage") return cr.OtherDamage;
      if (method == "damaged_structures_quantity") return (double)cr.DamagedStructuresQuantity;
      if (method == "damaged_contents_quantity") return (double)cr.DamagedContentsQuantity;
      if (method == "damaged_vehicles_quantity") return (double)cr.DamagedVehiclesQuantity;
      if (method == "damaged_others_quantity") return (double)cr.DamagedOthersQuantity;
      if (method == "equals") {
        var c2 = caseEl.GetProperty("compare_to").GetProperty("construct");
        var cr2 = new ConsequenceResult(c2.GetProperty("damage_category").GetString());
        foreach (var inc in caseEl.GetProperty("compare_to").GetProperty("increments").EnumerateArray()) {
          cr2.IncrementConsequence(D(inc[0]), D(inc[1]), D(inc[2]), D(inc[3]));
        }
        return cr.Equals(cr2) ? 1.0 : 0.0;
      }
      throw new Exception("unknown consequence_result method: " + method);
    }

    // AggregatedConsequencesBinned (Phase 4 Task 3) is the histogram-staging Monte Carlo
    // accumulator -- built from patched/AggregatedConsequencesBinned.cs (see that file's header
    // for why it's a patched local copy: WriteToXML/ReadFromXML/
    // ConvertToSingleEmpiricalDistributionOfConsequences dropped, everything else verbatim).
    // `construct` is {damage_category, asset_category, convergence: {min_iterations,
    // max_iterations}, impact_area_id, consequence_type, risk_type}, matching the
    // (string, string, ConvergenceCriteria, int, ConsequenceType, RiskType) compute ctor;
    // ConvergenceCriteria uses the 2-arg (minIterations, maxIterations) ctor, same as
    // EvalInventory's `generate_then_sample_struct_yvals` case. `realizations` is a list of
    // {iteration, damage, count} applied in order via AddConsequenceRealization before a single
    // PutDataIntoHistogram() call; one object is built and staged per case, shared across all of
    // that case's assertions (mirrors run_aggregated_consequences_binned in test_fixtures.cpp).
    // `method` dispatches SampleMeanExpectedAnnualConsequences (args []),
    // ConsequenceExceededWithProbabilityQ (args [q]), or QuantityExceededWithProbabilityQ
    // (args [q]) -- all three are `internal`, but Program.cs is compiled directly against
    // patched/AggregatedConsequencesBinned.cs (not a built assembly), so no reflection is needed.
    static object EvalAggregatedConsequencesBinned(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      var conv = c.GetProperty("convergence");
      var cc = new ConvergenceCriteria(conv.GetProperty("min_iterations").GetInt32(), conv.GetProperty("max_iterations").GetInt32());
      var consequenceType = Enum.Parse<ConsequenceType>(c.GetProperty("consequence_type").GetString());
      var riskType = Enum.Parse<RiskType>(c.GetProperty("risk_type").GetString());
      var acb = new AggregatedConsequencesBinned(c.GetProperty("damage_category").GetString(), c.GetProperty("asset_category").GetString(), cc, c.GetProperty("impact_area_id").GetInt32(), consequenceType, riskType);
      foreach (var r in caseEl.GetProperty("realizations").EnumerateArray()) {
        acb.AddConsequenceRealization(D(r.GetProperty("damage")), r.GetProperty("iteration").GetInt64(), r.GetProperty("count").GetInt32());
      }
      acb.PutDataIntoHistogram();
      if (method == "sample_mean_expected_annual_consequences") return acb.SampleMeanExpectedAnnualConsequences();
      if (method == "consequence_exceeded_with_probability_q") return acb.ConsequenceExceededWithProbabilityQ(D(argsEl[0]));
      if (method == "quantity_exceeded_with_probability_q") return acb.QuantityExceededWithProbabilityQ(D(argsEl[0]));
      throw new Exception("unknown aggregated_consequences_binned method: " + method);
    }

    // StudyAreaConsequencesBinned (Phase 4 Task 4) is the collection wrapper over
    // per-asset-category AggregatedConsequencesBinned results -- built from
    // patched/StudyAreaConsequencesBinned.cs + patched/ConsequenceExtensions.cs (see those files'
    // headers for the patch rationale: XML/quantile-result methods dropped as genuine compile
    // blockers, GetAggregateEmpiricalDistribution/SampleMeanDamage/ConsequenceExceededWithProbabilityQ
    // dropped to match the C++ port's scope). `construct` is {damage_category, impact_area_id,
    // convergence: {min_iterations, max_iterations}, asset_categories: [...]}; one
    // AggregatedConsequencesBinned is built per asset_category (ConsequenceType.Damage/
    // RiskType.Fail, matching GetConsequenceResult's own defaults), collected into a
    // StudyAreaConsequencesBinned via its (List<AggregatedConsequencesBinned>) ctor.
    // `consequence_results` is a list of {iteration, increments: [[structureDamage, contentDamage,
    // vehicleDamage, otherDamage], ...]}; each entry builds a fresh ConsequenceResult(damageCategory),
    // replays every increment tuple via IncrementConsequence (same convention as
    // EvalConsequenceResult), then feeds it through the stage-damage AddConsequenceRealization(
    // ConsequenceResult, damageCategory, impactAreaID, iteration) overload. PutDataIntoHistograms()
    // runs once after every consequence_results entry is applied, then ToUncertainPairedData(xs,
    // [study], impactAreaID) is called once per assertion (fresh per assertion, matching
    // run_study_area_consequences_binned in test_fixtures.cpp). `args[0]` indexes into the
    // construct's asset_categories list (GetDamageCategories/GetAssetCategories walk
    // ConsequenceResultList in construction order, so this indexing is deterministic). `method`
    // dispatches to_uncertain_paired_data_damage_yvals or to_uncertain_paired_data_quantity_yvals,
    // each returning SamplePairedData(0, true)'s (deterministic, monotonicity-forced) Yvals.
    static object EvalStudyAreaConsequencesBinned(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      var conv = c.GetProperty("convergence");
      var cc = new ConvergenceCriteria(conv.GetProperty("min_iterations").GetInt32(), conv.GetProperty("max_iterations").GetInt32());
      string damageCategory = c.GetProperty("damage_category").GetString();
      int impactAreaID = c.GetProperty("impact_area_id").GetInt32();
      var assetCategories = c.GetProperty("asset_categories").EnumerateArray().Select(x => x.GetString()).ToList();
      var results = assetCategories.Select(assetCategory =>
        new AggregatedConsequencesBinned(damageCategory, assetCategory, cc, impactAreaID, ConsequenceType.Damage, RiskType.Fail)).ToList();
      var study = new StudyAreaConsequencesBinned(results);

      foreach (var realization in caseEl.GetProperty("consequence_results").EnumerateArray()) {
        var cr = new ConsequenceResult(damageCategory);
        foreach (var inc in realization.GetProperty("increments").EnumerateArray()) {
          cr.IncrementConsequence(D(inc[0]), D(inc[1]), D(inc[2]), D(inc[3]));
        }
        study.AddConsequenceRealization(cr, damageCategory, impactAreaID, realization.GetProperty("iteration").GetInt32());
      }
      study.PutDataIntoHistograms();

      double[] xs = DA(caseEl.GetProperty("xs"));
      var (damageUpds, quantityUpds) = StudyAreaConsequencesBinned.ToUncertainPairedData(xs.ToList(), new List<StudyAreaConsequencesBinned> { study }, impactAreaID);

      int assetIndex = (int)D(argsEl[0]);
      if (method == "to_uncertain_paired_data_damage_yvals") return damageUpds[assetIndex].SamplePairedData(0, true).Yvals.ToArray();
      if (method == "to_uncertain_paired_data_quantity_yvals") return quantityUpds[assetIndex].SamplePairedData(0, true).Yvals.ToArray();
      throw new Exception("unknown study_area_consequences_binned method: " + method);
    }

    static void Main() {
      string fixturesDir = Environment.GetEnvironmentVariable("HECFDA_FIXTURES");
      if (string.IsNullOrEmpty(fixturesDir)) {
        var d = AppContext.BaseDirectory;
        // walk up to repo root (contains fixtures/)
        var dir = new DirectoryInfo(d);
        while (dir != null && !Directory.Exists(Path.Combine(dir.FullName,"fixtures"))) dir = dir.Parent;
        fixturesDir = dir == null ? "fixtures" : Path.Combine(dir.FullName,"fixtures");
      }
      var results = new List<object>();
      foreach (var file in Directory.EnumerateFiles(fixturesDir, "*.json", SearchOption.AllDirectories)) {
        using var doc = JsonDocument.Parse(File.ReadAllText(file));
        var root = doc.RootElement;
        if (!root.TryGetProperty("target", out var tEl)) continue;
        string target = tEl.GetString();
        string rel = Path.GetRelativePath(fixturesDir, file).Replace('\\','/');
        int ci = 0;
        foreach (var c in root.GetProperty("cases").EnumerateArray()) {
          string caseName = c.TryGetProperty("name", out var nm) ? nm.GetString() : ci.ToString();
          int ai = 0;
          foreach (var a in c.GetProperty("assertions").EnumerateArray()) {
            string method = a.GetProperty("method").GetString();
            var argsEl = a.GetProperty("args");
            object val;
            switch (target) {
              case "dotnet_random":
              case "rng_digest":
                val = EvalRng(method, c.GetProperty("construct").GetProperty("seed").GetInt32(), argsEl); break;
              case "distribution": val = EvalDistribution(c, method, argsEl); break;
              case "shifted_gamma": val = EvalShiftedGamma(c, method, argsEl); break;
              case "pearson3": val = EvalPearson3(c, method, argsEl); break;
              case "empirical": val = EvalEmpirical(c, method, argsEl); break;
              case "convergence_criteria": val = EvalConvergenceCriteria(c, method, argsEl); break;
              case "histogram": val = EvalHistogram(c, method, argsEl); break;
              case "paired_data": val = EvalPaired(c, method, argsEl); break;
              case "special_functions": val = EvalSpecial(method, argsEl); break;
              case "sample_statistics": val = EvalSampleStatistics(c.GetProperty("construct"), method); break;
              case "uncertain_paired_data": val = EvalUpd(c, method, argsEl); break;
              case "interpolate_quantiles": val = EvalInterpolateQuantiles(c, method, argsEl); break;
              case "graphical_calculators": val = EvalGraphicalCalculators(c, method, argsEl); break;
              case "graphical_uncertain_paired_data": val = EvalGupd(c, method, argsEl); break;
              case "value_uncertainty": val = EvalValueUncertainty(c, method, argsEl); break;
              case "value_ratio_with_uncertainty": val = EvalValueRatioWithUncertainty(c, method, argsEl); break;
              case "first_floor_elevation_uncertainty": val = EvalFirstFloorElevationUncertainty(c, method, argsEl); break;
              case "occupancy_type": val = EvalOccupancyType(c, a, method, argsEl); break;
              case "structure": val = EvalStructure(c, method, argsEl); break;
              case "inventory": val = EvalInventory(c, a, method, argsEl); break;
              case "inventory_compute_damages": val = EvalInventoryComputeDamages(c, method); break;
              case "consequence_result": val = EvalConsequenceResult(c, method); break;
              case "aggregated_consequences_binned": val = EvalAggregatedConsequencesBinned(c, method, argsEl); break;
              case "study_area_consequences_binned": val = EvalStudyAreaConsequencesBinned(c, method, argsEl); break;
              default: continue;
            }
            results.Add(new Dictionary<string,object>{
              {"file", rel}, {"case", caseName}, {"assertion", ai}, {"method", method}, {"value", val}
            });
            ai++;
          }
          ci++;
        }
      }
      var opts = new JsonSerializerOptions { WriteIndented = false };
      Console.WriteLine(JsonSerializer.Serialize(results, opts));
    }
  }
}
