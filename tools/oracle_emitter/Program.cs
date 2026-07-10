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
    static object EvalNormal(JsonElement c, string method, JsonElement args) {
      var d = new Normal(D(c.GetProperty("mean")), D(c.GetProperty("sd")));
      double x = D(args[0]);
      return method switch {
        "pdf" => d.PDF(x), "cdf" => d.CDF(x), "inverse_cdf" => d.InverseCDF(x),
        _ => throw new Exception("unknown normal method: " + method) };
    }
    static object EvalPaired(JsonElement c, string method, JsonElement args) {
      var pd = new PairedData(DA(c.GetProperty("xs")), DA(c.GetProperty("ys")));
      return method switch {
        "f" => pd.f(D(args[0])), "f_inverse" => pd.f_inverse(D(args[0])),
        "integrate" => pd.Integrate(),
        _ => throw new Exception("unknown paired_data method: " + method) };
    }
    static object EvalUpd(JsonElement c, string method, JsonElement args) {
      double[] xs = DA(c.GetProperty("xs"));
      var ys = c.GetProperty("ys").EnumerateArray()
        .Select(y => (IDistribution)new Normal(D(y.GetProperty("mean")), D(y.GetProperty("sd")))).ToArray();
      var upd = new UncertainPairedData(xs, ys, new CurveMetaData("x","y","oracle"));
      if (method == "sample_and_integrate") {
        int seed = (int)D(args[0]);
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
              case "normal": val = EvalNormal(c.GetProperty("construct"), method, argsEl); break;
              case "paired_data": val = EvalPaired(c.GetProperty("construct"), method, argsEl); break;
              case "uncertain_paired_data": val = EvalUpd(c.GetProperty("construct"), method, argsEl); break;
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
