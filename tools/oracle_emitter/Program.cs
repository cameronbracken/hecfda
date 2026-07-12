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
using HEC.FDA.Model.alternatives;
using HEC.FDA.Model.hydraulics;
using HEC.FDA.Model.stageDamage;
using HEC.FDA.Model.scenarios;

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
    // Empirical::stack_empirical_distributions (sum/subtract) + DynamicHistogram::
    // ConvertToEmpiricalDistribution (Phase 6 Task 1 un-severance) against the real C#. `construct`
    // is either `{"op": "sum"|"subtract", "distributions": [{"probabilities":[...], "values":[...],
    // "sample_mean": m}, ...]}` (stacking cases, mirroring run_empirical_stacking in
    // test_fixtures.cpp: build each Empirical via the two-array ctor, then set SampleMean
    // explicitly since neither ctor assigns it) or `{"histogram": {"bin_width": w, "data": [...],
    // "added": [...]}}` (built the same way as EvalHistogram above).
    static object EvalEmpiricalStacking(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      Empirical result;
      if (c.TryGetProperty("histogram", out var h)) {
        var hist = new DynamicHistogram(h.GetProperty("bin_width").GetDouble(), new ConvergenceCriteria());
        hist.AddObservationsToHistogram(DA(h.GetProperty("data")));
        if (h.TryGetProperty("added", out var added)) {
          foreach (var x in added.EnumerateArray()) hist.AddObservationToHistogram(x.GetDouble());
        }
        result = DynamicHistogram.ConvertToEmpiricalDistribution(hist);
      } else {
        var dists = new List<Empirical>();
        foreach (var d in c.GetProperty("distributions").EnumerateArray()) {
          var e = new Empirical(DA(d.GetProperty("probabilities")), DA(d.GetProperty("values")));
          e.SampleMean = d.GetProperty("sample_mean").GetDouble();
          dists.Add(e);
        }
        string op = c.GetProperty("op").GetString();
        Func<double, double, double> addOrSubtract = op == "sum" ? Empirical.Sum : Empirical.Subtract;
        result = Empirical.StackEmpiricalDistributions(dists, addOrSubtract);
      }
      if (method == "sample_mean") return result.SampleMean;
      if (method == "inverse_cdf") return result.InverseCDF(D(argsEl[0]));
      throw new Exception("unknown empirical_stacking method: " + method);
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

    // HydraulicProfiles (Phase 4 Task 5): the hydraulics-as-arrays input boundary +
    // CorrectDryStructureWSEs. The patched/HydraulicDataset.cs static methods reproduce the
    // pure-numeric correction with no disk-backed IHydraulicProfile objects. `construct` is
    // {probabilities, ground_elevations, wses_by_profile} ([profile][structure] raw WSEs).
    // "profile_probabilities" (args []) returns `probabilities` unchanged (an ordering sanity
    // check); "get_corrected_wses" (args []) runs HydraulicDataset.CorrectAllProfiles (the
    // per-profile driving loop from GetHydraulicDatasetInFloatsWithProbabilities, calling
    // CorrectDryStructureWSEs against each next profile, then the last profile against
    // groundElevs alone) and returns the corrected [profile][structure] matrix as nested
    // double[][] (JSON nested arrays, matching the fixture's "matrix" mode expected shape).
    static object EvalHydraulicProfiles(JsonElement caseEl, string method) {
      var c = caseEl.GetProperty("construct");
      double[] probabilities = DA(c.GetProperty("probabilities"));
      if (method == "profile_probabilities") return probabilities;
      if (method == "get_corrected_wses") {
        var waterData = new List<float[]>();
        foreach (var pf in c.GetProperty("wses_by_profile").EnumerateArray()) {
          waterData.Add(pf.EnumerateArray().Select(x => (float)x.GetDouble()).ToArray());
        }
        float[] groundElevs = c.GetProperty("ground_elevations").EnumerateArray()
            .Select(x => (float)x.GetDouble()).ToArray();
        var corrected = HydraulicDataset.CorrectAllProfiles(waterData, groundElevs);
        return corrected.Select(row => row.Select(v => (double)v).ToArray()).ToArray();
      }
      throw new Exception("unknown hydraulic_profiles method: " + method);
    }

    // ImpactAreaStageDamage GEOMETRY (Phase 4 Task 6) -- built from patched/ImpactAreaStageDamage.cs
    // (see that file's header for the patch rationale: HydraulicDataset -> List<double>
    // _ProfileProbabilities, MVVM base + ReportMessage severed to thrown exceptions, Compute()/CSV
    // methods dropped as out of this task's scope). Two case shapes: (a) 'extrapolate_from_above'/
    // 'extrapolate_from_below' call the two public static helpers directly, no ImpactAreaStageDamage
    // construction. (b) 'tractable_geometry' builds a tractable 2-structure Residential Inventory
    // (mirroring TractableStageDamageTests.cs's residential occ-type/structure data -- content is
    // NOT read by any geometry method, see fixtures/stage_damage/stage_damage_geometry.json's note)
    // + a graphical STAGE frequency (UsingStagesNotFlows=true) over `probabilities`/
    // `graphical_stages` with `equivalent_record_length`, and mock per-profile WSEs built the same
    // way TractableStageDamageTests.ComputeStagesAtStructures does (profile 0 = {hydraulic_stage1,
    // hydraulic_stage2}, each subsequent profile = previous + 1), one profile per `probabilities`
    // entry -- feeding `_ProfileProbabilities` directly with `probabilities` (this patch's
    // HydraulicDataset replacement).
    static Inventory MakeTractableResidentialInventory(int impactAreaID) {
      double[] depths = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
      IDistribution[] structDamages = { new Deterministic(0), new Deterministic(10), new Deterministic(20), new Deterministic(30), new Deterministic(40), new Deterministic(50), new Deterministic(60), new Deterministic(70), new Deterministic(80), new Deterministic(90), new Deterministic(100) };
      IDistribution[] contentDamages = { new Deterministic(0), new Deterministic(5), new Deterministic(15), new Deterministic(25), new Deterministic(35), new Deterministic(45), new Deterministic(55), new Deterministic(65), new Deterministic(75), new Deterministic(85), new Deterministic(95) };
      var md = new CurveMetaData("x", "y", "oracle");
      var structUpd = new UncertainPairedData(depths, structDamages, md);
      var contentUpd = new UncertainPairedData(depths, contentDamages, md);

      var residential = OccupancyType.Builder()
        .WithName("Residential")
        .WithDamageCategory("Residential")
        .WithStructureDepthPercentDamage(structUpd)
        .WithContentDepthPercentDamage(contentUpd)
        .WithContentToStructureValueRatio(new ValueRatioWithUncertainty(50))
        .Build();

      var occTypes = new Dictionary<string, OccupancyType> { { "Residential", residential } };
      var structures = new List<Structure> {
        new Structure("1", firstFloorElevation: 14, val_struct: 100, st_damcat: "Residential", occtype: "Residential", impactAreaID: impactAreaID, groundElevation: 12),
        new Structure("2", firstFloorElevation: 15, val_struct: 200, st_damcat: "Residential", occtype: "Residential", impactAreaID: impactAreaID, groundElevation: 12),
      };
      return new Inventory(occTypes, structures);
    }

    static object EvalStageDamageGeometry(JsonElement caseEl, string method, JsonElement argsEl) {
      if (method == "extrapolate_from_above") {
        float[] input = argsEl[0].EnumerateArray().Select(x => (float)x.GetDouble()).ToArray();
        float upperInterval = (float)D(argsEl[1]);
        int stepCount = argsEl[2].GetInt32();
        return ImpactAreaStageDamage.ExtrapolateFromAboveAtIndexLocation(input, upperInterval, stepCount).Select(v => (double)v).ToArray();
      }
      if (method == "extrapolate_from_below") {
        float[] input = argsEl[0].EnumerateArray().Select(x => (float)x.GetDouble()).ToArray();
        float interval = (float)D(argsEl[1]);
        int i = argsEl[2].GetInt32();
        int numInterpolated = argsEl[3].GetInt32();
        return ImpactAreaStageDamage.ExtrapolateFromBelowStagesAtIndexLocation(input, interval, i, numInterpolated).Select(v => (double)v).ToArray();
      }

      var c = caseEl.GetProperty("construct");
      int impactAreaID = c.GetProperty("impact_area_id").GetInt32();
      double[] probabilities = DA(c.GetProperty("probabilities"));
      double[] graphicalStages = DA(c.GetProperty("graphical_stages"));
      int erl = c.GetProperty("equivalent_record_length").GetInt32();
      float stage1 = (float)c.GetProperty("hydraulic_stage1").GetDouble();
      float stage2 = (float)c.GetProperty("hydraulic_stage2").GetDouble();

      var stageFrequencyMd = new CurveMetaData("probability", "stages", "graphical stage frequency");
      var stageFrequency = new GraphicalUncertainPairedData(probabilities, graphicalStages, erl, stageFrequencyMd, usingStagesNotFlows: true);

      var inventory = MakeTractableResidentialInventory(impactAreaID);
      // hydraulic_stage1/hydraulic_stage2 (mirroring TractableStageDamageTests.
      // ComputeStagesAtStructures's mock per-structure WSEs) are accepted above for fixture-shape
      // parity with the C++ test's mock HydraulicProfiles construction, but are NOT read here:
      // no geometry method (verified against the C# source) reads per-profile WSE values, only
      // _ProfileProbabilities -- see patched/ImpactAreaStageDamage.cs's header.
      _ = stage1;
      _ = stage2;

      var impactAreaStageDamage = new ImpactAreaStageDamage(impactAreaID, inventory, probabilities.ToList(), new List<float[]>(), analysisYear: 9999,
        analyticalFlowFrequency: null, graphicalFrequency: stageFrequency, dischargeStage: null, unregulatedRegulated: null, usingMockData: true);

      if (method == "compute_stages_at_index_location") return impactAreaStageDamage.ComputeStagesAtIndexLocation(probabilities.ToList());
      if (method == "bottom_extrapolation_points") return (double)impactAreaStageDamage.BottomExtrapolationPoints;
      if (method == "central_interpolation_points") return (double)impactAreaStageDamage.CentralInterpolationPoints;
      if (method == "top_extrapolation_points") return (double)impactAreaStageDamage.TopExtrapolationPoints;
      if (method == "min_stage_for_area") return impactAreaStageDamage.MinStageForArea;
      if (method == "max_stage_for_area") return impactAreaStageDamage.MaxStageForArea;
      throw new Exception("unknown stage_damage_geometry method: " + method);
    }

    // ImpactAreaStageDamage.Compute() (Phase 4 Task 7, the headline oracle) -- reproduces
    // TractableStageDamageTests.TrackStageDamageTest. MakeTractableCommercialInventory mirrors
    // MakeTractableResidentialInventory above (fid 3/4, FFE 17/18, val 300/400, Commercial occ type
    // CSVR 120, reusing residentialContentAndCommercialStructureDamage {0,5,15,...,95} as the
    // COMMERCIAL STRUCTURE curve, matching TractableStageDamageTests' array reuse verbatim).
    static Inventory MakeTractableCommercialInventory(int impactAreaID) {
      double[] depths = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
      IDistribution[] structDamages = { new Deterministic(0), new Deterministic(5), new Deterministic(15), new Deterministic(25), new Deterministic(35), new Deterministic(45), new Deterministic(55), new Deterministic(65), new Deterministic(75), new Deterministic(85), new Deterministic(95) };
      IDistribution[] contentDamages = { new Deterministic(0), new Deterministic(0), new Deterministic(10), new Deterministic(20), new Deterministic(30), new Deterministic(40), new Deterministic(50), new Deterministic(60), new Deterministic(70), new Deterministic(80), new Deterministic(90) };
      var md = new CurveMetaData("x", "y", "oracle");
      var structUpd = new UncertainPairedData(depths, structDamages, md);
      var contentUpd = new UncertainPairedData(depths, contentDamages, md);

      var commercial = OccupancyType.Builder()
        .WithName("Commercial")
        .WithDamageCategory("Commercial")
        .WithStructureDepthPercentDamage(structUpd)
        .WithContentDepthPercentDamage(contentUpd)
        .WithContentToStructureValueRatio(new ValueRatioWithUncertainty(120))
        .Build();

      var occTypes = new Dictionary<string, OccupancyType> { { "Commercial", commercial } };
      var structures = new List<Structure> {
        new Structure("3", firstFloorElevation: 17, val_struct: 300, st_damcat: "Commercial", occtype: "Commercial", impactAreaID: impactAreaID, groundElevation: 12),
        new Structure("4", firstFloorElevation: 18, val_struct: 400, st_damcat: "Commercial", occtype: "Commercial", impactAreaID: impactAreaID, groundElevation: 12),
      };
      return new Inventory(occTypes, structures);
    }

    // Shared tractable-ImpactAreaStageDamage construction helper (Phase 4 Task 7 originally,
    // factored out in Task 8 so EvalScenarioStageDamage's `construct.impact_areas` list can build
    // each entry the identical way EvalImpactAreaStageDamage does). `iaEl` is one
    // {impact_area_id, damage_category, hydraulic_stage1, hydraulic_stage2, use_reg_unreg} object
    // (asset_category is a selection key, not a construction input, so it is read by the caller,
    // not here).
    static ImpactAreaStageDamage BuildImpactAreaStageDamageFromJson(JsonElement iaEl) {
      int impactAreaID = iaEl.GetProperty("impact_area_id").GetInt32();
      string damageCategory = iaEl.GetProperty("damage_category").GetString();
      float stage1 = (float)iaEl.GetProperty("hydraulic_stage1").GetDouble();
      float stage2 = (float)iaEl.GetProperty("hydraulic_stage2").GetDouble();
      bool useRegUnreg = iaEl.GetProperty("use_reg_unreg").GetBoolean();

      double[] probabilities = { .5, .2, .1, .04, .02, .01, .004, .002 };
      var inventory = damageCategory == "Residential" ? MakeTractableResidentialInventory(impactAreaID) : MakeTractableCommercialInventory(impactAreaID);
      var wsesByProfile = ComputeStagesAtStructures(stage1, stage2, probabilities);

      double[] graphicalStages = { 12, 13, 14, 15, 16, 17, 18, 19 };
      var stageFrequencyMd = new CurveMetaData("probability", "stages", "graphical stage frequency");
      var stageFrequency = new GraphicalUncertainPairedData(probabilities, graphicalStages, 50, stageFrequencyMd, usingStagesNotFlows: true);

      double[] inflows = { 1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900 };
      var flowFrequencyMd = new CurveMetaData("probability", "discharge", "graphical flow frequency");
      var flowFrequency = new GraphicalUncertainPairedData(probabilities, inflows, 50, flowFrequencyMd, usingStagesNotFlows: false);

      IDistribution[] outflows = { new Deterministic(120), new Deterministic(130), new Deterministic(140), new Deterministic(150), new Deterministic(160), new Deterministic(170), new Deterministic(180), new Deterministic(190) };
      var unregRegMd = new CurveMetaData("unregulated", "regulated", "reg unreg function");
      var unregReg = new UncertainPairedData(inflows, outflows, unregRegMd);

      double[] flows = { 120, 130, 140, 150, 160, 170, 180, 190 };
      IDistribution[] stages = { new Deterministic(12), new Deterministic(13), new Deterministic(14), new Deterministic(15), new Deterministic(16), new Deterministic(17), new Deterministic(18), new Deterministic(19) };
      var dischargeStageMd = new CurveMetaData("discharge", "stage", "stage discharge function");
      var dischargeStage = new UncertainPairedData(flows, stages, dischargeStageMd);

      return new ImpactAreaStageDamage(impactAreaID, inventory, probabilities.ToList(), wsesByProfile, analysisYear: 9999,
        analyticalFlowFrequency: null,
        graphicalFrequency: useRegUnreg ? flowFrequency : stageFrequency,
        dischargeStage: useRegUnreg ? dischargeStage : null,
        unregulatedRegulated: useRegUnreg ? unregReg : null,
        usingMockData: true);
    }

    static object EvalImpactAreaStageDamage(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      string damageCategory = c.GetProperty("damage_category").GetString();
      string assetCategory = c.GetProperty("asset_category").GetString();
      var impactAreaStageDamage = BuildImpactAreaStageDamageFromJson(c);

      var (damageUpds, _) = impactAreaStageDamage.Compute(computeIsDeterministic: true);
      UncertainPairedData target = damageUpds.FirstOrDefault(u => u.CurveMetaData.DamageCategory == damageCategory && u.CurveMetaData.AssetCategory == assetCategory);
      if (target == null) throw new Exception($"impact_area_stage_damage: no UncertainPairedData for damage_category={damageCategory}, asset_category={assetCategory}");
      IPairedData sampled = target.SamplePairedData(iterationNumber: 1, retrieveDeterministicRepresentation: true);

      if (method == "f") return sampled.f(D(argsEl[0]));
      throw new Exception("unknown impact_area_stage_damage method: " + method);
    }

    // ScenarioStageDamage.Compute() (Phase 4 Task 8) -- the thin outer loop wrapping Task 7's
    // ImpactAreaStageDamage.Compute() across every impact area, built from
    // patched/ScenarioStageDamage.cs (CSV/ProgressReporter/Stopwatch dropped, the Compute() loop
    // itself verbatim -- see that file's header). `construct.impact_areas` is a list of the same
    // shape EvalImpactAreaStageDamage's `construct` uses, one entry per impact area, each built via
    // the shared BuildImpactAreaStageDamageFromJson helper above. `select` (case-level default,
    // overridable per assertion via the assertion's own `select`) is
    // {impact_area_id, damage_category, asset_category}, identifying which UncertainPairedData in
    // the concatenated Item1 (damage) result list `method: 'f'` (args [stage]) samples and
    // evaluates. `method: 'result_count'` (args []) returns the total Item1 list length, confirming
    // the AddRange-equivalent concatenation across impact areas.
    static object EvalScenarioStageDamage(JsonElement caseEl, JsonElement assertionEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      var impactAreaStageDamages = c.GetProperty("impact_areas").EnumerateArray()
          .Select(BuildImpactAreaStageDamageFromJson).ToList();
      var scenario = new ScenarioStageDamage(impactAreaStageDamages);
      var (damageUpds, _) = scenario.Compute(computeIsDeterministic: true);

      if (method == "result_count") return (double)damageUpds.Count;
      if (method == "f") {
        JsonElement sel = assertionEl.TryGetProperty("select", out var selEl) ? selEl : caseEl.GetProperty("select");
        int impactAreaID = sel.GetProperty("impact_area_id").GetInt32();
        string damageCategory = sel.GetProperty("damage_category").GetString();
        string assetCategory = sel.GetProperty("asset_category").GetString();
        UncertainPairedData target = damageUpds.FirstOrDefault(u =>
            u.CurveMetaData.ImpactAreaID == impactAreaID &&
            u.CurveMetaData.DamageCategory == damageCategory &&
            u.CurveMetaData.AssetCategory == assetCategory);
        if (target == null) throw new Exception($"scenario_stage_damage: no UncertainPairedData for impact_area_id={impactAreaID}, damage_category={damageCategory}, asset_category={assetCategory}");
        IPairedData sampled = target.SamplePairedData(iterationNumber: 1, retrieveDeterministicRepresentation: true);
        return sampled.f(D(argsEl[0]));
      }
      throw new Exception("unknown scenario_stage_damage method: " + method);
    }

    // ported from: TractableStageDamageTests.cs's ComputeStagesAtStructures(stage1, stage2):
    // profile 0 = {stage1, stage2}; each subsequent profile's per-structure WSE = previous + 1.
    // One profile per `probabilities` entry.
    static List<float[]> ComputeStagesAtStructures(float stage1, float stage2, double[] probabilities) {
      List<float[]> stages = new() { new float[] { stage1, stage2 } };
      for (int i = 0; i < probabilities.Length - 1; i++) {
        float[] previous = stages[i];
        stages.Add(new float[] { previous[0] + 1, previous[1] + 1 });
      }
      return stages;
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

    // AggregatedConsequencesByQuantile (Phase 6 Task 2) is the Empirical-backed quantile-result
    // leaf, sibling to AggregatedConsequencesBinned above -- built straight from the real
    // AggregatedConsequencesByQuantile.cs (compiled unpatched, see oracle_emitter.csproj's Task
    // Phase6T2 comment: it's a clean POCO, no MVVM/XML surface). `construct` is
    // {damage_category, asset_category, empirical: {probabilities, values}, impact_area_id,
    // consequence_type, risk_type}, matching the (string, string, Empirical, int, ConsequenceType,
    // RiskType) ctor; the Empirical is built via the two-array (probabilities, values) ctor, same
    // as EvalEmpirical above. `method` dispatches ConsequenceSampleMean (args []) or
    // ConsequenceExceededWithProbabilityQ (args [q]) -- both `internal`, but AggregatedConsequences
    // ByQuantile.cs is compiled directly into THIS project via a Compile Include (same assembly),
    // so no reflection is needed (mirrors EvalAggregatedConsequencesBinned).
    static object EvalAggregatedConsequencesByQuantile(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      var emp = c.GetProperty("empirical");
      var empirical = new Empirical(DA(emp.GetProperty("probabilities")), DA(emp.GetProperty("values")));
      var consequenceType = Enum.Parse<ConsequenceType>(c.GetProperty("consequence_type").GetString());
      var riskType = Enum.Parse<RiskType>(c.GetProperty("risk_type").GetString());
      var acq = new AggregatedConsequencesByQuantile(c.GetProperty("damage_category").GetString(), c.GetProperty("asset_category").GetString(), empirical, c.GetProperty("impact_area_id").GetInt32(), consequenceType, riskType);
      if (method == "consequence_sample_mean") return acq.ConsequenceSampleMean();
      if (method == "consequence_exceeded_with_probability_q") return acq.ConsequenceExceededWithProbabilityQ(D(argsEl[0]));
      throw new Exception("unknown aggregated_consequences_by_quantile method: " + method);
    }

    // StudyAreaConsequencesByQuantile (Phase 6 Task 3) is the collection wrapper over
    // AggregatedConsequencesByQuantile results, ByQuantile sibling of StudyAreaConsequencesBinned
    // above -- built straight from the real StudyAreaConsequencesByQuantile.cs (compiled
    // unpatched, see oracle_emitter.csproj's Task Phase6T3 comment: Validation/MessageReport are
    // already reachable transitively, same as OccupancyType.cs). `construct` is
    // {consequence_results: [{damage_category, asset_category, impact_area_id, consequence_type,
    // risk_type, empirical: {probabilities, values}, sample_mean}, ...]}; one
    // AggregatedConsequencesByQuantile is built per entry (list order), with Empirical.SampleMean
    // force-set to the entry's `sample_mean` field (SampleMean is external bookkeeping, always 0
    // straight out of the two-array ctor). An optional `add_existing` list of the same per-entry
    // shape is then applied via AddExistingConsequenceResultObject(...) in order. `method`
    // dispatches SampleMeanDamage (args [damageCategory, assetCategory, impactAreaID]) or a thin
    // GetAggregateEmpiricalDistribution(...).SampleMean read (same args).
    static AggregatedConsequencesByQuantile BuildAggregatedConsequencesByQuantileEntry(JsonElement entry) {
      var emp = entry.GetProperty("empirical");
      var empirical = new Empirical(DA(emp.GetProperty("probabilities")), DA(emp.GetProperty("values")));
      empirical.SampleMean = D(entry.GetProperty("sample_mean"));
      var consequenceType = Enum.Parse<ConsequenceType>(entry.GetProperty("consequence_type").GetString());
      var riskType = Enum.Parse<RiskType>(entry.GetProperty("risk_type").GetString());
      return new AggregatedConsequencesByQuantile(entry.GetProperty("damage_category").GetString(), entry.GetProperty("asset_category").GetString(), empirical, entry.GetProperty("impact_area_id").GetInt32(), consequenceType, riskType);
    }
    static string OptionalString(JsonElement e) => e.ValueKind == JsonValueKind.Null ? null : e.GetString();
    static object EvalStudyAreaConsequencesByQuantile(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      var results = c.GetProperty("consequence_results").EnumerateArray().Select(BuildAggregatedConsequencesByQuantileEntry).ToList();
      var study = new StudyAreaConsequencesByQuantile(results);
      if (caseEl.TryGetProperty("add_existing", out var addExisting)) {
        foreach (var entry in addExisting.EnumerateArray()) {
          study.AddExistingConsequenceResultObject(BuildAggregatedConsequencesByQuantileEntry(entry));
        }
      }
      string damageCategory = OptionalString(argsEl[0]);
      string assetCategory = OptionalString(argsEl[1]);
      int impactAreaID = argsEl[2].GetInt32();
      if (method == "sample_mean_damage") return study.SampleMeanDamage(damageCategory, assetCategory, impactAreaID);
      if (method == "get_aggregate_empirical_distribution_sample_mean") return study.GetAggregateEmpiricalDistribution(damageCategory, assetCategory, impactAreaID).SampleMean;
      throw new Exception("unknown study_area_consequences_by_quantile method: " + method);
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

    // Binned->quantile converters (Phase 6 Task 4): AggregatedConsequencesBinned.
    // ConvertToSingleEmpiricalDistributionOfConsequences + StudyAreaConsequencesBinned.
    // ConvertToStudyAreaConsequencesByQuantile, restored in patched/AggregatedConsequencesBinned.cs
    // + patched/StudyAreaConsequencesBinned.cs (see those files' updated headers). `construct` (per
    // consequence_results entry) is {damage_category, asset_category, convergence: {min_iterations,
    // max_iterations}, impact_area_id, consequence_type, risk_type}, exactly
    // EvalAggregatedConsequencesBinned's construct shape; `realizations` (per entry) is staged via
    // AddConsequenceRealization then a single PutDataIntoHistogram(), one AggregatedConsequencesBinned
    // per entry, collected into a StudyAreaConsequencesBinned. `filter_consequence_type` feeds
    // ConvertToStudyAreaConsequencesByQuantile directly. `method` dispatches SampleMeanDamage on the
    // resulting StudyAreaConsequencesByQuantile; `args` is [damage_category_or_null,
    // asset_category_or_null, impact_area_id], matching EvalStudyAreaConsequencesByQuantile's
    // convention, plus an OPTIONAL 4th element: a ConsequenceType name to pass as SampleMeanDamage's
    // own consequenceType query parameter (default Damage when omitted). This is distinct from
    // filter_consequence_type -- it lets an assertion query the CONVERTED collection for a type the
    // case claims was excluded during conversion, so an exclusion claim is actually falsifiable
    // (a leaked entry would surface as a nonzero sum instead of the expected 0).
    static AggregatedConsequencesBinned BuildStagedAggregatedConsequencesBinned(JsonElement entry) {
      var c = entry.GetProperty("construct");
      var conv = c.GetProperty("convergence");
      var cc = new ConvergenceCriteria(conv.GetProperty("min_iterations").GetInt32(), conv.GetProperty("max_iterations").GetInt32());
      var consequenceType = Enum.Parse<ConsequenceType>(c.GetProperty("consequence_type").GetString());
      var riskType = Enum.Parse<RiskType>(c.GetProperty("risk_type").GetString());
      var acb = new AggregatedConsequencesBinned(c.GetProperty("damage_category").GetString(), c.GetProperty("asset_category").GetString(), cc, c.GetProperty("impact_area_id").GetInt32(), consequenceType, riskType);
      foreach (var r in entry.GetProperty("realizations").EnumerateArray()) {
        acb.AddConsequenceRealization(D(r.GetProperty("damage")), r.GetProperty("iteration").GetInt64(), r.GetProperty("count").GetInt32());
      }
      acb.PutDataIntoHistogram();
      return acb;
    }
    static object EvalBinnedToQuantile(JsonElement caseEl, string method, JsonElement argsEl) {
      var results = caseEl.GetProperty("consequence_results").EnumerateArray().Select(BuildStagedAggregatedConsequencesBinned).ToList();
      var study = new StudyAreaConsequencesBinned(results);
      var filterType = Enum.Parse<ConsequenceType>(caseEl.GetProperty("filter_consequence_type").GetString());
      var quantileStudy = StudyAreaConsequencesBinned.ConvertToStudyAreaConsequencesByQuantile(study, filterType);
      string damageCategory = OptionalString(argsEl[0]);
      string assetCategory = OptionalString(argsEl[1]);
      int impactAreaID = argsEl[2].GetInt32();
      // args[3] is an optional ConsequenceType override for the query itself (distinct from
      // filter_consequence_type, which controls the conversion), defaulting to Damage to match
      // SampleMeanDamage's own default and keep pre-existing 3-arg assertions unchanged. Lets a
      // case query the CONVERTED collection for a type it claims was excluded during conversion.
      var queryConsequenceType = argsEl.GetArrayLength() > 3 ? Enum.Parse<ConsequenceType>(argsEl[3].GetString()) : ConsequenceType.Damage;
      if (method == "sample_mean_damage") return quantileStudy.SampleMeanDamage(damageCategory, assetCategory, impactAreaID, queryConsequenceType);
      throw new Exception("unknown binned_to_quantile method: " + method);
    }

    // CategoriedPairedData + CategoriedUncertainPairedData (Phase 5 Task 4) is the per-
    // (damageCategory, assetCategory, ConsequenceType, RiskType) damage/FN-frequency curve
    // accumulator -- built from patched/CategoriedUncertainPairedData.cs (see that file's header
    // for why it's a patched local copy: WriteToXML/ReadFromXML dropped, everything else
    // verbatim); CategoriedPairedData.cs (21 lines, no XML/MVVM) compiles unpatched. `construct`
    // is EITHER {xvals, damage_category, asset_category, consequence_type, risk_type, convergence:
    // {min_iterations, max_iterations}} (the 6-arg compute ctor) OR {initial_curve: {xvals, yvals,
    // damage_category, asset_category, consequence_type, risk_type}, convergence: {min_iterations,
    // max_iterations}} (the CategoriedPairedData-delegating ctor). `realization_batches` is a list
    // of batches, each a list of {iteration, yvals}: for every batch, build a fresh
    // PairedData(xvals, yvals) per realization and AddCurveRealization it in order, then
    // PutDataIntoHistograms() exactly once per batch -- one object is built and staged per case,
    // shared across every batch and assertion (mirrors run_categoried_uncertain_paired_data in
    // test_fixtures.cpp). `method` is always sample_paired_data_deterministic_yvals (args []):
    // GetUncertainPairedData().SamplePairedData(1, true).Yvals.
    static object EvalCategoriedUncertainPairedData(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      var conv = c.GetProperty("convergence");
      var cc = new ConvergenceCriteria(conv.GetProperty("min_iterations").GetInt32(), conv.GetProperty("max_iterations").GetInt32());

      CategoriedUncertainPairedData cupd;
      if (c.TryGetProperty("initial_curve", out var icEl)) {
        var initialCurve = new PairedData(DA(icEl.GetProperty("xvals")), DA(icEl.GetProperty("yvals")));
        var initial = new CategoriedPairedData(
          initialCurve,
          icEl.GetProperty("damage_category").GetString(),
          icEl.GetProperty("asset_category").GetString(),
          Enum.Parse<ConsequenceType>(icEl.GetProperty("consequence_type").GetString()),
          Enum.Parse<RiskType>(icEl.GetProperty("risk_type").GetString()));
        cupd = new CategoriedUncertainPairedData(initial, cc);
      } else {
        cupd = new CategoriedUncertainPairedData(
          DA(c.GetProperty("xvals")),
          c.GetProperty("damage_category").GetString(),
          c.GetProperty("asset_category").GetString(),
          Enum.Parse<ConsequenceType>(c.GetProperty("consequence_type").GetString()),
          Enum.Parse<RiskType>(c.GetProperty("risk_type").GetString()),
          cc);
      }

      double[] xvals = cupd.Xvals.ToArray();
      foreach (var batch in caseEl.GetProperty("realization_batches").EnumerateArray()) {
        foreach (var r in batch.EnumerateArray()) {
          var curve = new PairedData(xvals, DA(r.GetProperty("yvals")));
          cupd.AddCurveRealization(curve, r.GetProperty("iteration").GetInt64());
        }
        cupd.PutDataIntoHistograms();
      }

      if (method == "sample_paired_data_deterministic_yvals") return cupd.GetUncertainPairedData().SamplePairedData(1, true).Yvals.ToArray();
      throw new Exception("unknown categoried_uncertain_paired_data method: " + method);
    }

    // AssuranceResultStorage (Phase 5 Task 1) is the histogram-staging Monte Carlo accumulator for
    // one assurance metric -- built from patched/AssuranceResultStorage.cs (see that file's header
    // for why it's a patched local copy: WriteToXML/ReadFromXML and the XML-only private ctor
    // dropped, everything else verbatim). `construct` is {assurance_type, bin_width, convergence:
    // {min_iterations, max_iterations}, standard_non_exceedance_probability}, matching the
    // (string, double, ConvergenceCriteria, double) compute ctor; ConvergenceCriteria uses the
    // 2-arg (minIterations, maxIterations) ctor, same as EvalAggregatedConsequencesBinned.
    // `observations` is a list of {iteration, result} applied in order via AddObservation before a
    // single PutDataIntoHistogram() call; one object is built and staged per case, shared across
    // all of that case's assertions (mirrors run_assurance_result_storage in test_fixtures.cpp).
    // `method` dispatches SampleMean (args []) or InverseCDF (args [p]), both read off
    // AssuranceHistogram (a plain DynamicHistogram property, not IHistogram, so no cast needed).
    static object EvalAssuranceResultStorage(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      var conv = c.GetProperty("convergence");
      var cc = new ConvergenceCriteria(conv.GetProperty("min_iterations").GetInt32(), conv.GetProperty("max_iterations").GetInt32());
      var ars = new AssuranceResultStorage(c.GetProperty("assurance_type").GetString(), D(c.GetProperty("bin_width")), cc, D(c.GetProperty("standard_non_exceedance_probability")));
      foreach (var o in caseEl.GetProperty("observations").EnumerateArray()) {
        ars.AddObservation(D(o.GetProperty("result")), o.GetProperty("iteration").GetInt32());
      }
      ars.PutDataIntoHistogram();
      if (method == "sample_mean") return ars.AssuranceHistogram.SampleMean;
      if (method == "inverse_cdf") return ars.AssuranceHistogram.InverseCDF(D(argsEl[0]));
      throw new Exception("unknown assurance_result_storage method: " + method);
    }

    // SystemPerformanceResults (Phase 5 Task 2) is the system-performance metrics container --
    // built from patched/SystemPerformanceResults.cs (see that file's header for the patch
    // rationale: MVVM base + ReportMessage + XML dropped, everything else verbatim). Three case
    // shapes selected by `construct.case_kind` (see fixtures/metrics/system_performance_results.json's
    // note for the full rationale, matching run_system_performance_results in test_fixtures.cpp
    // case-for-case):
    //  - "aep": SystemPerformanceResults(ConvergenceCriteria), stages `aep_observations`
    //    ({iteration, result}) via AddAEPForAssurance, PutDataIntoHistograms() once, then dispatches
    //    mean_aep/median_aep/long_term_exceedance_probability (args [years]).
    //  - "rng_conformance": the PerformanceTest.AssuranceResultStorageShould pin. Builds
    //    SystemPerformanceResults(ConvergenceCriteria), AddStageAssuranceHistogram(standardProbability),
    //    seeds `iterationCount` master seeds via `new Random(masterSeed).Next()` (matching the real
    //    test's masterSeedList loop -- only the first IterationCount of MinIterations are ever
    //    consumed by the real test's Parallel.For bound, so generating exactly IterationCount
    //    reproduces the identical seed prefix), then for `computeChunks` outer passes: for each
    //    seed, RandomProvider(seed).NextRandom() -> standard Normal InverseCDF ->
    //    AddStageForAssurance(standardProbability, invCDF, i); PutDataIntoHistograms() once per
    //    pass. `method: assurance_of_event` (args []) returns AssuranceOfEvent(standardProbability,
    //    thresholdValue); `method: normal_cdf_reference` (args []) returns the same
    //    standardNormal.CDF(thresholdValue) PerformanceTest.cs compares against, letting the
    //    fixture pin both the exact RNG-seeded value and its theoretical cross-check from one C#
    //    run.
    //  - "levee": builds SystemPerformanceResults(UncertainPairedData systemResponse,
    //    ConvergenceCriteria) from `system_response_xs`/`system_response_ys` (Deterministic-
    //    distribution ys, matching ComputeLeveeAEP_Test's fragility curve), AddStageAssuranceHistogram
    //    (standardProbability), stages `stage_observations` via AddStageForAssurance,
    //    PutDataIntoHistograms() once, then dispatches `assurance_of_event` (args [thresholdValue]
    //    -- ignored by the levee branch, kept for construct-shape parity) through
    //    CalculateAssuranceForLevee.
    static object EvalSystemPerformanceResults(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      string caseKind = c.GetProperty("case_kind").GetString();
      var conv = c.GetProperty("convergence");
      var cc = new ConvergenceCriteria(conv.GetProperty("min_iterations").GetInt32(), conv.GetProperty("max_iterations").GetInt32());

      if (caseKind == "aep") {
        var spr = new SystemPerformanceResults(cc);
        foreach (var o in c.GetProperty("aep_observations").EnumerateArray()) {
          spr.AddAEPForAssurance(D(o.GetProperty("result")), o.GetProperty("iteration").GetInt32());
        }
        spr.PutDataIntoHistograms();
        if (method == "mean_aep") return spr.MeanAEP();
        if (method == "median_aep") return spr.MedianAEP();
        if (method == "long_term_exceedance_probability") return spr.LongTermExceedanceProbability((int)D(argsEl[0]));
        throw new Exception("unknown system_performance_results (aep) method: " + method);
      }

      if (caseKind == "rng_conformance") {
        double standardProbability = c.GetProperty("standard_probability").GetDouble();
        int masterSeed = c.GetProperty("master_seed").GetInt32();
        double thresholdValue = c.GetProperty("threshold_value").GetDouble();
        int computeChunks = c.GetProperty("compute_chunks").GetInt32();

        var spr = new SystemPerformanceResults(cc);
        spr.AddStageAssuranceHistogram(standardProbability);

        long iterationCount = cc.IterationCount;
        var masterSeedList = new Random(masterSeed);
        var seeds = new int[iterationCount];
        for (int i = 0; i < iterationCount; i++) seeds[i] = masterSeedList.Next();

        var standardNormal = new Normal();
        for (int j = 0; j < computeChunks; j++) {
          for (int i = 0; i < iterationCount; i++) {
            var threadlocalRandomProvider = new RandomProvider(seeds[i]);
            double invCDF = standardNormal.InverseCDF(threadlocalRandomProvider.NextRandom());
            spr.AddStageForAssurance(standardProbability, invCDF, i);
          }
          spr.PutDataIntoHistograms();
        }
        if (method == "assurance_of_event") return spr.AssuranceOfEvent(standardProbability, thresholdValue);
        if (method == "normal_cdf_reference") return standardNormal.CDF(thresholdValue);
        throw new Exception("unknown system_performance_results (rng_conformance) method: " + method);
      }

      if (caseKind == "levee") {
        double[] xs = DA(c.GetProperty("system_response_xs"));
        double[] ys = DA(c.GetProperty("system_response_ys"));
        IDistribution[] failureProbs = ys.Select(y => (IDistribution)new Deterministic(y)).ToArray();
        var md = new CurveMetaData("x", "y", "oracle");
        var systemResponse = new UncertainPairedData(xs, failureProbs, md);
        double standardProbability = c.GetProperty("standard_probability").GetDouble();

        var spr = new SystemPerformanceResults(systemResponse, cc);
        spr.AddStageAssuranceHistogram(standardProbability);
        foreach (var o in c.GetProperty("stage_observations").EnumerateArray()) {
          spr.AddStageForAssurance(standardProbability, D(o.GetProperty("result")), o.GetProperty("iteration").GetInt32());
        }
        spr.PutDataIntoHistograms();
        if (method == "assurance_of_event") return spr.AssuranceOfEvent(standardProbability, D(argsEl[0]));
        throw new Exception("unknown system_performance_results (levee) method: " + method);
      }
      throw new Exception("unknown system_performance_results case_kind: " + caseKind);
    }

    // PerformanceByThresholds (Phase 5 Task 3) -- the Threshold container. Built from
    // patched/Threshold.cs + patched/PerformanceByThresholds.cs (see those files' headers for the
    // patch rationale: XML/MVVM dropped, ctors/AddThreshold/GetThreshold/Equals kept VERBATIM) plus
    // the real (unpatched) ThresholdEnum. `construct.thresholds` is a list of {id, type
    // (ThresholdEnum name string), value, convergence}; each is built via the (id,
    // ConvergenceCriteria, ThresholdEnum, value) ctor and AddThreshold'd in list order.
    // `construct.get_threshold_id` selects which one GetThreshold retrieves; `threshold_value`/
    // `threshold_type`/`threshold_id` read straight off that retrieved Threshold (plain ctor-
    // assigned data, not oracle math). `construct.aep_observations` ({iteration, result}) are then
    // fed into the retrieved Threshold's own SystemPerformanceResults via AddAEPForAssurance,
    // followed by one PutDataIntoHistograms() call, so `mean_aep` proves AddThreshold/GetThreshold
    // hand back the SAME live SystemPerformanceResults the ctor built (not a copy).
    static object EvalPerformanceByThresholds(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      var pbt = new PerformanceByThresholds();
      foreach (var t in c.GetProperty("thresholds").EnumerateArray()) {
        int id = t.GetProperty("id").GetInt32();
        string typeName = t.GetProperty("type").GetString();
        ThresholdEnum type = Enum.Parse<ThresholdEnum>(typeName);
        double value = D(t.GetProperty("value"));
        var conv = t.GetProperty("convergence");
        var cc = new ConvergenceCriteria(conv.GetProperty("min_iterations").GetInt32(), conv.GetProperty("max_iterations").GetInt32());
        pbt.AddThreshold(new Threshold(id, cc, type, value));
      }
      int getThresholdId = c.GetProperty("get_threshold_id").GetInt32();
      Threshold threshold = pbt.GetThreshold(getThresholdId);

      if (method == "threshold_value") return threshold.ThresholdValue;
      if (method == "threshold_type") return (double)(int)threshold.ThresholdType;
      if (method == "threshold_id") return (double)threshold.ThresholdID;
      if (method == "mean_aep") {
        foreach (var o in c.GetProperty("aep_observations").EnumerateArray()) {
          threshold.SystemPerformanceResults.AddAEPForAssurance(D(o.GetProperty("result")), o.GetProperty("iteration").GetInt32());
        }
        threshold.SystemPerformanceResults.PutDataIntoHistograms();
        return threshold.SystemPerformanceResults.MeanAEP();
      }
      throw new Exception("unknown performance_by_thresholds method: " + method);
    }

    // ImpactAreaScenarioResults (Phase 5 Task 6) is the compute-output container holding one
    // PerformanceByThresholds + one StudyAreaConsequencesBinned -- built from
    // patched/ImpactAreaScenarioResults.cs (see that file's header for the patch rationale: XML
    // dropped, everything else including the GetOrCreateUncertainConsequenceFrequencyCurve lock
    // kept VERBATIM). `construct` is {impact_area_id, damage_category, asset_category,
    // consequence_convergence: {min_iterations, max_iterations}, threshold: {id, type
    // (ThresholdEnum name string), value, convergence: {min_iterations, max_iterations}}}. Builds
    // a fresh ImpactAreaScenarioResults(impactAreaID) (the 1-arg public ctor), AddThreshold's a
    // Threshold built from `threshold`, and AddNewConsequenceResultObject's ONE (damageCategory,
    // assetCategory) combo into ConsequenceResults with ConsequenceType.Damage/RiskType.Total
    // (Total, not StudyAreaConsequencesBinned's own Fail default -- matches the RiskType.Total
    // default MeanExpectedAnnualConsequences itself uses; see
    // fixtures/metrics/impact_area_scenario_results.json's note). `consequence_realizations`
    // ({iteration, damage}) feed ConsequenceResults.AddConsequenceRealization(damage,
    // damageCategory, assetCategory, impactAreaID, iteration, ConsequenceType.Damage,
    // RiskType.Total) (the EAD-binning overload), then ConsequenceResults.PutDataIntoHistograms()
    // runs once. `aep_observations` ({iteration, result}) feed the retrieved threshold's
    // SystemPerformanceResults.AddAEPForAssurance(...), then that same SystemPerformanceResults.
    // PutDataIntoHistograms() runs once. `method` dispatches mean_aep/median_aep (args
    // [thresholdID]), long_term_exceedance_probability (args [thresholdID, years]),
    // assurance_of_aep (args [thresholdID, exceedanceProbability]),
    // mean_expected_annual_consequences (args [impactAreaID]), and results_are_converged (args
    // [upper, lower] -- ResultsAreConverged(upper, lower, checkConsequenceResults: true), returned
    // as 1.0/0.0).
    // Shared per-case builder for one ImpactAreaScenarioResults, factored out (Phase 6 Task 5) so
    // EvalScenarioResults below can build MULTIPLE such objects (one per `impact_areas` entry)
    // from the exact same construct/consequence_realizations/aep_observations/stage_observations
    // shape EvalImpactAreaScenarioResults already used for its single object.
    static (ImpactAreaScenarioResults results, string damageCategory, string assetCategory) BuildImpactAreaScenarioResultsCase(JsonElement caseEl) {
      var c = caseEl.GetProperty("construct");
      int impactAreaID = c.GetProperty("impact_area_id").GetInt32();
      string damageCategory = c.GetProperty("damage_category").GetString();
      string assetCategory = c.GetProperty("asset_category").GetString();

      var results = new ImpactAreaScenarioResults(impactAreaID);

      var t = c.GetProperty("threshold");
      int thresholdId = t.GetProperty("id").GetInt32();
      ThresholdEnum thresholdType = Enum.Parse<ThresholdEnum>(t.GetProperty("type").GetString());
      double thresholdValue = D(t.GetProperty("value"));
      var tConv = t.GetProperty("convergence");
      var thresholdCc = new ConvergenceCriteria(tConv.GetProperty("min_iterations").GetInt32(), tConv.GetProperty("max_iterations").GetInt32());
      results.PerformanceByThresholds.AddThreshold(new Threshold(thresholdId, thresholdCc, thresholdType, thresholdValue));

      var consConv = c.GetProperty("consequence_convergence");
      var consequenceCc = new ConvergenceCriteria(consConv.GetProperty("min_iterations").GetInt32(), consConv.GetProperty("max_iterations").GetInt32());
      results.ConsequenceResults.AddNewConsequenceResultObject(damageCategory, assetCategory, consequenceCc, impactAreaID, ConsequenceType.Damage, RiskType.Total);

      foreach (var r in caseEl.GetProperty("consequence_realizations").EnumerateArray()) {
        results.ConsequenceResults.AddConsequenceRealization(D(r.GetProperty("damage")), damageCategory, assetCategory, impactAreaID, r.GetProperty("iteration").GetInt64(), ConsequenceType.Damage, RiskType.Total);
      }
      results.ConsequenceResults.PutDataIntoHistograms();

      Threshold threshold = results.PerformanceByThresholds.GetThreshold(thresholdId);
      threshold.SystemPerformanceResults.AddStageAssuranceHistogram(0.98);
      foreach (var o in caseEl.GetProperty("aep_observations").EnumerateArray()) {
        threshold.SystemPerformanceResults.AddAEPForAssurance(D(o.GetProperty("result")), o.GetProperty("iteration").GetInt32());
      }
      foreach (var o in caseEl.GetProperty("stage_observations").EnumerateArray()) {
        threshold.SystemPerformanceResults.AddStageForAssurance(0.98, D(o.GetProperty("result")), o.GetProperty("iteration").GetInt32());
      }
      threshold.SystemPerformanceResults.PutDataIntoHistograms();

      return (results, damageCategory, assetCategory);
    }

    static object EvalImpactAreaScenarioResults(JsonElement caseEl, string method, JsonElement argsEl) {
      var (results, damageCategory, assetCategory) = BuildImpactAreaScenarioResultsCase(caseEl);

      if (method == "mean_aep") return results.MeanAEP(argsEl[0].GetInt32());
      if (method == "median_aep") return results.MedianAEP(argsEl[0].GetInt32());
      if (method == "long_term_exceedance_probability") return results.LongTermExceedanceProbability(argsEl[0].GetInt32(), argsEl[1].GetInt32());
      if (method == "assurance_of_aep") return results.AssuranceOfAEP(argsEl[0].GetInt32(), D(argsEl[1]));
      if (method == "mean_expected_annual_consequences") return results.MeanExpectedAnnualConsequences(argsEl[0].GetInt32(), damageCategory, assetCategory, ConsequenceType.Damage, RiskType.Total);
      if (method == "results_are_converged") return results.ResultsAreConverged(D(argsEl[0]), D(argsEl[1]), true) ? 1.0 : 0.0;
      if (method == "uncertain_curve_count" || method == "uncertain_curve_yvals_a" || method == "uncertain_curve_yvals_b") {
        // Reference-stability coverage for GetOrCreateUncertainConsequenceFrequencyCurve (Task 6
        // follow-up): get_or_create for category_a (creates a curve, keeps the C# object
        // reference -- in C# this is always stable, matching the port's post-fix std::deque
        // behavior), feeds realization_a_before_grow, then get_or_create for a DIFFERENT
        // category_b (grows UncertainConsequenceFrequencyCurves), feeds realization_b, then
        // get_or_create for category_a again (must be the SAME object -- uncertainCurveCount
        // stays 2). realization_a_after_grow is added through the ORIGINAL category_a reference.
        var s = caseEl.GetProperty("uncertain_curve_stability");
        double[] curveXvals = DA(s.GetProperty("xvals"));
        var sc = s.GetProperty("convergence");
        var curveCc = new ConvergenceCriteria(sc.GetProperty("min_iterations").GetInt32(), sc.GetProperty("max_iterations").GetInt32());
        var ca = s.GetProperty("category_a");
        var cb = s.GetProperty("category_b");

        CategoriedUncertainPairedData curveA1 = results.GetOrCreateUncertainConsequenceFrequencyCurve(
            curveXvals, ca.GetProperty("damage_category").GetString(), ca.GetProperty("asset_category").GetString(),
            Enum.Parse<ConsequenceType>(ca.GetProperty("consequence_type").GetString()),
            Enum.Parse<RiskType>(ca.GetProperty("risk_type").GetString()), curveCc);
        var rABefore = s.GetProperty("realization_a_before_grow");
        curveA1.AddCurveRealization(new PairedData(curveXvals, DA(rABefore.GetProperty("yvals"))), rABefore.GetProperty("iteration").GetInt64());

        CategoriedUncertainPairedData curveB = results.GetOrCreateUncertainConsequenceFrequencyCurve(
            curveXvals, cb.GetProperty("damage_category").GetString(), cb.GetProperty("asset_category").GetString(),
            Enum.Parse<ConsequenceType>(cb.GetProperty("consequence_type").GetString()),
            Enum.Parse<RiskType>(cb.GetProperty("risk_type").GetString()), curveCc);
        var rB = s.GetProperty("realization_b");
        curveB.AddCurveRealization(new PairedData(curveXvals, DA(rB.GetProperty("yvals"))), rB.GetProperty("iteration").GetInt64());

        CategoriedUncertainPairedData curveA2 = results.GetOrCreateUncertainConsequenceFrequencyCurve(
            curveXvals, ca.GetProperty("damage_category").GetString(), ca.GetProperty("asset_category").GetString(),
            Enum.Parse<ConsequenceType>(ca.GetProperty("consequence_type").GetString()),
            Enum.Parse<RiskType>(ca.GetProperty("risk_type").GetString()), curveCc);

        var rAAfter = s.GetProperty("realization_a_after_grow");
        curveA1.AddCurveRealization(new PairedData(curveXvals, DA(rAAfter.GetProperty("yvals"))), rAAfter.GetProperty("iteration").GetInt64());

        if (method == "uncertain_curve_count") return (double)results.UncertainConsequenceFrequencyCurves.Count;
        results.PutUncertainFrequencyCurvesIntoHistograms();
        if (method == "uncertain_curve_yvals_a") return curveA2.GetUncertainPairedData().SamplePairedData(1, true).Yvals;
        return curveB.GetUncertainPairedData().SamplePairedData(1, true).Yvals;
      }
      throw new Exception("unknown impact_area_scenario_results method: " + method);
    }

    // ScenarioResults (Phase 6 Task 5): the compute-output container holding a list of
    // ImpactAreaScenarioResults plus the scenario-level aggregators that sum/enumerate across
    // every impact area. `impact_areas` is a list of entries in EXACTLY the
    // impact_area_scenario_results.json construct/consequence_realizations/aep_observations/
    // stage_observations shape (reused via BuildImpactAreaScenarioResultsCase above) -- one
    // ImpactAreaScenarioResults per entry, AddResults'd into a fresh ScenarioResults in order.
    // `method` dispatches: sample_mean_expected_annual_consequences (args [], ConsequenceType.
    // Damage/RiskType.Total explicit -- matches how every impact area's
    // AggregatedConsequencesBinned was built; this level's own C# default, RiskType.Fail, would
    // match none of it); consequences_distribution_sample_mean (args []) --
    // GetConsequencesDistribution()'s own defaults (wildcard impact area, ConsequenceType.Damage,
    // RiskType.Total) already match the built data, .SampleMean of the result; mean_aep (args
    // [impact_area_id, threshold_id]) -- a straight GetResults(impactAreaID).MeanAEP(thresholdID)
    // pass-through, exercising GetResults' lookup-by-ID across MULTIPLE stored impact areas.
    static object EvalScenarioResults(JsonElement caseEl, string method, JsonElement argsEl) {
      var scenario = new ScenarioResults();
      foreach (var iaCase in caseEl.GetProperty("impact_areas").EnumerateArray()) {
        var (results, _, _) = BuildImpactAreaScenarioResultsCase(iaCase);
        scenario.AddResults(results);
      }
      if (method == "sample_mean_expected_annual_consequences") {
        return scenario.SampleMeanExpectedAnnualConsequences(IntegerGlobalConstants.DEFAULT_MISSING_VALUE, null, null, ConsequenceType.Damage, RiskType.Total);
      }
      if (method == "consequences_distribution_sample_mean") {
        return scenario.GetConsequencesDistribution().SampleMean;
      }
      if (method == "mean_aep") {
        return scenario.MeanAEP(argsEl[0].GetInt32(), argsEl[1].GetInt32());
      }
      throw new Exception("unknown scenario_results method: " + method);
    }

    // AlternativeResults (Phase 6 Task 6): the compute-output container Alternative.
    // AnnualizationCompute (Task 9, not yet ported) returns -- the EqAD StudyAreaConsequencesByQuantile
    // results plus the base-year/future-year ScenarioResults they were computed from, and THE
    // identical-vs-eqad delegation pattern. Built via the internal "public for testing"
    // (StudyAreaConsequencesByQuantile, id, analysisYears, periodOfAnalysis, isNull) ctor, then
    // BaseYearScenarioResults/FutureYearScenarioResults/ScenariosAreIdentical are assigned directly
    // (all `internal set`, accessible since AlternativeResults.cs compiles into this same
    // assembly). `base_year_impact_areas`/`future_year_impact_areas` are each a list of entries in
    // EXACTLY the impact_area_scenario_results.json construct/consequence_realizations/
    // aep_observations/stage_observations shape (reused via BuildImpactAreaScenarioResultsCase,
    // same as EvalScenarioResults above) -- one ImpactAreaScenarioResults per entry, AddResults'd
    // into a fresh ScenarioResults in order. `eqad_results` is a
    // study_area_consequences_by_quantile.json-shaped {consequence_results: [...]} object, built
    // via BuildAggregatedConsequencesByQuantileEntry per entry and collected into a
    // StudyAreaConsequencesByQuantile via its (List<AggregatedConsequencesByQuantile>) ctor.
    // `scenarios_are_identical` sets ScenariosAreIdentical directly. `method` dispatches (args
    // always [damage_category_or_null, asset_category_or_null, impact_area_id],
    // ConsequenceType.Damage/RiskType.Total explicit at every level): sample_mean_eqad/
    // sample_mean_base_year_ead/sample_mean_future_year_ead, and get_eqad_distribution_sample_mean
    // (GetEqadDistribution(...).SampleMean). See fixtures/metrics/alternative_results.json's note.
    static object EvalAlternativeResults(JsonElement caseEl, string method, JsonElement argsEl) {
      var eqadEntries = caseEl.GetProperty("eqad_results").GetProperty("consequence_results")
          .EnumerateArray().Select(BuildAggregatedConsequencesByQuantileEntry).ToList();
      var eqadResults = new StudyAreaConsequencesByQuantile(eqadEntries);

      var alt = new AlternativeResults(eqadResults, 1, new List<int> { 2030, 2049 }, 50, false);
      alt.ScenariosAreIdentical = caseEl.GetProperty("scenarios_are_identical").GetBoolean();

      var baseYear = new ScenarioResults();
      foreach (var iaCase in caseEl.GetProperty("base_year_impact_areas").EnumerateArray()) {
        var (results, _, _) = BuildImpactAreaScenarioResultsCase(iaCase);
        baseYear.AddResults(results);
      }
      alt.BaseYearScenarioResults = baseYear;

      var futureYear = new ScenarioResults();
      foreach (var iaCase in caseEl.GetProperty("future_year_impact_areas").EnumerateArray()) {
        var (results, _, _) = BuildImpactAreaScenarioResultsCase(iaCase);
        futureYear.AddResults(results);
      }
      alt.FutureYearScenarioResults = futureYear;

      // Optional (Phase 6 Task 6 coverage addition): exercises AlternativeResults.
      // AddConsequenceResults (AlternativeResults.cs:231-238) against the EqadResults built above --
      // this method had no fixture coverage before. Its riskType arg to GetConsequenceResult is
      // omitted, defaulting to RiskType.Total, which FilterByCategories treats as a WILDCARD
      // matching a candidate of ANY risk type (not an exact-Total match) -- so the dedup key is
      // really just (damageCategory, assetCategory, RegionID, ConsequenceType).
      if (caseEl.TryGetProperty("consequence_result_to_add", out var addEl)) {
        alt.AddConsequenceResults(BuildAggregatedConsequencesByQuantileEntry(addEl));
      }

      string damageCategory = OptionalString(argsEl[0]);
      string assetCategory = OptionalString(argsEl[1]);
      int impactAreaID = argsEl[2].GetInt32();

      if (method == "eqad_consequence_result_list_count") {
        return (double)alt.EqadResults.ConsequenceResultList.Count;
      }
      if (method == "sample_mean_eqad") {
        return alt.SampleMeanEqad(impactAreaID, damageCategory, assetCategory, ConsequenceType.Damage, RiskType.Total);
      }
      if (method == "sample_mean_base_year_ead") {
        return alt.SampleMeanBaseYearEAD(impactAreaID, damageCategory, assetCategory, ConsequenceType.Damage, RiskType.Total);
      }
      if (method == "sample_mean_future_year_ead") {
        return alt.SampleMeanFutureYearEAD(impactAreaID, damageCategory, assetCategory, ConsequenceType.Damage, RiskType.Total);
      }
      if (method == "get_eqad_distribution_sample_mean") {
        return alt.GetEqadDistribution(impactAreaID, damageCategory, assetCategory, ConsequenceType.Damage, RiskType.Total).SampleMean;
      }
      throw new Exception("unknown alternative_results method: " + method);
    }

    // AlternativeComparisonReportResults (Phase 6 Task 7): the container
    // AlternativeComparisonReport.ComputeAlternativeComparisonReport (Task 10, not yet ported)
    // returns -- the with/without-project AlternativeResults plus three lists of REDUCED (benefit)
    // StudyAreaConsequencesByQuantile results (EqAD-reduced, base-year-EAD-reduced,
    // future-year-EAD-reduced). `with_project`/`without_project` are each
    // {alternative_id, eqad_results, base_year_impact_areas, future_year_impact_areas} objects,
    // built the same way EvalAlternativeResults above builds its `alt` (the (StudyAreaConsequences
    // ByQuantile, id, analysisYears, periodOfAnalysis, isNull) ctor for the AlternativeResults-level
    // EqadResults field, then BaseYearScenarioResults/FutureYearScenarioResults assigned via
    // BuildImpactAreaScenarioResultsCase + AddResults). Each of the three `*_reduced_results_list`
    // fields is a list of {alternative_id, consequence_results} objects: a
    // StudyAreaConsequencesByQuantile built via the (int alternativeID) ctor (the only ctor that
    // sets a real, non-zero AlternativeID) then AddExistingConsequenceResultObject-ed with its
    // consequence_results entries. `method` dispatch: alternative_id-taking methods read
    // args=[alternative_id, damage_category_or_null, asset_category_or_null, impact_area_id]; the
    // without-project delegators read args=[damage_category_or_null, asset_category_or_null,
    // impact_area_id]. See fixtures/metrics/alternative_comparison_report_results.json's note.
    static AlternativeResults BuildAlternativeComparisonAlternativeResults(JsonElement alt) {
      var eqadEntries = alt.GetProperty("eqad_results").GetProperty("consequence_results")
          .EnumerateArray().Select(BuildAggregatedConsequencesByQuantileEntry).ToList();
      var eqadResults = new StudyAreaConsequencesByQuantile(eqadEntries);

      var results = new AlternativeResults(eqadResults, alt.GetProperty("alternative_id").GetInt32(), new List<int> { 2030, 2049 }, 50, false);

      var baseYear = new ScenarioResults();
      foreach (var iaCase in alt.GetProperty("base_year_impact_areas").EnumerateArray()) {
        var (iaResults, _, _) = BuildImpactAreaScenarioResultsCase(iaCase);
        baseYear.AddResults(iaResults);
      }
      results.BaseYearScenarioResults = baseYear;

      var futureYear = new ScenarioResults();
      foreach (var iaCase in alt.GetProperty("future_year_impact_areas").EnumerateArray()) {
        var (iaResults, _, _) = BuildImpactAreaScenarioResultsCase(iaCase);
        futureYear.AddResults(iaResults);
      }
      results.FutureYearScenarioResults = futureYear;

      return results;
    }
    static StudyAreaConsequencesByQuantile BuildAlternativeComparisonReducedResultsEntry(JsonElement entry) {
      var study = new StudyAreaConsequencesByQuantile(entry.GetProperty("alternative_id").GetInt32());
      foreach (var cr in entry.GetProperty("consequence_results").EnumerateArray()) {
        study.AddExistingConsequenceResultObject(BuildAggregatedConsequencesByQuantileEntry(cr));
      }
      return study;
    }
    static List<StudyAreaConsequencesByQuantile> BuildAlternativeComparisonReducedResultsList(JsonElement listEl) {
      return listEl.EnumerateArray().Select(BuildAlternativeComparisonReducedResultsEntry).ToList();
    }
    static object EvalAlternativeComparisonReportResults(JsonElement caseEl, string method, JsonElement argsEl) {
      var withProject = caseEl.GetProperty("with_project").EnumerateArray().Select(BuildAlternativeComparisonAlternativeResults).ToList();
      var withoutProject = BuildAlternativeComparisonAlternativeResults(caseEl.GetProperty("without_project"));

      var report = new AlternativeComparisonReportResults(
          withProject, withoutProject,
          BuildAlternativeComparisonReducedResultsList(caseEl.GetProperty("eqad_reduced_results_list")),
          BuildAlternativeComparisonReducedResultsList(caseEl.GetProperty("base_year_ead_reduced_results_list")),
          BuildAlternativeComparisonReducedResultsList(caseEl.GetProperty("future_year_ead_reduced_results_list")));

      if (method == "sample_mean_without_project_base_year_ead" || method == "sample_mean_without_project_future_year_ead") {
        string dc0 = OptionalString(argsEl[0]);
        string ac0 = OptionalString(argsEl[1]);
        int ia0 = argsEl[2].GetInt32();
        if (method == "sample_mean_without_project_base_year_ead") {
          return report.SampleMeanWithoutProjectBaseYearEAD(ia0, dc0, ac0, ConsequenceType.Damage);
        }
        return report.SampleMeanWithoutProjectFutureYearEAD(ia0, dc0, ac0, ConsequenceType.Damage);
      }

      // Phase 6 Task 7 review fix coverage: GetRiskTypes(consequenceType) filters
      // _EqadReducedResultsList's ConsequenceResultList by ConsequenceType (like its three
      // siblings GetImpactAreaIDs/GetAssetCategories/GetDamageCategories), so the count returned
      // here for ConsequenceType.Damage must be strictly smaller than the unfiltered count when
      // the fixture data mixes Damage and LifeLoss entries -- that difference is what the C++
      // fixture pins to catch a missing filter.
      if (method == "get_risk_types_count") {
        return (double)report.GetRiskTypes(ConsequenceType.Damage).Count;
      }

      int alternativeID = argsEl[0].GetInt32();
      string damageCategory = OptionalString(argsEl[1]);
      string assetCategory = OptionalString(argsEl[2]);
      int impactAreaID = argsEl[3].GetInt32();

      if (method == "sample_mean_eqad_reduced") {
        return report.SampleMeanEqadReduced(alternativeID, impactAreaID, damageCategory, assetCategory, ConsequenceType.Damage, RiskType.Total);
      }
      if (method == "sample_mean_base_year_ead_reduced") {
        return report.SampleMeanBaseYearEADReduced(alternativeID, impactAreaID, damageCategory, assetCategory, ConsequenceType.Damage, RiskType.Total);
      }
      if (method == "sample_mean_future_year_ead_reduced") {
        return report.SampleMeanFutureYearEADReduced(alternativeID, impactAreaID, damageCategory, assetCategory, ConsequenceType.Damage, RiskType.Total);
      }
      if (method == "sample_mean_with_project_base_year_ead") {
        return report.SampleMeanWithProjectBaseYearEAD(alternativeID, impactAreaID, damageCategory, assetCategory, ConsequenceType.Damage);
      }
      if (method == "sample_mean_with_project_future_year_ead") {
        return report.SampleMeanWithProjectFutureYearEAD(alternativeID, impactAreaID, damageCategory, assetCategory, ConsequenceType.Damage);
      }
      if (method == "get_eqad_reduced_results_histogram_sample_mean") {
        return report.GetEqadReducedResultsHistogram(alternativeID, impactAreaID, damageCategory, assetCategory, ConsequenceType.Damage).SampleMean;
      }
      if (method == "get_base_year_ead_reduced_results_histogram_sample_mean") {
        return report.GetBaseYearEADReducedResultsHistogram(alternativeID, impactAreaID, damageCategory, assetCategory, ConsequenceType.Damage, RiskType.Total).SampleMean;
      }
      if (method == "get_future_year_ead_reduced_results_histogram_sample_mean") {
        return report.GetFutureYearEADReducedResultsHistogram(alternativeID, impactAreaID, damageCategory, assetCategory, ConsequenceType.Damage, RiskType.Total).SampleMean;
      }
      throw new Exception("unknown alternative_comparison_report_results method: " + method);
    }

    // Alternative (Phase 6 Task 9): the headline EqAD annualization math -- Alternative.
    // ComputeEqad (interpolate/present-value/PVIFA, no ScenarioResults involved) and Alternative.
    // AnnualizationCompute (single- and two-scenario paths), via patched/Alternative.cs (drops the
    // ProgressReporter parameter/calls -- see that file's header). Two case `kind`s: `compute_eqad`
    // dispatches straight to Alternative.ComputeEqad with args [baseYearEAD, baseYear,
    // mostLikelyFutureEAD, mostLikelyFutureYear, periodOfAnalysis, discountRate]. `annualization`
    // builds `base_impact_area`/`future_impact_area` (either may be JSON null) via
    // BuildAlternativeBinnedEntry below -- an AggregatedConsequencesBinned constructed directly
    // from an already-built DynamicHistogram (the (string,string,IHistogram,int,ConsequenceType,
    // RiskType) ctor), `values_start`/`values_count` reproducing
    // `Enumerable.Range(start,count).Select(i => (double)i)` -- mirrors AlternativeTest.cs's own
    // construction (e.g. LifeLossResultsExcludedFromEqad) exactly, RiskType.Fail throughout. See
    // fixtures/alternatives/alternative.json's note for the full method dispatch rationale.
    static AggregatedConsequencesBinned BuildAlternativeBinnedEntry(JsonElement entry, int impactAreaID, ConvergenceCriteria cc) {
      int start = entry.GetProperty("values_start").GetInt32();
      int count = entry.GetProperty("values_count").GetInt32();
      var values = Enumerable.Range(start, count).Select(i => (double)i).ToList();
      var histogram = new DynamicHistogram(values, cc);
      return new AggregatedConsequencesBinned(
          entry.GetProperty("damage_category").GetString(), entry.GetProperty("asset_category").GetString(),
          histogram, impactAreaID,
          Enum.Parse<ConsequenceType>(entry.GetProperty("consequence_type").GetString()),
          Enum.Parse<RiskType>(entry.GetProperty("risk_type").GetString()));
    }
    static ScenarioResults BuildAlternativeScenarioResults(JsonElement impactAreaEl, int impactAreaID, ConvergenceCriteria cc) {
      var scenario = new ScenarioResults();
      var ia = new ImpactAreaScenarioResults(impactAreaID);
      foreach (var entry in impactAreaEl.GetProperty("consequence_results").EnumerateArray()) {
        ia.ConsequenceResults.AddExistingConsequenceResultObject(BuildAlternativeBinnedEntry(entry, impactAreaID, cc));
      }
      scenario.AddResults(ia);
      return scenario;
    }
    static bool AlternativeScenarioHasLifeLoss(ScenarioResults results) {
      return results.ResultsList.SelectMany(r => r.ConsequenceResults.ConsequenceResultList)
          .Any(c => c.ConsequenceType == ConsequenceType.LifeLoss);
    }
    static object EvalAlternative(JsonElement caseEl, string method, JsonElement argsEl) {
      string kind = caseEl.GetProperty("kind").GetString();
      if (kind == "compute_eqad") {
        return Alternative.ComputeEqad(D(argsEl[0]), argsEl[1].GetInt32(), D(argsEl[2]), argsEl[3].GetInt32(),
                                        argsEl[4].GetInt32(), D(argsEl[5]));
      }

      // kind == "annualization"
      int impactAreaID = caseEl.GetProperty("impact_area_id").GetInt32();
      var conv = caseEl.GetProperty("convergence");
      var cc = new ConvergenceCriteria(conv.GetProperty("min_iterations").GetInt32(), conv.GetProperty("max_iterations").GetInt32());

      var baseEl = caseEl.GetProperty("base_impact_area");
      ScenarioResults baseResults = baseEl.ValueKind == JsonValueKind.Null ? null : BuildAlternativeScenarioResults(baseEl, impactAreaID, cc);
      var futureEl = caseEl.GetProperty("future_impact_area");
      ScenarioResults futureResults = futureEl.ValueKind == JsonValueKind.Null ? null : BuildAlternativeScenarioResults(futureEl, impactAreaID, cc);

      AlternativeResults alt = Alternative.AnnualizationCompute(
          D(caseEl.GetProperty("discount_rate")), caseEl.GetProperty("period_of_analysis").GetInt32(),
          caseEl.GetProperty("alternative_id").GetInt32(), baseResults, futureResults,
          caseEl.GetProperty("base_year").GetInt32(), caseEl.GetProperty("future_year").GetInt32());

      if (method == "sample_mean_eqad") {
        return alt.SampleMeanEqad(impactAreaID, argsEl[0].GetString(), argsEl[1].GetString(), ConsequenceType.Damage, RiskType.Total);
      }
      if (method == "eqad_count_by_type") {
        var want = Enum.Parse<ConsequenceType>(argsEl[0].GetString());
        return (double)alt.EqadResults.ConsequenceResultList.Count(r => r.ConsequenceType == want);
      }
      if (method == "base_has_lifeloss") {
        return AlternativeScenarioHasLifeLoss(alt.BaseYearScenarioResults) ? 1.0 : 0.0;
      }
      if (method == "future_has_lifeloss") {
        return AlternativeScenarioHasLifeLoss(alt.FutureYearScenarioResults) ? 1.0 : 0.0;
      }
      throw new Exception("unknown alternative method: " + method);
    }

    // ImpactAreaScenarioSimulation (Phase 5 Task 7): the skeleton + fluent SimulationBuilder +
    // CanCompute + InitializeConsequenceHistograms surface only -- see
    // patched/ImpactAreaScenarioSimulation.cs's header for what's kept/dropped and why. `construct`
    // is {impact_area_id, flow_frequency: {type,params}, flow_stage: {xs, ys:[{type,params}]},
    // stage_damage: [{damage_category, asset_category, xs, ys:[{type,params}]}],
    // non_failure_stage_damage (same per-item shape, optional)} -- same shape
    // core/tests/test_fixtures.cpp's run_simulation dispatch uses. Builds via
    // Builder(id).WithFlowFrequency(...).WithFlowStage(...).WithStageDamages(...)
    // [.WithNonFailureStageDamage(...)].Build(). `method` dispatches: is_null (args
    // [min_iterations, max_iterations]) -- Compute(new ConvergenceCriteria(min, max)).IsNull;
    // can_compute (args [min, max]) -- CanCompute(new ConvergenceCriteria(min, max)) directly;
    // consequence_result_count (args []) -- InitializeConsequenceHistograms(new
    // ConvergenceCriteria()) directly, then ImpactAreaScenarioResultsForTest.ConsequenceResults.
    // ConsequenceResultList.Count.
    static UncertainPairedData MakeSimulationUpd(JsonElement ctor) {
      double[] xs = DA(ctor.GetProperty("xs"));
      IDistribution[] ys = DistArray(ctor.GetProperty("ys"));
      if (ctor.TryGetProperty("damage_category", out var dc)) {
        string assetCategory = ctor.TryGetProperty("asset_category", out var ac) ? ac.GetString() : "unassigned";
        return new UncertainPairedData(xs, ys, new CurveMetaData(dc.GetString(), assetCategory));
      }
      return new UncertainPairedData(xs, ys, new CurveMetaData());
    }
    // Phase 5 Task 11: the direct graphical stage-frequency curve set via WithFrequencyStage() --
    // mirrors StudyDataGraphicalStageFrequencyResultsTests.ComputeMeanEADWithIterations_Test's
    // `.WithFrequencyStage(stageFrequency)` (no WithFlowFrequency/WithFlowStage at all in that
    // test). `ctor` is {exceedance_probabilities, values, equivalent_record_length,
    // using_stages_not_flows?, damage_category, asset_category?} -- damage_category/asset_category
    // matter here (unlike EvalGupd's fixed "hello" metadata) since the resulting curve's metadata
    // must match the mean_eac dispatch args.
    static GraphicalUncertainPairedData MakeSimulationGupd(JsonElement ctor) {
      double[] exceedanceProbabilities = DA(ctor.GetProperty("exceedance_probabilities"));
      double[] values = DA(ctor.GetProperty("values"));
      int erl = ctor.GetProperty("equivalent_record_length").GetInt32();
      bool usingStagesNotFlows = ctor.TryGetProperty("using_stages_not_flows", out var usnf) ? usnf.GetBoolean() : true;
      string damageCategory = ctor.GetProperty("damage_category").GetString();
      string assetCategory = ctor.TryGetProperty("asset_category", out var ac) ? ac.GetString() : "unassigned";
      return new GraphicalUncertainPairedData(exceedanceProbabilities, values, erl,
                                               new CurveMetaData(damageCategory, assetCategory), usingStagesNotFlows);
    }
    static ImpactAreaScenarioSimulation BuildSimulation(JsonElement ctor) {
      int impactAreaID = ctor.GetProperty("impact_area_id").GetInt32();
      var builder = ImpactAreaScenarioSimulation.Builder(impactAreaID);
      // flow_frequency/flow_stage are OPTIONAL as of Task 11 (a direct-graphical-frequency-stage
      // simulation never calls WithFlowFrequency/WithFlowStage at all -- see frequency_stage below).
      if (ctor.TryGetProperty("flow_frequency", out var ffEl)) {
        var flowFrequency = DistFactory(ffEl.GetProperty("type").GetString(), DA(ffEl.GetProperty("params")));
        builder = builder.WithFlowFrequency(flowFrequency);
      }
      if (ctor.TryGetProperty("flow_stage", out var fsEl)) {
        builder = builder.WithFlowStage(MakeSimulationUpd(fsEl));
      }
      if (ctor.TryGetProperty("frequency_stage", out var freqStageEl)) {
        builder = builder.WithFrequencyStage(MakeSimulationGupd(freqStageEl));
      }
      // stage_damage is OPTIONAL as of Phase 5 Task 10 (a levee-only simulation with no stage
      // damage at all, e.g. PerformanceTest.ComputeLeveeAEP_Test, never calls WithStageDamages).
      if (ctor.TryGetProperty("stage_damage", out var sdEl)) {
        var stageDamage = new List<UncertainPairedData>();
        foreach (var sd in sdEl.EnumerateArray()) {
          stageDamage.Add(MakeSimulationUpd(sd));
        }
        builder = builder.WithStageDamages(stageDamage);
      }
      if (ctor.TryGetProperty("non_failure_stage_damage", out var nfsd)) {
        var nonFailureStageDamage = new List<UncertainPairedData>();
        foreach (var sd in nfsd.EnumerateArray()) {
          nonFailureStageDamage.Add(MakeSimulationUpd(sd));
        }
        builder = builder.WithNonFailureStageDamage(nonFailureStageDamage);
      }
      // stage_life_loss (Phase 5 Task 10: ComputeEALL's WithStageLifeLoss).
      if (ctor.TryGetProperty("stage_life_loss", out var sllEl)) {
        var stageLifeLoss = new List<UncertainPairedData>();
        foreach (var sd in sllEl.EnumerateArray()) {
          stageLifeLoss.Add(MakeSimulationUpd(sd));
        }
        builder = builder.WithStageLifeLoss(stageLifeLoss);
      }
      // levee (Phase 5 Task 10: ComputeEAD_withLevee/TotalRiskShould/ComputeLeveeAEP's WithLevee).
      if (ctor.TryGetProperty("levee", out var leveeEl)) {
        var levee = MakeSimulationUpd(leveeEl);
        builder = builder.WithLevee(levee, leveeEl.GetProperty("top_of_levee_elevation").GetDouble());
      }
      // Phase 5 Task 9: optional additional_threshold ({threshold_id, type, value}) -- mirrors
      // DefaultThresholdShould.NotOverrideUserProvidedDefaultThreshold's pre-registered ID-0
      // Threshold, built with ConvergenceCriteria(1, 1) matching every DefaultThresholdShould test.
      // Phase 5 Task 11 adds an OPTIONAL `cc: [min, max, tolerance?]` override for cases (e.g.
      // PerformanceTest.ComputeConditionalNonExceedanceProbability_Test) whose threshold is built
      // with the SAME non-(1,1)/non-default-tolerance ConvergenceCriteria the simulation itself
      // computes with (see BuildSimulation's C++ mirror's comment for why this matters).
      if (ctor.TryGetProperty("additional_threshold", out var at)) {
        var thresholdCc = new ConvergenceCriteria(1, 1);
        if (at.TryGetProperty("cc", out var ccEl)) {
          var ccArr = ccEl.EnumerateArray().ToArray();
          double tolerance = ccArr.Length > 2 ? ccArr[2].GetDouble() : 0.01;
          thresholdCc = new ConvergenceCriteria(ccArr[0].GetInt32(), ccArr[1].GetInt32(), 1.96039491692543, tolerance);
        }
        var userThreshold = new Threshold(at.GetProperty("threshold_id").GetInt32(), thresholdCc,
                                           Enum.Parse<ThresholdEnum>(at.GetProperty("type").GetString()),
                                           at.GetProperty("value").GetDouble());
        builder = builder.WithAdditionalThreshold(userThreshold);
      }
      return builder.Build();
    }
    static object EvalSimulation(JsonElement caseEl, string method, JsonElement argsEl) {
      var simulation = BuildSimulation(caseEl.GetProperty("construct"));
      if (method == "is_null") {
        var cc = new ConvergenceCriteria(argsEl[0].GetInt32(), argsEl[1].GetInt32());
        return simulation.Compute(cc).IsNull ? 1.0 : 0.0;
      }
      if (method == "can_compute") {
        var cc = new ConvergenceCriteria(argsEl[0].GetInt32(), argsEl[1].GetInt32());
        return simulation.CanCompute(cc) ? 1.0 : 0.0;
      }
      if (method == "consequence_result_count") {
        var cc = new ConvergenceCriteria();
        simulation.InitializeConsequenceHistograms(cc);
        return (double)simulation.ImpactAreaScenarioResultsForTest.ConsequenceResults.ConsequenceResultList.Count;
      }
      // Phase 5 Task 8: frequency-stage assembly + seeded PopulateRandomNumbers. args =
      // [min_iterations, max_iterations, iteration_number, compute_is_deterministic (0/1)] --
      // see fixtures/compute/frequency_stage_sample.json's `note`.
      if (method == "frequency_stage_channel_yvals" || method == "frequency_stage_floodplain_yvals") {
        var cc = new ConvergenceCriteria(argsEl[0].GetInt32(), argsEl[1].GetInt32());
        simulation.PopulateRandomNumbers(cc);
        long iterationNumber = argsEl[2].GetInt64();
        bool computeIsDeterministic = argsEl[3].GetDouble() != 0.0;
        FrequencyStageCurves curves = simulation.GetFrequencyStageSample(computeIsDeterministic, iterationNumber);
        return method == "frequency_stage_channel_yvals" ? curves.ChannelStage.Yvals : curves.FloodplainStage.Yvals;
      }
      // Phase 5 Task 9: SetupPerformanceThresholds's own deterministic ComputeDefaultThreshold
      // pass. args = [min_iterations, max_iterations] (always [1, 1] in
      // fixtures/compute/default_threshold.json). Mirrors DefaultThresholdShould's
      // Compute(convergenceCriteria, new CancellationToken(), computeIsDeterministic: true) --
      // but calls SetupPerformanceThresholds directly rather than the full Compute()/
      // ComputeIterations path (not compiled into this subset-compiled emitter project, see the
      // patched file's header): SetupPerformanceThresholds's own internal deterministic pass fully
      // derives the default threshold's ThresholdValue, and nothing else downstream ever mutates it.
      if (method == "default_threshold_value") {
        var cc = new ConvergenceCriteria(argsEl[0].GetInt32(), argsEl[1].GetInt32());
        simulation.SetupPerformanceThresholds(cc);
        return simulation.ImpactAreaScenarioResultsForTest.PerformanceByThresholds.GetThreshold(0).ThresholdValue;
      }
      // Phase 5 Task 10: the full Compute()/ComputeIterations Monte Carlo loop's EAD oracle. args =
      // [min_iterations, max_iterations, compute_is_deterministic (0/1), impact_area_id,
      // damage_category, asset_category, consequence_type_name]. RiskType is never passed --
      // MeanExpectedAnnualConsequences's own RiskType.Total default is used (matches every
      // SimulationShould/TotalRiskShould test's own call).
      if (method == "mean_eac") {
        var cc = new ConvergenceCriteria(argsEl[0].GetInt32(), argsEl[1].GetInt32());
        bool computeIsDeterministic = argsEl[2].GetDouble() != 0.0;
        int impactAreaID = argsEl[3].GetInt32();
        string damageCategory = argsEl[4].GetString();
        string assetCategory = argsEl[5].GetString();
        var consequenceType = Enum.Parse<ConsequenceType>(argsEl[6].GetString());
        ImpactAreaScenarioResults results = simulation.Compute(cc, computeIsDeterministic);
        return results.MeanExpectedAnnualConsequences(impactAreaID, damageCategory, assetCategory, consequenceType);
      }
      // Phase 5 Task 10: PreviewCompute()'s single-deterministic-pass EAD oracle. args =
      // [damage_category, asset_category, impact_area_id]. RiskType is never passed --
      // SampleMeanDamage's own RiskType.Fail default is used, matching
      // StudyDataAnalyticalFrequencyResultsTests.ComputeMeanEAD_Test's
      // `ConsequenceResults.SampleMeanDamage(damCat, assetCat, impactAreaID)` call verbatim.
      if (method == "preview_mean_damage") {
        string damageCategory = argsEl[0].GetString();
        string assetCategory = argsEl[1].GetString();
        int impactAreaID = argsEl[2].GetInt32();
        ImpactAreaScenarioResults results = simulation.PreviewCompute();
        return results.ConsequenceResults.SampleMeanDamage(damageCategory, assetCategory, impactAreaID);
      }
      // Phase 5 Task 10: PerformanceTest.ComputeLeveeAEP_Test's levee-only (no stage damage)
      // MeanAEP oracle. args = [threshold_id, min_iterations, max_iterations,
      // compute_is_deterministic (0/1)].
      if (method == "mean_aep") {
        int thresholdID = argsEl[0].GetInt32();
        var cc = new ConvergenceCriteria(argsEl[1].GetInt32(), argsEl[2].GetInt32());
        bool computeIsDeterministic = argsEl[3].GetDouble() != 0.0;
        ImpactAreaScenarioResults results = simulation.Compute(cc, computeIsDeterministic);
        return results.MeanAEP(thresholdID);
      }
      // Phase 5 Task 11: PerformanceTest.ComputeConditionalNonExceedanceProbability_Test's seeded
      // assurance-of-threshold oracle. args = [threshold_id, min_iterations, max_iterations,
      // compute_is_deterministic (0/1), recurrence_interval, tolerance?] -- mirrors
      // `results.AssuranceOfEvent(thresholdID, recurrenceInterval)` verbatim. `tolerance` is an
      // OPTIONAL 6th arg (defaults to 0.01) matching the upstream test's `tolerance: .001`
      // ConvergenceCriteria override -- must match the fixture's `additional_threshold.cc`.
      if (method == "assurance_of_event") {
        int thresholdID = argsEl[0].GetInt32();
        double tolerance = argsEl.GetArrayLength() > 5 ? argsEl[5].GetDouble() : 0.01;
        var cc = new ConvergenceCriteria(argsEl[1].GetInt32(), argsEl[2].GetInt32(), 1.96039491692543, tolerance);
        bool computeIsDeterministic = argsEl[3].GetDouble() != 0.0;
        double recurrenceInterval = argsEl[4].GetDouble();
        ImpactAreaScenarioResults results = simulation.Compute(cc, computeIsDeterministic);
        return results.AssuranceOfEvent(thresholdID, recurrenceInterval);
      }
      throw new Exception("unknown simulation method: " + method);
    }

    // Scenario (Phase 6 Task 8): the impact-area fan-out built from patched/Scenario.cs (MVVM
    // messaging + CancellationToken dropped, the Compute() loop + ComputeDate/SoftwareVersion
    // stamp kept verbatim -- see that file's header). `construct.impact_areas` is a list of
    // entries in the exact BuildSimulation() shape (reused directly via the shared helper above),
    // one ImpactAreaScenarioSimulation per entry, moved into a single Scenario. `method` is always
    // mean_eac, args [min_iterations, max_iterations, compute_is_deterministic (0/1),
    // impact_area_id (or -999 for the DEFAULT_MISSING_VALUE wildcard), damage_category,
    // asset_category, consequence_type_name]: Compute(cc, computeIsDeterministic).
    // SampleMeanExpectedAnnualConsequences(impactAreaID, damageCategory, assetCategory,
    // consequenceType) -- RiskType is never passed, relying on that method's own RiskType.Fail
    // default (matches every consequence realization this construct's ComputeEAD path adds via
    // RiskType.Fail).
    static object EvalScenario(JsonElement caseEl, string method, JsonElement argsEl) {
      var c = caseEl.GetProperty("construct");
      var simulations = c.GetProperty("impact_areas").EnumerateArray().Select(BuildSimulation).ToList();
      IList<ImpactAreaScenarioSimulation> impactAreaSimulations = simulations;
      var scenario = new Scenario(impactAreaSimulations);

      if (method == "mean_eac") {
        var cc = new ConvergenceCriteria(argsEl[0].GetInt32(), argsEl[1].GetInt32());
        bool computeIsDeterministic = argsEl[2].GetDouble() != 0.0;
        int impactAreaID = argsEl[3].GetInt32();
        string damageCategory = argsEl[4].GetString();
        string assetCategory = argsEl[5].GetString();
        var consequenceType = Enum.Parse<ConsequenceType>(argsEl[6].GetString());
        ScenarioResults results = scenario.Compute(cc, computeIsDeterministic);
        return results.SampleMeanExpectedAnnualConsequences(impactAreaID, damageCategory, assetCategory, consequenceType);
      }
      throw new Exception("unknown scenario method: " + method);
    }

    // ContinuousDistributionExtensions.BootstrapToPairedData(this ContinuousDistribution, long
    // iterationNumber, double[] ExceedanceProbabilities, bool computeIsDeterministic) (Phase 5
    // Task 5) -- the analytical-frequency realization Task 8's EAD compute uses to turn a fitted
    // flow-frequency distribution into a PairedData flow-frequency curve, either deterministically
    // (the distribution's own fit) or via a seeded parametric bootstrap resample
    // (ContinuousDistribution.Sample(iterationNumber)). `construct` is {mean, standard_deviation,
    // skew, sample_size} for LogPearson3(mean, standardDeviation, skew, sampleSize). `seed` +
    // `quantity_of_samples`, if present on the case, call GenerateRandomSamplesofNumbers(seed,
    // quantityOfSamples) before BootstrapToPairedData (the seeded case; omitted for the
    // deterministic case, matching the C# path that never touches RandomSamplesofNumbers).
    // `iteration_number` + `compute_is_deterministic` are passed straight through, along with
    // DoubleGlobalStatics.RequiredExceedanceProbabilities (the fixed 173-point grid). `method` is
    // always bootstrap_to_paired_data_yvals (args []): the resulting PairedData's Yvals.
    static object EvalBootstrapToPairedData(JsonElement caseEl, string method) {
      var c = caseEl.GetProperty("construct");
      var lp3 = new LogPearson3(D(c.GetProperty("mean")), D(c.GetProperty("standard_deviation")), D(c.GetProperty("skew")), c.GetProperty("sample_size").GetInt32());
      if (caseEl.TryGetProperty("seed", out var seedEl)) {
        lp3.GenerateRandomSamplesofNumbers(seedEl.GetInt32(), caseEl.GetProperty("quantity_of_samples").GetInt32());
      }
      long iterationNumber = caseEl.GetProperty("iteration_number").GetInt64();
      bool computeIsDeterministic = caseEl.GetProperty("compute_is_deterministic").GetBoolean();
      PairedData pd = lp3.BootstrapToPairedData(iterationNumber, DoubleGlobalStatics.RequiredExceedanceProbabilities, computeIsDeterministic);
      if (method == "bootstrap_to_paired_data_yvals") return pd.Yvals;
      throw new Exception("unknown bootstrap_to_paired_data method: " + method);
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
              case "empirical_stacking": val = EvalEmpiricalStacking(c, method, argsEl); break;
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
              case "aggregated_consequences_by_quantile": val = EvalAggregatedConsequencesByQuantile(c, method, argsEl); break;
              case "study_area_consequences_binned": val = EvalStudyAreaConsequencesBinned(c, method, argsEl); break;
              case "study_area_consequences_by_quantile": val = EvalStudyAreaConsequencesByQuantile(c, method, argsEl); break;
              case "binned_to_quantile": val = EvalBinnedToQuantile(c, method, argsEl); break;
              case "categoried_uncertain_paired_data": val = EvalCategoriedUncertainPairedData(c, method, argsEl); break;
              case "assurance_result_storage": val = EvalAssuranceResultStorage(c, method, argsEl); break;
              case "system_performance_results": val = EvalSystemPerformanceResults(c, method, argsEl); break;
              case "performance_by_thresholds": val = EvalPerformanceByThresholds(c, method, argsEl); break;
              case "impact_area_scenario_results": val = EvalImpactAreaScenarioResults(c, method, argsEl); break;
              case "scenario_results": val = EvalScenarioResults(c, method, argsEl); break;
              case "alternative_results": val = EvalAlternativeResults(c, method, argsEl); break;
              case "alternative_comparison_report_results": val = EvalAlternativeComparisonReportResults(c, method, argsEl); break;
              case "alternative": val = EvalAlternative(c, method, argsEl); break;
              case "simulation": val = EvalSimulation(c, method, argsEl); break;
              case "scenario": val = EvalScenario(c, method, argsEl); break;
              case "bootstrap_to_paired_data": val = EvalBootstrapToPairedData(c, method); break;
              case "correct_dry_structure_wses": val = EvalHydraulicProfiles(c, method); break;
              case "stage_damage_geometry": val = EvalStageDamageGeometry(c, method, argsEl); break;
              case "impact_area_stage_damage": val = EvalImpactAreaStageDamage(c, method, argsEl); break;
              case "scenario_stage_damage": val = EvalScenarioStageDamage(c, a, method, argsEl); break;
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
