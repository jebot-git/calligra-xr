#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent mpmath/Fraction distribution cases and untouched FODS caches."""

import argparse
from fractions import Fraction
import json
import math
import os
from pathlib import Path
import random
import subprocess
import xml.etree.ElementTree as ET
import mpmath as mp

ROOT = Path(__file__).resolve().parents[2]
mp.mp.dps = 100


def run(binary, mode, text=None, files=()):
    return subprocess.check_output(
        [str(binary.resolve()), mode, *map(str, files)],
        input=text,
        text=True,
        env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
    )


def independent(binary):
    cases = []
    inverses = []
    rng = random.Random(20261103)

    def add(name, args, expected):
        # Evaluate the actual binary cell inputs, not their decimal spelling.
        formula = name + "(" + ",".join(format(float(a), ".17g") for a in args) + ")"
        cases.append((formula, float(expected)))

    def normal(x):
        return mp.erfc(-x / mp.sqrt(2)) / 2

    def beta(x, a, b):
        return mp.betainc(a, b, 0, x, regularized=True)

    def gamma(x, a):
        return mp.gammainc(a, 0, x, regularized=True)

    def student(x, df):
        q = mp.betainc(df / 2, mp.mpf(".5"), 0, df / (df + x * x), regularized=True) / 2
        return q if x < 0 else 1 - q

    def inv(name, args, p, cdf):
        formula = name + "(" + ",".join(format(float(a), ".17g") for a in args) + ")"
        inverses.append((formula, mp.mpf(p), cdf))

    for _ in range(35):
        a, b = map(mp.mpf, [10 ** rng.uniform(-1, 2), 10 ** rng.uniform(-1, 2)])
        x = mp.mpf(rng.uniform(0.0001, 0.9999))
        add("BETADIST", [x, a, b], beta(x, a, b))
        add(
            "BETA.DIST", [x, a, b, 0], x ** (a - 1) * (1 - x) ** (b - 1) / mp.beta(a, b)
        )
        p = mp.mpf(rng.uniform(0.001, 0.999))
        inv("BETAINV", [p, a, b], p, lambda q, a=a, b=b: beta(q, a, b))
        z = mp.mpf(rng.uniform(-15, 15))
        add("NORMSDIST", [z], normal(z))
        add("PHI", [z], mp.exp(-z * z / 2) / mp.sqrt(2 * mp.pi))
        add("GAUSS", [z], mp.erf(z / mp.sqrt(2)) / 2)
        add("NORMSINV", [p], mp.sqrt(2) * mp.erfinv(2 * p - 1))
        logx, mean, sd = z / 3, mp.mpf(rng.uniform(-2, 2)), mp.mpf(rng.uniform(0.1, 3))
        lx = mp.mpf(float(mp.exp(logx)))
        standardized = (mp.log(lx) - mean) / sd
        add("LOGNORMDIST", [lx, mean, sd], normal(standardized))
        add(
            "LOGNORM.DIST",
            [lx, mean, sd, 0],
            mp.exp(-(standardized**2) / 2) / (lx * sd * mp.sqrt(2 * mp.pi)),
        )
        add(
            "LOGINV",
            [p, mean, sd],
            mp.exp(mean + sd * mp.sqrt(2) * mp.erfinv(2 * p - 1)),
        )
        rate, positive = a / 5, mp.mpf(rng.uniform(0.001, 20))
        add("EXPONDIST", [positive, rate], -mp.expm1(-rate * positive))
        add("EXPON.DIST", [positive, rate, 0], rate * mp.exp(-rate * positive))
        # Shape/scale rather than a rate; chosen unequal to catch confusion.
        add("GAMMADIST", [positive, a, b], gamma(positive / b, a))
        add(
            "GAMMA.DIST",
            [positive, a, b, 0],
            positive ** (a - 1) * mp.exp(-positive / b) / (mp.gamma(a) * b**a),
        )
        inv("GAMMAINV", [p, a, b], p, lambda q, a=a, b=b: gamma(q / b, a))
        df, df2 = mp.mpf(rng.randint(1, 100)), mp.mpf(rng.randint(1, 100))
        add("CHISQDIST", [positive, df], gamma(positive / 2, df / 2))
        add(
            "CHIDIST",
            [positive, df],
            mp.gammainc(df / 2, positive / 2, mp.inf, regularized=True),
        )
        inv("CHISQINV", [p, df], p, lambda q, df=df: gamma(q / 2, df / 2))
        inv(
            "CHISQ.INV.RT",
            [p, df],
            p,
            lambda q, df=df: mp.gammainc(df / 2, q / 2, mp.inf, regularized=True),
        )
        fz = df * positive / (df * positive + df2)
        add("FDIST", [positive, df, df2], beta(fz, df / 2, df2 / 2))
        add(
            "F.DIST.RT",
            [positive, df, df2],
            mp.betainc(df / 2, df2 / 2, fz, 1, regularized=True),
        )
        add(
            "F.DIST",
            [positive, df, df2, 0],
            (df / df2) ** (df / 2)
            * positive ** (df / 2 - 1)
            / (
                mp.beta(df / 2, df2 / 2) * (1 + df * positive / df2) ** ((df + df2) / 2)
            ),
        )
        inv(
            "FINV",
            [p, df, df2],
            p,
            lambda q, df=df, df2=df2: beta(df * q / (df * q + df2), df / 2, df2 / 2),
        )
        inv(
            "F.INV.RT",
            [p, df, df2],
            p,
            lambda q, df=df, df2=df2: mp.betainc(
                df / 2, df2 / 2, df * q / (df * q + df2), 1, regularized=True
            ),
        )
        add("T.DIST", [z, df, 1], student(z, df))
        add(
            "T.DIST",
            [z, df, 0],
            mp.gamma((df + 1) / 2)
            / (mp.sqrt(df * mp.pi) * mp.gamma(df / 2))
            * (1 + z * z / df) ** (-(df + 1) / 2),
        )
        add("T.DIST.RT", [z, df], student(-z, df))
        add("TDIST", [abs(z), df, 2], 2 * student(-abs(z), df))
        inv("T.INV", [p, df], p, lambda q, df=df: student(q, df))
        inv("TINV", [p, df], p, lambda q, df=df: 2 * student(-q, df))
        add("WEIBULL", [positive, a, b, 1], -mp.expm1(-((positive / b) ** a)))
        add(
            "WEIBULL",
            [positive, a, b, 0],
            a / b * (positive / b) ** (a - 1) * mp.exp(-((positive / b) ** a)),
        )
        add("GAMMA", [a], mp.gamma(a))
        add("GAMMALN", [a], mp.loggamma(a))
        r = mp.mpf(rng.uniform(-0.999, 0.999))
        add("FISHER", [r], mp.atanh(r))
        add("FISHERINV", [z], mp.tanh(z))
        add(
            "CONFIDENCE",
            [p, b, 100.5],
            mp.sqrt(2) * mp.erfinv(1 - p) * b / mp.sqrt(mp.mpf("100.5")),
        )
        add("CONFIDENCE.NORM", [p, b, 100.5], mp.sqrt(2) * mp.erfinv(1 - p) * b / 10)
        inv(
            "CONFIDENCE.T",
            [p, b, 100.5],
            p / 2,
            lambda q, b=b: student(-q * 10 / b, mp.mpf(99)),
        )
    for _ in range(100):
        n = rng.randint(0, 200)
        p = Fraction(rng.choice([0.125, 0.25, 0.5, 0.75, 0.875]))
        x = rng.randint(0, n)
        masses = [
            Fraction(math.comb(n, k)) * p**k * (1 - p) ** (n - k) for k in range(n + 1)
        ]
        add("BINOMDIST", [x, n, p, 0], masses[x])
        add("BINOMDIST", [x, n, p, 1], sum(masses[: x + 1]))
        high = rng.randint(x, n)
        add("B", [n, p, x, high], sum(masses[x : high + 1]))
        alpha = rng.choice([0.125, 0.5, 0.875])
        cumulative = Fraction(0)
        quantile = 0
        for quantile, mass in enumerate(masses):
            cumulative += mass
            if cumulative >= alpha:
                break
        add("CRITBINOM", [n, p, alpha], quantile)
        pop = rng.randint(1, 100)
        draws, successes = rng.randint(0, pop), rng.randint(0, pop)
        x = rng.randint(0, draws)

        def mass(k):
            return (
                Fraction(
                    math.comb(successes, k) * math.comb(pop - successes, draws - k),
                    math.comb(pop, draws),
                )
                if 0 <= k <= successes and 0 <= draws - k <= pop - successes
                else Fraction(0)
            )

        add("HYPGEOMDIST", [x, draws, successes, pop], mass(x))
        add(
            "HYPGEOM.DIST",
            [x, draws, successes, pop, 1],
            sum(mass(k) for k in range(x + 1)),
        )
        x, r = rng.randint(0, 100), rng.randint(1, 100)
        add(
            "NEGBINOMDIST",
            [x, r, p],
            Fraction(math.comb(x + r - 1, x)) * p**r * (1 - p) ** x,
        )
        add(
            "NEGBINOM.DIST",
            [x, r, p, 1],
            sum(
                Fraction(math.comb(k + r - 1, k)) * p**r * (1 - p) ** k
                for k in range(x + 1)
            ),
        )
        mean = mp.mpf(rng.uniform(0.001, 100))
        add("POISSON.DIST", [x, mean, 0], mean**x * mp.exp(-mean) / mp.factorial(x))
        add("POISSON", [x, mean], mp.gammainc(x + 1, mean, mp.inf, regularized=True))
    # Very small tails must remain relative-accurate, not pass an absolute-zero
    # tolerance. Tiny near-zero CDFs also exercise expm1/erf cancellation paths.
    for x in [-37.0, -20.0, -10.0, -1e-100, 0.0, 1e-100]:
        q = mp.mpf(x)
        add("NORMSDIST", [x], normal(q))
        add("GAUSS", [x], mp.erf(q / mp.sqrt(2)) / 2)
    add("CHIDIST", [1000, 2], mp.exp(-500))
    add("F.DIST.RT", [1e100, 2, 2], 1 / (1 + mp.mpf(1e100)))
    add(
        "B",
        [100, 0.5, 90, 95],
        sum(Fraction(math.comb(100, k), 2**100) for k in range(90, 96)),
    )
    for n in [2000000, 2000000000000, 2000000000000000]:
        k = n // 2
        add(
            "B",
            [n, 0.5, k, k + 1],
            (mp.binomial(n, k) + mp.binomial(n, k + 1)) * mp.power(mp.mpf(".5"), n),
        )
    results = [
        json.loads(line)
        for line in run(
            binary,
            "--eval",
            "".join(f + "\n" for f, _ in cases + [(f, 0) for f, _, _ in inverses]),
        ).splitlines()
    ]
    for (formula, want), got in zip(cases, results, strict=False):
        assert not got["incomplete"] and got["kind"] == 1, (formula, got, want)
        assert math.isclose(got["value"], want, rel_tol=3e-12, abs_tol=5e-324), (
            formula,
            got,
            want,
        )
    for (formula, p, cdf), got in zip(inverses, results[len(cases) :], strict=True):
        assert not got["incomplete"] and got["kind"] == 1, (formula, got)
        actual = cdf(mp.mpf(got["value"]))
        endpoints = [
            cdf(mp.mpf(math.nextafter(got["value"], direction)))
            for direction in [-math.inf, math.inf]
        ]
        tolerance = mp.mpf("3e-12") * min(p, 1 - p)
        assert min(endpoints) - tolerance <= p <= max(endpoints) + tolerance, (
            formula,
            got,
            str(actual),
            str(p),
        )
    return {
        "direct_numeric_cases": len(cases),
        "independent_inverse_cdf_cases": len(inverses),
        "total": len(results),
        "precision_digits": mp.mp.dps,
    }


def retained(binary):
    # A dedicated selector leaves earlier order/moment/paired audit populations
    # stable when new distribution helper rows become evaluable.
    names = "beta.inv beta.dist betainv betadist binom.dist binom.inv binomdist critbinom chidist chiinv chisq.dist chisq.dist.rt chisq.inv chisq.inv.rt chisqdist chisqinv confidence confidence.norm confidence.t expon.dist expondist f.dist f.dist.rt f.inv f.inv.rt fdist finv fisher fisherinv gamma.dist gamma.inv gammadist gammainv gauss hypgeom.dist hypgeomdist loginv lognorm.inv lognorm.dist lognormdist negbinom.dist negbinomdist norm.dist norm.inv norm.s.dist norm.s.inv normdist norminv normsdist normsinv phi poisson poisson.dist t.dist t.dist.2t t.dist.rt t.inv.2t tdist tinv weibull weibull.dist".split()
    paths = [
        ROOT / "sheets/tests/data/fods/statistical/fods" / (n + ".fods") for n in names
    ]
    paths.append(ROOT / "sheets/tests/data/fods/financial/t.inv.fods")
    records = json.loads(run(binary, "--distribution-fods", files=paths))
    counts = {
        "cases": len(records),
        "agreeing": 0,
        "error_label_differences": 0,
        "unavailable": 0,
        "differences": [],
    }
    for r in records:
        got = r["actual"]
        if got["incomplete"]:
            counts["unavailable"] += 1
            counts["differences"].append(r)
        elif r["expected_error"]:
            if got["kind"] == 4:
                counts[
                    "agreeing"
                    if got["display"] == r["expected_text"]
                    else "error_label_differences"
                ] += 1
            else:
                counts["differences"].append(r)
        elif (
            "expected_value" in r
            and got["kind"] == 1
            and math.isclose(
                got["value"], r["expected_value"], rel_tol=3e-10, abs_tol=1e-14
            )
        ):
            counts["agreeing"] += 1
        else:
            counts["differences"].append(r)
    allowed = {
        **{
            ("beta.dist", 1, r, 1): (
                0.685470581054688,
                "#N/A",
                "Fixed result has one row; native pads the declared five-row block with #N/A, while Calc repeats the first row.",
            )
            for r in range(27, 31)
        },
        ("expon.dist", 1, 8, 1): (
            0.0,
            "#NUM!",
            "Microsoft EXPON.DIST rejects negative x; the retained Calc cache uses the ODF extension.",
        ),
        ("f.dist", 1, 7, 1): (
            "Err:502",
            0.0,
            "ODF FDIST returns zero below its support; this retained cache is an error.",
        ),
        ("f.inv", 1, 13, 1): (
            "Err:502",
            0.0,
            "ODF FINV explicitly accepts probability zero and returns zero.",
        ),
        ("lognorm.dist", 1, 27, 1): (
            0.0,
            "#NUM!",
            "Microsoft LOGNORM.DIST rejects nonpositive x; Calc cached zero.",
        ),
        ("lognorm.dist", 1, 28, 1): (
            0.0,
            "#NUM!",
            "Microsoft LOGNORM.DIST rejects nonpositive x; Calc cached zero.",
        ),
    }
    seen = set()
    for r in counts["differences"]:
        if r["actual"]["incomplete"]:
            # Omitted arguments/malformed syntax and a named undefined input
            # remain parser/model work. Keep their source caches untouched.
            assert r["actual"]["display"] in ("#PARSE!", "#NAME?"), r
            continue
        key = (Path(r["file"]).stem, r["sheet"], r["row"], r["column"])
        if key == ("chiinv", 1, 8, 1):
            table_ns = "{urn:oasis:names:tc:opendocument:xmlns:table:1.0}"
            office = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
            tables = list(ET.parse(r["file"]).getroot().iter(table_ns + "table"))
            cells = (
                tables[1]
                .findall(table_ns + "table-row")[7]
                .findall(table_ns + "table-cell")
            )
            p = mp.mpf(float(cells[5].get(office + "value")))
            df = float(cells[6].get(office + "value"))
            assert df == 10
            # For df=10 the survival function is a finite elementary sum.
            survival = lambda x: (
                mp.exp(-x / 2) * sum((x / 2) ** k / mp.factorial(k) for k in range(5))
            )
            lo, hi = mp.mpf(0), mp.mpf(1)
            for _ in range(300):
                mid = (lo + hi) / 2
                if survival(mid) > p:
                    lo = mid
                else:
                    hi = mid
            oracle = float((lo + hi) / 2)
            assert math.isclose(r["actual"]["value"], oracle, rel_tol=2e-13), r
            r["independent_reference"] = oracle
            r["reason"] = (
                "Independent 100-digit finite survival sum resolves the inaccurate old extreme-tail inverse cache."
            )
        else:
            assert key in allowed, r
            old, want, reason = allowed[key]
            assert r.get("expected_value", r["expected_text"]) == old, r
            got = r["actual"].get("value", r["actual"]["display"])
            assert got == want, r
            r["reason"] = reason
        seen.add(key)
    assert seen == set(allowed) | {("chiinv", 1, 8, 1)}, seen
    assert (
        counts["cases"],
        counts["agreeing"],
        counts["error_label_differences"],
        counts["unavailable"],
    ) == (2745, 2458, 250, 27), counts
    assert len(counts["differences"]) == 37
    return counts


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-bin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = {
        "independent": independent(args.test_bin),
        "retained": retained(args.test_bin),
    }
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print("PASS independent distribution reference", result["independent"], flush=True)
    print(
        "Retained caches",
        {k: v if k != "differences" else len(v) for k, v in result["retained"].items()},
        flush=True,
    )
