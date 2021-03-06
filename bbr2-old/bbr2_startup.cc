#include "bbr2_startup.h"
#include "bbr2_sender.h"
namespace dqc {

Bbr2StartupMode::Bbr2StartupMode(const Bbr2Sender* sender,
                                 Bbr2NetworkModel* model)
    : Bbr2ModeBase(sender, model),
      full_bandwidth_reached_(false),
      full_bandwidth_baseline_(QuicBandwidth::Zero()),
      rounds_without_bandwidth_growth_(0),
      loss_events_in_round_(0) {}

void Bbr2StartupMode::Enter(const Bbr2CongestionEvent& /*congestion_event*/) {
  //QUIC_BUG << "Bbr2StartupMode::Enter should not be called";
}

Bbr2Mode Bbr2StartupMode::OnCongestionEvent(
    QuicByteCount /*prior_in_flight*/,
    ProtoTime /*event_time*/,
    const AckedPacketVector& /*acked_packets*/,
    const LostPacketVector& lost_packets,
    const Bbr2CongestionEvent& congestion_event) {
  CheckFullBandwidthReached(congestion_event);

  CheckExcessiveLosses(lost_packets, congestion_event);

  model_->set_pacing_gain(Params().startup_gain);
  model_->set_cwnd_gain(Params().startup_gain);

  // TODO(wub): Maybe implement STARTUP => PROBE_RTT.
  return full_bandwidth_reached_ ? Bbr2Mode::DRAIN : Bbr2Mode::STARTUP;
}

void Bbr2StartupMode::CheckFullBandwidthReached(
    const Bbr2CongestionEvent& congestion_event) {
  DCHECK(!full_bandwidth_reached_);
  if (full_bandwidth_reached_ || !congestion_event.end_of_round_trip ||
      congestion_event.last_sample_is_app_limited) {
    return;
  }

  QuicBandwidth threshold =
      full_bandwidth_baseline_ * Params().startup_full_bw_threshold;

  if (model_->MaxBandwidth() >= threshold) {
    /*QUIC_DVLOG(3)
        << sender_
        << " CheckFullBandwidthReached at end of round. max_bandwidth:"
        << model_->MaxBandwidth() << ", threshold:" << threshold
        << " (Still growing)  @ " << congestion_event.event_time;*/
    full_bandwidth_baseline_ = model_->MaxBandwidth();
    rounds_without_bandwidth_growth_ = 0;
    return;
  }

  ++rounds_without_bandwidth_growth_;
  full_bandwidth_reached_ =
      rounds_without_bandwidth_growth_ >= Params().startup_full_bw_rounds;
  /*QUIC_DVLOG(3) << sender_
                << " CheckFullBandwidthReached at end of round. max_bandwidth:"
                << model_->MaxBandwidth() << ", threshold:" << threshold
                << " rounds_without_growth:" << rounds_without_bandwidth_growth_
                << " full_bw_reached:" << full_bandwidth_reached_ << "  @ "
                << congestion_event.event_time;*/
}

void Bbr2StartupMode::CheckExcessiveLosses(
    const LostPacketVector& lost_packets,
    const Bbr2CongestionEvent& congestion_event) {
  if (full_bandwidth_reached_) {
    return;
  }

  if (!lost_packets.empty()) {
    ++loss_events_in_round_;
  }

  // TODO(wub): In TCP, loss based exit only happens at end of a loss round, in
  // QUIC we use the end of the normal round here. It is possible to exit after
  // any congestion event, using information of the "rolling round".
  if (!congestion_event.end_of_round_trip) {
    return;
  }

  /*QUIC_DVLOG(3)
      << sender_
      << " CheckExcessiveLosses at end of round. loss_events_in_round_:"
      << loss_events_in_round_
      << ", threshold:" << Params().startup_full_loss_count << "  @ "
      << congestion_event.event_time;*/

  // At the end of a round trip. Check if loss is too high in this round.
  if (loss_events_in_round_ >= Params().startup_full_loss_count &&
      model_->IsInflightTooHigh(congestion_event)) {
    const QuicByteCount bdp = model_->BDP(model_->MaxBandwidth());
    //QUIC_DVLOG(3) << sender_
    //              << " Exiting STARTUP due to loss. inflight_hi:" << bdp;
    model_->set_inflight_hi(bdp);

    full_bandwidth_reached_ = true;
  }

  loss_events_in_round_ = 0;
}

Bbr2StartupMode::DebugState Bbr2StartupMode::ExportDebugState() const {
  DebugState s;
  s.full_bandwidth_reached = full_bandwidth_reached_;
  s.full_bandwidth_baseline = full_bandwidth_baseline_;
  s.round_trips_without_bandwidth_growth = rounds_without_bandwidth_growth_;
  return s;
}

std::ostream& operator<<(std::ostream& os,
                         const Bbr2StartupMode::DebugState& state) {
  os << "[STARTUP] full_bandwidth_reached: " << state.full_bandwidth_reached
     << "\n";
  os << "[STARTUP] full_bandwidth_baseline: " << state.full_bandwidth_baseline
     << "\n";
  os << "[STARTUP] round_trips_without_bandwidth_growth: "
     << state.round_trips_without_bandwidth_growth << "\n";
  return os;
}

const Bbr2Params& Bbr2StartupMode::Params() const {
  return sender_->Params();
}

}  // namespace quic
