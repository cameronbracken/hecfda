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
using HEC.FDA.Model.paireddata;
using HEC.FDA.Model.compute;

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
    static object EvalPaired(JsonElement c, string method, JsonElement args) {
      var pd = new PairedData(DA(c.GetProperty("xs")), DA(c.GetProperty("ys")));
      return method switch {
        "f" => pd.f(D(args[0])), "f_inverse" => pd.f_inverse(D(args[0])),
        "integrate" => pd.Integrate(),
        _ => throw new Exception("unknown paired_data method: " + method) };
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
    static object EvalUpd(JsonElement caseEl, string method, JsonElement args) {
      var c = caseEl.GetProperty("construct");
      double[] xs = DA(c.GetProperty("xs"));
      var ys = c.GetProperty("ys").EnumerateArray()
        .Select(y => (IDistribution)new Normal(D(y.GetProperty("mean")), D(y.GetProperty("sd")))).ToArray();
      var upd = new UncertainPairedData(xs, ys, new CurveMetaData("x","y","oracle"));
      if (method == "sample_and_integrate") {
        int seed = caseEl.GetProperty("seed").GetInt32();
        double p = new RandomProvider(seed).NextRandom();
        return upd.SamplePairedDataRaw(p).Integrate();
      }
      throw new Exception("unknown uncertain_paired_data method: " + method);
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
              case "paired_data": val = EvalPaired(c.GetProperty("construct"), method, argsEl); break;
              case "special_functions": val = EvalSpecial(method, argsEl); break;
              case "sample_statistics": val = EvalSampleStatistics(c.GetProperty("construct"), method); break;
              case "uncertain_paired_data": val = EvalUpd(c, method, argsEl); break;
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
