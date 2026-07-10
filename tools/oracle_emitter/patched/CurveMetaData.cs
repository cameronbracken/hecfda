// PATCHED LOCAL COPY of HEC.FDA.Model/paireddata/CurveMetaData.cs @ f63682a86a30dc306a105689714a92bfd95956c5
// Fields + constructors retained verbatim. WriteToXML/ReadFromXML are replaced with trivial stubs
// (the oracle gate never serializes) so the upstream UncertainPairedData.cs XML methods still
// compile, while avoiding the HEC.FDA.Model.utilities.Serialization XML closure.
﻿using HEC.FDA.Model.utilities;
using System;
using System.Xml.Linq;

namespace HEC.FDA.Model.paireddata
{
    [StoredProperty("CurveMetaData")]
    public class CurveMetaData
    {
        [StoredProperty("XLabel")]
        public string XLabel { get; }
        // I'm opening this property up for modification because we're inconsistent with setting it initially, and as
        // as stop-gap it's useful to be able to set it properly before it's needed. 
        [StoredProperty("YLabel")]
        public string YLabel { get; set; }
        [StoredProperty("Name")]
        public string Name { get; }
        [StoredProperty("DamCat")]
        public string DamageCategory { get; }
        [StoredProperty("AssetCat")]
        public string AssetCategory { get; }
        [StoredProperty("IsNull")]
        public bool IsNull { get; set; }
        [StoredProperty("ImpactAreaID")]
        public int ImpactAreaID { get; } = 0;
        public CurveMetaData()
        {
            XLabel = "xlabel";
            YLabel = "ylabel";
            Name = "unnamed";
            DamageCategory = "unassiged";
            AssetCategory = "unassigned";
            IsNull = true;
        }
        public CurveMetaData(string damageCategory, string assetCategory = "unassigned")
        {
            XLabel = "xlabel";
            YLabel = "ylabel";
            Name = "unnamed";
            DamageCategory = damageCategory;
            AssetCategory = assetCategory;
            IsNull = false;
        }
        public CurveMetaData(string xlabel, string ylabel, string name, string damageCategory, string assetCategory = "unassigned")
        {
            XLabel = xlabel;
            YLabel = ylabel;
            Name = name;
            DamageCategory = damageCategory;
            AssetCategory = assetCategory;
            IsNull = false;
        }
        public CurveMetaData(string xlabel, string ylabel, string name)
        {
            XLabel = xlabel;
            YLabel = ylabel;
            Name = name;
            DamageCategory = "unassigned";
            AssetCategory = "unassigned";
            IsNull = false;
        }
        public CurveMetaData(string xlabel, string ylabel, string name, string damageCategory, int impactAreaID, string assetCategory = "unassigned")
        {
            XLabel = xlabel;
            YLabel = ylabel;
            Name = name;
            DamageCategory = damageCategory;
            AssetCategory = assetCategory;
            IsNull = false;
            ImpactAreaID = impactAreaID;
        }
        public System.Xml.Linq.XElement WriteToXML() => new System.Xml.Linq.XElement("CurveMetaData");
        public static CurveMetaData ReadFromXML(System.Xml.Linq.XElement xElement) => new CurveMetaData();
    }
}
