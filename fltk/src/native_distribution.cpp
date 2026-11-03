// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_distribution.h"
#include <boost/math/distributions/beta.hpp>
#include <boost/math/distributions/binomial.hpp>
#include <boost/math/distributions/chi_squared.hpp>
#include <boost/math/distributions/fisher_f.hpp>
#include <boost/math/distributions/gamma.hpp>
#include <boost/math/distributions/hypergeometric.hpp>
#include <boost/math/distributions/negative_binomial.hpp>
#include <boost/math/distributions/normal.hpp>
#include <boost/math/distributions/poisson.hpp>
#include <boost/math/distributions/students_t.hpp>
#include <boost/math/special_functions/gamma.hpp>
#include <cmath>
#include <limits>
namespace DomeOffice {
namespace {
using V = NativeValue;
namespace M = boost::math;
namespace P = boost::math::policies;
// Underflow to zero is a valid probability. A convergence failure is instead
// incomplete, so it cannot replace an imported cache or be hidden by IFERROR.
using Policy =
    P::policy<P::underflow_error<P::ignore_error>,
              P::denorm_error<P::ignore_error>,
              P::max_series_iterations<100000>, P::max_root_iterations<256>>;
template <template <class, class> class D> using Dist = D<long double, Policy>;
V numeric(long double n) { return V::numeric(static_cast<double>(n)); }
V num_error() { return V::error("#NUM!"); }
V limit() { return V::error("#LIMIT!", true); }
bool probability(long double p) { return p >= 0 && p <= 1; }
template <class D>
long double value(const D &d, long double x, bool cumulative) {
  return cumulative ? M::cdf(d, x) : M::pdf(d, x);
}
template <class D> long double tail(const D &d, long double x) {
  return M::cdf(M::complement(d, x));
}
template <class D>
long double inverse(const D &d, long double p, bool right = false) {
  return right ? M::quantile(M::complement(d, p)) : M::quantile(d, p);
}
} // namespace
const NativeSignatures &distribution_signatures() {
  static const NativeSignatures names = {{"BETADIST", {3, 6}},
                                         {"BETA.DIST", {4, 6}},
                                         {"BETAINV", {3, 5}},
                                         {"B", {3, 4}},
                                         {"BINO", {3, 3}},
                                         {"BINOMDIST", {4, 4}},
                                         {"CRITBINOM", {3, 3}},
                                         {"CHIDIST", {2, 2}},
                                         {"CHISQ.DIST.RT", {2, 2}},
                                         {"CHISQDIST", {2, 3}},
                                         {"CHISQ.DIST", {3, 3}},
                                         {"CHISQINV", {2, 2}},
                                         {"LEGACYCHIINV", {2, 2}},
                                         {"CONFIDENCE", {3, 3}},
                                         {"CONFIDENCE.NORM", {3, 3}},
                                         {"CONFIDENCE.T", {3, 3}},
                                         {"EXPONDIST", {2, 3}},
                                         {"EXPON.DIST", {3, 3}},
                                         {"FDIST", {3, 4}},
                                         {"F.DIST", {4, 4}},
                                         {"LEGACYFDIST", {3, 3}},
                                         {"FINV", {3, 3}},
                                         {"LEGACYFINV", {3, 3}},
                                         {"FISHER", {1, 1}},
                                         {"FISHERINV", {1, 1}},
                                         {"GAMMADIST", {3, 4}},
                                         {"GAMMA.DIST", {4, 4}},
                                         {"GAMMAINV", {3, 3}},
                                         {"GAMMA", {1, 1}},
                                         {"GAMMALN", {1, 1}},
                                         {"GAUSS", {1, 1}},
                                         {"HYPGEOMDIST", {4, 5}},
                                         {"HYPGEOM.DIST", {5, 5}},
                                         {"LOGINV", {1, 3}},
                                         {"LOGNORM.INV", {3, 3}},
                                         {"LOGNORMDIST", {1, 4}},
                                         {"LOGNORM.DIST", {4, 4}},
                                         {"NEGBINOMDIST", {3, 3}},
                                         {"NEGBINOM.DIST", {4, 4}},
                                         {"NORMDIST", {3, 4}},
                                         {"NORM.DIST", {4, 4}},
                                         {"NORMINV", {3, 3}},
                                         {"NORMSDIST", {1, 2}},
                                         {"NORM.S.DIST", {2, 2}},
                                         {"NORMSINV", {1, 1}},
                                         {"PHI", {1, 1}},
                                         {"POISSON", {2, 3}},
                                         {"POISSON.DIST", {3, 3}},
                                         {"TDIST", {3, 3}},
                                         {"T.DIST", {3, 3}},
                                         {"T.DIST.RT", {2, 2}},
                                         {"T.DIST.2T", {2, 2}},
                                         {"TINV", {2, 2}},
                                         {"T.INV", {2, 2}},
                                         {"WEIBULL", {4, 4}}};
  return names;
}
Text distribution_name(const Text &name) {
  auto base = name;
  if (base.startsWith("_XLFN."))
    base = base.mid(6);
  if (base.startsWith("COM.MICROSOFT."))
    base = base.mid(14);
  static const std::map<Text, Text> aliases = {
      {"BETA.INV", "BETAINV"},
      {"BINOM.DIST", "BINOMDIST"},
      {"BINOM.DIST.RANGE", "B"},
      {"BINOM.INV", "CRITBINOM"},
      {"LEGACY.CHIDIST", "CHIDIST"},
      {"LEGACYCHIDIST", "CHIDIST"},
      {"CHISQ.INV", "CHISQINV"},
      {"CHIINV", "LEGACYCHIINV"},
      {"LEGACY.CHIINV", "LEGACYCHIINV"},
      {"CHISQ.INV.RT", "LEGACYCHIINV"},
      {"F.DIST.RT", "LEGACYFDIST"},
      {"LEGACY.FDIST", "LEGACYFDIST"},
      {"F.INV", "FINV"},
      {"F.INV.RT", "LEGACYFINV"},
      {"LEGACY.FINV", "LEGACYFINV"},
      {"GAMMA.INV", "GAMMAINV"},
      {"GAMMALN.PRECISE", "GAMMALN"},
      {"NORM.INV", "NORMINV"},
      {"NORM.S.INV", "NORMSINV"},
      {"LEGACYNORMSDIST", "NORMSDIST"},
      {"LEGACY.NORMSDIST", "NORMSDIST"},
      {"LEGACYNORMSINV", "NORMSINV"},
      {"LEGACY.NORMSINV", "NORMSINV"},
      {"LEGACY.TDIST", "TDIST"},
      {"LEGACYTDIST", "TDIST"},
      {"T.INV.2T", "TINV"},
      {"WEIBULL.DIST", "WEIBULL"}};
  if (auto i = aliases.find(base); i != aliases.end())
    return i->second;
  return distribution_signatures().contains(base) ? base : name;
}
V distribution_function(const Text &name, const std::vector<V> &args) {
  auto signature = distribution_signatures().find(name);
  if (signature == distribution_signatures().end())
    return V::error("#NAME?", true);
  if (args.size() < signature->second.first ||
      args.size() > signature->second.second)
    return V::error("#VALUE!");
  for (const auto &arg : args)
    if (arg.isError())
      return arg;
  std::vector<long double> a;
  for (const auto &arg : args) {
    bool ok = true;
    double n = arg.isNumber()          ? arg.number
               : arg.isBoolean()       ? (arg.boolean ? 1 : 0)
               : arg.kind == V::Empty  ? 0
               : arg.kind == V::String ? arg.string.toDouble(&ok)
                                       : (ok = false, 0);
    if (!ok)
      return V::error("#VALUE!");
    if (!std::isfinite(n))
      return num_error();
    a.push_back(n);
  }
  auto optional = [&](size_t i, long double fallback) {
    return i < a.size() ? a[i] : fallback;
  };
  try {
    if (name == "FISHER")
      return a[0] <= -1 || a[0] >= 1 ? num_error() : numeric(std::atanh(a[0]));
    if (name == "FISHERINV")
      return numeric(std::tanh(a[0]));
    if (name == "GAMMA")
      return numeric(M::tgamma(a[0], Policy()));
    if (name == "GAMMALN")
      return a[0] <= 0 ? num_error() : numeric(M::lgamma(a[0], Policy()));
    if (name == "GAUSS")
      return numeric(std::erf(a[0] / std::sqrt(2.L)) / 2);
    if (name == "PHI")
      return numeric(M::pdf(Dist<M::normal_distribution>(), a[0]));
    if (name == "NORMDIST" || name == "NORM.DIST" || name == "NORMINV" ||
        name == "NORMSDIST" || name == "NORM.S.DIST" || name == "NORMSINV" ||
        name == "LOGINV" || name == "LOGNORM.INV" || name == "LOGNORMDIST" ||
        name == "LOGNORM.DIST") {
      bool standard =
          name == "NORMSDIST" || name == "NORM.S.DIST" || name == "NORMSINV";
      bool logarithmic = name.startsWith("LOG");
      bool inv = name.endsWith("INV");
      long double mean = standard ? 0 : optional(1, 0),
                  sd = standard ? 1 : optional(2, 1);
      if (sd <= 0 || (inv && (a[0] <= 0 || a[0] >= 1)))
        return num_error();
      Dist<M::normal_distribution> d(mean, sd);
      if (inv) {
        auto x = inverse(d, a[0]);
        return numeric(logarithmic ? std::exp(x) : x);
      }
      bool cumulative = optional(standard ? 1 : 3, 1) != 0;
      long double x = a[0];
      if (logarithmic) {
        if (x <= 0)
          return name == "LOGNORM.DIST" || !cumulative ? num_error()
                                                       : numeric(0);
        x = std::log(x);
      }
      return numeric(value(d, x, cumulative) /
                     (logarithmic && !cumulative ? a[0] : 1));
    }
    if (name == "CONFIDENCE" || name == "CONFIDENCE.NORM" ||
        name == "CONFIDENCE.T") {
      long double n = name == "CONFIDENCE" ? a[2] : std::trunc(a[2]);
      if (a[0] <= 0 || a[0] >= 1 || a[1] <= 0 || n < 1)
        return num_error();
      if (name == "CONFIDENCE.T" && n == 1)
        return V::error("#DIV/0!");
      // Complement quantiles retain tiny alpha that would disappear in
      // 1-alpha/2.
      auto critical =
          name == "CONFIDENCE.T"
              ? inverse(Dist<M::students_t_distribution>(n - 1), a[0] / 2, true)
              : inverse(Dist<M::normal_distribution>(), a[0] / 2, true);
      return numeric(critical * a[1] / std::sqrt(n));
    }
    if (name == "BETADIST" || name == "BETA.DIST" || name == "BETAINV") {
      bool modern = name == "BETA.DIST", inv = name == "BETAINV";
      long double low = optional(modern ? 4 : 3, 0),
                  high = optional(modern ? 5 : 4, 1);
      if (a[1] <= 0 || a[2] <= 0 || low >= high || (inv && !probability(a[0])))
        return num_error();
      Dist<M::beta_distribution> d(a[1], a[2]);
      if (inv)
        return numeric(low + (high - low) * inverse(d, a[0]));
      bool cumulative = optional(modern ? 3 : 5, 1) != 0;
      if (a[0] < low || a[0] > high) {
        if (modern)
          return num_error();
        return numeric(cumulative && a[0] > high ? 1 : 0);
      }
      return numeric(value(d, (a[0] - low) / (high - low), cumulative) /
                     (cumulative ? 1 : high - low));
    }
    if (name == "EXPONDIST" || name == "EXPON.DIST") {
      if (a[1] <= 0)
        return num_error();
      if (a[0] < 0)
        return name == "EXPON.DIST" ? num_error() : numeric(0);
      long double t = a[0] * a[1];
      return numeric(optional(2, 1) != 0 ? -std::expm1(-t)
                                         : a[1] * std::exp(-t));
    }
    if (name == "GAMMADIST" || name == "GAMMA.DIST" || name == "GAMMAINV") {
      bool inv = name == "GAMMAINV";
      if (a[1] <= 0 || a[2] <= 0 || (inv && (a[0] < 0 || a[0] >= 1)))
        return num_error();
      if (!inv && a[0] < 0)
        return name == "GAMMA.DIST" ? num_error() : numeric(0);
      Dist<M::gamma_distribution> d(a[1], a[2]);
      if (!inv && a[0] == 0 && optional(3, 1) == 0)
        return a[1] < 1 ? num_error() : numeric(a[1] == 1 ? 1 / a[2] : 0);
      return numeric(inv ? inverse(d, a[0])
                         : value(d, a[0], optional(3, 1) != 0));
    }
    if (name == "CHIDIST" || name == "CHISQ.DIST.RT" || name == "CHISQDIST" ||
        name == "CHISQ.DIST" || name == "CHISQINV" || name == "LEGACYCHIINV") {
      long double df = std::trunc(a[1]);
      bool inv = name == "CHISQINV" || name == "LEGACYCHIINV",
           right = name == "CHIDIST" || name == "CHISQ.DIST.RT" ||
                   name == "LEGACYCHIINV";
      if (df < 1 ||
          (inv && (!probability(a[0]) || (right ? a[0] == 0 : a[0] == 1))))
        return num_error();
      Dist<M::chi_squared_distribution> d(df);
      if (inv)
        return numeric(inverse(d, a[0], right));
      if (a[0] < 0)
        return name == "CHISQ.DIST" || name == "CHISQ.DIST.RT"
                   ? num_error()
                   : numeric(right ? 1 : 0);
      if (a[0] == 0 && name == "CHISQDIST")
        return numeric(0);
      return numeric(right ? tail(d, a[0])
                           : value(d, a[0], optional(2, 1) != 0));
    }
    if (name == "FDIST" || name == "F.DIST" || name == "LEGACYFDIST" ||
        name == "FINV" || name == "LEGACYFINV") {
      long double df1 = std::trunc(a[1]), df2 = std::trunc(a[2]);
      bool inv = name == "FINV" || name == "LEGACYFINV",
           right = name.startsWith("LEGACY");
      if (df1 < 1 || df2 < 1 ||
          (inv && (!probability(a[0]) || (right ? a[0] == 0 : a[0] == 1))))
        return num_error();
      Dist<M::fisher_f_distribution> d(df1, df2);
      if (inv)
        return numeric(inverse(d, a[0], right));
      if (a[0] < 0)
        return right || name == "F.DIST" ? num_error() : numeric(0);
      return numeric(right ? tail(d, a[0])
                           : value(d, a[0], optional(3, 1) != 0));
    }
    if (name == "TDIST" || name == "T.DIST" || name == "T.DIST.RT" ||
        name == "T.DIST.2T" || name == "TINV" || name == "T.INV") {
      long double df = std::trunc(a[1]);
      bool inv = name == "TINV" || name == "T.INV";
      if (df < 1)
        return num_error();
      Dist<M::students_t_distribution> d(df);
      if (inv) {
        if (a[0] <= 0 || a[0] > 1 || (name == "T.INV" && a[0] == 1))
          return num_error();
        return numeric(name == "TINV" ? inverse(d, a[0] / 2, true)
                                      : inverse(d, a[0]));
      }
      if (name == "T.DIST")
        return numeric(value(d, a[0], a[2] != 0));
      if (name == "T.DIST.RT")
        return numeric(tail(d, a[0]));
      long double tails = name == "TDIST" ? std::trunc(a[2]) : 2;
      if (a[0] < 0 || (tails != 1 && tails != 2))
        return num_error();
      return numeric(tails * tail(d, a[0]));
    }
    if (name == "WEIBULL") {
      if (a[0] < 0 || a[1] <= 0 || a[2] <= 0)
        return num_error();
      if (a[0] == 0)
        return a[3] != 0  ? numeric(0)
               : a[1] < 1 ? num_error()
                          : numeric(a[1] == 1 ? 1 / a[2] : 0);
      long double logx = std::log(a[0]) - std::log(a[2]),
                  power = std::exp(a[1] * logx);
      if (a[3] != 0)
        return numeric(-std::expm1(-power));
      if (!std::isfinite(power))
        return numeric(0);
      return numeric(std::exp(std::log(a[1]) - std::log(a[2]) +
                              (a[1] - 1) * logx - power));
    }
    if (name == "BINOMDIST" || name == "BINO" || name == "B" ||
        name == "CRITBINOM") {
      bool range = name == "B", inv = name == "CRITBINOM";
      long double n = std::trunc(a[name == "BINOMDIST" ? 1 : 0]),
                  p = a[name == "BINOMDIST" || name == "BINO" ? 2 : 1];
      long double x = std::trunc(a[range            ? 2
                                   : name == "BINO" ? 1
                                                    : 0]),
                  high = range ? std::trunc(optional(3, x)) : x;
      if (n < 0 || !probability(p) ||
          (inv ? !probability(a[2]) : x < 0 || high < x || high > n))
        return num_error();
      // A double cell cannot distinguish consecutive integers beyond 2^53.
      if (n > 9007199254740991.L)
        return limit();
      if (inv) {
        long double alpha = a[2];
        if (alpha == 0 || p == 0 || n == 0)
          return numeric(0);
        if (p == 1 || alpha == 1)
          return numeric(n);
        Dist<M::binomial_distribution> d(n, p);
        // Search for the least k with CDF(k)>=alpha. Boost's default discrete
        // quantile rounds outwards, which does not implement CRITBINOM.
        long double low = 0, upper = n;
        while (low < upper) {
          long double mid = low + std::floor((upper - low) / 2);
          bool reaches = alpha <= .5L ? M::cdf(d, mid) >= alpha
                                      : tail(d, mid) <= 1 - alpha;
          if (reaches)
            upper = mid;
          else
            low = mid + 1;
        }
        return numeric(low);
      }
      bool cumulative = name == "BINOMDIST" && a[3] != 0;
      if (p == 0)
        return numeric(cumulative || x == 0 ? 1 : 0);
      if (p == 1)
        return numeric((range ? x <= n && high >= n : x == n) ? 1 : 0);
      Dist<M::binomial_distribution> d(n, p);
      if (!range || x == high)
        return numeric(value(d, x, cumulative));
      if (high - x < 64) {
        // A short interval near the mode can lose digits when subtracting
        // almost equal CDFs, or require expensive incomplete-beta convergence
        // for very large trial counts. Sum densities with compensation instead.
        long double total = 0, correction = 0;
        for (long double k = x; k <= high; ++k) {
          long double next = M::pdf(d, k) - correction;
          long double sum = total + next;
          correction = (sum - total) - next;
          total = sum;
        }
        return numeric(total);
      }
      if (x == 0)
        return numeric(M::cdf(d, high));
      if (high == n)
        return numeric(tail(d, x - 1));
      // Subtract on the small tail to retain intervals far from the mean.
      return numeric(x > n * p ? tail(d, x - 1) - tail(d, high)
                               : M::cdf(d, high) - M::cdf(d, x - 1));
    }
    if (name == "POISSON" || name == "POISSON.DIST") {
      long double x = std::trunc(a[0]);
      if (x < 0 || a[1] <= 0)
        return num_error();
      return numeric(
          value(Dist<M::poisson_distribution>(a[1]), x, optional(2, 1) != 0));
    }
    if (name == "NEGBINOMDIST" || name == "NEGBINOM.DIST") {
      long double x = std::trunc(a[0]), r = std::trunc(a[1]), p = a[2];
      if (x < 0 || r < 1 || (name == "NEGBINOMDIST" && x + r <= 1) ||
          !probability(p))
        return num_error();
      if (p == 0)
        return numeric(0);
      if (p == 1)
        return numeric(optional(3, 0) != 0 || x == 0 ? 1 : 0);
      return numeric(value(Dist<M::negative_binomial_distribution>(r, p), x,
                           optional(3, 0) != 0));
    }
    if (name == "HYPGEOMDIST" || name == "HYPGEOM.DIST") {
      long double x = std::trunc(a[0]), draws = std::trunc(a[1]),
                  successes = std::trunc(a[2]), population = std::trunc(a[3]);
      if (x < 0 || draws < 0 || successes < 0 || population < 0 || x > draws ||
          draws > population || successes > population)
        return num_error();
      bool cumulative = optional(4, 0) != 0;
      long double low = std::max(0.L, draws - (population - successes)),
                  high = std::min(draws, successes);
      if (x < low)
        return numeric(0);
      if (x > high)
        return numeric(cumulative ? 1 : 0);
      if (low == high)
        return numeric(1);
      if (population > std::numeric_limits<unsigned>::max() ||
          (cumulative && high - low > 100000))
        return limit();
      return numeric(value(Dist<M::hypergeometric_distribution>(
                               static_cast<unsigned>(successes),
                               static_cast<unsigned>(draws),
                               static_cast<unsigned>(population)),
                           x, cumulative));
    }
  } catch (const boost::math::evaluation_error &) {
    return limit();
  } catch (const boost::math::rounding_error &) {
    return limit();
  } catch (const std::domain_error &) {
    return num_error();
  } catch (const std::overflow_error &) {
    return num_error();
  }
  return V::error("#NAME?", true);
}
} // namespace DomeOffice
