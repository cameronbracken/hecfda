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
