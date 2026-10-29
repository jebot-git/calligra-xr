// SPDX-FileCopyrightText: 1998-2002 The KSpread Team <calligra-devel@kde.org>
// SPDX-FileCopyrightText: 2005 Tomas Mecir <mecirt@gmail.com>
// SPDX-FileCopyrightText: 2007 Sascha Pfau <MrPeacock@gmail.com>
// SPDX-License-Identifier: LGPL-2.0-only
// Native names/aliases ported from sheets/functions/engineering.cpp.
// Factors follow ODF 1.4 Part 4, table 24, and NIST SP 811 Appendix B.
// Exact inch/pound/gravity definitions replace rounded legacy coefficients.
// Astronomical distances use the modern IAU parsec and Julian year; atomic
// mass uses CODATA 2022. BTU is the International Table definition.
#pragma once
#include "native_types.h"
#include <numbers>
namespace DomeOffice {
struct EngineeringUnits {
  Text dimension;
  std::map<Text, double> inverse;
};
inline const std::vector<EngineeringUnits> &engineering_units() {
  static const auto groups = [] {
    std::vector<EngineeringUnits> result;
    {
      std::map<Text, double> entries;
      entries["g"] = 1.0;
      entries["sg"] = 1.0 / (453.59237 * 9.80665 / 0.3048);
      entries["lbm"] = 1.0 / 453.59237;
      entries["u"] = 1.0 / 1.66053906892e-24;
      entries["ozm"] = 16.0 / 453.59237;
      entries["stone"] = 1.0 / (14 * 453.59237);
      entries["ton"] = 1.0 / (2000 * 453.59237);
      entries["grain"] = 7000.0 / 453.59237;
      entries["pweight"] = 1.0 / 1.55517384;
      entries["hweight"] = 1.0 / (112 * 453.59237);
      entries["shweight"] = 1.0 / (100 * 453.59237);
      entries["cwt"] = 2.2046226218487758E-05;
      entries["uk_cwt"] = 1.9684130552221213E-05;
      entries["lcwt"] = entries.at("uk_cwt");
      entries["uk_ton"] = 9.8420652761106063E-07;
      entries["LTON"] = entries.at("uk_ton");
      entries["brton"] = entries.at("uk_ton");
      result.push_back({"mass", std::move(entries)});
    }
    {
      std::map<Text, double> entries;
      entries["m"] = 1.0;
      entries["ang"] = 1e10;
      entries["ell"] = 1.0 / (45 * 0.0254);
      entries["ft"] = 1.0 / (12.0 * 0.0254);
      entries["in"] = 1.0 / 0.0254;
      entries["lightyear"] = 1.0 / (299792458.0 * 86400 * 365.25);
      entries["ly"] = 1.0 / (299792458.0 * 86400 * 365.25);
      entries["mi"] = 6.2137119223733397e-4;
      entries["Nmi"] = 5.3995680345572354e-04;
      entries["parsec"] = std::numbers::pi / (149597870700.0 * 648000);
      entries["pc"] = std::numbers::pi / (149597870700.0 * 648000);
      entries["Pica"] = 1.0 * 72 / 0.0254;
      entries["picapt"] = 2.8346456692913386E03;
      entries["pica"] = 2.36220472441E02;
      entries["survey_mi"] = 6.2136994949494949E-04;
      entries["statute_mi"] = 6.2136994949494949E-04;
      entries["yd"] = 1.0 / (3.0 * 12.0 * 0.0254);
      result.push_back({"distance", std::move(entries)});
    }
    {
      std::map<Text, double> entries;
      entries["Pa"] = 1.0;
      entries["p"] = 1.0;
      entries["P"] = 1.0;
      entries["SI_at"] = 1.0 / 98066.5;
      entries["atm"] = 1.0 / 101325;
      entries["at"] = entries.at("atm");
      entries["mmHg"] = 1.0 / 133.322387415;
      entries["psi"] = 0.0254 * 0.0254 / (0.45359237 * 9.80665);
      entries["Torr"] = 760.0 / 101325;
      result.push_back({"pressure", std::move(entries)});
    }
    {
      std::map<Text, double> entries;
      entries["N"] = 1.0;
      entries["dy"] = 1.0e5;
      entries["dyn"] = 1.0e5;
      entries["lbf"] = 1.0 / (0.45359237 * 9.80665);
      entries["pond"] = 1.0 / 0.00980665;
      result.push_back({"force", std::move(entries)});
    }
    {
      std::map<Text, double> entries;
      entries["J"] = 1.0;
      entries["e"] = 1.0e7;
      entries["c"] = 1.0 / 4.184;
      entries["cal"] = 1.0 / 4.1868;
      entries["eV"] = 1.0 / 1.602176634e-19;
      entries["HPh"] = 1.0 / (550 * 0.3048 * 0.45359237 * 9.80665 * 3600);
      entries["Wh"] = 2.7777777777777778e-4;
      entries["flb"] = 1.0 / (0.3048 * 0.45359237 * 9.80665);
      entries["BTU"] = 1.0 / 1055.05585262;
      entries["btu"] = entries.at("BTU");
      entries["ev"] = entries.at("eV");
      entries["hh"] = entries.at("HPh");
      entries["wh"] = entries.at("Wh");
      result.push_back({"energy", std::move(entries)});
    }
    {
      std::map<Text, double> entries;
      entries["W"] = 1.0;
      entries["HP"] = 1.0 / (550 * 0.3048 * 0.45359237 * 9.80665);
      entries["PS"] = 1.0 / (75 * 9.80665);
      entries["w"] = entries.at("W");
      entries["h"] = entries.at("HP");
      result.push_back({"power", std::move(entries)});
    }
    {
      std::map<Text, double> entries;
      entries["T"] = 1.0;
      entries["ga"] = 1.0e4;
      result.push_back({"magnetism", std::move(entries)});
    }
    {
      std::map<Text, double> entries;
      entries["l"] = 1.0;
      entries["ang3"] = 1.0e27;
      entries["barrel"] = 6.2898107704321051e-3;
      entries["bushel"] = 1.0 / (2150.42 * 0.0254 * 0.0254 * 0.0254 * 1000);
      entries["cup"] = 4.2267528377303746;
      entries["ft3"] = 3.5314666721488590e-2;
      entries["gal"] = 2.6417205235814842e-1;
      entries["in3"] = 6.1023744094732284e1;
      entries["m3"] = 1.0e-3;
      entries["mi3"] = 2.3991275857892772e-13;
      entries["MTON"] = 1.0 / (40 * 0.3048 * 0.3048 * 0.3048 * 1000);
      entries["Nmi3"] = 1.5742621468581148e-13;
      entries["oz"] = 3.3814022701842997e1;
      entries["Pica3"] = 2.2776990435870636e7;
      entries["picapt3"] = 2.2776990435870636e7;
      entries["pica3"] = 1.31811287245e4;
      entries["pt"] = 2.1133764188651873;
      entries["qt"] = 1.0566882094325937;
      entries["GRT"] = 1.0 / (100 * 0.3048 * 0.3048 * 0.3048 * 1000);
      entries["regton"] = entries.at("GRT");
      entries["tbs"] = 6.7628045403685994e1;
      entries["tsp"] = 2.0288413621105798e2;
      entries["tspm"] = 2.0e2;
      entries["uk_gal"] = 2.1996924829908779e-1;
      entries["uk_pt"] = 1.7597539863927023;
      entries["uk_qt"] = 8.7987699319635115e-1;
      entries["ly3"] = std::pow(1.0 / (299792458.0 * 86400 * 365.25), 3) / 1000;
      entries["yd3"] = 1.3079506193143922e-3;
      entries["ang^3"] = entries.at("ang3");
      entries["ft^3"] = entries.at("ft3");
      entries["lt"] = entries.at("l");
      entries["L"] = entries.at("l");
      entries["in^3"] = entries.at("in3");
      entries["ly^3"] = entries.at("ly3");
      entries["m^3"] = entries.at("m3");
      entries["mi^3"] = entries.at("mi3");
      entries["Nmi^3"] = entries.at("Nmi3");
      entries["Pica^3"] = entries.at("Pica3");
      entries["picapt^3"] = entries.at("picapt3");
      entries["pica^3"] = entries.at("pica3");
      entries["us_pt"] = entries.at("pt");
      entries["yd^3"] = entries.at("yd3");
      result.push_back({"volume", std::move(entries)});
    }
    {
      std::map<Text, double> entries;
      entries["m2"] = 1.0;
      entries["m^2"] = 1.0;
      entries["acre"] = 2.471053815e-4;
      entries["uk_acre"] = 2.4710538146716534e-4;
      entries["us_acre"] = 2.4710439304662790e-4;
      entries["ang2"] = 1.0e20;
      entries["ar"] = 1.0 / 100;
      entries["ft2"] = 1.0763910416709722e1;
      entries["ft^2"] = 1.0763910416709722e1;
      entries["ha"] = 1.0e-4;
      entries["in2"] = 1.5500031000062000e3;
      entries["in^2"] = 1.5500031000062000e3;
      entries["mi2"] = 3.8610215854244585e-7;
      entries["mi^2"] = 3.8610215854244585e-7;
      entries["Nmi2"] = 2.9155334959812286e-7;
      entries["Nmi^2"] = 2.9155334959812286e-7;
      entries["yd2"] = 1.1959900463010803;
      entries["yd^2"] = 1.1959900463010803;
      entries["Pica2"] = 8.0352160704321409e6;
      entries["picapt2"] = 8.0352160704321409e6;
      entries["pica2"] = 5.58001116002232e4;
      entries["Morgen"] = 4.0e-4;
      entries["ly2"] = std::pow(1.0 / (299792458.0 * 86400 * 365.25), 2);
      entries["ang^2"] = entries.at("ang2");
      entries["ly^2"] = entries.at("ly2");
      entries["Pica^2"] = entries.at("Pica2");
      entries["picapt^2"] = entries.at("picapt2");
      result.push_back({"area", std::move(entries)});
    }
    {
      std::map<Text, double> entries;
      entries["m/s"] = 1.0;
      entries["m/h"] = 3.6e3;
      entries["mph"] = 2.2369362920544023;
      entries["kn"] = 1.9438444924406048;
      entries["admkn"] = 3600.0 / (6080 * 0.3048);
      entries["m/hr"] = entries.at("m/h");
      entries["m/sec"] = entries.at("m/s");
      result.push_back({"speed", std::move(entries)});
    }
    {
      std::map<Text, double> entries;
      entries["s"] = 1.0;
      entries["sec"] = 1.0;
      entries["mn"] = 1.0 / 60;
      entries["min"] = 1.0 / 60;
      entries["hr"] = 1.0 / 3600;
      entries["d"] = 1.0 / (3600 * 24);
      entries["day"] = 1.0 / (3600 * 24);
      entries["yr"] = 1.0 / (3600 * 24 * 365.25);
      result.push_back({"time", std::move(entries)});
    }
    {
      std::map<Text, double> entries;
      entries["bit"] = 1.0;
      entries["byte"] = 1.0 / 8;
      result.push_back({"info", std::move(entries)});
    }
    return result;
  }();
  return groups;
}
} // namespace DomeOffice
