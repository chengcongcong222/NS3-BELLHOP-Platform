#pragma once
// Author-reference algorithms: uwa-channels/python@dbf373ca.
// Upstream attribution/license: adapters/UWA-LICENSE.txt.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <limits>
#include <numeric>
#include <span>
#include <vector>
#include <ns3_factory/contracts/errors.hpp>

namespace ns3_factory::phy::internal::recorded {
using Complex = std::complex<double>;
using contracts::Error;
using contracts::ErrorCode;
using contracts::Result;

// Uniform not-a-knot cubic spline (SciPy CubicSpline default boundary).
// Outside support is zero, as in the pinned author's replay implementation.
// Production coverage checks must reject missing support before calling it.
class Spline {
 public:
  Spline(std::span<const Complex> y, double step)
      : values_(y.begin(), y.end()), second_(y.size()), step_(step) {
    const auto n = y.size();
    if(n < 3) return;
    const auto inv = 1.0 / (step * step);
    if(n == 3) {
      std::fill(second_.begin(), second_.end(), (y[2] - 2.0*y[1] + y[0])*inv);
      return;
    }
    const auto count = n - 2;
    std::vector<double> diagonal(count, 4.0);
    std::vector<Complex> rhs(count);
    for(std::size_t i=0; i<count; ++i)
      rhs[i] = 6.0 * (y[i+2] - 2.0*y[i+1] + y[i]) * inv;
    diagonal.front() = diagonal.back() = 1.0;
    rhs.front() /= 6.0; rhs.back() /= 6.0;
    for(std::size_t i=1; i<count-1; ++i) {
      const auto upper = i == 1 ? 0.0 : 1.0;
      diagonal[i] -= upper / diagonal[i-1];
      rhs[i] -= rhs[i-1] / diagonal[i-1];
    }
    second_[n-2] = rhs.back();
    for(std::size_t i=count-1; i-- > 0;)
      second_[i+1] = (rhs[i] - (i == 0 ? Complex{} : second_[i+2])) / diagonal[i];
    second_.front() = 2.0*second_[1] - second_[2];
    second_.back() = 2.0*second_[n-2] - second_[n-3];
  }
  Complex At(double time) const {
    if(values_.empty() || time < 0 || time > (values_.size()-1)*step_) return {};
    if(values_.size() == 1) return time == 0 ? values_[0] : Complex{};
    const auto i = std::min(static_cast<std::size_t>(time/step_), values_.size()-2);
    const auto b = (time - i*step_)/step_, a = 1.0-b;
    return a*values_[i] + b*values_[i+1] +
      ((a*a*a-a)*second_[i] + (b*b*b-b)*second_[i+1])*(step_*step_/6.0);
  }
 private:
  std::vector<Complex> values_, second_;
  double step_;
};

// Exact rational resample_poly convention: Kaiser(beta=5), 10*max(up,down)
// half length, DC-normalized FIR, centered first sample, zero padding.
inline Result<std::vector<Complex>> Resample(std::span<const Complex> x,
    std::size_t up, std::size_t down) {
  if(!up || !down || x.empty())
    return std::unexpected(Error{ErrorCode::kInvalidArgument,"Invalid replay resampling ratio"});
  const auto divisor=std::gcd(up,down); up/=divisor; down/=divisor;
  if(up == down) return std::vector<Complex>{x.begin(),x.end()};
  const auto rate=std::max(up,down);
  if(rate > 100'000 || x.size() > std::numeric_limits<std::size_t>::max()/up ||
     (x.size()*up+down-1)/down > 10'000'000)
    return std::unexpected(Error{ErrorCode::kOutOfRange,"Replay resampling compute budget exceeded"});
  const auto half=10*rate, pre=down-half%down, remove=(half+pre)/down;
  std::vector<double> h(2*half+1+pre);
  double sum=0;
  for(std::size_t i=0; i<=2*half; ++i) {
    const auto d=(static_cast<double>(i)-half)/rate;
    const auto sinc=d == 0 ? 1.0 : std::sin(std::numbers::pi*d)/(std::numbers::pi*d);
    const auto w=(static_cast<double>(i)-half)/half;
    h[pre+i]=sinc/rate*std::cyl_bessel_i(0.0,5.0*std::sqrt(std::max(0.0,1-w*w)))/
      std::cyl_bessel_i(0.0,5.0);
    sum+=h[pre+i];
  }
  for(auto& v:h) v*=up/sum;
  std::vector<Complex> out((x.size()*up+down-1)/down);
  for(std::size_t i=0; i<out.size(); ++i) {
    const auto at=(i+remove)*down;
    const auto begin=at>=h.size()-1 ? (at-(h.size()-1)+up-1)/up : 0;
    const auto end=std::min(at/up+1,x.size());
    for(auto j=begin; j<end; ++j) out[i]+=x[j]*h[at-j*up];
  }
  return out;
}

struct Channel {
  double carrier_hz;
  double delay_rate_hz;
  double snapshot_rate_hz;
  std::size_t snapshots, delay_taps;
  // One selected receiver element/trial; immutable snapshot-major samples.
  std::vector<Complex> coefficients;
  std::vector<double> phase; // selected phi_hat, otherwise theta_hat
  bool delay_tracking = false;
  std::size_t resample_up = 1, resample_down = 1;
};

// Same continued-fraction best rational bound as Fraction(x).limit_denominator().
// Library clocks need not be integer Hz (Blue uses 9765.625 Hz).
inline Result<std::pair<std::size_t,std::size_t>> Ratio(double value) {
  if(!std::isfinite(value)||value<=0||value>100'000)
    return std::unexpected(Error{ErrorCode::kInvalidArgument,"Invalid recorded resampling ratio"});
  constexpr std::size_t limit=1'000'000;
  std::size_t p0=0,q0=1,p1=1,q1=0;
  long double remainder=value;
  for(;;) {
    const auto a=std::floor(remainder);
    if(a>limit || (q1 && a>(limit-q0)/q1)) break;
    const auto ai=static_cast<std::size_t>(a),p2=p0+ai*p1,q2=q0+ai*q1;
    p0=p1;q0=q1;p1=p2;q1=q2;
    remainder-=a;
    if(std::abs(remainder)<1e-18L) return std::pair{p1,q1};
    remainder=1/remainder;
  }
  if(!q1) return std::unexpected(Error{ErrorCode::kOutOfRange,"Unrepresentable replay ratio"});
  const auto k=(limit-q0)/q1;
  const std::pair<std::size_t,std::size_t> a{p0+k*p1,q0+k*q1}, b{p1,q1};
  const auto distance=[value](auto v){return std::abs(static_cast<long double>(v.first)/v.second-value);};
  return distance(b)<=distance(a)?b:a;
}

// Author-reference real-passband replay. start is the explicit zero-based
// fs_delay sample offset, never randomly selected and never simulation time.
// This numerical kernel does not schedule events or assert physical flight time.
inline Result<std::vector<double>> Replay(const Channel& c, std::span<const double> input,
    std::size_t sample_rate_hz, std::size_t start) {
  if(!std::isfinite(c.carrier_hz) || c.carrier_hz<=0 || !sample_rate_hz ||
     sample_rate_hz<=2*c.carrier_hz || !std::isfinite(c.delay_rate_hz) || c.delay_rate_hz<=0 ||
     !std::isfinite(c.snapshot_rate_hz) || c.snapshot_rate_hz<=0 ||
     c.snapshots<2 || c.snapshots>1'000'000 || !c.delay_taps || c.delay_taps>100'000 ||
     c.coefficients.size()!=c.snapshots*c.delay_taps || input.empty() ||
     c.coefficients.size()>20'000'000 || input.size()>10'000'000)
    return std::unexpected(Error{ErrorCode::kInvalidArgument,"Invalid or oversized recorded replay input"});
  for(const auto v:c.coefficients)
    if(!std::isfinite(v.real()) || !std::isfinite(v.imag()))
      return std::unexpected(Error{ErrorCode::kInvalidArgument,"Non-finite recorded coefficient"});
  std::vector<Complex> mixed(input.size());
  for(std::size_t i=0; i<input.size(); ++i) {
    if(!std::isfinite(input[i]))
      return std::unexpected(Error{ErrorCode::kInvalidArgument,"Non-finite replay waveform"});
    mixed[i]=input[i]*std::polar(1.0,-2*std::numbers::pi*c.carrier_hz*i/sample_rate_hz);
  }
  auto ratio=Ratio(c.delay_rate_hz/sample_rate_hz);
  if(!ratio) return std::unexpected(ratio.error());
  auto bb=Resample(mixed,ratio->first,ratio->second);
  if(!bb) return std::unexpected(bb.error());
  const auto count=bb->size()+c.delay_taps;
  if(count>10'000'000 || c.delay_taps>100'000'000/count ||
     start>std::numeric_limits<std::size_t>::max()-count ||
     (!c.phase.empty() && start+count>c.phase.size()) ||
     static_cast<double>(start+count-1)/c.delay_rate_hz > (c.snapshots-1)/c.snapshot_rate_hz)
    return std::unexpected(Error{ErrorCode::kOutOfRange,"Recording does not cover the complete waveform and tail"});
  if(c.delay_tracking && c.phase.empty())
    return std::unexpected(Error{ErrorCode::kInvalidArgument,"Delay tracking requires phi samples"});
  std::vector<Spline> taps;
  std::vector<Complex> samples(c.snapshots);
  for(std::size_t k=0; k<c.delay_taps; ++k) {
    for(std::size_t t=0; t<c.snapshots; ++t) samples[t]=c.coefficients[t*c.delay_taps+k];
    taps.emplace_back(samples,1.0/c.snapshot_rate_hz);
  }
  std::vector<Complex> output(count);
  for(std::size_t i=0; i<count-1; ++i) {
    const auto time=static_cast<double>(start+i)/c.delay_rate_hz;
    for(std::size_t k=0; k<c.delay_taps && k<=i; ++k)
      if(i-k<bb->size()) output[i]+=taps[k].At(time)*(*bb)[i-k];
    if(!c.phase.empty()) {
      if(!std::isfinite(c.phase[start+i]))
        return std::unexpected(Error{ErrorCode::kInvalidArgument,"Non-finite replay tracking phase"});
      output[i]*=std::polar(1.0,c.phase[start+i]);
    }
  }
  if(c.delay_tracking) {
    Spline drifted{output,1.0/c.delay_rate_hz};
    for(std::size_t i=0; i<count; ++i) {
      if(!std::isfinite(c.phase[start+i]))
        return std::unexpected(Error{ErrorCode::kInvalidArgument,"Non-finite replay tracking phase"});
      output[i]=drifted.At(static_cast<double>(i)/c.delay_rate_hz+
                          c.phase[start+i]/(2*std::numbers::pi*c.carrier_hz));
    }
  }
  auto up=Resample(output,ratio->second,ratio->first);
  if(!up) return std::unexpected(up.error());
  for(std::size_t i=0; i<up->size(); ++i)
    (*up)[i]=2*std::real((*up)[i]*std::polar(1.0,2*std::numbers::pi*c.carrier_hz*i/sample_rate_hz));
  auto final=Resample(*up,c.resample_up,c.resample_down);
  if(!final) return std::unexpected(final.error());
  std::vector<double> result; result.reserve(final->size());
  for(const auto v:*final) {
    if(!std::isfinite(v.real()))
      return std::unexpected(Error{ErrorCode::kOutOfRange,"Replay output is non-finite"});
    result.push_back(v.real());
  }
  return result;
}
} // namespace ns3_factory::phy::internal::recorded
